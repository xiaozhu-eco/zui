#include "image/svg/parser/paint.h"

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

}  // namespace

bool Paint::parse(std::string_view text, Paint& out) {
    text = trim(text);
    out = Paint{};
    if (text.empty() || text == "none") {
        out.kind = Kind::None;
        return true;
    }
    if (text.size() > 4 && text.substr(0, 4) == "url(") {
        const std::size_t close = text.rfind(')');
        if (close == std::string_view::npos) {
            return false;
        }
        std::string_view body = trim(text.substr(4, close - 4));
        if (!body.empty() && (body.front() == '\'' || body.front() == '"')) {
            body.remove_prefix(1);
        }
        if (!body.empty() && (body.back() == '\'' || body.back() == '"')) {
            body.remove_suffix(1);
        }
        body = trim(body);
        if (!body.empty() && body.front() == '#') {
            body.remove_prefix(1);
        }
        out.kind = Kind::Gradient;
        out.gradient_id.assign(body.begin(), body.end());
        return !out.gradient_id.empty();
    }
    if (!Color::parse(text, out.color) || out.color.none) {
        out.kind = Kind::None;
        return text == "none";
    }
    out.kind = Kind::Color;
    return true;
}

}  // namespace svg
}  // namespace image
}  // namespace zui
