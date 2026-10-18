#include "document.h"

#include <sstream>

namespace designer {
namespace {

std::string esc(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else out.push_back(c);
    }
    return out;
}

std::string esc_attr(const std::string& text) {
    std::string out = esc(text);
    std::string quoted;
    for (char c : out) {
        if (c == '"') quoted += "&quot;";
        else quoted.push_back(c);
    }
    return quoted;
}

const char* kKindCss =
    ".k-heading{margin:0;font-weight:700;display:flex;align-items:center;overflow:hidden;}"
    ".k-text{margin:0;display:flex;align-items:flex-start;overflow:hidden;}"
    ".k-button{border:none;display:flex;align-items:center;justify-content:center;overflow:hidden;"
    "background:#2c6b4e;color:#fffdf8;border-radius:8px;}"
    ".k-input,.k-area{display:block;border:1px solid rgba(72,54,32,.22);background:#fffdf8;"
    "border-radius:8px;padding:4px 8px;}"
    ".k-check{display:flex;align-items:center;gap:6px;}"
    ".k-image{display:flex;align-items:center;justify-content:center;overflow:hidden;"
    "background:rgba(44,107,78,.12);color:#8a7f72;border-radius:12px;}"
    ".k-rule{border:none;border-top:1px solid rgba(72,54,32,.35);height:0;margin:8px 0;}"
    ".k-row{display:flex;flex-direction:row;flex-wrap:wrap;gap:8px;align-items:flex-start;}"
    ".k-col{display:flex;flex-direction:column;gap:8px;}";

std::string paint(const Node& node) {
    std::string parts;
    auto add = [&](const char* key, const char* value) {
        if (!parts.empty()) {
            parts.push_back(';');
        }
        parts += key;
        parts.push_back(':');
        parts += value;
    };
    if (node.bg == "bg-paper") add("background", "#fffdf8");
    else if (node.bg == "bg-tint") add("background", "rgba(44,107,78,.12)");
    else if (node.bg == "bg-accent") add("background", "#2c6b4e");
    else if (node.bg == "bg-ink") add("background", "#2b261f");
    else if (node.bg == "bg-danger") add("background", "#8c3a32");
    else if (node.bg == "bg-none") add("background", "transparent");
    if (node.fg == "fg-ink") add("color", "#2b261f");
    else if (node.fg == "fg-soft") add("color", "#8a7f72");
    else if (node.fg == "fg-paper") add("color", "#fffdf8");
    else if (node.fg == "fg-accent") add("color", "#2c6b4e");
    else if (node.fg == "fg-danger") add("color", "#8c3a32");
    if (node.fs == "fs-12") add("font-size", "12px");
    else if (node.fs == "fs-14") add("font-size", "14px");
    else if (node.fs == "fs-16") add("font-size", "16px");
    else if (node.fs == "fs-20") add("font-size", "20px");
    else if (node.fs == "fs-28") add("font-size", "28px");
    if (node.radius == "radius-0") add("border-radius", "0");
    else if (node.radius == "radius-8") add("border-radius", "8px");
    else if (node.radius == "radius-12") add("border-radius", "12px");
    else if (node.radius == "radius-999") add("border-radius", "999px");
    if (node.align == "align-left") add("text-align", "left");
    else if (node.align == "align-center") add("text-align", "center");
    else if (node.align == "align-right") add("text-align", "right");
    if (!parts.empty()) {
        parts.push_back(';');
    }
    return parts;
}

std::string box_style(const Document& doc, const Node& node, bool top) {
    const DeviceSpec& dev = device_of(doc);
    const int px = kCell;
    if (top && !dev.free) {
        return "position:relative;width:100%;height:" + std::to_string(node.h * px) + "px;" + paint(node);
    }
    if (!top) {
        return "position:relative;width:" + std::to_string(node.w * px) + "px;height:" +
               std::to_string(node.h * px) + "px;" + paint(node);
    }
    return "position:absolute;left:" + std::to_string(node.x * px) + "px;top:" +
           std::to_string(node.y * px) + "px;width:" + std::to_string(node.w * px) + "px;height:" +
           std::to_string(node.h * px) + "px;" + paint(node);
}

std::string page_css(const Document& doc) {
    const DeviceSpec& dev = device_of(doc);
    if (!dev.free) {
        return ".page{display:flex;flex-direction:column;gap:8px;width:" + std::to_string(dev.w) +
               "px;min-height:" + std::to_string(dev.h) +
               "px;margin:0 auto;background:#fffdf8;padding:16px;}";
    }
    return ".page{position:relative;width:" + std::to_string(dev.w) + "px;height:" +
           std::to_string(dev.h) + "px;margin:0 auto;background:#fffdf8;overflow:hidden;}";
}

void emit(std::ostream& out, const Document& doc, const Node& node, bool top, int indent) {
    const std::string pad(static_cast<size_t>(indent) * 2, ' ');
    const std::string cls = "k-" + node.kind + " n-" + node.id;
    const std::string style = box_style(doc, node, top);
    const std::string text = esc(node.text);
    if (node.kind == "heading") {
        out << pad << "<h2 class=\"" << cls << "\" style=\"" << style << "\">" << text << "</h2>\n";
        return;
    }
    if (node.kind == "text") {
        out << pad << "<p class=\"" << cls << "\" style=\"" << style << "\">" << text << "</p>\n";
        return;
    }
    if (node.kind == "button") {
        out << pad << "<button class=\"" << cls << "\" style=\"" << style
            << "\" type=\"button\">" << text << "</button>\n";
        return;
    }
    if (node.kind == "input") {
        out << pad << "<input class=\"" << cls << "\" style=\"" << style
            << "\" type=\"text\" placeholder=\"" << esc_attr(node.text) << "\">\n";
        return;
    }
    if (node.kind == "area") {
        out << pad << "<textarea class=\"" << cls << "\" style=\"" << style << "\" placeholder=\""
            << esc_attr(node.text) << "\"></textarea>\n";
        return;
    }
    if (node.kind == "check") {
        out << pad << "<label class=\"" << cls << "\" style=\"" << style
            << "\"><input type=\"checkbox\"> <span>" << text << "</span></label>\n";
        return;
    }
    if (node.kind == "image") {
        out << pad << "<div class=\"" << cls << "\" style=\"" << style << "\" role=\"img\">" << text
            << "</div>\n";
        return;
    }
    if (node.kind == "rule") {
        out << pad << "<hr class=\"" << cls << "\" style=\"" << style << "\">\n";
        return;
    }
    out << pad << "<div class=\"" << cls << "\" style=\"" << style << "\">\n";
    for (const auto& child : node.children) {
        emit(out, doc, *child, false, indent + 1);
    }
    out << pad << "</div>\n";
}

}  // namespace

std::string generate_html(const Document& doc) {
    std::ostringstream body;
    for (const auto& node : doc.roots) {
        emit(body, doc, *node, true, 2);
    }
    const std::string title = doc.title.empty() ? "未命名界面" : doc.title;
    std::ostringstream out;
    out << "<!DOCTYPE html>\n"
        << "<html lang=\"zh-CN\">\n"
        << "<head>\n"
        << "<meta charset=\"utf-8\">\n"
        << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
        << "<title>" << esc(title) << "</title>\n"
        << "<style>\n"
        << "*{box-sizing:border-box;}\n"
        << "body{margin:0;background:#fbf6ec;color:#2b261f;font-family:\"Microsoft YaHei\","
           "\"PingFang SC\",\"Noto Sans SC\",sans-serif;}\n"
        << page_css(doc) << "\n"
        << kKindCss << "\n"
        << "</style>\n"
        << "</head>\n"
        << "<body>\n"
        << "  <div class=\"page\">\n"
        << body.str() << "  </div>\n"
        << "</body>\n"
        << "</html>\n";
    return out.str();
}

}  // namespace designer
