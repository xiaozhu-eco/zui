#include "paint/display_list.h"
#include "paint/painter_internal.h"

#include "ascii.h"
#include "css/values.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace zui {
namespace paint {
namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

int hexv(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return 0;
}

}  // namespace

Color parse_color(std::string_view css) {
    // Prefer the shared css parser (system colours Highlight / HighlightText, named colours, …).
    if (const std::optional<css::CssRgba> parsed = css::parse_css_color(css)) {
        return Color{parsed->r, parsed->g, parsed->b, parsed->a};
    }
    css = trim(css);
    if (css.empty() || css == "transparent" || css == "none") {
        return Color::transparent();
    }
    // Fallback keeps the previous hex / rgb path for any edge the shared parser rejects.
    if (css.size() > 0 && css[0] == '#') {
        if (css.size() == 4) {
            int r = hexv(css[1]);
            int g = hexv(css[2]);
            int b = hexv(css[3]);
            return Color{static_cast<uint8_t>(r * 17), static_cast<uint8_t>(g * 17),
                         static_cast<uint8_t>(b * 17), 255};
        }
        if (css.size() >= 7) {
            return Color{static_cast<uint8_t>((hexv(css[1]) << 4) | hexv(css[2])),
                         static_cast<uint8_t>((hexv(css[3]) << 4) | hexv(css[4])),
                         static_cast<uint8_t>((hexv(css[5]) << 4) | hexv(css[6])), 255};
        }
    }
    auto eq = [&](const char* n) {
        if (css.size() != std::char_traits<char>::length(n)) {
            return false;
        }
        for (size_t i = 0; i < css.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(css[i])) !=
                static_cast<unsigned char>(n[i])) {
                return false;
            }
        }
        return true;
    };
    if (eq("white")) {
        return Color{255, 255, 255, 255};
    }
    if (eq("black")) {
        return Color{0, 0, 0, 255};
    }
    if (eq("red")) {
        return Color{255, 0, 0, 255};
    }

    std::string s(css);
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (s.rfind("rgb", 0) == 0) {
        auto lp = s.find('(');
        auto rp = s.rfind(')');
        if (lp != std::string::npos && rp > lp) {
            std::string inner = s.substr(lp + 1, rp - lp - 1);
            int vals[4] = {0, 0, 0, 255};
            int n = 0;
            size_t i = 0;
            while (i < inner.size() && n < 4) {
                while (i < inner.size() && (inner[i] == ',' || inner[i] == '/' ||
                                            std::isspace(static_cast<unsigned char>(inner[i])))) {
                    ++i;
                }
                if (i >= inner.size()) {
                    break;
                }
                char* end = nullptr;
                const float f = std::strtof(inner.c_str() + static_cast<int>(i), &end);
                if (n == 3 && f <= 1.0f) {
                    vals[n++] = static_cast<int>(f * 255.0f + 0.5f);
                } else {
                    vals[n++] = static_cast<int>(f);
                }
                while (i < inner.size() && inner[i] != ',' && inner[i] != '/') {
                    ++i;
                }
            }
            auto clamp = [](int v) { return static_cast<uint8_t>(std::max(0, std::min(255, v))); };
            return Color{clamp(vals[0]), clamp(vals[1]), clamp(vals[2]),
                         n >= 4 ? clamp(vals[3]) : static_cast<uint8_t>(255)};
        }
    }
    return Color::transparent();
}

bool color_is_transparent(Color c) { return c.a == 0; }

}  // namespace paint
}  // namespace zui
