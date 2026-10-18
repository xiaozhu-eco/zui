#include "css/values.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#include "css/values_internal.h"

namespace zui {
namespace css {

namespace values_detail {

bool at_function(std::string_view s, size_t i, std::string_view name) {
    if (i > 0 && is_ident_char(static_cast<unsigned char>(s[i - 1]))) {
        return false;
    }
    if (s.size() - i < name.size() + 1) {
        return false;
    }
    for (size_t k = 0; k < name.size(); ++k) {
        if (lower_char(static_cast<unsigned char>(s[i + k])) != name[k]) {
            return false;
        }
    }
    return s[i + name.size()] == '(';
}

std::string_view trim_view(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

std::optional<std::string> substitute_at(std::string_view value, const CustomLookup& lookup,
                                         const MediaState& media, int depth);

std::string css_px(float v) {
    char buf[64];
    if (std::isfinite(v) && v == static_cast<float>(static_cast<int>(v))) {
        std::snprintf(buf, sizeof(buf), "%dpx", static_cast<int>(v));
    } else {
        std::snprintf(buf, sizeof(buf), "%gpx", static_cast<double>(v));
    }
    return std::string(buf);
}

std::optional<std::string> eval_var(std::string_view args, const CustomLookup& lookup,
                                    const MediaState& media, int depth) {
    Scan sc{args, 0};
    std::string name;
    if (!sc.read_ident(name) || name.size() < 3 || name[0] != '-' || name[1] != '-') {
        return std::nullopt;
    }
    sc.skip();
    std::optional<std::string_view> fallback;
    if (sc.eat(',')) {
        fallback = trim_view(args.substr(sc.i));
    } else if (!sc.eof()) {
        return std::nullopt;
    }
    if (lookup) {
        if (std::optional<std::string> got = lookup(name)) {
            return got;
        }
    }
    if (!fallback) {
        return std::nullopt;
    }
    return substitute_at(*fallback, lookup, media, depth + 1);
}

std::optional<std::string> eval_env(std::string_view args, const CustomLookup& lookup,
                                    const MediaState& media, int depth) {
    Scan sc{args, 0};
    std::string name;
    if (!sc.read_ident(name)) {
        return std::nullopt;
    }
    const std::string n = lower_copy(name);
    sc.skip();
    std::optional<std::string_view> fallback;
    if (sc.eat(',')) {
        fallback = trim_view(args.substr(sc.i));
    } else if (!sc.eof()) {
        return std::nullopt;
    }
    if (n == "safe-area-inset-top" || n == "safe-area-max-inset-top") {
        return css_px(media.safe_top);
    }
    if (n == "safe-area-inset-right" || n == "safe-area-max-inset-right") {
        return css_px(media.safe_right);
    }
    if (n == "safe-area-inset-bottom" || n == "safe-area-max-inset-bottom") {
        return css_px(media.safe_bottom);
    }
    if (n == "safe-area-inset-left" || n == "safe-area-max-inset-left") {
        return css_px(media.safe_left);
    }
    if (n == "keyboard-inset-bottom") {
        return css_px(media.keyboard_bottom);
    }
    // Other keyboard inset edges are 0 until a host reports them.
    if (n == "keyboard-inset-top" || n == "keyboard-inset-left" || n == "keyboard-inset-right" ||
        n == "keyboard-inset-width" || n == "keyboard-inset-height") {
        return std::string("0px");
    }
    // Overlay titlebar geometry (Window Controls Overlay). Desktop without overlay → 0.
    if (n == "titlebar-area-x" || n == "titlebar-area-y" || n == "titlebar-area-width" ||
        n == "titlebar-area-height") {
        return std::string("0px");
    }
    if (!fallback) {
        return std::nullopt;
    }
    return substitute_at(*fallback, lookup, media, depth + 1);
}

std::optional<std::string> substitute_at(std::string_view value, const CustomLookup& lookup,
                                         const MediaState& media, int depth) {
    if (depth > 64) {
        return std::nullopt;
    }
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size();) {
        const bool is_var = at_function(value, i, "var");
        const bool is_env = at_function(value, i, "env");
        if (!is_var && !is_env) {
            out.push_back(value[i]);
            ++i;
            continue;
        }
        const size_t name_len = is_var ? 3 : 3;
        const size_t open = i + name_len;
        const size_t close = matching_paren(value, open);
        if (close == std::string_view::npos) {
            return std::nullopt;
        }
        const std::string_view args = value.substr(open + 1, close - open - 1);
        std::optional<std::string> repl =
            is_var ? eval_var(args, lookup, media, depth) : eval_env(args, lookup, media, depth);
        if (!repl) {
            return std::nullopt;
        }
        out += *repl;
        i = close + 1;
    }
    return out;
}

/// Property names zui accepts in `@supports (prop: value)`. Custom properties (`--*`) always
/// support. Unknown names fail so feature queries for unimplemented APIs stay false.

}  // namespace values_detail

using namespace values_detail;

std::optional<std::string> substitute(std::string_view value, const CustomLookup& lookup,
                                      const MediaState& media) {
    return substitute_at(value, lookup, media, 0);
}

bool contains_substitution(std::string_view value) {
    // `at_function` is a real call under /Od /Ob0. Finalize scans every computed value, so two
    // calls per character dominated a cold cascade. The common byte is neither 'v' nor 'e' and
    // stays in this loop; the ident-boundary check runs only on those two letters.
    const char* const s = value.data();
    const size_t n = value.size();
    for (size_t i = 0; i + 4 <= n; ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const unsigned char low =
            (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c + ('a' - 'A')) : c;
        if (low != 'v' && low != 'e') {
            continue;
        }
        if (i > 0 && is_ident_char(static_cast<unsigned char>(s[i - 1]))) {
            continue;
        }
        const unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
        const unsigned char c2 = static_cast<unsigned char>(s[i + 2]);
        if (s[i + 3] != '(') {
            continue;
        }
        if (low == 'v') {
            if ((c1 == 'a' || c1 == 'A') && (c2 == 'r' || c2 == 'R')) {
                return true;
            }
        } else if ((c1 == 'n' || c1 == 'N') && (c2 == 'v' || c2 == 'V')) {
            return true;
        }
    }
    return false;
}
}  // namespace css
}  // namespace zui
