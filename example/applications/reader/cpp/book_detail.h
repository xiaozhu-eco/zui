#ifndef READER_BOOK_DETAIL_H
#define READER_BOOK_DETAIL_H

#include "book.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace reader {

constexpr const char* kDi = "\xe7\xac\xac";
constexpr const char* kZhang = "\xe7\xab\xa0";
constexpr const char* kJie = "\xe8\x8a\x82";
constexpr const char* kHui = "\xe5\x9b\x9e";
constexpr const char* kJuan = "\xe5\x8d\xb7";
constexpr const char* kBu = "\xe9\x83\xa8";
constexpr const char* kJi = "\xe9\x9b\x86";
constexpr const char* kJianJie = "\xe7\xae\x80\xe4\xbb\x8b";
constexpr const char* kZhengWen = "\xe6\xad\xa3\xe6\x96\x87";
constexpr const char* kPian = "\xe7\xaf\x87";
constexpr const char* kJuHao = "\xe3\x80\x82";
constexpr const char* kComma = "\xef\xbc\x8c";
constexpr const char* kEmpty = "\xef\xbc\x88\xe7\xa9\xba\xef\xbc\x89";
constexpr const char* kZhangCount = " \xe7\xab\xa0";
constexpr const char* kDot = " \xc2\xb7 ";
constexpr const char* kUnread = "\xe6\x9c\xaa\xe8\xaf\xbb";
constexpr const char* kFinished = "\xe5\xb7\xb2\xe8\xaf\xbb\xe5\xae\x8c";
constexpr const char* kReadOnce = "\xe8\xaf\xbb\xe8\xbf\x87";
constexpr const char* kTitleKeys[] = {
    "\xe4\xb9\xa6\xe5\x90\x8d\xef\xbc\x9a",
    "\xe4\xb9\xa6\xe5\x90\x8d:",
    "\xe6\xa0\x87\xe9\xa2\x98\xef\xbc\x9a",
    "\xe6\xa0\x87\xe9\xa2\x98:",
};
constexpr const char* kAuthorKeys[] = {
    "\xe4\xbd\x9c\xe8\x80\x85\xef\xbc\x9a",
    "\xe4\xbd\x9c\xe8\x80\x85:",
};
constexpr const char* kSpecialHeads[] = {
    "\xe5\xba\x8f\xe7\xab\xa0", "\xe6\xa5\x94\xe5\xad\x90", "\xe5\x89\x8d\xe8\xa8\x80",
    "\xe5\xba\x8f\xe8\xa8\x80", "\xe5\xbc\x95\xe5\xad\x90", "\xe5\xb0\xbe\xe5\xa3\xb0",
    "\xe5\x90\x8e\xe8\xae\xb0", "\xe7\x95\xaa\xe5\xa4\x96", "\xe7\xbb\x88\xe7\xab\xa0",
    "\xe9\x99\x84\xe5\xbd\x95",
};
constexpr const char* kMarks[] = {kZhang, kJie, kHui, kJuan, kBu, kJi, kPian};

bool starts_with(std::string_view s, std::string_view prefix);
std::size_t cp_count(std::string_view s);
std::string decode_book(std::string bytes);
std::string read_file(const std::filesystem::path& path);

enum class BookEncoding { Utf8, Utf16Le, Gb18030, Gbk, Raw };

BookEncoding detect_book_encoding(const std::filesystem::path& path);
std::string decode_file_line(BookEncoding encoding, std::string_view bytes);

std::size_t trim_prefix(std::string_view s);
std::string trim_copy(std::string_view in);
std::vector<std::string> split_lines(std::string_view text);
std::string clean_line(std::string_view raw);
std::string normalize_head(std::string_view line);
bool looks_like_url(std::string_view text);
std::size_t utf8_cp_len(std::string_view s, std::size_t i);
std::string_view utf8_cp_at(std::string_view s, std::size_t i);
int parse_chapter_number(std::string_view text, std::size_t& i);

struct HeadingInfo {
    std::string title;
    bool has_num = false;
    int num = 0;
    std::string rest;
};

bool same_chapter(const HeadingInfo& info, bool have_last, int last_num, const std::string& last_rest);
HeadingInfo heading_info(const std::string& line);
bool is_chapter_heading(const std::string& line);
const char* meta_value(const std::string& line, const char* const* keys, std::size_t count);
Book parse_book(const std::filesystem::path& path, const std::string& text);

}  // namespace reader

#endif
