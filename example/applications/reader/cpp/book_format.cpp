#include "book_format.h"

namespace reader {

std::string lower_ext(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    for (char& c : ext) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return ext;
}

BookFormat detect_format(const std::filesystem::path& path) {
    const std::string ext = lower_ext(path);
    if (ext == ".txt" || ext == ".text") {
        return BookFormat::Txt;
    }
    if (ext == ".epub") {
        return BookFormat::Epub;
    }
    if (ext == ".pdf") {
        return BookFormat::Pdf;
    }
    return BookFormat::Unknown;
}

bool is_book(const std::filesystem::path& path) {
    return detect_format(path) != BookFormat::Unknown;
}

const char* format_name(BookFormat format) {
    switch (format) {
        case BookFormat::Txt:
            return "TXT";
        case BookFormat::Epub:
            return "EPUB";
        case BookFormat::Pdf:
            return "PDF";
        case BookFormat::Unknown:
            break;
    }
    return "";
}

}  // namespace reader
