#include "image/svg/parser/style_resolver.h"

#include "image/svg/parser/length.h"
#include "image/svg/parser/xml_scanner.h"

#include <string>
#include <utility>
#include <vector>

namespace zui {
namespace image {
namespace svg {
namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

void apply_one(StyleResolver& style, std::string_view key, std::string_view value) {
    key = trim(key);
    value = trim(value);
    if (key == "fill") {
        Paint parsed;
        if (Paint::parse(value, parsed)) {
            style.fill = std::move(parsed);
        }
    } else if (key == "stroke") {
        Paint parsed;
        if (Paint::parse(value, parsed)) {
            style.stroke = std::move(parsed);
        }
    } else if (key == "fill-opacity") {
        float v = 1.f;
        if (Length::px(value, v)) {
            style.fill_opacity = v;
        }
    } else if (key == "stroke-opacity") {
        float v = 1.f;
        if (Length::px(value, v)) {
            style.stroke_opacity = v;
        }
    } else if (key == "stroke-width") {
        float v = 1.f;
        if (Length::px(value, v)) {
            style.stroke_width = v;
        }
    } else if (key == "fill-rule") {
        style.fill_rule = value == "evenodd" ? 1 : 0;
    }
}

}  // namespace

StyleResolver StyleResolver::initial() {
    StyleResolver style;
    style.fill.kind = Paint::Kind::Color;
    style.fill.color = Color{};
    style.stroke.kind = Paint::Kind::None;
    return style;
}

StyleResolver StyleResolver::applied(const XmlScanner& tag) const {
    StyleResolver style = *this;
    static const char* kKeys[] = {"fill",         "stroke",        "fill-opacity", "stroke-opacity",
                                  "stroke-width", "fill-rule"};
    for (const char* key : kKeys) {
        if (tag.has(key)) {
            apply_one(style, key, tag.attr(key));
        }
    }
    if (tag.has("style")) {
        std::string_view text = tag.attr("style");
        while (!text.empty()) {
            const std::size_t semi = text.find(';');
            const std::string_view item = text.substr(0, semi);
            const std::size_t colon = item.find(':');
            if (colon != std::string_view::npos) {
                apply_one(style, item.substr(0, colon), item.substr(colon + 1));
            }
            if (semi == std::string_view::npos) {
                break;
            }
            text.remove_prefix(semi + 1);
        }
    }
    return style;
}

}  // namespace svg
}  // namespace image
}  // namespace zui
