#ifndef ZUI_CSS_ENGINE_STRIP_KEYFRAMES_H
#define ZUI_CSS_ENGINE_STRIP_KEYFRAMES_H

#include <cctype>
#include <string>
#include <string_view>

namespace zui {
namespace css {
namespace keyframes_scan {

inline bool ident_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_';
}

inline void skip_comment(std::string_view css, std::size_t& i) {
    if (i + 1 < css.size() && css[i] == '/' && css[i + 1] == '*') {
        const std::size_t end = css.find("*/", i + 2);
        i = end == std::string_view::npos ? css.size() : end + 2;
    }
}

inline std::size_t skip_block(std::string_view css, std::size_t i) {
    int depth = 0;
    bool seen = false;
    while (i < css.size()) {
        skip_comment(css, i);
        if (i >= css.size()) {
            break;
        }
        if (css[i] == '"' || css[i] == '\'') {
            const char q = css[i++];
            while (i < css.size() && css[i] != q) {
                if (css[i] == '\\' && i + 1 < css.size()) {
                    i += 2;
                    continue;
                }
                ++i;
            }
            if (i < css.size()) {
                ++i;
            }
            continue;
        }
        if (css[i] == '{') {
            ++depth;
            seen = true;
        } else if (css[i] == '}') {
            --depth;
            ++i;
            if (seen && depth == 0) {
                return i;
            }
            continue;
        }
        ++i;
    }
    return css.size();
}

inline bool starts_keyframes(std::string_view css, std::size_t i) {
    constexpr std::string_view kAt = "@keyframes";
    if (css.compare(i, kAt.size(), kAt) != 0) {
        return false;
    }
    if (i > 0 && ident_char(css[i - 1])) {
        return false;
    }
    const std::size_t after = i + kAt.size();
    return after >= css.size() || !ident_char(css[after]);
}

}  // namespace keyframes_scan

/// Drops `@keyframes` rules so lexbor, which has no keyframes type, still accepts the rest of
/// the sheet. The original text stays available for `Animations`. Inline because the engine
/// module links without the transition translation unit.
inline std::string css_without_keyframes(std::string_view css) {
    using keyframes_scan::ident_char;
    using keyframes_scan::skip_block;
    using keyframes_scan::skip_comment;
    using keyframes_scan::starts_keyframes;
    std::string out;
    out.reserve(css.size());
    for (std::size_t i = 0; i < css.size();) {
        skip_comment(css, i);
        if (i >= css.size()) {
            break;
        }
        if (starts_keyframes(css, i)) {
            i += std::string_view("@keyframes").size();
            while (i < css.size() && std::isspace(static_cast<unsigned char>(css[i])) != 0) {
                ++i;
            }
            while (i < css.size() && ident_char(css[i])) {
                ++i;
            }
            while (i < css.size() && css[i] != '{') {
                ++i;
            }
            i = skip_block(css, i);
            continue;
        }
        out.push_back(css[i]);
        ++i;
    }
    return out;
}

}  // namespace css
}  // namespace zui

#endif
