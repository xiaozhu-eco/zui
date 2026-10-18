#ifndef ZUI_SOURCE_CSS_VALUES_INTERNAL_H
#define ZUI_SOURCE_CSS_VALUES_INTERNAL_H

// Internals shared by the css value translation units. The helpers are declared first because the
// shared Scan cursor calls them; each helper is defined once in media_query.cpp/substitution.cpp.

#include "css/values.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace zui {
namespace css {
namespace values_detail {

char lower_char(unsigned char c);
std::string lower_copy(std::string_view s);
bool is_ident_char(unsigned char c);
float absolute_px(float num, std::string_view unit);
std::size_t matching_paren(std::string_view s, std::size_t open);
bool at_function(std::string_view s, std::size_t i, std::string_view name);
std::string_view trim_view(std::string_view s);
std::string css_px(float v);
bool property_supported(std::string_view name);
struct Scan {
    std::string_view s;
    size_t i = 0;

    void skip() {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
            ++i;
        }
    }

    bool eof() {
        skip();
        return i >= s.size();
    }

    bool eat(char c) {
        skip();
        if (i < s.size() && s[i] == c) {
            ++i;
            return true;
        }
        return false;
    }

    bool ident_is(std::string_view word) {
        skip();
        if (s.size() - i < word.size()) {
            return false;
        }
        for (size_t k = 0; k < word.size(); ++k) {
            if (lower_char(static_cast<unsigned char>(s[i + k])) != word[k]) {
                return false;
            }
        }
        const size_t end = i + word.size();
        if (end < s.size() && is_ident_char(static_cast<unsigned char>(s[end]))) {
            return false;
        }
        return true;
    }

    bool eat_ident(std::string_view word) {
        if (!ident_is(word)) {
            return false;
        }
        i += word.size();
        return true;
    }

    bool read_ident(std::string& out) {
        skip();
        if (i >= s.size() || !is_ident_char(static_cast<unsigned char>(s[i]))) {
            return false;
        }
        const size_t start = i;
        if (s[i] == '-' && i + 1 < s.size() && s[i + 1] == '-') {
            i += 2;
        }
        while (i < s.size() && is_ident_char(static_cast<unsigned char>(s[i]))) {
            ++i;
        }
        out.assign(s.substr(start, i - start));
        return !out.empty();
    }
};

}  // namespace values_detail
}  // namespace css
}  // namespace zui

#endif
