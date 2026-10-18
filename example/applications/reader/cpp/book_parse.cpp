#include "book_detail.h"
#include "book_epub.h"
#include "book_pdf.h"
#include "book_format.h"
namespace reader {

Book parse_book(const std::filesystem::path& path, const std::string& text) {
    Book book;
    book.path = path;
    book.key = to_utf8(path.filename());
    book.title = to_utf8(path.stem());
    const std::vector<std::string> lines = split_lines(text);

    std::vector<std::string> body;
    body.reserve(lines.size());
    bool seen_heading = false;
    bool used_title = false;
    for (const std::string& raw_line : lines) {
        const std::string line = clean_line(raw_line);
        if (!seen_heading && !line.empty()) {
            if (const char* key = meta_value(line, kTitleKeys, 4)) {
                const std::string value =
                    trim_copy(std::string_view(line).substr(std::char_traits<char>::length(key)));
                if (!value.empty()) {
                    book.title = value;
                }
                continue;
            }
            if (const char* key = meta_value(line, kAuthorKeys, 2)) {
                book.author =
                    trim_copy(std::string_view(line).substr(std::char_traits<char>::length(key)));
                continue;
            }
            // reader.js parseBook preamble: a short line naming the section ("简介：",
            // "书籍简介：") and any site banner are skipped, and the first short line without
            // sentence punctuation is the book name. Without these three the app prints the
            // header of the text file as the first paragraphs of the 简介 chapter.
            if (line.find(kJianJie) != std::string::npos && cp_count(line) <= 16) {
                continue;
            }
            if (looks_like_url(line)) {
                continue;
            }
            if (!used_title && !is_chapter_heading(line) && cp_count(line) <= 24 &&
                line.find(kComma) == std::string::npos &&
                line.find(kJuHao) == std::string::npos) {
                book.title = line;
                used_title = true;
                continue;
            }
            used_title = true;
        }
        if (is_chapter_heading(line)) {
            seen_heading = true;
        }
        body.push_back(line);
    }

    Chapter* cur = nullptr;
    bool have_last_heading = false;
    int last_num = 0;
    std::string last_rest;
    auto ensure = [&](const std::string& title) {
        book.chapters.push_back(Chapter{title, {}});
        cur = &book.chapters.back();
    };

    // reader.js keeps one paragraph per non-empty line. Joining wrapped lines into a
    // single giant <p> made long chapters fail to paint and disagreed with the page.
    for (const std::string& line : body) {
        const HeadingInfo info = heading_info(line);
        if (!info.title.empty()) {
            if (same_chapter(info, have_last_heading, last_num, last_rest)) {
                continue;
            }
            ensure(info.title);
            have_last_heading = info.has_num;
            last_num = info.num;
            last_rest = info.rest;
            continue;
        }
        if (line.empty()) {
            continue;
        }
        if (looks_like_url(line)) {
            continue;
        }
        if (cur == nullptr) {
            ensure(kJianJie);
        }
        cur->paragraphs.push_back(line);
    }

    if (book.chapters.empty()) {
        book.chapters.push_back(Chapter{kZhengWen, {}});
    }
    if (book.chapters.size() == 1 && book.chapters[0].title == kJianJie) {
        book.chapters[0].title = kZhengWen;
    }
    for (Chapter& chapter : book.chapters) {
        chapter.parsed = true;
        if (chapter.paragraphs.empty()) {
            chapter.paragraphs.push_back(kEmpty);
        }
    }
    long long total = 0;
    for (const Chapter& chapter : book.chapters) {
        for (const std::string& para : chapter.paragraphs) {
            total += static_cast<long long>(cp_count(para));
        }
    }
    book.chars = total;
    book.chapter_count = static_cast<int>(book.chapters.size());
    return book;
}

Book load_book(const std::filesystem::path& path) {
    const BookFormat format = detect_format(path);
    if (format == BookFormat::Epub) {
        Book book;
        book.path = path;
        book.key = to_utf8(path.filename());
        book.title = to_utf8(path.stem());
        book.format = BookFormat::Epub;
        if (!load_epub_window(book, 0, 100000)) {
            return book;
        }
        return book;
    }
    if (format == BookFormat::Pdf) {
        Book book;
        book.path = path;
        book.key = to_utf8(path.filename());
        book.title = to_utf8(path.stem());
        book.format = BookFormat::Pdf;
        if (!load_pdf_window(book, 0, 100000)) {
            return book;
        }
        return book;
    }
    Book book = parse_book(path, decode_book(read_file(path)));
    book.format = BookFormat::Txt;
    return book;
}

/// The shelf only needs the title, the author and the chapter count. Running the full
/// `parse_book` for it split five 20 MB novels into paragraph strings before the first window
/// could appear (~7 s); this walks each file once, keeps only the current line and never builds
/// a paragraph. `open_book` parses the saved chapter and three chapters on either side.
bool book_summary(const std::filesystem::path& path, Book& book) {
    const std::string text = decode_book(read_file(path));
    if (text.empty() && !std::filesystem::exists(path)) {
        return false;
    }
    // The shelf used to keep the scan result and forget which file it came from.
    // open then parsed an empty path and showed the synthetic "正文 / （空）" chapter.
    book.path = path;
    book.key = to_utf8(path.filename());
    book.title = to_utf8(path.stem());
    book.chapters.clear();
    book.chapter_count = 0;
    bool seen_heading = false;
    bool have_last_heading = false;
    int last_num = 0;
    std::string last_rest;
    for (const std::string& line : split_lines(text)) {
        const std::string cleaned = clean_line(line);
        if (!seen_heading && !cleaned.empty()) {
            if (const char* key = meta_value(cleaned, kTitleKeys, 4)) {
                const std::string value =
                    trim_copy(std::string_view(cleaned).substr(std::char_traits<char>::length(key)));
                if (!value.empty()) {
                    book.title = value;
                }
                continue;
            }
            if (const char* key = meta_value(cleaned, kAuthorKeys, 2)) {
                book.author =
                    trim_copy(std::string_view(cleaned).substr(std::char_traits<char>::length(key)));
                continue;
            }
        }
        const HeadingInfo info = heading_info(cleaned);
        if (info.title.empty()) {
            continue;
        }
        if (same_chapter(info, have_last_heading, last_num, last_rest)) {
            continue;
        }
        seen_heading = true;
        have_last_heading = info.has_num;
        last_num = info.num;
        last_rest = info.rest;
        ++book.chapter_count;
    }
    if (book.chapter_count == 0) {
        book.chapter_count = 1;
    }
    return true;
}

}  // namespace reader
