#include "textutil.h"

#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

namespace writer {

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

Ymd today_ymd() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    return {local.tm_year + 1900, local.tm_mon + 1, local.tm_mday};
}

std::string ymd_text(Ymd y) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y.year, y.month, y.day);
    return buf;
}

std::string today() { return ymd_text(today_ymd()); }

int utf8_len(const std::string& text) {
    int count = 0;
    for (unsigned char c : text) {
        if ((c & 0xc0) != 0x80) {
            ++count;
        }
    }
    return count;
}

bool heading_line(const std::string& line, int& level, std::string& title) {
    std::size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
        ++i;
    }
    if (i < line.size() && line[i] == '#') {
        level = 0;
        while (i < line.size() && line[i] == '#' && level < 3) {
            ++level;
            ++i;
        }
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
            ++i;
        }
        title = line.substr(i);
        return !title.empty();
    }
    const bool chapter = line.find("章") != std::string::npos;
    const bool section = line.find("节") != std::string::npos;
    if (line.find("第") != std::string::npos && (chapter || section) && utf8_len(line) <= 40) {
        level = section && !chapter ? 2 : 1;
        title = trim(line);
        return !title.empty();
    }
    return false;
}

int days_in_month(int year, int month) {
    static const int kDays[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2) {
        const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    if (month < 1 || month > 12) {
        return 30;
    }
    return kDays[month];
}

int monday_index(int year, int month, int day) {
    std::tm local{};
    local.tm_year = year - 1900;
    local.tm_mon = month - 1;
    local.tm_mday = day;
    local.tm_hour = 12;
    if (std::mktime(&local) == static_cast<std::time_t>(-1)) {
        return 0;
    }
    return (local.tm_wday + 6) % 7;
}

void write_bytes(const std::filesystem::path& path, const std::string& text) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

}  // namespace writer
