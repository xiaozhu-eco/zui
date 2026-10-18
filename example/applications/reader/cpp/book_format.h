#ifndef READER_BOOK_FORMAT_H
#define READER_BOOK_FORMAT_H

#include <filesystem>
#include <string>

namespace reader {

enum class BookFormat {
    Unknown = 0,
    Txt,
    Epub,
    Pdf,
};

BookFormat detect_format(const std::filesystem::path& path);
bool is_book(const std::filesystem::path& path);
const char* format_name(BookFormat format);
std::string lower_ext(const std::filesystem::path& path);

}  // namespace reader

#endif
