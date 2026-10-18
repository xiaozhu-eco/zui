#include "book_detail.h"
#include "book_epub.h"
#include "book_pdf.h"
#include "book_format.h"

#include <cstdint>
#include <fstream>
#include <utility>
#include <vector>

namespace reader {
namespace {

constexpr const char* kIndexMagic = "zui1";

std::filesystem::path index_file(const Book& book, const std::filesystem::path& given) {
    if (!given.empty()) {
        return given;
    }
    return chapter_index_path(book.path);
}

bool file_stamp(const std::filesystem::path& path, std::uint64_t& size, std::int64_t& mtime) {
    std::error_code ec;
    const auto bytes = std::filesystem::file_size(path, ec);
    if (ec) {
        return false;
    }
    const auto stamp = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return false;
    }
    size = static_cast<std::uint64_t>(bytes);
    mtime = static_cast<std::int64_t>(stamp.time_since_epoch().count());
    return true;
}

std::string index_title(std::string title) {
    for (char& c : title) {
        if (c == '\t' || c == '\n' || c == '\r') {
            c = ' ';
        }
    }
    return title;
}

void remember(Book& book, int index, const std::string& title, std::uint64_t offset) {
    if (index < 0) {
        return;
    }
    if (static_cast<int>(book.chapters.size()) <= index) {
        book.chapters.resize(static_cast<std::size_t>(index) + 1);
    }
    Chapter& chapter = book.chapters[static_cast<std::size_t>(index)];
    if (chapter.title.empty() && !title.empty()) {
        chapter.title = title;
    }
    if (chapter.file_offset == kChapterOffsetUnknown) {
        chapter.file_offset = offset;
    }
}

class FileLines {
public:
    bool open(const std::filesystem::path& path, BookEncoding encoding, std::uint64_t start) {
        in_.open(path, std::ios::binary);
        if (!in_) {
            return false;
        }
        encoding_ = encoding;
        if (start > 0) {
            in_.seekg(static_cast<std::streamoff>(start), std::ios::beg);
            if (!in_) {
                return false;
            }
        }
        pos_ = start;
        if (start == 0) {
            skip_bom();
        }
        return true;
    }

    bool next(std::uint64_t& offset, std::string& line) {
        if (encoding_ == BookEncoding::Utf16Le) {
            return next16(offset, line);
        }
        return next8(offset, line);
    }

private:
    int pull() {
        if (!unread_.empty()) {
            const int byte = unread_.back();
            unread_.pop_back();
            ++pos_;
            return byte;
        }
        if (at_ >= block_.size()) {
            if (eof_) {
                return -1;
            }
            block_.assign(1 << 16, '\0');
            in_.read(block_.data(), static_cast<std::streamsize>(block_.size()));
            const auto got = in_.gcount();
            if (got <= 0) {
                eof_ = true;
                block_.clear();
                at_ = 0;
                return -1;
            }
            block_.resize(static_cast<std::size_t>(got));
            at_ = 0;
            if (got < (1 << 16)) {
                eof_ = true;
            }
        }
        const int byte = static_cast<unsigned char>(block_[at_]);
        ++at_;
        ++pos_;
        return byte;
    }

    void push_back(int byte) {
        if (byte < 0) {
            return;
        }
        unread_.push_back(byte);
        if (pos_ > 0) {
            --pos_;
        }
    }

    void skip_bom() {
        if (encoding_ == BookEncoding::Utf8) {
            const int b0 = pull();
            const int b1 = pull();
            const int b2 = pull();
            if (b0 == 0xef && b1 == 0xbb && b2 == 0xbf) {
                return;
            }
            push_back(b2);
            push_back(b1);
            push_back(b0);
            return;
        }
        if (encoding_ == BookEncoding::Utf16Le) {
            const int b0 = pull();
            const int b1 = pull();
            if (b0 == 0xff && b1 == 0xfe) {
                return;
            }
            push_back(b1);
            push_back(b0);
        }
    }

    bool next8(std::uint64_t& offset, std::string& line) {
        for (;;) {
            std::string raw;
            offset = pos_;
            bool any = false;
            while (true) {
                const int byte = pull();
                if (byte < 0) {
                    break;
                }
                any = true;
                if (byte == 0) {
                    continue;
                }
                if (byte == '\r') {
                    const int next = pull();
                    if (next >= 0 && next != '\n') {
                        push_back(next);
                    }
                    break;
                }
                if (byte == '\n') {
                    break;
                }
                raw.push_back(static_cast<char>(byte));
            }
            if (!any) {
                return false;
            }
            if (raw.empty()) {
                continue;
            }
            line = decode_file_line(encoding_, raw);
            return true;
        }
    }

