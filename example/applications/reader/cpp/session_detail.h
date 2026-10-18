#ifndef READER_SESSION_DETAIL_H
#define READER_SESSION_DETAIL_H

#include "session.h"

#include <zui/model.h>

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

namespace reader {

constexpr const char* kHint =
    "\xe6\x8a\x8a txt / epub / pdf \xe6\x94\xbe\xe8\xbf\x9b books \xe7\x9b\xae\xe5\xbd\x95";
constexpr const char* kEmptyShelf = "\xe4\xb9\xa6\xe6\x9e\xb6\xe8\xbf\x98\xe7\xa9\xba\xe7\x9d\x80";
constexpr const char* kNoMatch = "\xe6\xb2\xa1\xe6\x9c\x89\xe5\x8c\xb9\xe9\x85\x8d\xe7\x9a\x84\xe4\xb9\xa6";
constexpr const char* kNoMatchHint =
    "\xe6\x8d\xa2\xe4\xb8\xaa\xe8\xaf\x8d\xef\xbc\x8c\xe6\x88\x96\xe8\x80\x85\xe6\xb8\x85\xe7\xa9\xba\xe6\x90\x9c\xe7\xb4\xa2\xe3\x80\x82";
constexpr const char* kZhangCount = " \xe7\xab\xa0";
constexpr const char* kDot = " \xc2\xb7 ";
constexpr const char* kNextChapter = "\xe4\xb8\x8b\xe4\xb8\x80\xe7\xab\xa0";
constexpr const char* kTheEnd = "\xe5\x85\xa8\xe4\xb9\xa6\xe5\xae\x8c";
constexpr const char* kSortTitle = "\xe4\xb9\xa6\xe5\x90\x8d";
constexpr const char* kSortRecent = "\xe6\x9c\x80\xe8\xbf\x91";
constexpr const char* kBookmarked =
    "\xe5\xb7\xb2\xe8\xae\xb0\xe4\xb8\x8b\xe8\xbf\x99\xe4\xb8\x80\xe7\xab\xa0";
constexpr const char* kImportHint =
    "\xe6\x8a\x8a txt / epub / pdf \xe6\x94\xbe\xe8\xbf\x9b books\xef\xbc\x8c\xe9\x87\x8d\xe6\x96\xb0\xe6\x89\x93\xe5\xbc\x80\xe4\xb9\xa6\xe6\x9e\xb6";
constexpr const char* kImportedOk =
    "\xe5\xb7\xb2\xe5\xaf\xbc\xe5\x85\xa5\xe4\xb9\xa6\xe7\xb1\x8d";
constexpr const char* kOpenFailed =
    "\xe6\x89\x93\xe5\xbc\x80\xe5\xa4\xb1\xe8\xb4\xa5";
constexpr const char* kClearedHint =
    "\xe5\xb7\xb2\xe6\xb8\x85\xe7\xa9\xba\xe9\x98\x85\xe8\xaf\xbb\xe8\xbf\x9b\xe5\xba\xa6";
constexpr const char* kOpeningPrefix = "\xe6\xad\xa3\xe5\x9c\xa8\xe6\x89\x93\xe5\xbc\x80\xe3\x80\x8a";
constexpr const char* kOpeningSuffix = "\xe3\x80\x8b\xef\xbc\x8c\xe5\x85\x88\xe8\xaf\xbb\xe8\xbf\x9b\xe5\xba\xa6\xe9\x99\x84\xe8\xbf\x91\xe7\x9a\x84\xe5\x87\xa0\xe7\xab\xa0\xe3\x80\x82";
constexpr const char* kMarkAdded =
    "\xe5\xb7\xb2\xe6\xb7\xbb\xe5\x8a\xa0\xe4\xb9\xa6\xe7\xad\xbe";
constexpr const char* kMarkRemoved =
    "\xe5\xb7\xb2\xe5\x8f\x96\xe6\xb6\x88\xe4\xb9\xa6\xe7\xad\xbe";
constexpr const char* kFindPrompt =
    "\xe8\xbe\x93\xe5\x85\xa5\xe8\xaf\x8d\xe8\xaf\xad\xef\xbc\x8c\xe6\x9f\xa5\xe6\x89\xbe\xe8\xbf\x99\xe4\xb8\x80\xe6\x9c\xac\xe7\x9a\x84\xe6\xad\xa3\xe6\x96\x87\xe3\x80\x82";
constexpr const char* kFindNone = "\xe6\xb2\xa1\xe6\x9c\x89\xe6\x89\xbe\xe5\x88\xb0";
constexpr const char* kFindUnit = " \xe5\xa4\x84";
constexpr const char* kFindCap =
    "\xef\xbc\x8c\xe8\xbf\x99\xe9\x87\x8c\xe5\x88\x97\xe5\x87\xba\xe5\x89\x8d 100 \xe5\xa4\x84";
constexpr const char* kBookmarkLabel = "\xe4\xb9\xa6\xe7\xad\xbe";

class BookRows : public zui::ListModel {
public:
    struct Row {
        std::string title;
        std::string meta;
        std::string author;
        std::string where;
        std::string fill;
        int tone = 0;
        /// Index in `Session::books`. A filtered shelf skips rows, and open/remove use this index.
        int index = 0;
    };
    std::vector<Row> rows;

    size_t row_count() const override { return rows.size(); }

    std::string value(size_t row, std::string_view key) const override {
        if (row >= rows.size()) {
            return {};
        }
        if (key == "title") {
            return rows[row].title;
        }
        if (key == "meta") {
            return rows[row].meta;
        }
        if (key == "author") {
            return rows[row].author;
        }
        if (key == "where") {
            return rows[row].where;
        }
        if (key == "fill") {
            return rows[row].fill;
        }
        if (key == "tone") {
            return std::to_string(rows[row].tone);
        }
        if (key == "index") {
            return std::to_string(rows[row].index);
        }
        return {};
    }
};

class ChapterRows : public zui::ListModel {
public:
    const std::vector<Chapter>* chapters = nullptr;

    size_t row_count() const override { return chapters == nullptr ? 0 : chapters->size(); }

    std::string value(size_t row, std::string_view key) const override {
        if (chapters == nullptr || row >= chapters->size()) {
            return {};
        }
        if (key == "title") {
            return (*chapters)[row].title;
        }
        return {};
    }
};

class ParaRows : public zui::ListModel {
public:
    const std::vector<std::string>* rows = nullptr;

    size_t row_count() const override { return rows == nullptr ? 0 : rows->size(); }

    std::string value(size_t row, std::string_view key) const override {
        if (rows == nullptr || row >= rows->size()) {
            return {};
        }
        if (key == "text") {
            return (*rows)[row];
        }
        return {};
    }
};

double ms_since(const std::chrono::steady_clock::time_point& start);
void trace_ms(const char* what, double ms);
int suffix_index(std::string_view id, std::string_view prefix);
void toast(Session& session, const char* text);
void set_ui(Session& session, bool on);
void drop_toc_model(Session& session);
void set_toc_hidden(Session& session, bool hidden);
void set_settings(Session& session, bool open);
void ensure_toc(Session& session);
bool next_chapter_can_reach_the_anchor(Session& session);

/// The shelf search box stays the header `#shelf_query`. `read` is its trimmed value.
class ShelfQuery {
public:
    static ShelfQuery read(zui::App& app);
    bool active() const { return !needle_.empty(); }
    const std::string& needle() const { return needle_; }
    bool matches(const Book& book) const;

private:
    std::string needle_;
};

}  // namespace reader

#endif