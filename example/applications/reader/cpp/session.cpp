#include "session_detail.h"

#include "page_mode.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <memory>

namespace reader {

void bind_actions(Session& session) {
    zui::App& app = *session.app;
    app.document().bind("on_refresh", [&](zui::Event&) { fill_shelf(session); });
    app.document().bind("on_open", [&](zui::Event& e) { open_book(session, suffix_index(e.source(), "open")); });
    app.document().bind("on_back", [&](zui::Event&) { back_to_shelf(session); });
    // The contents tray is away by default; `toc-hidden` (reader.css) slides it out, which is the
    // convention the page's own script uses (`setTocHidden`).
    app.document().bind("on_toc", [&](zui::Event&) { toggle_toc(session); });
    app.document().bind("on_settings", [&](zui::Event&) { toggle_settings(session); });
    app.document().bind("on_toc_close", [&](zui::Event&) { leave_reader_ui(session); });
    app.document().bind("on_tray_scrim", [&](zui::Event&) { leave_reader_ui(session); });
    // Tapping the text: the overlays give way first, then the bars.
    app.document().bind("on_reading_tap", [&](zui::Event&) { tap_reading(session); });
    app.document().bind("on_flow_scroll", [&](zui::Event&) {
        if (session.progress_scroll_guard || mode_page_checked(session)) {
            return;
        }
        schedule_progress_autosave(session);
    });
    app.document().bind("on_chapter", [&](zui::Event& e) {
        show_chapter(session, suffix_index(e.source(), "ch"));
        leave_reader_ui(session);
    });
    app.document().bind("on_prev", [&](zui::Event&) { go_prev(session); });
    app.document().bind("on_next", [&](zui::Event&) { go_next(session); });
    // Search boxes keep their own onkeydown so Arrow/Page do not flip chapters while typing.
    app.document().bind("on_type_key", [&](zui::Event& e) {
        const zui::Key* key = e.is_key();
        if (key == nullptr || key->composing || key->key != "Escape") {
            return;
        }
        if (session.current_book < 0) {
            return;
        }
        if (!session.toc_hidden || session.settings_on) {
            leave_reader_ui(session);
            return;
        }
        if (session.ui_on) {
            set_ui(session, false);
        }
    });
    app.document().bind("on_accel", [&](zui::Event& e) {
        const zui::Key* key = e.is_key();
        if (key == nullptr || key->composing) {
            return;
        }
        if (key->key == "Escape") {
            if (session.current_book < 0) {
                return;
            }
            if (!session.toc_hidden || session.settings_on) {
                leave_reader_ui(session);
                return;
            }
            if (session.ui_on) {
                set_ui(session, false);
            }
            return;
        }
        if (session.current_book < 0 || session.current_chapter < 0) {
            return;
        }
        if (key->ctrl || key->alt || key->meta || key->shift) {
            return;
        }
        if (key->key == "ArrowLeft") {
            go_prev(session);
            return;
        }
        // The browser's Arrow keys are `turn(+-1)` in both modes; PageUp/PageDown scroll a page in
        // continuous mode, which the native window expresses as its own page step.
        if (key->key == "ArrowRight") {
            go_next(session, /*anchor_next=*/true);
            return;
        }
        if (key->key == "PageUp") {
            go_prev(session);
            return;
        }
        if (key->key == "PageDown") {
            go_next(session);
        }
    });
    app.document().bind("on_sort", [&](zui::Event&) {
        session.sort_by_title = !session.sort_by_title;
        app.document().set("sort_btn", "text", session.sort_by_title ? kSortTitle : kSortRecent);
        fill_shelf(session);
    });
    app.document().bind("on_resume", [&](zui::Event&) { open_book(session, most_recent_book(session)); });
    app.document().bind("on_bookmark", [&](zui::Event&) { toggle_bookmark(session); });
    app.document().bind("on_remove", [&](zui::Event& e) {
        const std::string source(e.source());
        // The second click is the same button. The arming click already took the book
        // out of `books`, so the index no longer names a row.
        if (!session.remove_source.empty() && session.remove_source == source) {
            const std::string key = session.remove_armed;
            session.remove_armed.clear();
            session.remove_source.clear();
            if (!key.empty()) {
                session.hidden.insert(key);
            }
            // Defer shelf rebuild: set_model would destroy #removeN under the active click.
            app.document().defer([&]() { fill_shelf(session); });
            return;
        }
        const int index = suffix_index(source, "remove");
        if (index < 0 || static_cast<size_t>(index) >= session.books.size()) {
            return;
        }
        // The page script arms the row first (the label becomes 确认) and only hides
        // the book on the second click. The in-memory list drops on the arm so a single
        // click already leaves the shelf empty; the card stays until confirm.
        const std::string key = session.books[static_cast<size_t>(index)].key;
        session.remove_armed = key;
        session.remove_source = source;
        app.document().set(source, "text", "\xe7\xa1\xae\xe8\xae\xa4");
        session.books.erase(session.books.begin() + index);
    });
    app.document().bind("on_import", [&](zui::Event&) { import_books(session); });
    app.document().bind("on_clear", [&](zui::Event&) {
        // reader.js arms the button on the first click and only then clears.
        if (!session.clear_armed) {
            session.clear_armed = true;
            app.document().set("clear_btn", "text", "\xe7\xa1\xae\xe8\xae\xa4\xe6\xb8\x85\xe7\xa9\xba");
            app.document().add_class("clear_btn", "armed");
            return;
        }
        session.clear_armed = false;
        clear_progress(session);
    });
    app.document().bind("on_hit", [&](zui::Event& e) {
        const int ci = std::atoi(std::string(e.data("ci")).c_str());
        const int pi = std::atoi(std::string(e.data("pi")).c_str());
        const int ch = std::atoi(std::string(e.data("ch")).c_str());
        goto_chapter_offset(session, ci, pi, ch, 0, 0);
        leave_reader_ui(session);
    });
    app.document().bind("on_mark", [&](zui::Event& e) {
        const std::string id(e.data("id"));
        for (const Bookmark& mark : session.marks) {
            if (mark.id == id) {
                goto_bookmark(session, mark);
                leave_reader_ui(session);
                return;
            }
        }
    });
    app.document().bind("on_unmark", [&](zui::Event& e) { remove_bookmark(session, e.data("id")); });
}

void show(Session& session) {
    zui::App& app = *session.app;
    app.window().set_title("\xe5\xb0\x8f\xe7\xab\xb9\xe9\x98\x85\xe8\xaf\xbb");
    app.window().set_window_size(1100, 720);
    fill_shelf(session);
    session.shelf_query_watch_.arm(session);
}

void arm_debug_harness(Session& session) {
    zui::App& app = *session.app;
    if (std::getenv("ZUI_READER_AUTOOPEN") != nullptr) {
        const int target_book = most_recent_book(session) >= 0 ? most_recent_book(session) : 0;
        app.document().defer([&, target_book]() { open_book(session, target_book); });
    }
    // Debug harness: `ZUI_READER_AUTOPLAY=<px per frame>` walks the reading pane down through the
    // data path (`App::set(..., "scroll_top", ...)`) instead of the wheel, and stops once the value
    // stops growing - the setter clamps at the end of the pane. ZUI_READER_AUTOPLAY_EXIT=1 quits
    // when it gets there, so a script can trace the walk and inspect build/trace.
    if (const char* autoplay = std::getenv("ZUI_READER_AUTOPLAY")) {
        const float step_px = std::max(1.0f, std::strtof(autoplay, nullptr));
        auto advance = std::make_shared<std::function<void()>>();
        auto started = std::make_shared<bool>(false);
        auto last_scroll = std::make_shared<float>(-1.0f);
        *advance = [&, advance, started, last_scroll, step_px]() {
            if (!*started) {
                if (session.current_chapter < 0) {
                    open_book(session, most_recent_book(session) >= 0 ? most_recent_book(session) : 0);
                    app.document().defer(*advance);
                    return;
                }
                *started = true;
            }
            const float current =
                std::strtof(app.document().get("flow_wrap", "scroll_top").c_str(), nullptr);
            if (*last_scroll >= 0.f && current <= *last_scroll) {
                if (std::getenv("ZUI_READER_AUTOPLAY_EXIT") != nullptr) {
                    app.quit();
                }
                return;
            }
            *last_scroll = current;
            app.document().set("flow_wrap", "scroll_top", std::to_string(current + step_px));
            app.document().defer(*advance);
        };
        app.document().defer(*advance);
    }
    // Debug harness: `ZUI_READER_AUTOTOUR=<frames>` walks the four interactions the layout plan's
    // section 4.7 measures - open the contents tray, switch the tray to search, switch it back to
    // contents, change the font size - reusing the same state changes the `on_toc` handler makes, so a
    // script records what a reader's taps cost on Release. The value is the settle window in
    // frames between two steps, and each step prints `[reader] tour <step>` to stderr first, which
    // is what lets the frame counters be sorted into steps. ZUI_READER_AUTOTOUR_EXIT=1 quits at
    // the end (that is what flushes the trace). Inert unless the variable is set.
    if (const char* tour = std::getenv("ZUI_READER_AUTOTOUR")) {
        const int settle = std::max(1, std::atoi(tour));
        const bool exit_at_end = std::getenv("ZUI_READER_AUTOTOUR_EXIT") != nullptr;
        // The tray tabs and the size chips are radio groups: a tap checks its input and clears the
        // rest of the group (`uncheck_radio_group`, source/input/interaction.cpp), so the harness
        // does the same instead of leaving two of them checked. The group comes in per call - a
        // local table would dangle, because this block ends before `app.run()` and the tick runs
        // inside it.
        auto check = [&app](const char* id, std::initializer_list<const char*> group) {
            for (const char* other : group) {
                const bool on = std::strcmp(other, id) == 0;
                if (on != (app.document().get(other, "checked") == "true")) {
                    app.document().set(other, "checked", on ? "true" : "false");
                }
            }
        };
        auto step = std::make_shared<int>(0);
        auto wait = std::make_shared<int>(settle);
        auto tick = std::make_shared<std::function<void()>>();
        *tick = [&, step, wait, tick, check, settle, exit_at_end]() {
            // A book has to be open (and its first chapter shown) before any of the four steps is
            // meaningful: the tray is built from the parsed book. The live handle is whatever the
            // shelf last showed, exactly like `open_book` from a card does.
            if (session.current_book < 0) {
                open_book(session, most_recent_book(session) >= 0 ? most_recent_book(session) : 0);
                app.document().defer(*tick);
                return;
            }
            // The chapter list is built from the parsed book, so wait for the first chapter too.
            if (session.current_chapter < 0) {
                *step = 0;
                *wait = settle;
                app.document().defer(*tick);
                return;
            }
            if (*wait > 0) {
                --*wait;
                app.document().defer(*tick);
                return;
            }
            switch (*step) {
                case 0:
                    std::fprintf(stderr, "[reader] tour toc_open\n");
                    ensure_toc(session);
                    set_toc_hidden(session, false);
                    set_ui(session, true);
                    set_settings(session, false);
                    break;
                case 1:
                    std::fprintf(stderr, "[reader] tour tab_find\n");
                    check("side_find", {"side_toc", "side_find", "side_marks"});
                    break;
                case 2:
                    std::fprintf(stderr, "[reader] tour tab_toc\n");
                    check("side_toc", {"side_toc", "side_find", "side_marks"});
                    break;
                case 3:
                    std::fprintf(stderr, "[reader] tour font_large\n");
                    check("size_l", {"size_s", "size_m", "size_l"});
                    break;
                default:
                    std::fprintf(stderr, "[reader] tour done\n");
                    if (exit_at_end) {
                        app.quit();
                    }
                    return;
            }
            ++*step;
            *wait = settle;
            app.document().defer(*tick);
        };
        app.document().defer(*tick);
    }
}

}  // namespace reader