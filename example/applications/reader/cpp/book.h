#ifndef READER_BOOK_H
#define READER_BOOK_H

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "book_format.h"

namespace reader {

/// Byte offset is not known yet. A parsed chapter stores the file position of its first line.
constexpr std::uint64_t kChapterOffsetUnknown = static_cast<std::uint64_t>(-1);

/// Chapters of body text loaded around the current one. The chapter itself is included.
constexpr int kChapterRadius = 3;

struct Chapter {
    std::string title;
    std::vector<std::string> paragraphs;
    /// File offset of this chapter's first line. Seeking here resumes a later open.
    std::uint64_t file_offset = kChapterOffsetUnknown;
    /// `paragraphs` was read from the file. A located chapter can have a title and an
    /// offset before its body is parsed.
    bool parsed = false;
    /// When non-empty, UI prefers replace_fragment over paras (EPUB/PDF).
    std::string html;
};

struct Book {
    std::filesystem::path path;
    BookFormat format = BookFormat::Txt;
    /// EPUB image/chapter cache under books/.cache/<key>/
    std::filesystem::path cache_dir;
    std::string key;
    std::string title;
    std::string author;
    /// Filled by the cheap shelf scan; the full parse fills `chapters` instead.
    int chapter_count = 0;
    /// Approximate body size for the shelf meta line (`N 章 · N 万字`). Catalog may
    /// supply it; a summary scan leaves it 0 and the meta line then omits the size.
    long long chars = 0;
    std::vector<Chapter> chapters;
    /// A heading scan has reached the end of the file, so `chapter_count` is final.
    bool chapters_indexed = false;
    /// The offset file has been read (or found missing) for this in-memory book.
    bool index_loaded = false;
};

struct Progress {
    int chapter = 0;
    long long opened_at = 0;  // unix seconds; newer means more recent for shelf order
    /// Page-mode index within `chapter` (0-based). Ignored in scroll mode.
    int page = 0;
    /// Scroll-mode `#flow_wrap` scroll_top in CSS pixels (integer).
    int scroll = 0;
    /// Optional paragraph / char hint (bookmarks + future fine restore).
    int para = 0;
    int ch = 0;
};

bool is_txt(const std::filesystem::path& path);
std::string to_utf8(const std::filesystem::path& path);
Book load_book(const std::filesystem::path& path);
bool book_summary(const std::filesystem::path& path, Book& book);

/// Sidecar of file offsets for chapters this book has already parsed.
std::filesystem::path chapter_index_path(const std::filesystem::path& book_path);

/// How many chapters the book has. A partial `chapters` vector is not the whole book:
/// the shelf summary's `chapter_count` still counts the chapters that were not parsed.
int book_length(const Book& book);

/// Parse paragraph text only for `[center - radius, center + radius]`. Offsets of every
/// chapter the scan actually reaches are merged into `index_path` (an empty path uses
/// `chapter_index_path(book.path)`). The next call seeks to a saved offset instead of
/// reading the file from the first byte. `book.path` must already name the txt.
/// `book.chapter_count` is kept when the scan stops before the end of the file.
bool load_book_window(Book& book, int center, int radius,
                      const std::filesystem::path& index_path = {});

/// Fill titles and file offsets through the end of the file, without paragraph text.
void ensure_chapter_index(Book& book, const std::filesystem::path& index_path = {});
std::map<std::string, Book> load_catalog(const std::filesystem::path& path);
std::map<std::string, Progress> load_progress(const std::filesystem::path& path);
void save_progress(const std::filesystem::path& path,
                   const std::map<std::string, Progress>& progress);

long long now_unix();
int progress_pct(const Book& book, int chapter);
std::string pct_class(int pct);
std::string format_chars(long long n);
std::string card_where(const Book& book, const std::map<std::string, Progress>& progress);
std::string book_meta(const Book& book);
/// `reader.js` orders titles with `localeCompare(a, b, "zh")`: the host's pinyin collation, not
/// code point order. The shelf has to use the same sequence, because the cover tone is picked from
/// the title in placement order (`toneOf`), so a different order repaints the whole grid.
bool zh_title_less(std::string_view a, std::string_view b);

}  // namespace reader

#endif
