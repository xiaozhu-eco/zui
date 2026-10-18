#include "image/svg/parser/color.h"

#include <cstdlib>
#include <string>

namespace zui {
namespace image {
namespace svg {
namespace {

int hex_nibble(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

float channel(int value) { return static_cast<float>(value) / 255.f; }

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\n' ||
                              text.front() == '\r')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\n' ||
                              text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

bool hex_byte(std::string_view text, int index, int& out) {
    const int hi = hex_nibble(text[static_cast<std::size_t>(index)]);
    const int lo = hex_nibble(text[static_cast<std::size_t>(index + 1)]);
    if (hi < 0 || lo < 0) {
        return false;
    }
    out = hi * 16 + lo;
    return true;
}

}  // namespace

bool Color::parse(std::string_view text, Color& out) {
    text = trim(text);
    if (text.empty()) {
        return false;
    }
    if (text == "none") {
        out = Color{};
        out.none = true;
        out.a = 0.f;
        return true;
    }
    if (text == "transparent") {
        out = Color{};
        out.a = 0.f;
        return true;
    }
    if (text == "black") {
        out = Color{};
        return true;
    }
    if (text == "white") {
        out = Color{};
        out.r = out.g = out.b = 1.f;
        return true;
    }
    if (text.front() == '#') {
        text.remove_prefix(1);
        int r = 0, g = 0, b = 0, a = 255;
        if (text.size() == 3 || text.size() == 4) {
            const int rn = hex_nibble(text[0]);
            const int gn = hex_nibble(text[1]);
            const int bn = hex_nibble(text[2]);
            if (rn < 0 || gn < 0 || bn < 0) {
                return false;
            }
            r = rn * 16 + rn;
            g = gn * 16 + gn;
            b = bn * 16 + bn;
            if (text.size() == 4) {
                const int an = hex_nibble(text[3]);
                if (an < 0) {
                    return false;
                }
                a = an * 16 + an;
            }
        } else if (text.size() == 6 || text.size() == 8) {
            if (!hex_byte(text, 0, r) || !hex_byte(text, 2, g) || !hex_byte(text, 4, b)) {
                return false;
            }
            if (text.size() == 8 && !hex_byte(text, 6, a)) {
                return false;
            }
        } else {
            return false;
        }
        out.r = channel(r);
        out.g = channel(g);
        out.b = channel(b);
        out.a = channel(a);
        out.none = false;
        return true;
    }
    const bool rgba = text.size() > 5 && text.substr(0, 5) == "rgba(";
    const bool rgb = text.size() > 4 && text.substr(0, 4) == "rgb(";
    if (!rgb && !rgba) {
        return false;
    }
    const std::size_t open = text.find('(');
    const std::size_t close = text.rfind(')');
    if (open == std::string_view::npos || close == std::string_view::npos || close < open) {
        return false;
    }
    std::string body(text.substr(open + 1, close - open - 1));
    float comp[4] = {0.f, 0.f, 0.f, 1.f};
    int n = 0;
    char* cursor = body.data();
    while (n < 4 && cursor != nullptr && *cursor != '\0') {
        char* end = nullptr;
        comp[n] = std::strtof(cursor, &end);
        if (end == cursor) {
            break;
        }
        if (end != nullptr && *end == '%') {
            comp[n] /= 100.f;
            ++end;
        } else if (n < 3) {
            comp[n] /= 255.f;
        }
        ++n;
        cursor = end;
        while (cursor != nullptr && (*cursor == ',' || *cursor == ' ' || *cursor == '\t')) {
            ++cursor;
        }
    }
    if (n < 3) {
        return false;
    }
    out.r = comp[0];
    out.g = comp[1];
    out.b = comp[2];
    out.a = n >= 4 ? comp[3] : 1.f;
    out.none = false;
    return true;
}

}  // namespace svg
}  // namespace image
}  // namespace zui