    bool next16(std::uint64_t& offset, std::string& line) {
        for (;;) {
            std::string raw;
            offset = pos_;
            bool any = false;
            while (true) {
                const int lo = pull();
                if (lo < 0) {
                    break;
                }
                const int hi = pull();
                if (hi < 0) {
                    break;
                }
                any = true;
                const unsigned unit = static_cast<unsigned>(lo) | (static_cast<unsigned>(hi) << 8);
                if (unit == 0) {
                    continue;
                }
                if (unit == '\r') {
                    const int nlo = pull();
                    const int nhi = nlo < 0 ? -1 : pull();
                    const unsigned next = nhi < 0
                                              ? 0
                                              : static_cast<unsigned>(nlo) |
                                                    (static_cast<unsigned>(nhi) << 8);
                    if (nhi >= 0 && next != '\n') {
                        push_back(nhi);
                        push_back(nlo);
                    }
                    break;
                }
                if (unit == '\n') {
                    break;
                }
                raw.push_back(static_cast<char>(lo));
                raw.push_back(static_cast<char>(hi));
            }
            if (!any) {
                return false;
            }
            if (raw.empty()) {
                continue;
            }
            line = decode_file_line(encoding_, raw);
            return true;
        }
    }

    std::ifstream in_;
    BookEncoding encoding_ = BookEncoding::Utf8;
    std::uint64_t pos_ = 0;
    std::string block_;
    std::size_t at_ = 0;
    bool eof_ = false;
    std::vector<int> unread_;
};

struct Scan {
    Book* book = nullptr;
    int lo = 0;
    int hi = 0;
    bool bodies = true;
    bool until_eof = false;
    bool preamble = true;
    int next_index = 0;
    int current = -1;
    int filling = -1;
    bool seen_heading = false;
    bool used_title = false;
    bool have_last = false;
    int last_num = 0;
    std::string last_rest;
    bool stop = false;

    void begin(int start_index, bool from_start) {
        next_index = start_index;
        preamble = from_start;
        if (!from_start) {
            seen_heading = true;
            used_title = true;
        }
    }

    bool feed(std::uint64_t offset, const std::string& raw) {
        if (stop) {
            return false;
        }
        const std::string line = clean_line(raw);
        if (line.empty()) {
            return true;
        }
        if (preamble && !seen_heading) {
            if (const char* key = meta_value(line, kTitleKeys, 4)) {
                const std::string value =
                    trim_copy(std::string_view(line).substr(std::char_traits<char>::length(key)));
                if (!value.empty()) {
                    book->title = value;
                }
                return true;
            }
            if (const char* key = meta_value(line, kAuthorKeys, 2)) {
                book->author =
                    trim_copy(std::string_view(line).substr(std::char_traits<char>::length(key)));
                return true;
            }
            if (line.find(kJianJie) != std::string::npos && cp_count(line) <= 16) {
                return true;
            }
            if (looks_like_url(line)) {
                return true;
            }
            if (!used_title && !is_chapter_heading(line) && cp_count(line) <= 24 &&
                line.find(kComma) == std::string::npos && line.find(kJuHao) == std::string::npos) {
                book->title = line;
                used_title = true;
                return true;
            }
            used_title = true;
        }
        if (is_chapter_heading(line)) {
            seen_heading = true;
        }
        take(offset, line);
        return !stop;
    }

    void take(std::uint64_t offset, const std::string& line) {
        const HeadingInfo info = heading_info(line);
        if (!info.title.empty()) {
            if (same_chapter(info, have_last, last_num, last_rest)) {
                return;
            }
            if (!until_eof && next_index > hi) {
                remember(*book, next_index, info.title, offset);
                stop = true;
                return;
            }
            open_at(next_index, info.title, offset);
            have_last = info.has_num;
            last_num = info.num;
            last_rest = info.rest;
            return;
        }
        if (looks_like_url(line)) {
            return;
        }
        if (current < 0) {
            if (!until_eof && next_index > hi) {
                stop = true;
                return;
            }
            open_at(next_index, kJianJie, offset);
        }
        if (filling == current && current >= 0) {
            book->chapters[static_cast<std::size_t>(current)].paragraphs.push_back(line);
        }
    }

