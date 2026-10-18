#ifndef ZUI_CSS_ENGINE_NESTING_H
#define ZUI_CSS_ENGINE_NESTING_H

#include "css/engine/strip_keyframes.h"

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace zui {
namespace css {

/// Flattens CSS Nesting so a sheet lexbor can parse as ordinary style rules.
/// `&` becomes `:is(<parent>)`. A nested selector with no `&` is a descendant of that
/// parent, and a leading combinator stays attached to it. `@media` / `@supports` /
/// `@container` / `@layer` wrapped around a nested rule stay wrapped. A sheet with no
/// nested style rule is returned unchanged.
inline std::string css_expand_nesting(std::string_view css);

namespace nesting_detail {

inline void skip_ws(std::string_view css, std::size_t& i) {
    while (i < css.size()) {
        if (css[i] == '/' && i + 1 < css.size() && css[i + 1] == '*') {
            keyframes_scan::skip_comment(css, i);
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(css[i])) == 0) {
            break;
        }
        ++i;
    }
}

inline std::string_view trim_view(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())) != 0) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())) != 0) {
        s.remove_suffix(1);
    }
    return s;
}

inline bool ident_start(std::string_view css, std::size_t i) {
    if (i >= css.size()) {
        return false;
    }
    const unsigned char c = static_cast<unsigned char>(css[i]);
    if (std::isalpha(c) != 0 || c == '_') {
        return true;
    }
    if (c == '-' && i + 1 < css.size()) {
        const unsigned char n = static_cast<unsigned char>(css[i + 1]);
        return std::isalnum(n) != 0 || n == '_' || n == '-';
    }
    return false;
}

inline std::size_t find_delim(std::string_view css, std::size_t i, bool brace, bool semi,
                              bool close_brace) {
    int paren = 0;
    int bracket = 0;
    while (i < css.size()) {
        if (css[i] == '/' && i + 1 < css.size() && css[i + 1] == '*') {
            keyframes_scan::skip_comment(css, i);
            continue;
        }
        const char c = css[i];
        if (c == '"' || c == '\'') {
            const char q = c;
            ++i;
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
        if (c == '(') {
            ++paren;
        } else if (c == ')' && paren > 0) {
            --paren;
        } else if (c == '[') {
            ++bracket;
        } else if (c == ']' && bracket > 0) {
            --bracket;
        } else if (paren == 0 && bracket == 0) {
            if ((brace && c == '{') || (semi && c == ';') || (close_brace && c == '}')) {
                return i;
            }
        }
        ++i;
    }
    return css.size();
}

inline bool looks_like_declaration(std::string_view css, std::size_t i) {
    skip_ws(css, i);
    if (!ident_start(css, i)) {
        return false;
    }
    ++i;
    while (i < css.size() && keyframes_scan::ident_char(css[i])) {
        ++i;
    }
    skip_ws(css, i);
    return i < css.size() && css[i] == ':';
}

inline bool has_amp(std::string_view s) {
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '"' || s[i] == '\'') {
            const char q = s[i++];
            while (i < s.size() && s[i] != q) {
                if (s[i] == '\\' && i + 1 < s.size()) {
                    ++i;
                }
                ++i;
            }
            continue;
        }
        if (s[i] == '&') {
            return true;
        }
    }
    return false;
}

inline std::string replace_amp(std::string_view alt, std::string_view parent) {
    std::string out;
    out.reserve(alt.size() + parent.size() + 8);
    const std::string wrapped = ":is(" + std::string(parent) + ")";
    for (std::size_t i = 0; i < alt.size(); ++i) {
        if (alt[i] == '"' || alt[i] == '\'') {
            const char q = alt[i];
            const std::size_t start = i++;
            while (i < alt.size() && alt[i] != q) {
                if (alt[i] == '\\' && i + 1 < alt.size()) {
                    i += 2;
                    continue;
                }
                ++i;
            }
            if (i < alt.size()) {
                ++i;
            }
            out.append(alt.substr(start, i - start));
            --i;
            continue;
        }
        if (alt[i] == '&') {
            out += wrapped;
            continue;
        }
        out.push_back(alt[i]);
    }
    return out;
}

inline std::vector<std::string_view> split_commas(std::string_view s) {
    std::vector<std::string_view> out;
    int paren = 0;
    int bracket = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '"' || s[i] == '\'') {
            const char q = s[i++];
            while (i < s.size() && s[i] != q) {
                if (s[i] == '\\' && i + 1 < s.size()) {
                    ++i;
                }
                ++i;
            }
            continue;
        }
        if (s[i] == '(') {
            ++paren;
        } else if (s[i] == ')' && paren > 0) {
            --paren;
        } else if (s[i] == '[') {
            ++bracket;
        } else if (s[i] == ']' && bracket > 0) {
            --bracket;
        } else if (paren == 0 && bracket == 0 && s[i] == ',') {
            out.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    out.push_back(s.substr(start));
    return out;
}

