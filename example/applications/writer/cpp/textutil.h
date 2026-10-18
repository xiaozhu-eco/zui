#ifndef WRITER_TEXTUTIL_H
#define WRITER_TEXTUTIL_H

#include <filesystem>
#include <string>
#include <string_view>

namespace writer {

struct Ymd {
    int year = 1970;
    int month = 1;
    int day = 1;
};

std::string trim(std::string_view in);
Ymd today_ymd();
std::string ymd_text(Ymd y);
std::string today();
int utf8_len(const std::string& text);
bool heading_line(const std::string& line, int& level, std::string& title);
int days_in_month(int year, int month);
int monday_index(int year, int month, int day);
void write_bytes(const std::filesystem::path& path, const std::string& text);
std::string read_bytes(const std::filesystem::path& path);

}  // namespace writer

#endif
