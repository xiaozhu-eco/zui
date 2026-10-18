#include "image/svg/parser/parser.h"

#include "image/svg/parser/image_node.h"
#include "image/svg/parser/length.h"
#include "image/svg/parser/shape.h"
#include "image/svg/parser/shape_lowerer.h"
#include "image/svg/parser/style_resolver.h"
#include "image/svg/parser/xml_scanner.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace zui {
namespace image {
namespace svg {
namespace {

bool skipped(std::string_view name) {
    return name == "use" || name == "text" || name == "tspan" || name == "clipPath" || name == "mask" ||
           name == "filter" || name == "style" || name == "title" || name == "desc" || name == "metadata" ||
           name == "script" || name == "pattern" || name == "marker" || name == "symbol";
}

float attr_num(const XmlScanner& tag, const char* key, float fallback) {
    if (!tag.has(key)) {
        return fallback;
    }
    float value = fallback;
    if (!Length::px(tag.attr(key), value)) {
        return fallback;
    }
    return value;
}

bool read_viewbox(std::string_view text, Document& doc) {
    float n[4] = {};
    int count = 0;
    const std::string token(text);
    const char* cursor = token.c_str();
    while (*cursor != '\0' && count < 4) {
        while (*cursor == ' ' || *cursor == '\t' || *cursor == ',' || *cursor == '\n') {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }
        char* end = nullptr;
        n[count] = std::strtof(cursor, &end);
        if (end == cursor) {
            return false;
        }
        ++count;
        cursor = end;
    }
    if (count != 4 || n[2] == 0.f || n[3] == 0.f) {
        return false;
    }
    doc.vb_x = n[0];
    doc.vb_y = n[1];
    doc.vb_w = n[2];
    doc.vb_h = n[3];
    doc.has_viewbox = true;
    return true;
}

float stop_offset(std::string_view text) {
    const bool percent = !text.empty() && text.back() == '%';
    float value = 0.f;
    Length::px(text, value);
    if (percent) {
        value /= 100.f;
    }
    return value;
}

void read_stop(const XmlScanner& tag, Gradient& gradient) {
    GradientStop stop;
    if (tag.has("offset")) {
        stop.offset = stop_offset(tag.attr("offset"));
    }
    if (tag.has("stop-color")) {
        Color::parse(tag.attr("stop-color"), stop.color);
    }
    if (tag.has("stop-opacity")) {
        float opacity = 1.f;
        if (Length::px(tag.attr("stop-opacity"), opacity)) {
            stop.color.a *= opacity;
        }
    }
    if (tag.has("style")) {
        std::string_view text = tag.attr("style");
        while (!text.empty()) {
            const std::size_t semi = text.find(';');
            const std::string_view item = text.substr(0, semi == std::string_view::npos ? text.size() : semi);
            const std::size_t colon = item.find(':');
            if (colon != std::string_view::npos) {
                std::string_view key = item.substr(0, colon);
                std::string_view value = item.substr(colon + 1);
                while (!key.empty() && key.front() == ' ') {
                    key.remove_prefix(1);
                }
                while (!value.empty() && value.front() == ' ') {
                    value.remove_prefix(1);
                }
                if (key == "stop-color") {
                    Color::parse(value, stop.color);
                } else if (key == "stop-opacity") {
                    float opacity = 1.f;
                    if (Length::px(value, opacity)) {
                        stop.color.a *= opacity;
                    }
                } else if (key == "offset") {
                    stop.offset = stop_offset(value);
                }
            }
            if (semi == std::string_view::npos) {
                break;
            }
            text.remove_prefix(semi + 1);
        }
    }
    gradient.stops.push_back(stop);
}

}  // namespace

bool Parser::parse(std::string_view xml, Document& out, std::string& error) const {
    out = Document{};
    XmlScanner scan(xml);
    struct Frame {
        Group* group = nullptr;
        StyleResolver style = StyleResolver::initial();
    };
    std::vector<Frame> stack;
    int gradient = -1;
    int gradient_depth = 0;
    int skip = 0;
    bool saw_svg = false;

    auto push_group = [&](const XmlScanner& tag, const StyleResolver& style, bool painted) -> bool {
        auto group = std::make_unique<Group>();
        group->painted = painted;
        group->opacity = attr_num(tag, "opacity", 1.f);
        if (tag.has("id")) {
            group->id.assign(tag.attr("id"));
        }
        if (tag.has("transform") && !Transform::parse(tag.attr("transform"), group->transform)) {
            error = "svg transform";
            return false;
        }
        Group* raw = group.get();
        if (stack.empty()) {
            out.root = std::move(group);
        } else {
            stack.back().group->children.push_back(std::move(group));
        }
        stack.push_back(Frame{raw, style});
        return true;
    };

    while (true) {
        const XmlScanner::Kind kind = scan.next();
        if (kind == XmlScanner::Kind::End) {
            break;
        }
        if (kind == XmlScanner::Kind::Error) {
            error = scan.error().empty() ? "svg xml" : scan.error();
            return false;
        }
        if (skip > 0) {
            if (kind == XmlScanner::Kind::Open) {
                ++skip;
            } else if (kind == XmlScanner::Kind::Close) {
                --skip;
            }
            continue;
        }
        if (kind == XmlScanner::Kind::Close) {
            if (gradient >= 0) {
                --gradient_depth;
                if (gradient_depth <= 0) {
                    gradient = -1;
                    gradient_depth = 0;
                }
                continue;
            }
            if (stack.empty()) {
                error = "svg close";
                return false;
            }
            stack.pop_back();
            continue;
        }
        const std::string_view name = scan.element();
        const bool empty = kind == XmlScanner::Kind::Empty;
        if (gradient >= 0) {
            if (name == "stop") {
                read_stop(scan, out.gradients[static_cast<std::size_t>(gradient)]);
                if (!empty) {
                    skip = 1;
                }
            } else if (!empty) {
                ++gradient_depth;
            }
            continue;
        }
        if (name == "linearGradient" || name == "radialGradient") {
            Gradient item;
            if (scan.has("id")) {
                item.id.assign(scan.attr("id"));
            }
            item.radial = name == "radialGradient";
            item.object_bounding_box = !(scan.has("gradientUnits") && scan.attr("gradientUnits") == "userSpaceOnUse");
            item.x1 = attr_num(scan, "x1", 0.f);
            item.y1 = attr_num(scan, "y1", 0.f);
            item.x2 = attr_num(scan, "x2", item.radial ? 0.f : 1.f);
            item.y2 = attr_num(scan, "y2", 0.f);
            item.cx = attr_num(scan, "cx", 0.5f);
            item.cy = attr_num(scan, "cy", 0.5f);
            item.r = attr_num(scan, "r", 0.5f);
            out.gradients.push_back(std::move(item));
            if (!empty) {
                gradient = static_cast<int>(out.gradients.size()) - 1;
                gradient_depth = 1;
            }
            continue;
        }
        if (skipped(name)) {
            if (!empty) {
                skip = 1;
            }
            continue;
        }
        if (name == "svg" || name == "g" || name == "defs" || name == "a") {
            if (!saw_svg && name != "svg") {
                error = "svg root";
                return false;
            }
            const StyleResolver base = stack.empty() ? StyleResolver::initial() : stack.back().style;
            const StyleResolver style = base.applied(scan);
            if (name == "svg") {
                saw_svg = true;
                if (stack.empty()) {
                    if (scan.has("viewBox") && !read_viewbox(scan.attr("viewBox"), out)) {
                        error = "svg viewBox";
                        return false;
                    }
                    if (scan.has("width")) {
                        if (!Length::px(scan.attr("width"), out.width)) {
                            error = "svg width";
                            return false;
                        }
                    } else if (out.has_viewbox) {
                        out.width = out.vb_w;
                    }
                    if (scan.has("height")) {
                        if (!Length::px(scan.attr("height"), out.height)) {
                            error = "svg height";
                            return false;
                        }
                    } else if (out.has_viewbox) {
                        out.height = out.vb_h;
                    }
                    if (scan.has("preserveAspectRatio") &&
                        scan.attr("preserveAspectRatio").find("none") != std::string_view::npos) {
                        out.preserve = "none";
                    }
                }
            }
            const bool parent_painted = stack.empty() || stack.back().group->painted;
            if (!push_group(scan, style, parent_painted && name != "defs")) {
                return false;
            }
            if (empty) {
                stack.pop_back();
            }
            continue;
        }

        if (stack.empty()) {
            error = "svg root";
            return false;
        }
        const StyleResolver style = stack.back().style.applied(scan);
        const bool painted = stack.back().group->painted;
        if (name == "image") {
            if (painted) {
                auto node = std::make_unique<ImageNode>();
                node->opacity = attr_num(scan, "opacity", 1.f);
                node->x = attr_num(scan, "x", 0.f);
                node->y = attr_num(scan, "y", 0.f);
                node->w = attr_num(scan, "width", 0.f);
                node->h = attr_num(scan, "height", 0.f);
                if (scan.has("href")) {
                    node->href.assign(scan.attr("href"));
                }
                if (scan.has("transform") && !Transform::parse(scan.attr("transform"), node->transform)) {
                    error = "svg transform";
                    return false;
                }
                stack.back().group->children.push_back(std::move(node));
            }
        } else if (name == "rect" || name == "circle" || name == "ellipse" || name == "path" || name == "line" ||
                   name == "polyline" || name == "polygon") {
            if (painted) {
                auto shape = std::make_unique<Shape>();
                shape->opacity = attr_num(scan, "opacity", 1.f);
                shape->fill = style.fill;
                shape->stroke = style.stroke;
                shape->stroke_width = style.stroke_width;
                shape->fill_rule = style.fill_rule;
                if (shape->fill.kind == Paint::Kind::Color) {
                    shape->fill.color.a *= style.fill_opacity;
                } else if (shape->fill.kind == Paint::Kind::Gradient) {
                    shape->fill.color.a = style.fill_opacity;
                }
                if (shape->stroke.kind == Paint::Kind::Color) {
                    shape->stroke.color.a *= style.stroke_opacity;
                }
                if (scan.has("transform") && !Transform::parse(scan.attr("transform"), shape->transform)) {
                    error = "svg transform";
                    return false;
                }
                if (name == "rect") {
                    const float rx = attr_num(scan, "rx", attr_num(scan, "ry", 0.f));
                    const float ry = attr_num(scan, "ry", rx);
                    ShapeLowerer::rect(shape->path, attr_num(scan, "x", 0.f), attr_num(scan, "y", 0.f),
                                       attr_num(scan, "width", 0.f), attr_num(scan, "height", 0.f), rx, ry);
                } else if (name == "circle") {
                    const float r = attr_num(scan, "r", 0.f);
                    ShapeLowerer::ellipse(shape->path, attr_num(scan, "cx", 0.f), attr_num(scan, "cy", 0.f), r, r);
                } else if (name == "ellipse") {
                    ShapeLowerer::ellipse(shape->path, attr_num(scan, "cx", 0.f), attr_num(scan, "cy", 0.f),
                                          attr_num(scan, "rx", 0.f), attr_num(scan, "ry", 0.f));
                } else if (name == "line") {
                    ShapeLowerer::line(shape->path, attr_num(scan, "x1", 0.f), attr_num(scan, "y1", 0.f),
                                       attr_num(scan, "x2", 0.f), attr_num(scan, "y2", 0.f));
                } else if (name == "path") {
                    if (scan.has("d") && !shape->path.parse(scan.attr("d"), error)) {
                        return false;
                    }
                } else if (!ShapeLowerer::points(shape->path, scan.attr("points"), name == "polygon", error)) {
                    return false;
                }
                stack.back().group->children.push_back(std::move(shape));
            }
        } else if (!empty) {
            StyleResolver style = stack.back().style.applied(scan);
            if (!push_group(scan, style, stack.back().group->painted)) {
                return false;
            }
        }
        if (!empty && (name == "rect" || name == "circle" || name == "ellipse" || name == "path" || name == "line" ||
                       name == "polyline" || name == "polygon" || name == "image")) {
            skip = 1;
        }
    }
    if (!saw_svg || out.root == nullptr) {
        error = "svg root";
        return false;
    }
    if (out.width <= 0.f || out.height <= 0.f) {
        error = "svg viewport";
        return false;
    }
    return true;
}

}  // namespace svg
}  // namespace image
}  // namespace zui
