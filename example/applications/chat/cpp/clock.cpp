#include "clock.h"

#include <chrono>
#include <cstdio>
#include <ctime>

namespace chat {

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

std::string fold_ascii(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

long long now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

long long ago(double hours) { return now_ms() - static_cast<long long>(hours * 3600000.0); }

std::tm local_from(long long ms) {
    const std::time_t t = static_cast<std::time_t>(ms / 1000);
    std::tm out{};
#ifdef _WIN32
    localtime_s(&out, &t);
#else
    localtime_r(&t, &out);
#endif
    return out;
}

std::string iso_day(const std::tm& tm) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return buf;
}

std::string day_label(long long ms) {
    const std::tm when = local_from(ms);
    const std::tm today = local_from(now_ms());
    if (iso_day(when) == iso_day(today)) {
        return "今天";
    }
    const std::tm yday = local_from(now_ms() - 86400000);
    if (iso_day(when) == iso_day(yday)) {
        return "昨天";
    }
    return std::to_string(when.tm_mon + 1) + "月" + std::to_string(when.tm_mday) + "日";
}

std::string clock_label(long long ms) {
    const std::tm when = local_from(ms);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", when.tm_hour, when.tm_min);
    return buf;
}

}  // namespace chat