inline std::string expand_selector(std::string_view parent, std::string_view child) {
    child = trim_view(child);
    parent = trim_view(parent);
    if (parent.empty()) {
        return std::string(child);
    }
    const std::string wrapped = ":is(" + std::string(parent) + ")";
    std::string out;
    bool first = true;
    for (const std::string_view part : split_commas(child)) {
        const std::string_view alt = trim_view(part);
        if (alt.empty()) {
            continue;
        }
        if (!first) {
            out += ", ";
        }
        first = false;
        if (has_amp(alt)) {
            out += replace_amp(alt, parent);
        } else if (alt.front() == '>' || alt.front() == '+' || alt.front() == '~') {
            out += wrapped;
            out.push_back(' ');
            out.append(alt);
        } else {
            out += wrapped;
            out.push_back(' ');
            out.append(alt);
        }
    }
    return out;
}

inline bool group_at(std::string_view name) {
    return name == "media" || name == "supports" || name == "container" || name == "layer";
}

struct Flattener {
    std::string_view css;
    std::string out;
    bool nested = false;

    void flatten_style_block(std::size_t& i, std::string_view parent);
    void flatten_sheet(std::size_t& i, bool stop_brace);
    void read_at(std::size_t& i, bool in_style, std::string_view parent);
};

inline void Flattener::read_at(std::size_t& i, bool in_style, std::string_view parent) {
    const std::size_t start = i;
    std::size_t n = i + 1;
    while (n < css.size() && keyframes_scan::ident_char(css[n])) {
        ++n;
    }
    std::string name(css.substr(i + 1, n - (i + 1)));
    for (char& c : name) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    const std::size_t stop = find_delim(css, i, true, true, false);
    if (stop >= css.size()) {
        out.append(css.substr(start));
        i = css.size();
        return;
    }
    if (css[stop] == ';') {
        out.append(css.substr(start, stop - start + 1));
        out.push_back('\n');
        i = stop + 1;
        return;
    }
    if (!group_at(name)) {
        const std::size_t end = keyframes_scan::skip_block(css, stop);
        out.append(css.substr(start, end - start));
        out.push_back('\n');
        i = end;
        return;
    }
    if (in_style) {
        nested = true;
    }
    out.append(css.substr(start, stop - start));
    out += "{\n";
    i = stop + 1;
    if (in_style) {
        flatten_style_block(i, parent);
    } else {
        flatten_sheet(i, true);
    }
    out += "}\n";
}

inline void Flattener::flatten_style_block(std::size_t& i, std::string_view parent) {
    std::string decls;
    while (i < css.size()) {
        const std::size_t before = i;
        skip_ws(css, i);
        if (i >= css.size()) {
            break;
        }
        if (css[i] == '}') {
            ++i;
            break;
        }
        if (css[i] == '@') {
            read_at(i, true, parent);
            if (i == before) {
                ++i;
            }
            continue;
        }
        if (!looks_like_declaration(css, i)) {
            const std::size_t brace = find_delim(css, i, true, false, true);
            if (brace >= css.size() || css[brace] != '{') {
                decls.append(css.substr(i));
                i = css.size();
                break;
            }
            const std::string sel = expand_selector(parent, css.substr(i, brace - i));
            i = brace + 1;
            nested = true;
            flatten_style_block(i, sel);
            continue;
        }
        const std::size_t start = i;
        const std::size_t end = find_delim(css, i, false, true, true);
        if (end < css.size() && css[end] == ';') {
            decls.append(css.substr(start, end - start + 1));
            i = end + 1;
        } else {
            decls.append(css.substr(start, end - start));
            i = end;
        }
        if (i == before) {
            ++i;
        }
    }
    if (!trim_view(parent).empty() && !trim_view(decls).empty()) {
        out.append(trim_view(parent));
        out += " {";
        out += decls;
        out += "}\n";
    }
}

inline void Flattener::flatten_sheet(std::size_t& i, bool stop_brace) {
    while (i < css.size()) {
        const std::size_t before = i;
        skip_ws(css, i);
        if (i >= css.size()) {
            break;
        }
        if (css[i] == '}') {
            if (stop_brace) {
                ++i;
            }
            break;
        }
        if (css[i] == '@') {
            read_at(i, false, {});
            if (i == before) {
                ++i;
            }
            continue;
        }
        const std::size_t brace = find_delim(css, i, true, false, stop_brace);
        if (brace >= css.size() || css[brace] != '{') {
            out.append(css.substr(i));
            i = css.size();
            break;
        }
        const std::string_view prelude = css.substr(i, brace - i);
        i = brace + 1;
        flatten_style_block(i, prelude);
        if (i == before) {
            ++i;
        }
    }
}

}  // namespace nesting_detail

inline std::string css_expand_nesting(std::string_view css) {
    nesting_detail::Flattener flat;
    flat.css = css;
    std::size_t i = 0;
    flat.flatten_sheet(i, false);
    if (!flat.nested) {
        return std::string(css);
    }
    return std::move(flat.out);
}

}  // namespace css
}  // namespace zui

#endif
