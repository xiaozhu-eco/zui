#include "session_detail.h"
#include "book_epub.h"
#include "book_pdf.h"
#include "book_format.h"

#include "page_mode.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <fstream>

namespace reader {

/// Shelf order is the one place the two engines must agree. Recent puts a higher
/// opened_at first and treats a missing stamp as 0; every tie, and title mode itself,
/// falls through to the host pinyin order. Title mode skips the stamp compare.
class ShelfOrder {
public:
    virtual ~ShelfOrder() = default;
    virtual bool less(const Book& a, const Book& b, const Session& session) const = 0;
};

class RecentOrder final : public ShelfOrder {
public:
    bool less(const Book& a, const Book& b, const Session& session) const override {
        const auto ia = session.progress.find(a.key);
        const auto ib = session.progress.find(b.key);
        const long long ta = ia == session.progress.end() ? 0 : ia->second.opened_at;
        const long long tb = ib == session.progress.end() ? 0 : ib->second.opened_at;
        if (ta != tb) {
            return ta > tb;
        }
        return zh_title_less(a.title, b.title);
    }
};

class TitleOrder final : public ShelfOrder {
public:
    bool less(const Book& a, const Book& b, const Session&) const override {
        return zh_title_less(a.title, b.title);
    }
};

void fill_shelf(Session& session) {
    zui::App& app = *session.app;
        const ShelfQuery query = ShelfQuery::read(app);
        const auto started = std::chrono::steady_clock::now();
        const std::map<std::string, Book> catalog = load_catalog(session.books_dir / "catalog.json");
        session.books.clear();
        std::error_code ec;
        std::filesystem::create_directories(session.books_dir, ec);
        ec.clear();
        for (std::filesystem::directory_iterator it(session.books_dir, ec);
             !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
            std::error_code file_ec;
            if (!it->is_regular_file(file_ec) || !is_book(it->path())) {
                continue;
            }
            const std::string name = to_utf8(it->path().filename());
            if (name.empty() || name[0] == '.') {
                continue;
            }
            Book loaded;
            const auto listed = catalog.find(name);
            if (listed != catalog.end()) {
                loaded = listed->second;
                loaded.path = it->path();
                loaded.format = detect_format(it->path());
            } else {
                bool ok = false;
                try {
                    loaded.format = detect_format(it->path());
                    if (loaded.format == BookFormat::Epub) {
                        ok = epub_summary(it->path(), loaded);
                    } else if (loaded.format == BookFormat::Pdf) {
                        ok = pdf_summary(it->path(), loaded);
                    } else {
                        ok = book_summary(it->path(), loaded);
                        loaded.format = BookFormat::Txt;
                    }
                } catch (const std::exception&) {
                    ok = false;
                }
                if (!ok) {
                    continue;
                }
            }
            if (session.hidden.find(loaded.key) != session.hidden.end()) {
                continue;
            }
            session.books.push_back(std::move(loaded));
        }
        const RecentOrder by_recent;
        const TitleOrder by_title;
        const ShelfOrder& order = session.sort_by_title
                                       ? static_cast<const ShelfOrder&>(by_title)
                                       : static_cast<const ShelfOrder&>(by_recent);
        std::sort(session.books.begin(), session.books.end(),
                  [&](const Book& a, const Book& b) { return order.less(a, b, session); });
        BookRows rows;
        rows.rows.reserve(session.books.size());
        // The page hands each cover a tone class ("tone-0".."tone-3") and its stylesheet paints
        // the gradient from it (`.tone-0 .cover { background: linear-gradient(...) }`). reader.js
        // computes that class while it clones the card, so the app has to compute the same number
        // and let the template bind it: without it the shelf keeps its paper background and the
        // covers vanish. The hash is reader.js's own (UTF-16 code units, times 31), and a tone
        // already taken steps to the next one, which is what keeps neighbouring books apart.
        int used_tone[4] = {0, 0, 0, 0};
        int shown = 0;
        int book_index = 0;
        for (const Book& book : session.books) {
            const int index = book_index++;
            if (!query.matches(book)) {
                continue;
            }
            ++shown;
            std::uint32_t hash = 0;
            for (std::size_t i = 0; i < book.title.size();) {
                const unsigned char lead = static_cast<unsigned char>(book.title[i]);
                std::uint32_t point = lead;
                int extra = 0;
                if (lead >= 0xF0) {
                    point = lead & 0x07u;
                    extra = 3;
                } else if (lead >= 0xE0) {
                    point = lead & 0x0Fu;
                    extra = 2;
                } else if (lead >= 0xC0) {
                    point = lead & 0x1Fu;
                    extra = 1;
                }
                ++i;
                for (int k = 0; k < extra && i < book.title.size(); ++k, ++i) {
                    point = (point << 6) | (static_cast<unsigned char>(book.title[i]) & 0x3Fu);
                }
                if (point >= 0x10000u) {
                    const std::uint32_t rest = point - 0x10000u;
                    hash = hash * 31u + (0xD800u + (rest >> 10));
                    hash = hash * 31u + (0xDC00u + (rest & 0x3FFu));
                } else {
                    hash = hash * 31u + point;
                }
            }
            int tone = static_cast<int>(hash % 4u);
            for (int guard = 0; guard < 4 && used_tone[tone] != 0; ++guard) {
                tone = (tone + 1) % 4;
            }
            used_tone[tone] = 1;
            const auto found = session.progress.find(book.key);
            const int chapter = found == session.progress.end() ? -1 : found->second.chapter;
            const int pct = chapter < 0 ? 0 : progress_pct(book, chapter);
            BookRows::Row row{book.title, book_meta(book), book.author,
                              card_where(book, session.progress), pct_class(pct), tone};
            row.index = index;
            rows.rows.push_back(std::move(row));
        }
        app.document().set_model("books", rows);
        // The page's own design language: `is-booting` holds the empty state back until the
        // shelf has been scanned, and `is-empty` is what shows `#empty_*` (reader.css).
        app.document().remove_class("shelf", "is-booting");
        if (session.books.empty()) {
            app.document().add_class("shelf", "is-empty");
            app.document().set("empty_title", "text", kEmptyShelf);
            app.document().set("empty_hint", "text", kHint);
        } else if (shown == 0) {
            app.document().add_class("shelf", "is-empty");
            app.document().set("empty_title", "text", kNoMatch);
            app.document().set("empty_hint", "text", kNoMatchHint);
        } else {
            app.document().remove_class("shelf", "is-empty");
        }
        // `#resume` stays `hidden` in the markup until there is something to continue. The
        // browser page flips that flag from the saved place map; do the same with progress.
        const int recent = most_recent_book(session);
        if (!query.active() && recent >= 0 && recent < static_cast<int>(session.books.size()) &&
            session.progress.find(session.books[static_cast<size_t>(recent)].key) != session.progress.end()) {
            const Book& book = session.books[static_cast<size_t>(recent)];
            app.document().set("resume", "visible", "true");
            app.document().set("resume_title", "text", book.title);
            app.document().set("resume_where", "text", card_where(book, session.progress));
        } else {
            app.document().set("resume", "visible", "false");
            app.document().set("resume_title", "text", "");
        }
        trace_ms("shelf scan", ms_since(started));
}

void show_chapter(Session& session, int index, bool restore_offset) {
    invalidate_para_geom(session);
    zui::App& app = *session.app;
        if (session.current_book < 0 || static_cast<size_t>(session.current_book) >= session.books.size()) {
            return;
        }
        Book& book = session.books[static_cast<size_t>(session.current_book)];
        if (session.current_chapter >= 0 && session.current_chapter != index) {
            capture_reading_progress(session);
        }
        if (index < 0) {
            index = 0;
        }
        {
            const int known = book_length(book);
            if (known > 0 && index >= known) {
                index = known - 1;
            }
        }
        if (!load_book_window(book, index, kChapterRadius)) {
            return;
        }
        const int count = book_length(book);
        if (count <= 0 || book.chapters.empty()) {
            return;
        }
        if (index >= count) {
            index = count - 1;
        }
        if (index >= static_cast<int>(book.chapters.size()) ||
            !book.chapters[static_cast<std::size_t>(index)].parsed) {
            return;
        }
        if (mode_page_checked(session)) {
            int page_hint = 0;
            if (restore_offset) {
                const auto found = session.progress.find(book.key);
                if (found != session.progress.end() && found->second.chapter == index) {
                    page_hint = found->second.page;
                }
            }
            show_in_page_mode(session, index, page_hint);
            if (session.toc_ready) {
                for (int i = 0; i < static_cast<int>(book.chapters.size()); ++i) {
                    app.document().set("ch" + std::to_string(i), "checked", i == index ? "true" : "false");
                }
            }
            Progress& prog = session.progress[book.key];
            prog.chapter = index;
            prog.page = session.page.page_index;
            prog.opened_at = now_unix();
            if (!restore_offset) {
                prog.scroll = 0;
                prog.page = session.page.page_index;
            }
            save_progress(session.progress_path, session.progress);
            return;
        }
        if (index == session.current_chapter) {
            return;
        }
        invalidate_page_cache(session);
        if (session.toc_ready && session.current_chapter >= 0 && session.current_chapter != index) {
            app.document().set("ch" + std::to_string(session.current_chapter), "checked", "false");
        }
        session.current_chapter = index;
        if (session.toc_ready) {
            app.document().set("ch" + std::to_string(index), "checked", "true");
        }
        app.document().set("opening", "visible", "false");
        // Stable markup in reader.html, but force a box-tree teardown each chapter change.
        // Without this, detached paragraph boxes from an earlier window can remain below
        // the new window and show up after the next chapter.
        {
            ParaRows empty;
            app.document().set("chapter_page", "visible", "false");
            app.document().set_model("paras", empty);
            app.document().set_model("paras_b", empty);
            app.document().set("chapter_title", "text", "");
            app.document().set("chapter_title_b", "text", "");
            app.document().set("chapter_b", "visible", "false");
        }
        app.document().set("chapter_page", "visible", "true");
        const Chapter& chapter = book.chapters[static_cast<size_t>(index)];
        app.document().set("chapter_title", "text", chapter.title);
        ParaRows paras;
        if (!chapter.html.empty()) {
            const std::filesystem::path frag_dir = book.cache_dir.empty()
                                                       ? (session.books_dir / ".cache" / book.key)
                                                       : book.cache_dir;
            std::error_code ec;
            std::filesystem::create_directories(frag_dir, ec);
            const std::filesystem::path frag = frag_dir / ("chapter-" + std::to_string(index) + ".html");
            {
                std::ofstream out(frag, std::ios::binary | std::ios::trunc);
                if (out) {
                    out << "<!DOCTYPE html><html><body>";
                    out << chapter.html;
                    out << "</body></html>";
                }
            }
            app.document().set("paras", "visible", "false");
            app.document().set("paras_b", "visible", "false");
            app.document().set("chapter_host", "visible", "true");
            if (book.format == BookFormat::Pdf) {
                app.document().set("pdf_host", "visible", "true");
                (void)app.document().replace_fragment(to_utf8(frag), "pdf_page_body");
            } else {
                app.document().set("pdf_host", "visible", "false");
                (void)app.document().replace_fragment(to_utf8(frag), "chapter_host");
            }
            {
                ParaRows empty;
                app.document().set_model("paras", empty);
                app.document().set_model("paras_b", empty);
            }
        } else {
            app.document().set("chapter_host", "visible", "false");
            app.document().set("pdf_host", "visible", "false");
            app.document().set("paras", "visible", "true");
            paras.rows = &chapter.paragraphs;
            app.document().set_model("paras", paras);
            // Match reader.js's windowed continuous scroll: keep the next chapter in the
            // same flow so scrolling past the end of this one lands on a real heading.
            const bool has_follow = index + 1 < count &&
                                    index + 1 < static_cast<int>(book.chapters.size()) &&
                                    book.chapters[static_cast<std::size_t>(index) + 1].parsed;
            if (has_follow) {
                const Chapter& follow = book.chapters[static_cast<size_t>(index) + 1];
                app.document().set("chapter_b", "visible", "true");
                app.document().set("chapter_title_b", "text", follow.title);
                ParaRows follow_paras;
                follow_paras.rows = &follow.paragraphs;
                app.document().set_model("paras_b", follow_paras);
            } else {
                app.document().set("chapter_b", "visible", "false");
                ParaRows empty;
                app.document().set_model("paras_b", empty);
                app.document().set("chapter_title_b", "text", "");
            }
        }
        // The toolbar shows the book name. The chapter title lives in the status line and the
        // heading, matching reader.js (`book_title` stays the book, `status_chapter` the chapter).
        app.document().set("book_title", "text", book.title);
        (void)app.window().set_title(chapter.title + kDot + book.title);
        app.document().set("chapter_next", "visible", "true");
        if (index + 1 < count && index + 1 < static_cast<int>(book.chapters.size()) &&
            !book.chapters[static_cast<std::size_t>(index) + 1].title.empty()) {
            app.document().set("chapter_next", "text",
                    std::string(kNextChapter) + kDot +
                        book.chapters[static_cast<std::size_t>(index) + 1].title);
        } else if (index + 1 < count) {
            app.document().set("chapter_next", "text", kNextChapter);
        } else {
            app.document().set("chapter_next", "text", kTheEnd);
        }
        app.document().set("status_chapter", "text", chapter.title);
        app.document().set("status_progress", "text",
                std::to_string(index + 1) + " / " + std::to_string(count));
        // A chapter change replaces #flow's contents; start the reading pane at the top
        // the way the browser version does after it rebuilds the article.
        app.document().set("flow_wrap", "scroll_top", "0");
        // Drop any previous pct-* class on the status hairline, then apply the new one.
        for (int step = 0; step <= 100; step += 5) {
            app.document().remove_class("status_fill", "pct-" + std::to_string(step));
        }
        app.document().add_class("status_fill", pct_class(progress_pct(book, index)));
        Progress& prog = session.progress[book.key];
        prog.chapter = index;
        prog.opened_at = now_unix();
        if (!restore_offset) {
            prog.scroll = 0;
            prog.page = 0;
            prog.para = 0;
            prog.ch = 0;
        }
        save_progress(session.progress_path, session.progress);
        if (restore_offset) {
            restore_reading_progress(session);
        }
}

void open_book(Session& session, int index) {
    zui::App& app = *session.app;
        if (index < 0 || static_cast<size_t>(index) >= session.books.size()) {
            return;
        }
        if (session.current_book >= 0 && session.current_chapter >= 0 &&
            session.current_book != index) {
            capture_reading_progress(session);
        }
        // Paint the reading shell and the same opening line the browser shows, then
        // parse on the next frame so the UI does not freeze on the shelf.
        session.current_book = index;
        session.current_chapter = -1;
        session.toc_ready = false;
        session.toc_built = false;
        set_toc_hidden(session, true);
        app.document().add_class("reader", "open");
        set_ui(session, false);
        set_settings(session, false);
        app.document().set("chapter_page", "visible", "false");
        {
            ParaRows empty;
            app.document().set_model("paras", empty);
            app.document().set_model("paras_b", empty);
        }
        app.document().set("chapter_title", "text", "");
        app.document().set("chapter_title_b", "text", "");
        const Book& pending = session.books[static_cast<size_t>(index)];
        app.document().set("book_title", "text", pending.title);
        app.document().set("opening", "text",
                std::string(kOpeningPrefix) + pending.title + kOpeningSuffix);
        app.document().set("opening", "visible", "true");

        const int token = ++session.open_token;
        app.document().defer([&, token]() {
            if (token != session.open_token) {
                return;
            }
            if (session.current_book < 0 || static_cast<size_t>(session.current_book) >= session.books.size()) {
                return;
            }
            Book& book = session.books[static_cast<size_t>(session.current_book)];
            int saved = 0;
            const auto found = session.progress.find(book.key);
            if (found != session.progress.end()) {
                saved = found->second.chapter;
            }
            const auto parse_started = std::chrono::steady_clock::now();
            try {
                // Paragraph text for the saved chapter and three chapters on either side.
                // The contents list stays unbuilt until the first contents tap.
                if (book.path.empty() || !load_book_window(book, saved, kChapterRadius)) {
                    app.document().set("opening", "visible", "false");
                    app.document().remove_class("reader", "open");
                    toast(session, kOpenFailed);
                    return;
                }
            } catch (const std::exception&) {
                app.document().set("opening", "visible", "false");
                app.document().remove_class("reader", "open");
                toast(session, kOpenFailed);
                return;
            }
            trace_ms("chapter parse", ms_since(parse_started));
            app.document().set("opening", "visible", "false");
            try {
                show_chapter(session, saved, true);
                arm_progress_autosave(session);
            } catch (const std::exception&) {
                app.document().remove_class("reader", "open");
                toast(session, kOpenFailed);
            }
        });
}

}  // namespace reader