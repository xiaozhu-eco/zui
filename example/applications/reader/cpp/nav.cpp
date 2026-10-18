#include "session_detail.h"

#include "page_mode.h"
#include "book_format.h"

namespace reader {

int most_recent_book(const Session& session) {
    int best = -1;
    long long best_at = -1;
    int best_chapter = -1;
    for (size_t i = 0; i < session.books.size(); ++i) {
        const auto found = session.progress.find(session.books[i].key);
        if (found == session.progress.end()) {
            continue;
        }
        const long long at = found->second.opened_at;
        const int chapter = found->second.chapter;
        if (best < 0 || at > best_at || (at == best_at && chapter > best_chapter)) {
            best = static_cast<int>(i);
            best_at = at;
            best_chapter = chapter;
        }
    }
    return best;
}

void leave_reader_ui(Session& session) {
    set_ui(session, false);
    set_settings(session, false);
    if (!session.toc_hidden) {
        set_toc_hidden(session, true);
    }
}

void back_to_shelf(Session& session) {
    zui::App& app = *session.app;
    capture_reading_progress(session);
    stop_progress_autosave(session);
    ++session.open_token;
    drop_toc_model(session);
    set_toc_hidden(session, true);
    app.document().set("chapter_page", "visible", "false");
    {
        ParaRows empty;
        app.document().set_model("paras", empty);
        app.document().set_model("paras_b", empty);
    }
    app.document().remove_class("reader", "open");
    app.document().set("opening", "visible", "false");
    set_ui(session, false);
    set_settings(session, false);
    invalidate_page_cache(session);
}

void toggle_toc(Session& session) {
    zui::App& app = *session.app;
    if (session.toc_hidden) {
        ensure_toc(session);
        set_toc_hidden(session, false);
        set_ui(session, true);
        set_settings(session, false);
        if (session.current_chapter >= 0) {
            const std::string ch_id = "ch" + std::to_string(session.current_chapter);
            app.document().defer([&app, ch_id]() {
                (void)app.document().scroll_into_view(ch_id, zui::ScrollAlign::Nearest);
            });
        }
        if (!session.toc_filter.empty()) {
            filter_chapters(session, session.toc_filter);
        }
    } else {
        set_toc_hidden(session, true);
        set_ui(session, false);
    }
}

void toggle_settings(Session& session) {
    set_ui(session, true);
    set_settings(session, !session.settings_on);
    if (session.settings_on && !session.toc_hidden) {
        set_toc_hidden(session, true);
    }
    sync_mode_from_dom(session);
}

void tap_reading(Session& session) {
    if (session.current_book < 0 || session.current_chapter < 0) {
        return;
    }
    if (!session.toc_hidden || session.settings_on) {
        leave_reader_ui(session);
        return;
    }
    set_ui(session, !session.ui_on);
    // reader.js markHint(): the first tap that toggles the bars retires the onboarding hint.
    session.app->document().add_class("reader", "hint-seen");
}

void go_prev(Session& session) {
    if (mode_page_checked(session)) {
        turn_page(session, -1);
        return;
    }
    show_chapter(session, session.current_chapter - 1);
}

void go_next(Session& session, bool anchor_next) {
    if (mode_page_checked(session)) {
        // Last page: chapter_next / on_next advances chapter via turn_page overflow.
        turn_page(session, 1);
        return;
    }
    if (anchor_next && !next_chapter_can_reach_the_anchor(session)) {
        return;
    }
    show_chapter(session, session.current_chapter + 1);
}

int import_picked_books(Session& session, const std::vector<std::string>& utf8_paths) {
    std::error_code dir_ec;
    std::filesystem::create_directories(session.books_dir, dir_ec);
    int imported = 0;
    for (const std::string& raw : utf8_paths) {
        // The host hands back UTF-8. The narrow `path` constructor reads the process code page,
        // so a Chinese title never copied and the shelf stayed empty.
        const std::filesystem::path src = std::filesystem::u8path(raw);
        if (!is_book(src)) {
            continue;
        }
        std::error_code ec;
        const std::filesystem::path dest = session.books_dir / src.filename();
        bool same = false;
        if (std::filesystem::exists(src, ec) && std::filesystem::exists(dest, ec)) {
            ec.clear();
            same = std::filesystem::equivalent(src, dest, ec);
        }
        if (!same) {
            ec.clear();
            std::filesystem::copy_file(src, dest, std::filesystem::copy_options::overwrite_existing,
                                       ec);
            if (ec) {
                continue;
            }
        }
        // reader.js `ingest` deletes `hidden[book.key]`. The key is the UTF-8 filename.
        session.hidden.erase(to_utf8(dest.filename()));
        ++imported;
    }
    return imported;
}

void import_books(Session& session) {
    zui::App& app = *session.app;
    zui::FileDialogOptions opt;
    opt.title = "Import books";
    opt.multi = true;
    opt.filters.push_back({"Books", "txt;text;epub;pdf"});
    const zui::PickPaths picked = app.host().pick_open(opt);
    if (picked.status != zui::PickStatus::Ok || picked.paths.empty()) {
        if (picked.status == zui::PickStatus::Unsupported) {
            toast(session, kImportHint);
        }
        return;
    }
    if (import_picked_books(session, picked.paths) == 0) {
        toast(session, kImportHint);
        return;
    }
    fill_shelf(session);
    toast(session, kImportedOk);
}

void clear_progress(Session& session) {
    // Drop pending open/turn work that would rewrite progress after this clear.
    ++session.open_token;
    ++session.page.turn_token;
    session.progress.clear();
    save_progress(session.progress_path, session.progress);
    session.marks.clear();
    persist_marks(session);
    toast(session, kClearedHint);
}

}  // namespace reader