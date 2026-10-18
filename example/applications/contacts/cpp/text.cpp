#include "text.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace contacts {
namespace {

bool is_js_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

/// `stop_at_dot` is only for the label between `@` and the first `.`.
bool take_label(const std::string& email, std::size_t& i, bool stop_at_dot) {
    const std::size_t start = i;
    while (i < email.size()) {
        const unsigned char c = static_cast<unsigned char>(email[i]);
        if (c == '@' || is_js_space(c) || (stop_at_dot && c == '.')) {
            break;
        }
        ++i;
    }
    return i > start;
}

#ifdef _WIN32
std::wstring utf8_to_wide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(n > 0 ? static_cast<std::size_t>(n) : 0, L'\0');
    if (n > 0) {
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n);
    }
    return out;
}
#endif

}  // namespace

std::string trim(std::string_view in) {
    std::size_t a = 0;
    while (a < in.size() && (in[a] == ' ' || in[a] == '\t' || in[a] == '\n' || in[a] == '\r')) {
        ++a;
    }
    std::size_t b = in.size();
    while (b > a && (in[b - 1] == ' ' || in[b - 1] == '\t' || in[b - 1] == '\n' || in[b - 1] == '\r')) {
        --b;
    }
    return std::string(in.substr(a, b - a));
}

std::string first_char(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const unsigned char lead = static_cast<unsigned char>(text[0]);
    std::size_t n = 1;
    if ((lead & 0xe0) == 0xc0) {
        n = 2;
    } else if ((lead & 0xf0) == 0xe0) {
        n = 3;
    } else if ((lead & 0xf8) == 0xf0) {
        n = 4;
    }
    if (n > text.size()) {
        n = text.size();
    }
    return text.substr(0, n);
}

std::string group_label(const std::string& id) {
    if (id == "work") {
        return "同事";
    }
    if (id == "family") {
        return "家人";
    }
    if (id == "friend") {
        return "朋友";
    }
    return "其他";
}

bool valid_email(const std::string& email) {
    std::size_t i = 0;
    if (!take_label(email, i, false) || i >= email.size() || email[i] != '@') {
        return false;
    }
    ++i;
    if (!take_label(email, i, true) || i >= email.size() || email[i] != '.') {
        return false;
    }
    ++i;
    if (!take_label(email, i, false) || i != email.size()) {
        return false;
    }
    return true;
}

#ifdef _WIN32
int name_cmp(const std::string& a, const std::string& b) {
    const std::wstring wa = utf8_to_wide(a);
    const std::wstring wb = utf8_to_wide(b);
    return CompareStringW(LOCALE_USER_DEFAULT, 0, wa.c_str(), -1, wb.c_str(), -1);
}
#else
int name_cmp(const std::string& a, const std::string& b) {
    if (a < b) {
        return 1;
    }
    if (b < a) {
        return 3;
    }
    return 2;
}
#endif

}  // namespace contacts