    void open_at(int index, const std::string& title, std::uint64_t offset) {
        remember(*book, index, title, offset);
        Chapter& chapter = book->chapters[static_cast<std::size_t>(index)];
        const bool want = bodies && index >= lo && index <= hi;
        if (want && !chapter.parsed) {
            chapter.parsed = true;
            chapter.paragraphs.clear();
            filling = index;
        } else {
            filling = -1;
        }
        current = index;
        ++next_index;
    }
};

bool scan_file(Book& book, BookEncoding encoding, std::uint64_t start, int start_index,
               bool from_start, int lo, int hi, bool bodies, bool until_eof, bool& reached_eof) {
    FileLines lines;
    if (!lines.open(book.path, encoding, start)) {
        return false;
    }
    Scan scan;
    scan.book = &book;
    scan.lo = lo;
    scan.hi = hi;
    scan.bodies = bodies;
    scan.until_eof = until_eof;
    scan.begin(start_index, from_start);
    std::uint64_t offset = 0;
    std::string line;
    bool stopped = false;
    while (lines.next(offset, line)) {
        if (!scan.feed(offset, line)) {
            stopped = true;
            break;
        }
    }
    reached_eof = !stopped;
    return true;
}

void finish_parsed(Book& book, bool reached_eof) {
    if (reached_eof && book.chapters.empty()) {
        Chapter chapter;
        chapter.title = kZhengWen;
        chapter.parsed = true;
        chapter.paragraphs.push_back(kEmpty);
        book.chapters.push_back(std::move(chapter));
    }
    if (reached_eof && book.chapters.size() == 1 && book.chapters[0].title == kJianJie) {
        book.chapters[0].title = kZhengWen;
    }
    for (Chapter& chapter : book.chapters) {
        if (!chapter.parsed) {
            continue;
        }
        if (chapter.paragraphs.empty()) {
            chapter.paragraphs.push_back(kEmpty);
        }
    }
    if (reached_eof) {
        book.chapters_indexed = true;
        const int found = static_cast<int>(book.chapters.size());
        book.chapter_count = found > 0 ? found : 1;
    } else if (book.chapter_count < static_cast<int>(book.chapters.size())) {
        // The sentinel just past the window is a real heading, so the book is at least this long.
        // A shelf summary that already counted every heading stays larger than this.
        book.chapter_count = static_cast<int>(book.chapters.size());
    }
}

bool load_index(Book& book, const std::filesystem::path& path) {
    if (book.index_loaded) {
        return true;
    }
    book.index_loaded = true;
    std::uint64_t size = 0;
    std::int64_t mtime = 0;
    if (!file_stamp(book.path, size, mtime)) {
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return true;
    }
    std::string header;
    if (!std::getline(in, header)) {
        return true;
    }
    if (!header.empty() && header.back() == '\r') {
        header.pop_back();
    }
    std::uint64_t file_size = 0;
    std::int64_t file_mtime = 0;
    int encoding = 0;
    int eof = 0;
    const std::size_t t1 = header.find('\t');
    const std::size_t t2 = t1 == std::string::npos ? std::string::npos : header.find('\t', t1 + 1);
    const std::size_t t3 = t2 == std::string::npos ? std::string::npos : header.find('\t', t2 + 1);
    const std::size_t t4 = t3 == std::string::npos ? std::string::npos : header.find('\t', t3 + 1);
    if (t1 == std::string::npos || header.substr(0, t1) != kIndexMagic || t4 == std::string::npos) {
        return true;
    }
    try {
        file_size = static_cast<std::uint64_t>(std::stoull(header.substr(t1 + 1, t2 - t1 - 1)));
        file_mtime = std::stoll(header.substr(t2 + 1, t3 - t2 - 1));
        encoding = std::stoi(header.substr(t3 + 1, t4 - t3 - 1));
        eof = std::stoi(header.substr(t4 + 1));
    } catch (...) {
        return true;
    }
    (void)encoding;
    if (file_size != size || file_mtime != mtime) {
        return true;
    }
    std::string row;
    while (std::getline(in, row)) {
        if (!row.empty() && row.back() == '\r') {
            row.pop_back();
        }
        const std::size_t a = row.find('\t');
        const std::size_t b = a == std::string::npos ? std::string::npos : row.find('\t', a + 1);
        if (a == std::string::npos || b == std::string::npos) {
            continue;
        }
        try {
            const int index = std::stoi(row.substr(0, a));
            const auto offset = static_cast<std::uint64_t>(std::stoull(row.substr(a + 1, b - a - 1)));
            remember(book, index, row.substr(b + 1), offset);
        } catch (...) {
        }
    }
    if (eof != 0) {
        book.chapters_indexed = true;
        if (book.chapter_count < static_cast<int>(book.chapters.size())) {
            book.chapter_count = static_cast<int>(book.chapters.size());
        }
    }
    return true;
}

void save_index(const Book& book, const std::filesystem::path& path, BookEncoding encoding) {
    std::uint64_t size = 0;
    std::int64_t mtime = 0;
    if (path.empty() || !file_stamp(book.path, size, mtime)) {
        return;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return;
    }
    out << kIndexMagic << '\t' << size << '\t' << mtime << '\t' << static_cast<int>(encoding)
        << '\t' << (book.chapters_indexed ? 1 : 0) << '\n';
    for (int i = 0; i < static_cast<int>(book.chapters.size()); ++i) {
        const Chapter& chapter = book.chapters[static_cast<std::size_t>(i)];
        if (chapter.file_offset == kChapterOffsetUnknown) {
            continue;
        }
        out << i << '\t' << chapter.file_offset << '\t' << index_title(chapter.title) << '\n';
    }
}

bool locate_anchor(const Book& book, int lo, int& start_index, std::uint64_t& start_offset) {
    int anchor = -1;
    const int limit = static_cast<int>(book.chapters.size());
    for (int i = lo; i >= 0 && i < limit; --i) {
        if (book.chapters[static_cast<std::size_t>(i)].file_offset != kChapterOffsetUnknown) {
            anchor = i;
            break;
        }
    }
    if (anchor < 0) {
        for (int i = limit - 1; i >= 0; --i) {
            if (book.chapters[static_cast<std::size_t>(i)].file_offset != kChapterOffsetUnknown &&
                i <= lo) {
                anchor = i;
                break;
            }
        }
    }
    if (anchor < 0) {
        return false;
    }
    start_index = anchor;
    start_offset = book.chapters[static_cast<std::size_t>(anchor)].file_offset;
    return true;
}

bool window_ready(const Book& book, int lo, int hi) {
    if (hi < lo) {
        return true;
    }
    if (static_cast<int>(book.chapters.size()) <= hi) {
        return false;
    }
    for (int i = lo; i <= hi; ++i) {
        if (!book.chapters[static_cast<std::size_t>(i)].parsed) {
            return false;
        }
    }
    return true;
}

void clamp_window(const Book& book, int& center, int radius, int& lo, int& hi) {
    if (center < 0) {
        center = 0;
    }
    if (book.chapter_count > 0 && center >= book.chapter_count) {
        center = book.chapter_count - 1;
    }
    lo = center - radius;
    if (lo < 0) {
        lo = 0;
    }
    hi = center + radius;
    if (book.chapter_count > 0 && hi >= book.chapter_count) {
        hi = book.chapter_count - 1;
    }
    if (hi < lo) {
        hi = lo;
    }
}

}  // namespace

