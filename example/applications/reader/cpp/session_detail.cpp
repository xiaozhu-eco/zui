#include "session_detail.h"
#include "page_mode.h"
#include <zui/host_types.h>
#include <algorithm>
#include <cmath>
#include <limits>

#include <cstdio>
#include <cstdlib>

namespace reader {

double ms_since(const std::chrono::steady_clock::time_point& start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
        .count();
}

void trace_ms(const char* what, double ms) {
    if (std::getenv("ZUI_READER_TRACE") != nullptr) {
        std::fprintf(stderr, "[reader] %s %.1f ms\n", what, ms);
    }
}

int suffix_index(std::string_view id, std::string_view prefix) {
    if (id.size() <= prefix.size() || id.compare(0, prefix.size(), prefix) != 0) {
        return -1;
    }
    int n = 0;
    for (size_t i = prefix.size(); i < id.size(); ++i) {
        const char c = id[i];
        if (c < '0' || c > '9') {
            return -1;
        }
        n = n * 10 + (c - '0');
    }
    return n;
}

void toast(Session& session, const char* text) { session.app->document().set("toast", "text", text); }

void set_ui(Session& session, bool on) {
    session.ui_on = on;
    zui::App& app = *session.app;
    if (on) {
        app.document().add_class("toolbar", "ui-on");
        app.document().add_class("status", "ui-on");
    } else {
        app.document().remove_class("toolbar", "ui-on");
        app.document().remove_class("status", "ui-on");
    }
}

void drop_toc_model(Session& session) {
    ChapterRows empty;
    session.app->document().set_model("chapters", empty);
    session.toc_ready = false;
    session.toc_built = false;
}

void set_toc_hidden(Session& session, bool hidden) {
    session.toc_hidden = hidden;
    zui::App& app = *session.app;
    if (hidden) {
        app.document().add_class("toc_panel", "toc-hidden");
        app.document().add_class("tray_scrim", "toc-hidden");
        app.document().add_class("toolbar", "toc-hidden");
        app.document().add_class("toc_toggle", "toc-hidden");
    } else {
        app.document().remove_class("toc_panel", "toc-hidden");
        app.document().remove_class("tray_scrim", "toc-hidden");
        app.document().remove_class("toolbar", "toc-hidden");
        app.document().remove_class("toc_toggle", "toc-hidden");
    }
}

void set_settings(Session& session, bool open) {
    session.settings_on = open;
    session.app->document().set("settings_open", "checked", open ? "true" : "false");
}

void ensure_toc(Session& session) {
    if (session.toc_built || session.current_book < 0 ||
        static_cast<size_t>(session.current_book) >= session.books.size()) {
        return;
    }
    Book& book = session.books[static_cast<size_t>(session.current_book)];
    ensure_chapter_index(book);
    ChapterRows rows;
    rows.chapters = &book.chapters;
    session.toc_ready = false;
    session.app->document().set_model("chapters", rows);
    session.toc_ready = true;
    session.toc_built = true;
}

/// reader.js keeps several chapters in one flow and derives `status_chapter` from the scroll
/// anchor: `turn(1)` scrolls the next chapter to the top of `#flow_wrap`, and when that chapter is
/// the last one and shorter than the reading area the page is already at its end - the anchor, and
/// with it the status line, stays on the current chapter. The native window always renders the
/// next chapter as `#chapter_b`, so its height against the reading area is the same test.
bool next_chapter_can_reach_the_anchor(Session& session) {
    if (session.current_book < 0 ||
        static_cast<std::size_t>(session.current_book) >= session.books.size()) {
        return true;
    }
    const Book& book = session.books[static_cast<std::size_t>(session.current_book)];
    const int last = book_length(book) - 1;
    if (session.current_chapter + 1 != last) {
        // Only the last chapter can leave the flow without enough content behind it.
        return true;
    }
    const auto flow = session.app->document().measure("flow_wrap");
    const auto follow = session.app->document().measure("chapter_b");
    if (!flow || !follow) {
        return true;
    }
    return follow->border.height + 0.5f >= flow->client_height;
}




void invalidate_para_geom(Session& session) {
    session.para_geom_hint = 0;
    session.para_geom_chapter = -1;
}

void clear_restore_flash(Session& session) {
    if (session.app == nullptr) {
        return;
    }
    if (session.restore_flash_timer != 0) {
        session.app->document().clear_timer(session.restore_flash_timer);
        session.restore_flash_timer = 0;
    }
    if (!session.restore_flash_id.empty()) {
        session.app->document().remove_class(session.restore_flash_id, "restore-flash");
        session.restore_flash_id.clear();
    }
}

void flash_restore_target(Session& session, int pi) {
    if (session.app == nullptr || pi < 0) {
        return;
    }
    clear_restore_flash(session);
    const std::string id = "pa" + std::to_string(pi);
    // Re-add so a repeated jump to the same para retriggers the animation.
    session.app->document().remove_class(id, "restore-flash");
    if (!session.app->document().add_class(id, "restore-flash")) {
        return;
    }
    session.restore_flash_id = id;
    session.restore_flash_timer = session.app->document().set_timeout([&session]() {
        session.restore_flash_timer = 0;
        clear_restore_flash(session);
    }, 1400);
}

namespace {

/// Largest i with measure(pa_i).border.y <= top_limit. Uses an exponential probe from
/// `hint` then binary search so large chapters cost O(log n) measures, not O(n).
int find_para_at_or_above(zui::App& app, int n, float top_limit, int hint) {
    if (n <= 0) {
        return 0;
    }
    auto top_of = [&](int i) -> float {
        const auto box = app.document().measure("pa" + std::to_string(i));
        if (!box || !box->laid) {
            return std::numeric_limits<float>::quiet_NaN();
        }
        return box->border.y;
    };
    auto ok_at = [&](int i) -> bool {
        const float y = top_of(i);
        return !std::isnan(y) && y <= top_limit;
    };

    if (hint < 0) {
        hint = 0;
    }
    if (hint >= n) {
        hint = n - 1;
    }

    // Fast path: hint still visible and the next para (if any) has crossed the top.
    if (ok_at(hint)) {
        if (hint + 1 >= n || !ok_at(hint + 1)) {
            return hint;
        }
    }

    int lo = -1;  // last known ok index (or -1)
    int hi = n;   // first known not-ok index (or n)
    if (ok_at(hint)) {
        lo = hint;
        int step = 1;
        while (lo + step < n && ok_at(lo + step)) {
            lo += step;
            step *= 2;
        }
        hi = std::min(n, lo + step);
    } else {
        hi = hint;
        int step = 1;
        while (hi - step >= 0) {
            if (ok_at(hi - step)) {
                lo = hi - step;
                break;
            }
            hi = hi - step;
            step *= 2;
        }
        if (lo < 0 && hi > 0 && ok_at(0)) {
            lo = 0;
        }
    }

    while (hi - lo > 1) {
        const int mid = lo + (hi - lo) / 2;
        if (ok_at(mid)) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return lo < 0 ? 0 : lo;
}

}  // namespace

int estimate_visible_para(Session& session) {
    if (session.app == nullptr || session.current_book < 0 || session.current_chapter < 0 ||
        static_cast<size_t>(session.current_book) >= session.books.size()) {
        return 0;
    }
    const Book& book = session.books[static_cast<size_t>(session.current_book)];
    if (session.current_chapter >= static_cast<int>(book.chapters.size())) {
        return 0;
    }
    const Chapter& chapter = book.chapters[static_cast<size_t>(session.current_chapter)];
    if (chapter.paragraphs.empty()) {
        const auto found = session.progress.find(book.key);
        return found == session.progress.end() ? 0 : found->second.para;
    }
    zui::App& app = *session.app;
    const auto flow = app.document().measure("flow_wrap");
    if (!flow) {
        return 0;
    }
    // Prefer the last paragraph whose top edge is at or above the viewport top (+ fudge).
    // Works for TXT (#paras model) and EPUB/PDF rich HTML (#paN in chapter_host).
    const float top_limit = flow->border.y + 12.f;
    const int n = static_cast<int>(chapter.paragraphs.size());
    int hint = session.para_geom_hint;
    if (session.para_geom_chapter != session.current_chapter) {
        hint = 0;
    }
    // Probe whether any #paN exists (HTML not laid yet / wrong host).
    {
        const auto probe = app.document().measure("pa0");
        if (!probe || !probe->laid) {
            // Try a middle id in case pa0 is missing but others exist.
            const auto mid = app.document().measure("pa" + std::to_string(n / 2));
            if (!mid || !mid->laid) {
                const auto found = session.progress.find(book.key);
                return found == session.progress.end() ? 0 : found->second.para;
            }
        }
    }
    const int best = find_para_at_or_above(app, n, top_limit, hint);
    session.para_geom_hint = best;
    session.para_geom_chapter = session.current_chapter;
    return best;
}

/// Best-effort UTF-8 byte offset at the viewport top inside #paN.
///
/// Host limitation: `Document` exposes `measure` / `scroll_into_view` for reading panes, but no
/// public offset→caret-rect (and this tree has no `text_caret_hit` on `Document` for apps to invert).
/// Layout's internal `get_rect` is not part of app.h. Until that lands, ch is a content-height
/// fraction of the paragraph byte length — good for long uniform CJK lines, weaker with mixed
/// Latin wrap / large inline images.
int estimate_visible_ch(Session& session, int para) {
    if (session.app == nullptr || session.current_book < 0 || session.current_chapter < 0 ||
        para < 0 || static_cast<size_t>(session.current_book) >= session.books.size()) {
        return 0;
    }
    const Book& book = session.books[static_cast<size_t>(session.current_book)];
    if (session.current_chapter >= static_cast<int>(book.chapters.size())) {
        return 0;
    }
    const Chapter& chapter = book.chapters[static_cast<size_t>(session.current_chapter)];
    if (para >= static_cast<int>(chapter.paragraphs.size())) {
        return 0;
    }
    const size_t len = chapter.paragraphs[static_cast<size_t>(para)].size();
    if (len == 0) {
        return 0;
    }
    zui::App& app = *session.app;
    const auto flow = app.document().measure("flow_wrap");
    const auto box = app.document().measure("pa" + std::to_string(para));
    if (!flow || !box || !box->laid) {
        return 0;
    }
    const float height =
        box->content_height > 1.f ? box->content_height : box->border.height;
    if (height < 1.f) {
        return 0;
    }
    const float into = flow->border.y - box->border.y;
    if (into <= 0.f) {
        return 0;
    }
    float frac = into / height;
    if (frac > 1.f) {
        frac = 1.f;
    }
    return static_cast<int>(frac * static_cast<float>(len));
}

void apply_para_char_offset(Session& session, int pi, int ch) {
    if (session.app == nullptr || ch <= 0 || pi < 0 || session.current_book < 0 ||
        session.current_chapter < 0 ||
        static_cast<size_t>(session.current_book) >= session.books.size()) {
        return;
    }
    const Book& book = session.books[static_cast<size_t>(session.current_book)];
    if (session.current_chapter >= static_cast<int>(book.chapters.size())) {
        return;
    }
    const Chapter& chapter = book.chapters[static_cast<size_t>(session.current_chapter)];
    if (pi >= static_cast<int>(chapter.paragraphs.size())) {
        return;
    }
    const size_t len = chapter.paragraphs[static_cast<size_t>(pi)].size();
    if (len == 0) {
        return;
    }
    zui::App& app = *session.app;
    const auto box = app.document().measure("pa" + std::to_string(pi));
    if (!box || !box->laid) {
        return;
    }
    const float height =
        box->content_height > 1.f ? box->content_height : box->border.height;
    if (height < 1.f) {
        return;
    }
    const size_t target = std::min(static_cast<size_t>(ch), len);
    float frac = static_cast<float>(target) / static_cast<float>(len);
    if (frac > 1.f) {
        frac = 1.f;
    }
    // Leave a little headroom so the target line is not glued to the chrome.
    const float extra = frac * height;
    if (extra < 1.f) {
        return;
    }
    const float cur =
        std::strtof(app.document().get("flow_wrap", "scroll_top").c_str(), nullptr);
    app.document().set("flow_wrap", "scroll_top", std::to_string(cur + extra));
}

void fill_bookmark_position(Session& session, Bookmark& mark) {
    mark.ci = session.current_chapter;
    if (mode_page_checked(session)) {
        mark.page = session.page.page_index;
        mark.scroll = 0;
        mark.pi = 0;
        mark.ch = 0;
    } else {
        const std::string top = session.app->document().get("flow_wrap", "scroll_top");
        mark.scroll = static_cast<int>(std::strtof(top.c_str(), nullptr));
        if (mark.scroll < 0) {
            mark.scroll = 0;
        }
        mark.page = 0;
        mark.pi = estimate_visible_para(session);
        mark.ch = estimate_visible_ch(session, mark.pi);
    }
}

/// Scroll to #paN (+ optional ch nudge), falling back to absolute scroll_top. Double-defers so
/// layout after replace_fragment / set_model has settled, then flashes the target and verifies.
void settle_restore_offset(Session& session, int pi, int ch, int scroll) {
    if (session.app == nullptr) {
        return;
    }
    session.progress_scroll_guard = true;
    session.para_geom_hint = std::max(0, pi);
    session.para_geom_chapter = session.current_chapter;
    const int target_pi = pi;
    const int target_ch = ch;
    const int target_scroll = scroll;
    // First defer: wait for the chapter fragment / model to lay out.
    session.app->document().defer([&, target_pi, target_ch, target_scroll]() {
        if (session.app == nullptr || session.current_chapter < 0) {
            session.progress_scroll_guard = false;
            return;
        }
        // Second defer: one more frame so measure/scroll_into_view see stable boxes.
        session.app->document().defer([&, target_pi, target_ch, target_scroll]() {
            if (session.app == nullptr || session.current_chapter < 0) {
                session.progress_scroll_guard = false;
                return;
            }
            bool landed = false;
            if (target_pi > 0 || target_ch > 0) {
                const std::string id = "pa" + std::to_string(target_pi);
                landed = session.app->document().scroll_into_view(id, zui::ScrollAlign::Start);
                if (landed && target_ch > 0) {
                    apply_para_char_offset(session, target_pi, target_ch);
                }
                if (landed) {
                    // Re-assert after the ch nudge: some layouts reflow and drift the para.
                    (void)session.app->document().scroll_into_view(id, zui::ScrollAlign::Start);
                    if (target_ch > 0) {
                        apply_para_char_offset(session, target_pi, target_ch);
                    }
                    flash_restore_target(session, target_pi);
                }
            }
            if (!landed && target_scroll > 0) {
                session.app->document().set("flow_wrap", "scroll_top",
                                            std::to_string(target_scroll));
                if (target_pi >= 0) {
                    flash_restore_target(session, target_pi);
                }
            }
            // Hold the guard briefly so the flash / settle scroll does not overwrite progress.
            session.app->document().set_timeout([&session]() {
                session.progress_scroll_guard = false;
            }, 200);
        });
    });
}

void goto_chapter_offset(Session& session, int ci, int pi, int ch, int page, int scroll) {
    if (session.app == nullptr || session.current_book < 0) {
        return;
    }
    // Seed progress so show_chapter(..., true) / restore picks the fine offset.
    Book& book = session.books[static_cast<size_t>(session.current_book)];
    Progress& prog = session.progress[book.key];
    prog.chapter = ci;
    prog.para = pi;
    prog.ch = ch;
    prog.page = page;
    prog.scroll = scroll;
    prog.opened_at = now_unix();
    save_progress(session.progress_path, session.progress);
    show_chapter(session, ci, true);
    // After layout, prefer paragraph scroll_into_view when we have #paN, then nudge by ch.
    if (!mode_page_checked(session) && (pi > 0 || ch > 0 || scroll > 0)) {
        settle_restore_offset(session, pi, ch, scroll);
    }
}

void goto_bookmark(Session& session, const Bookmark& mark) {
    goto_chapter_offset(session, mark.ci, mark.pi, mark.ch, mark.page, mark.scroll);
}

void stop_progress_autosave(Session& session) {
    if (session.app == nullptr) {
        return;
    }
    clear_restore_flash(session);
    if (session.progress_autosave_debounce != 0) {
        session.app->document().clear_timer(session.progress_autosave_debounce);
        session.progress_autosave_debounce = 0;
    }
    if (session.progress_autosave_timer != 0) {
        session.app->document().clear_timer(session.progress_autosave_timer);
        session.progress_autosave_timer = 0;
    }
    session.autosave_seen_chapter = -1;
    session.autosave_seen_page = -1;
    session.autosave_seen_scroll = -1;
}

void schedule_progress_autosave(Session& session) {
    if (session.app == nullptr || session.current_book < 0 || session.current_chapter < 0) {
        return;
    }
    int scroll = 0;
    int page = 0;
    if (mode_page_checked(session)) {
        page = session.page.page_index;
    } else {
        scroll = static_cast<int>(
            std::strtof(session.app->document().get("flow_wrap", "scroll_top").c_str(), nullptr));
        if (scroll < 0) {
            scroll = 0;
        }
    }
    if (session.current_chapter == session.autosave_seen_chapter &&
        page == session.autosave_seen_page && scroll == session.autosave_seen_scroll) {
        return;
    }
    session.autosave_seen_chapter = session.current_chapter;
    session.autosave_seen_page = page;
    session.autosave_seen_scroll = scroll;
    if (session.progress_autosave_debounce != 0) {
        session.app->document().clear_timer(session.progress_autosave_debounce);
        session.progress_autosave_debounce = 0;
    }
    // Quiet period after the last scroll/page change before hitting disk.
    session.progress_autosave_debounce = session.app->document().set_timeout([&session]() {
        session.progress_autosave_debounce = 0;
        if (session.current_chapter < 0 || session.progress_scroll_guard) {
            return;
        }
        capture_reading_progress(session);
    }, 400);
}

void arm_progress_autosave(Session& session) {
    if (session.app == nullptr) {
        return;
    }
    stop_progress_autosave(session);
    // Scroll mode: Host fires onscroll on #flow_wrap (bound to on_flow_scroll) which calls
    // schedule_progress_autosave. Keep a light interval only for page mode, where turns do not
    // always move scroll_top.
    session.progress_autosave_timer = session.app->document().set_interval([&session]() {
        if (session.app == nullptr || session.current_book < 0 || session.current_chapter < 0) {
            return;
        }
        if (!mode_page_checked(session)) {
            return;
        }
        schedule_progress_autosave(session);
    }, 500);
}

void capture_reading_progress(Session& session) {
    if (session.app == nullptr || session.current_book < 0 || session.current_chapter < 0 ||
        static_cast<size_t>(session.current_book) >= session.books.size()) {
        return;
    }
    const Book& book = session.books[static_cast<size_t>(session.current_book)];
    Progress& prog = session.progress[book.key];
    prog.chapter = session.current_chapter;
    prog.opened_at = now_unix();
    if (mode_page_checked(session)) {
        prog.page = session.page.page_index;
        prog.scroll = 0;
        prog.para = 0;
        prog.ch = 0;
    } else {
        const std::string top = session.app->document().get("flow_wrap", "scroll_top");
        prog.scroll = static_cast<int>(std::strtof(top.c_str(), nullptr));
        if (prog.scroll < 0) {
            prog.scroll = 0;
        }
        prog.page = 0;
        prog.para = estimate_visible_para(session);
        prog.ch = estimate_visible_ch(session, prog.para);
    }
    save_progress(session.progress_path, session.progress);
}

void restore_reading_progress(Session& session) {
    if (session.app == nullptr || session.current_book < 0 || session.current_chapter < 0 ||
        static_cast<size_t>(session.current_book) >= session.books.size()) {
        return;
    }
    const Book& book = session.books[static_cast<size_t>(session.current_book)];
    const auto found = session.progress.find(book.key);
    if (found == session.progress.end() || found->second.chapter != session.current_chapter) {
        return;
    }
    const Progress& prog = found->second;
    if (mode_page_checked(session)) {
        // Page restore is applied in show_chapter via page hint.
        return;
    }
    if (prog.para > 0 || prog.ch > 0 || prog.scroll > 0) {
        settle_restore_offset(session, prog.para, prog.ch, prog.scroll);
    }
}


}  // namespace reader
