#include "dates.h"

#include <cstdio>
#include <cstdlib>

namespace calendar {

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

std::tm today_tm() {
    const std::time_t now = std::time(nullptr);
    std::tm out{};
#ifdef _WIN32
    localtime_s(&out, &now);
#else
    localtime_r(&now, &out);
#endif
    return out;
}

std::string iso_from(int y, int m, int d) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, m, d);
    return buf;
}

bool parse_iso(const std::string& iso, int& y, int& m, int& d) {
    if (iso.size() < 10) {
        return false;
    }
    y = std::atoi(iso.c_str());
    m = std::atoi(iso.c_str() + 5);
    d = std::atoi(iso.c_str() + 8);
    return y > 0 && m >= 1 && m <= 12 && d >= 1 && d <= 31;
}

std::string add_days(const std::string& iso, int days) {
    int y = 0, m = 0, d = 0;
    if (!parse_iso(iso, y, m, d)) {
        return iso;
    }
    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon = m - 1;
    tm.tm_mday = d + days;
    tm.tm_isdst = -1;
    if (std::mktime(&tm) == static_cast<std::time_t>(-1)) {
        return iso;
    }
    return iso_from(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

int weekday_monday(const std::string& iso) {
    int y = 0, m = 0, d = 0;
    if (!parse_iso(iso, y, m, d)) {
        return 0;
    }
    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon = m - 1;
    tm.tm_mday = d;
    tm.tm_isdst = -1;
    if (std::mktime(&tm) == static_cast<std::time_t>(-1)) {
        return 0;
    }
    return (tm.tm_wday + 6) % 7;
}

std::string start_of_week(const std::string& iso) { return add_days(iso, -weekday_monday(iso)); }

int minutes_of(const std::string& hhmm) {
    const auto colon = hhmm.find(':');
    if (colon == std::string::npos) {
        return -1;
    }
    const int h = std::atoi(hhmm.c_str());
    const int m = std::atoi(hhmm.c_str() + colon + 1);
    if (h < 0 || h > 23 || m < 0 || m > 59) {
        return -1;
    }
    return h * 60 + m;
}

}  // namespace calendar