std::filesystem::path chapter_index_path(const std::filesystem::path& book_path) {
    std::filesystem::path index = book_path;
    index += ".zui-ch";
    return index;
}

bool load_book_window(Book& book, int center, int radius, const std::filesystem::path& index_path) {
    if (book.format == BookFormat::Unknown) {
        book.format = detect_format(book.path);
    }
    if (book.format == BookFormat::Epub) {
        return load_epub_window(book, center, radius);
    }
    if (book.format == BookFormat::Pdf) {
        return load_pdf_window(book, center, radius);
    }
    if (book.path.empty()) {
        return false;
    }
    std::error_code exists_ec;
    if (!std::filesystem::is_regular_file(book.path, exists_ec) || exists_ec) {
        return false;
    }
    if (radius < 0) {
        radius = 0;
    }
    const std::filesystem::path index = index_file(book, index_path);
    load_index(book, index);
    int lo = 0;
    int hi = 0;
    clamp_window(book, center, radius, lo, hi);
    if (window_ready(book, lo, hi)) {
        return true;
    }
    const BookEncoding encoding = detect_book_encoding(book.path);
    int start_index = 0;
    std::uint64_t start_offset = 0;
    const bool anchored = locate_anchor(book, lo, start_index, start_offset);
    bool reached_eof = false;
    if (!scan_file(book, encoding, anchored ? start_offset : 0, anchored ? start_index : 0,
                   !anchored, lo, hi, true, false, reached_eof)) {
        return false;
    }
    finish_parsed(book, reached_eof);
    save_index(book, index, encoding);
    return true;
}

void ensure_chapter_index(Book& book, const std::filesystem::path& index_path) {
    if (book.format == BookFormat::Unknown) {
        book.format = detect_format(book.path);
    }
    if (book.format == BookFormat::Epub) {
        ensure_epub_index(book);
        return;
    }
    if (book.format == BookFormat::Pdf) {
        ensure_pdf_index(book);
        return;
    }
    if (book.path.empty()) {
        return;
    }
    const std::filesystem::path index = index_file(book, index_path);
    load_index(book, index);
    if (book.chapters_indexed) {
        return;
    }
    const BookEncoding encoding = detect_book_encoding(book.path);
    int start_index = 0;
    std::uint64_t start_offset = 0;
    const int tail = static_cast<int>(book.chapters.size());
    const bool anchored = locate_anchor(book, tail > 0 ? tail - 1 : 0, start_index, start_offset);
    bool reached_eof = false;
    if (!scan_file(book, encoding, anchored ? start_offset : 0, anchored ? start_index : 0,
                   !anchored, 0, 0, false, true, reached_eof)) {
        return;
    }
    finish_parsed(book, reached_eof);
    save_index(book, index, encoding);
}

}  // namespace reader
