#include "book_detail.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <fstream>
namespace reader {

bool is_txt(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    for (char& c : ext) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return ext == ".txt" || ext == ".text";
}

std::string format_chars(long long n) {
    if (n >= 10000) {
        const double wan = static_cast<double>(n) / 10000.0;
        if (wan >= 10.0) {
            return std::to_string(static_cast<long long>(wan + 0.5)) + " \xe4\xb8\x87\xe5\xad\x97";
        }
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f", wan);
        std::string text = buf;
        if (text.size() >= 2 && text[text.size() - 2] == '.' && text.back() == '0') {
            text.resize(text.size() - 2);
        }
        return text + " \xe4\xb8\x87\xe5\xad\x97";
    }
    if (n > 0) {
        return std::to_string(n) + " \xe5\xad\x97";
    }
    return {};
}

int book_length(const Book& book) {
    const int loaded = static_cast<int>(book.chapters.size());
    if (book.chapter_count > loaded) {
        return book.chapter_count;
    }
    if (loaded > 0) {
        return loaded;
    }
    return book.chapter_count > 0 ? book.chapter_count : 0;
}

int progress_pct(const Book& book, int chapter) {
    const int count = book_length(book);
    if (count <= 0) {
        return 0;
    }
    int pct = static_cast<int>((static_cast<long long>(chapter + 1) * 100) / count);
    if (pct < 1) {
        pct = 1;
    }
    if (pct > 100) {
        pct = 100;
    }
    return pct;
}

std::string pct_class(int pct) {
    if (pct < 0) {
        pct = 0;
    }
    if (pct > 100) {
        pct = 100;
    }
    pct = (pct / 5) * 5;
    return "pct-" + std::to_string(pct);
}


long long now_unix() {
    return static_cast<long long>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

std::string card_where(const Book& book, const std::map<std::string, Progress>& progress) {
    const auto found = progress.find(book.key);
    if (found == progress.end()) {
        return kUnread;
    }
    const int chapter = found->second.chapter;
    const int pct = progress_pct(book, chapter);
    if (pct >= 100) {
        return kFinished;
    }
    if (!book.chapters.empty() && chapter >= 0 &&
        chapter < static_cast<int>(book.chapters.size())) {
        return std::to_string(pct) + "% · " + book.chapters[static_cast<size_t>(chapter)].title;
    }
    if (pct > 0) {
        return std::to_string(pct) + "%";
    }
    return kReadOnce;
}

std::string book_meta(const Book& book) {
    const std::size_t count = static_cast<std::size_t>(book_length(book));
    // Match reader.js: the cover already shows the author, and the meta line is
    // "N 章 · N 万字" (or just the chapter count when the size is unknown).
    std::string meta = std::to_string(count) + kZhangCount;
    const std::string sized = format_chars(book.chars);
    if (!sized.empty()) {
        meta += kDot + sized;
    }
    return meta;
}

/// `books/catalog.json` carries the shelf metadata the browser version already reads:
/// title, author and chapter count per file name. Reading it is instant, while walking five
/// 20 MB novels to count headings took ~4.3 s before the first window could appear. A `.txt`
/// the catalog does not list still works - it just pays for the scan.
std::map<std::string, Book> load_catalog(const std::filesystem::path& path) {
    std::map<std::string, Book> out;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return out;
    }
    nlohmann::json doc;
    try {
        in >> doc;
    } catch (...) {
        return out;
    }
    if (!doc.is_array()) {
        return out;
    }
    for (const auto& item : doc) {
        if (!item.is_object()) {
            continue;
        }
        Book book;
        book.key = item.value("file", std::string{});
        if (book.key.empty()) {
            continue;
        }
        book.title = item.value("title", book.key);
        book.author = item.value("author", std::string{});
        book.chapter_count = item.value("chapters", 0);
        if (book.chapter_count <= 0) {
            book.chapter_count = 1;
        }
        book.chars = item.value("chars", 0LL);
        out.emplace(book.key, std::move(book));
    }
    return out;
}

std::map<std::string, Progress> load_progress(const std::filesystem::path& path) {
    std::map<std::string, Progress> out;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return out;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const size_t tab = line.find('\t');
        if (tab == std::string::npos || tab == 0) {
            continue;
        }
        try {
            Progress progress;
            std::string rest = line.substr(tab + 1);
            auto next = [&](std::string& field) -> std::string {
                const size_t t = rest.find('\t');
                if (t == std::string::npos) {
                    field = rest;
                    rest.clear();
                    return field;
                }
                field = rest.substr(0, t);
                rest = rest.substr(t + 1);
                return field;
            };
            std::string field;
            progress.chapter = std::stoi(next(field));
            if (!rest.empty()) {
                progress.opened_at = std::stoll(next(field));
            }
            if (!rest.empty()) {
                progress.page = std::stoi(next(field));
            }
            if (!rest.empty()) {
                progress.scroll = std::stoi(next(field));
            }
            if (!rest.empty()) {
                progress.para = std::stoi(next(field));
            }
            if (!rest.empty()) {
                progress.ch = std::stoi(next(field));
            }
            out[line.substr(0, tab)] = progress;
        } catch (...) {
        }
    }
    return out;
}

void save_progress(const std::filesystem::path& path,
                   const std::map<std::string, Progress>& progress) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return;
    }
    for (const auto& item : progress) {
        const Progress& pr = item.second;
        out << item.first << '\t' << pr.chapter << '\t' << pr.opened_at << '\t' << pr.page << '\t'
            << pr.scroll << '\t' << pr.para << '\t' << pr.ch << '\n';
    }
}

}  // namespace reader
