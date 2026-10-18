#ifndef ZUI_SOURCE_ASCII_H
#define ZUI_SOURCE_ASCII_H

// One ASCII case and whitespace helper for every domain. Callers that already hold a
// lower-case literal still go through these, so a second copy cannot drift.

#include <cctype>
#include <string>
#include <string_view>

namespace zui {

inline char ascii_lower_char(unsigned char c) {
    return static_cast<char>(std::tolower(c));
}

inline std::string ascii_lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = ascii_lower_char(static_cast<unsigned char>(c));
    }
    return out;
}

inline bool equals_ci(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (ascii_lower_char(static_cast<unsigned char>(a[i])) !=
            ascii_lower_char(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

inline bool starts_with_ci(std::string_view text, std::string_view prefix) {
    if (text.size() < prefix.size()) {
        return false;
    }
    return equals_ci(text.substr(0, prefix.size()), prefix);
}

inline std::string_view trim_sv(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

inline void trim_inplace(std::string& s) {
    const std::string_view trimmed = trim_sv(s);
    if (trimmed.data() != s.data() || trimmed.size() != s.size()) {
        s.assign(trimmed.begin(), trimmed.end());
    }
}

}  // namespace zui

#endif
