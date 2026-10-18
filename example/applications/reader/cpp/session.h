#ifndef READER_SESSION_H
#define READER_SESSION_H

#include "book.h"

#include <zui/app.h>

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace reader {

class Session;

/// `#shelf_query` only listens for keydown, and a typed value lands after timers drain.
/// The outer defer runs in that drain and queues the refresh for the next flush or frame,
/// when the field already holds the query. The header input stays where the page put it.
class ShelfQueryWatch {
public:
    void arm(Session& session);

private:
    void stage(Session& session);
    void apply(Session& session);

    std::string seen_;
    bool armed_ = false;
};

struct Bookmark {
    std::string id;
    std::string key;
    int ci = 0;
    int pi = 0;
    int ch = 0;
    /// Page-mode index within `ci` when the mark was taken (0 in scroll mode).
    int page = 0;
    /// Scroll-mode `#flow_wrap` scroll_top when the mark was taken.
    int scroll = 0;
    std::string label;
    std::string snip;
    long long t = 0;
};

struct FindHit {
    int ci = 0;
    int pi = 0;
    int ch = 0;
    std::string title;
    std::string snip;
};

struct PageSlice {
    int para_index = 0;
    std::string text;
};

struct PageCache {
    std::string key;
    std::vector<std::vector<PageSlice>> pages;
};

struct PageRuntime {
    bool measure_loaded = false;
    bool hits_loaded = false;
    bool marks_loaded = false;
    int page_index = 0;
    int page_chapter = -1;
    int turn_token = 0;
    PageCache cache;
};

class Session {
public:
    Session() = default;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;

    zui::App* app = nullptr;
    std::filesystem::path books_dir;
    std::filesystem::path progress_path;
    std::filesystem::path marks_path;
    std::vector<Book> books;
    std::map<std::string, Progress> progress;
    std::vector<Bookmark> marks;
    int current_book = -1;
    int current_chapter = -1;
    bool toc_ready = false;
    bool toc_built = false;
    int open_token = 0;
    bool toc_hidden = false;  // matches main.cpp initial; open_book sets true
    bool ui_on = false;
    bool settings_on = false;
    bool sort_by_title = false;
    /// First click on 移出 arms the book (the button reads 确认) and drops it from the
    /// in-memory shelf list. The card stays until the second click, which hides it.
    std::string remove_armed;
    std::string remove_source;
    /// First click on 清空书架 arms the button (确认清空). The next click drops progress.
    bool clear_armed = false;
    std::set<std::string> hidden;
    std::string toc_filter;
    std::string find_query;
    ShelfQueryWatch shelf_query_watch_;
    std::vector<FindHit> find_hits;
    PageRuntime page;
    /// Debounced progress autosave while reading (see arm_progress_autosave).
    zui::TimerId progress_autosave_timer = 0;
    zui::TimerId progress_autosave_debounce = 0;
    int autosave_seen_chapter = -1;
    int autosave_seen_page = -1;
    int autosave_seen_scroll = -1;
    /// Suppress onscroll autosave while programmatic restore is adjusting scroll_top.
    bool progress_scroll_guard = false;
    /// Last visible paragraph index — seeds exponential search on the next capture.
    int para_geom_hint = 0;
    /// Chapter identity for which para_geom_hint is valid (-1 = unset).
    int para_geom_chapter = -1;
    /// Brief restore flash timer / target id.
    zui::TimerId restore_flash_timer = 0;
    std::string restore_flash_id;
};

void bind_actions(Session& session);
void show(Session& session);
void arm_debug_harness(Session& session);

void fill_shelf(Session& session);
void open_book(Session& session, int index);
void show_chapter(Session& session, int index, bool restore_offset = false);
void capture_reading_progress(Session& session);
void arm_progress_autosave(Session& session);
void stop_progress_autosave(Session& session);
void schedule_progress_autosave(Session& session);
void invalidate_para_geom(Session& session);
void flash_restore_target(Session& session, int pi);
int estimate_visible_para(Session& session);
int estimate_visible_ch(Session& session, int para);
void fill_bookmark_position(Session& session, Bookmark& mark);
void goto_bookmark(Session& session, const Bookmark& mark);
void goto_chapter_offset(Session& session, int ci, int pi, int ch, int page, int scroll);

void restore_reading_progress(Session& session);
void persist_marks(Session& session);
void load_session_marks(Session& session);
int most_recent_book(const Session& session);

void back_to_shelf(Session& session);
void toggle_toc(Session& session);
void toggle_settings(Session& session);
void tap_reading(Session& session);
void leave_reader_ui(Session& session);
void go_prev(Session& session);
/// `anchor_next` applies the browser's scroll-anchor rule to the Arrow keys: reader.js `turn(1)`
/// scrolls the next chapter to the top of the reading area and derives `status_chapter` from where
/// the page actually landed, so a last chapter too short to reach the top leaves the status on the
/// current chapter. The 下一章 button and PageUp/PageDown keep the plain chapter step.
void go_next(Session& session, bool anchor_next = false);
void import_books(Session& session);
/// Copies UTF-8 paths from a file picker into `books_dir`. A book that was hidden comes back.
/// Returns how many files landed.
int import_picked_books(Session& session, const std::vector<std::string>& utf8_paths);
void clear_progress(Session& session);
void toggle_bookmark(Session& session);

void filter_chapters(Session& session, std::string_view query);
void find_in_book(Session& session, std::string_view query);
std::vector<Bookmark> list_bookmarks(const Session& session);
void add_bookmark(Session& session, int ci, int pi, int ch, int page = 0, int scroll = 0);
void remove_bookmark(Session& session, std::string_view id);
void ensure_hits_host(Session& session);
void ensure_marks_host(Session& session);
void render_marks(Session& session);

}  // namespace reader

#endif
