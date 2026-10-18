#include "page_mode.h"
#include "book_format.h"
#include <fstream>

#include <zui/model.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string_view>

namespace reader {
namespace {

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

std::string size_token(zui::App& app) {
    if (app.document().get("size_s", "checked") == "true") {
        return "s";
    }
    if (app.document().get("size_l", "checked") == "true") {
        return "l";
    }
    return "m";
}

std::string lead_token(zui::App& app) {
    if (app.document().get("lead_s", "checked") == "true") {
        return "s";
    }
    if (app.document().get("lead_l", "checked") == "true") {
        return "l";
    }
    return "m";
}

std::string font_token(zui::App& app) {
    if (app.document().get("font_sans", "checked") == "true") {
        return "sans";
    }
    return "serif";
}

std::string cache_key(Session& session, int chapter_index) {
    zui::App& app = *session.app;
    const auto flow = app.document().measure("flow_wrap");
    const float content_width = flow ? flow->content_width : 0.f;
    const float client_height = flow ? flow->client_height : 0.f;
    return std::to_string(chapter_index) + "|" + size_token(app) + "|" + lead_token(app) + "|" +
           font_token(app) + "|" + std::to_string(content_width) + "|" +
           std::to_string(client_height);
}

constexpr const char* kNextChapter = "\xe4\xb8\x8b\xe4\xb8\x80\xe7\xab\xa0";
constexpr const char* kTheEnd = "\xe5\x85\xa8\xe4\xb9\xa6\xe5\xae\x8c";
constexpr const char* kDot = " \xc2\xb7 ";

}  // namespace

bool mode_page_checked(const Session& session) {
    return session.app != nullptr && session.app->document().get("mode_page", "checked") == "true";
}

void ensure_measure_host(Session& session) {
    if (session.page.measure_loaded || session.app == nullptr) {
        return;
    }
    if (session.app->document().load_fragment("fragments/measure.html")) {
        session.page.measure_loaded = true;
    }
}

void invalidate_page_cache(Session& session) {
    session.page.cache = {};
    session.page.page_chapter = -1;
    session.page.page_index = 0;
}

int measure_pages(Session& session, int chapter_index) {
    if (session.app == nullptr || session.current_book < 0 ||
        static_cast<size_t>(session.current_book) >= session.books.size()) {
        return 0;
    }
    Book& book = session.books[static_cast<size_t>(session.current_book)];
    if (chapter_index < 0) {
        return 0;
    }
    const int total = book_length(book);
    if (total > 0 && chapter_index >= total) {
        return 0;
    }
    if (!load_book_window(book, chapter_index, kChapterRadius) ||
        chapter_index >= static_cast<int>(book.chapters.size()) ||
        !book.chapters[static_cast<std::size_t>(chapter_index)].parsed) {
        return 0;
    }

    ensure_measure_host(session);
    if (!session.page.measure_loaded) {
        return 0;
    }

    zui::App& app = *session.app;
    const std::string key = cache_key(session, chapter_index);
    if (session.page.cache.key == key && !session.page.cache.pages.empty()) {
        return static_cast<int>(session.page.cache.pages.size());
    }

    const Chapter& chapter = book.chapters[static_cast<size_t>(chapter_index)];
    if (!chapter.html.empty()) {
        // EPUB/PDF chapter bodies are one visual page in page-mode; text find still
        // uses paragraphs scraped alongside the fragment.
        PageSlice slice;
        slice.para_index = 0;
        slice.text = chapter.paragraphs.empty() ? std::string() : chapter.paragraphs.front();
        session.page.cache.key = book.key + "#" + std::to_string(chapter_index) + "#html";
        session.page.cache.pages = {{slice}};
        return 1;
    }

    app.document().set("measure_title", "text", chapter.title);
    ParaRows paras;
    paras.rows = &chapter.paragraphs;
    if (!app.document().set_model("measure_body", paras)) {
        session.page.cache = {};
        return 0;
    }
    const auto flow = app.document().measure("flow_wrap");
    if (!flow || flow->client_height <= 1.f) {
        session.page.cache = {};
        return 0;
    }
    // Model rows must exist before offset packing; empty text means the fragment
    // template did not expand (wrong host / missing <template>).
    if (app.document().get("mp0", "text").empty() && !chapter.paragraphs.empty()) {
        session.page.cache = {};
        return 0;
    }

    const float limit = flow->client_height;
    const auto probe = app.document().measure("measure_probe");
    if (!probe) {
        session.page.cache = {};
        return 0;
    }

    struct Item {
        int para_index = -1;  // -1 = title
        float top = 0.f;
        float bottom = 0.f;
        std::string text;
    };
    std::vector<Item> items;
    {
        const auto title = app.document().measure("measure_title");
        if (title && title->border.height > 0.f) {
            items.push_back(
                Item{-1, title->border.y, title->border.y + title->border.height, chapter.title});
        }
    }
    for (size_t i = 0; i < chapter.paragraphs.size(); ++i) {
        const std::string id = "mp" + std::to_string(i);
        const auto row = app.document().measure(id);
        if (!row || row->border.height <= 0.f) {
            continue;
        }
        items.push_back(Item{static_cast<int>(i), row->border.y,
                             row->border.y + row->border.height, chapter.paragraphs[i]});
    }

    // Offset pack: item fits on the page that starts at page_origin when
    // (bottom - page_origin) <= client_height(flow_wrap). Title sets origin for page 0.
    std::vector<std::vector<PageSlice>> pages;
    std::vector<PageSlice> current;
    float page_origin = probe->border.y;
    bool page0_has_title = false;
    for (const Item& item : items) {
        if (item.para_index < 0) {
            page_origin = item.top;
            page0_has_title = true;
            continue;
        }
        if (current.empty() && pages.empty() && page0_has_title) {
            // Keep title origin for the first page.
        } else if (current.empty()) {
            page_origin = item.top;
        }
        const float extent = item.bottom - page_origin;
        if (!current.empty() && extent > limit + 1.f) {
            pages.push_back(current);
            current.clear();
            page_origin = item.top;
        }
        current.push_back(PageSlice{item.para_index, item.text});
    }
    if (!current.empty()) {
        pages.push_back(current);
    }
    if (pages.empty()) {
        pages.push_back({});
    }

    session.page.cache.key = key;
    session.page.cache.pages = std::move(pages);
    return static_cast<int>(session.page.cache.pages.size());
}

void paint_page(Session& session, int chapter_index, int page_index, int dir) {
    if (session.app == nullptr || session.current_book < 0 ||
        static_cast<size_t>(session.current_book) >= session.books.size()) {
        return;
    }
    Book& book = session.books[static_cast<size_t>(session.current_book)];
    if (chapter_index < 0 || chapter_index >= static_cast<int>(book.chapters.size())) {
        return;
    }
    const int count = measure_pages(session, chapter_index);
    if (count <= 0) {
        return;
    }
    if (page_index < 0) {
        page_index = 0;
    }
    if (page_index >= count) {
        page_index = count - 1;
    }

    zui::App& app = *session.app;
    session.current_chapter = chapter_index;
    session.page.page_chapter = chapter_index;
    session.page.page_index = page_index;

    const Chapter& chapter = book.chapters[static_cast<size_t>(chapter_index)];
    app.document().set("opening", "visible", "false");
    app.document().set("chapter_page", "visible", "true");
    app.document().set("chapter_b", "visible", "false");
    {
        ParaRows empty;
        app.document().set_model("paras_b", empty);
        app.document().set("chapter_title_b", "text", "");
    }
    app.document().set("chapter_title", "text", chapter.title);

    if (!chapter.html.empty()) {
        const std::filesystem::path frag_dir = book.cache_dir.empty()
                                                   ? (session.books_dir / ".cache" / book.key)
                                                   : book.cache_dir;
        std::error_code ec;
        std::filesystem::create_directories(frag_dir, ec);
        const std::filesystem::path frag =
            frag_dir / ("chapter-" + std::to_string(chapter_index) + "-p.html");
        {
            std::ofstream out(frag, std::ios::binary | std::ios::trunc);
            if (out) {
                out << "<!DOCTYPE html><html><body>";
                out << chapter.html;
                out << "</body></html>";
            }
        }
        ParaRows empty;
        app.document().set_model("paras", empty);
        app.document().set("paras", "visible", "false");
        if (book.format == BookFormat::Pdf) {
            app.document().set("pdf_host", "visible", "true");
            app.document().set("chapter_host", "visible", "false");
            (void)app.document().replace_fragment(to_utf8(frag), "pdf_page_body");
        } else {
            app.document().set("pdf_host", "visible", "false");
            app.document().set("chapter_host", "visible", "true");
            (void)app.document().replace_fragment(to_utf8(frag), "chapter_host");
        }
    } else {
        app.document().set("chapter_host", "visible", "false");
        app.document().set("pdf_host", "visible", "false");
        app.document().set("paras", "visible", "true");
    std::vector<std::string> texts;
    if (page_index < static_cast<int>(session.page.cache.pages.size())) {
        for (const PageSlice& slice : session.page.cache.pages[static_cast<size_t>(page_index)]) {
            texts.push_back(slice.text);
        }
    }
    ParaRows paras;
    paras.rows = &texts;
    app.document().set_model("paras", paras);
    }

    app.document().set("book_title", "text", book.title + std::string(kDot) + chapter.title);
    (void)app.window().set_title(book.title + std::string(kDot) + chapter.title);
    app.document().set("status_chapter", "text", chapter.title);
    const int total = book_length(book);
    app.document().set("status_progress", "text",
            std::to_string(page_index + 1) + "/" + std::to_string(count) + " · " +
                std::to_string(chapter_index + 1) + " / " + std::to_string(total));

    const bool last_page = page_index + 1 >= count;
    if (last_page) {
        app.document().set("chapter_next", "visible", "true");
        const bool has_next = chapter_index + 1 < total &&
                              chapter_index + 1 < static_cast<int>(book.chapters.size()) &&
                              !book.chapters[static_cast<std::size_t>(chapter_index) + 1].title.empty();
        if (has_next) {
            app.document().set("chapter_next", "text",
                    std::string(kNextChapter) + kDot +
                        book.chapters[static_cast<std::size_t>(chapter_index) + 1].title);
        } else if (chapter_index + 1 < total) {
            app.document().set("chapter_next", "text", kNextChapter);
        } else {
            app.document().set("chapter_next", "text", kTheEnd);
        }
    } else {
        app.document().set("chapter_next", "visible", "false");
    }

    app.document().set("flow_wrap", "scroll_top", "0");
    if (dir > 0) {
        app.document().remove_class("flow", "from-left");
        app.document().add_class("flow", "from-right");
    } else if (dir < 0) {
        app.document().remove_class("flow", "from-right");
        app.document().add_class("flow", "from-left");
    }

    Progress& prog = session.progress[book.key];
    prog.chapter = chapter_index;
    prog.page = page_index;
    prog.scroll = 0;
    prog.opened_at = now_unix();
    save_progress(session.progress_path, session.progress);
    (void)dir;
}

void turn_page(Session& session, int dir) {
    if (session.app == nullptr || dir == 0) {
        return;
    }
    if (session.current_book < 0 || session.current_chapter < 0) {
        return;
    }
    const int token = ++session.page.turn_token;
    const int chapter = session.page.page_chapter >= 0 ? session.page.page_chapter
                                                      : session.current_chapter;
    const int page = session.page.page_index;
    session.app->document().defer([&, token, dir, chapter, page]() {
        if (token != session.page.turn_token) {
            return;
        }
        if (session.current_book < 0 ||
            static_cast<size_t>(session.current_book) >= session.books.size()) {
            return;
        }
        Book& book = session.books[static_cast<size_t>(session.current_book)];
        const int count = measure_pages(session, chapter);
        int next_page = page + dir;
        if (next_page >= 0 && next_page < count) {
            paint_page(session, chapter, next_page, dir);
            return;
        }
        const int next_chapter = chapter + dir;
        const int total = book_length(book);
        if (next_chapter < 0 || (total > 0 && next_chapter >= total)) {
            return;
        }
        const int pages = measure_pages(session, next_chapter);
        const int land = dir < 0 ? std::max(0, pages - 1) : 0;
        paint_page(session, next_chapter, land, dir);
    });
}

void show_in_page_mode(Session& session, int chapter_index, int page_hint) {
    invalidate_page_cache(session);
    const int pages = measure_pages(session, chapter_index);
    int page = page_hint;
    if (page < 0) {
        page = 0;
    }
    if (pages > 0 && page >= pages) {
        page = pages - 1;
    }
    paint_page(session, chapter_index, page, 0);
}

void sync_mode_from_dom(Session& session) {
    if (session.app == nullptr) {
        return;
    }
    const bool want_page = mode_page_checked(session);
    if (!want_page) {
        session.page.page_chapter = -1;
        return;
    }
    if (session.current_book < 0 || session.current_chapter < 0) {
        return;
    }
    if (session.page.page_chapter == session.current_chapter &&
        !session.page.cache.pages.empty()) {
        return;
    }
    // Ratio estimate: map scroll_top / scroll_height into a page index.
    zui::App& app = *session.app;
    const float top = std::strtof(app.document().get("flow_wrap", "scroll_top").c_str(), nullptr);
    const auto flow = app.document().measure("flow_wrap");
    if (!flow) {
        return;
    }
    const float sh = flow->scroll_height;
    const float ch = flow->client_height;
    const int pages = measure_pages(session, session.current_chapter);
    int hint = 0;
    if (pages > 1 && sh > ch + 1.f) {
        const float ratio = top / std::max(1.f, sh - ch);
        hint = static_cast<int>(std::floor(ratio * static_cast<float>(pages) + 1e-3f));
        if (hint < 0) {
            hint = 0;
        }
        if (hint >= pages) {
            hint = pages - 1;
        }
    }
    paint_page(session, session.current_chapter, hint, 0);
}

}  // namespace reader
