#include "session_detail.h"

#include <nlohmann/json.hpp>
#include <fstream>

#include <algorithm>

namespace reader {

namespace {

bool ascii_only(std::string_view s) {
    for (unsigned char c : s) {
        if (c > 127) {
            return false;
        }
    }
    return true;
}

std::string lower_ascii(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

int locate_text(std::string_view text, std::string_view query) {
    if (query.empty()) {
        return -1;
    }
    if (ascii_only(query)) {
        const std::string hay = lower_ascii(text);
        const std::string needle = lower_ascii(query);
        const size_t at = hay.find(needle);
        return at == std::string::npos ? -1 : static_cast<int>(at);
    }
    const size_t at = text.find(query);
    return at == std::string::npos ? -1 : static_cast<int>(at);
}

std::string make_mark_id() {
    static int seq = 0;
    return "m" + std::to_string(now_unix()) + "_" + std::to_string(++seq);
}

class HitRows : public zui::ListModel {
public:
    const std::vector<FindHit>* rows = nullptr;

    size_t row_count() const override { return rows == nullptr ? 0 : rows->size(); }

    std::string value(size_t row, std::string_view key) const override {
        if (rows == nullptr || row >= rows->size()) {
            return {};
        }
        const FindHit& hit = (*rows)[row];
        if (key == "ci") {
            return std::to_string(hit.ci);
        }
        if (key == "pi") {
            return std::to_string(hit.pi);
        }
        if (key == "ch") {
            return std::to_string(hit.ch);
        }
        if (key == "title") {
            return hit.title;
        }
        if (key == "snip") {
            return hit.snip;
        }
        return {};
    }
};

class MarkRows : public zui::ListModel {
public:
    std::vector<Bookmark> rows;

    size_t row_count() const override { return rows.size(); }

    std::string value(size_t row, std::string_view key) const override {
        if (row >= rows.size()) {
            return {};
        }
        const Bookmark& mark = rows[row];
        if (key == "id") {
            return mark.id;
        }
        if (key == "label") {
            return mark.label;
        }
        if (key == "snip") {
            return mark.snip;
        }
        return {};
    }
};

}  // namespace

void ensure_hits_host(Session& session) {
    if (session.page.hits_loaded || session.app == nullptr) {
        return;
    }
    // Relative to reader.html (App::load_fragment joins document.source_path parent).
    if (session.app->document().load_fragment("fragments/hits.html", "find_hits")) {
        session.page.hits_loaded = true;
    }
}

void ensure_marks_host(Session& session) {
    if (session.page.marks_loaded || session.app == nullptr) {
        return;
    }
    if (session.app->document().load_fragment("fragments/marks.html", "marks")) {
        session.page.marks_loaded = true;
    }
}

void filter_chapters(Session& session, std::string_view query) {
    session.toc_filter = std::string(query);
    if (session.app == nullptr || !session.toc_ready || session.current_book < 0 ||
        static_cast<size_t>(session.current_book) >= session.books.size()) {
        return;
    }
    const Book& book = session.books[static_cast<size_t>(session.current_book)];
    for (size_t i = 0; i < book.chapters.size(); ++i) {
        const bool show =
            query.empty() || book.chapters[i].title.find(query) != std::string::npos;
        session.app->document().set("ch" + std::to_string(i), "visible", show ? "true" : "false");
    }
}

void find_in_book(Session& session, std::string_view query) {
    session.find_query = std::string(query);
    session.find_hits.clear();
    zui::App& app = *session.app;
    ensure_hits_host(session);
    if (session.current_book < 0 ||
        static_cast<size_t>(session.current_book) >= session.books.size()) {
        app.document().set("find_count", "text", query.empty() ? kFindPrompt : "");
        HitRows empty;
        empty.rows = &session.find_hits;
        app.document().set_model("find_hits", empty);
        return;
    }
    if (query.empty()) {
        app.document().set("find_count", "text", kFindPrompt);
        HitRows empty;
        empty.rows = &session.find_hits;
        app.document().set_model("find_hits", empty);
        return;
    }
    Book& book = session.books[static_cast<size_t>(session.current_book)];
    // Search still covers the whole file. Open itself only parsed the chapter window.
    const int last = book_length(book) - 1;
    if (last >= 0) {
        load_book_window(book, 0, last);
    }
    for (size_t ci = 0; ci < book.chapters.size(); ++ci) {
        const Chapter& ch = book.chapters[ci];
        for (size_t pi = 0; pi < ch.paragraphs.size(); ++pi) {
            const std::string& text = ch.paragraphs[pi];
            const int at = locate_text(text, query);
            if (at < 0) {
                continue;
            }
            FindHit hit;
            hit.ci = static_cast<int>(ci);
            hit.pi = static_cast<int>(pi);
            hit.ch = at;
            hit.title = ch.title;
            const size_t start = static_cast<size_t>(std::max(0, at - 14));
            const size_t end = std::min(text.size(), static_cast<size_t>(at) + query.size() + 18);
            hit.snip = (start ? "\xe2\x80\xa6" : "") + text.substr(start, end - start) +
                       (end < text.size() ? "\xe2\x80\xa6" : "");
            session.find_hits.push_back(std::move(hit));
            if (session.find_hits.size() >= 100) {
                break;
            }
        }
        if (session.find_hits.size() >= 100) {
            break;
        }
    }
    if (session.find_hits.empty()) {
        app.document().set("find_count", "text", kFindNone);
    } else {
        std::string msg = std::to_string(session.find_hits.size()) + kFindUnit;
        // Cap message when truncated during scan; we stop at 100.
        if (session.find_hits.size() >= 100) {
            msg += kFindCap;
        }
        app.document().set("find_count", "text", msg);
    }
    HitRows rows;
    rows.rows = &session.find_hits;
    app.document().set_model("find_hits", rows);
}


void persist_marks(Session& session) {
    if (session.marks_path.empty()) {
        return;
    }
    nlohmann::json root = nlohmann::json::array();
    for (const Bookmark& mark : session.marks) {
        root.push_back({
            {"id", mark.id},
            {"key", mark.key},
            {"ci", mark.ci},
            {"pi", mark.pi},
            {"ch", mark.ch},
            {"page", mark.page},
            {"scroll", mark.scroll},
            {"label", mark.label},
            {"snip", mark.snip},
            {"t", mark.t},
        });
    }
    std::ofstream out(session.marks_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return;
    }
    out << root.dump(2);
}

void load_session_marks(Session& session) {
    session.marks.clear();
    if (session.marks_path.empty()) {
        return;
    }
    std::ifstream in(session.marks_path, std::ios::binary);
    if (!in) {
        return;
    }
    try {
        nlohmann::json root;
        in >> root;
        if (!root.is_array()) {
            return;
        }
        for (const nlohmann::json& item : root) {
            if (!item.is_object()) {
                continue;
            }
            Bookmark mark;
            mark.id = item.value("id", "");
            mark.key = item.value("key", "");
            if (mark.id.empty() || mark.key.empty()) {
                continue;
            }
            mark.ci = item.value("ci", 0);
            mark.pi = item.value("pi", 0);
            mark.ch = item.value("ch", 0);
            mark.page = item.value("page", 0);
            mark.scroll = item.value("scroll", 0);
            mark.label = item.value("label", "");
            mark.snip = item.value("snip", "");
            mark.t = item.value("t", 0LL);
            session.marks.push_back(std::move(mark));
        }
    } catch (...) {
        session.marks.clear();
    }
}

std::vector<Bookmark> list_bookmarks(const Session& session) {
    std::vector<Bookmark> out;
    if (session.current_book < 0 ||
        static_cast<size_t>(session.current_book) >= session.books.size()) {
        return out;
    }
    const std::string& key = session.books[static_cast<size_t>(session.current_book)].key;
    for (const Bookmark& mark : session.marks) {
        if (mark.key == key) {
            out.push_back(mark);
        }
    }
    std::sort(out.begin(), out.end(),
              [](const Bookmark& a, const Bookmark& b) { return a.t > b.t; });
    return out;
}

void render_marks(Session& session) {
    if (session.app == nullptr) {
        return;
    }
    ensure_marks_host(session);
    MarkRows rows;
    rows.rows = list_bookmarks(session);
    session.app->document().set("marks_empty", "visible", rows.rows.empty() ? "true" : "false");
    session.app->document().set_model("marks", rows);
}

void add_bookmark(Session& session, int ci, int pi, int ch, int page, int scroll) {
    if (session.current_book < 0 ||
        static_cast<size_t>(session.current_book) >= session.books.size()) {
        return;
    }
    const Book& book = session.books[static_cast<size_t>(session.current_book)];
    if (ci < 0 || ci >= static_cast<int>(book.chapters.size())) {
        return;
    }
    Bookmark mark;
    mark.id = make_mark_id();
    mark.key = book.key;
    mark.ci = ci;
    mark.pi = pi;
    mark.ch = ch;
    mark.page = page;
    mark.scroll = scroll;
    mark.label = book.chapters[static_cast<size_t>(ci)].title.empty()
                     ? std::string(kBookmarkLabel)
                     : book.chapters[static_cast<size_t>(ci)].title;
    if (pi >= 0 && pi < static_cast<int>(book.chapters[static_cast<size_t>(ci)].paragraphs.size())) {
        const std::string& text =
            book.chapters[static_cast<size_t>(ci)].paragraphs[static_cast<size_t>(pi)];
        const size_t from = static_cast<size_t>(std::max(0, ch));
        mark.snip = text.substr(from, std::min<size_t>(42, text.size() - std::min(from, text.size())));
    } else if (scroll > 0) {
        mark.snip = "scroll " + std::to_string(scroll) + "px";
    } else if (page > 0) {
        mark.snip = "page " + std::to_string(page + 1);
    }
    mark.t = now_unix();
    session.marks.push_back(std::move(mark));
    Progress& prog = session.progress[book.key];
    prog.chapter = ci;
    prog.para = pi;
    prog.ch = ch;
    prog.page = page;
    prog.scroll = scroll;
    prog.opened_at = now_unix();
    save_progress(session.progress_path, session.progress);
    persist_marks(session);
    render_marks(session);
}

void remove_bookmark(Session& session, std::string_view id) {
    session.marks.erase(std::remove_if(session.marks.begin(), session.marks.end(),
                                       [&](const Bookmark& m) { return m.id == id; }),
                        session.marks.end());
    persist_marks(session);
    render_marks(session);
}

void toggle_bookmark(Session& session) {
    if (session.current_book < 0 || session.current_chapter < 0 ||
        static_cast<size_t>(session.current_book) >= session.books.size()) {
        return;
    }
    const Book& book = session.books[static_cast<size_t>(session.current_book)];
    Bookmark probe;
    probe.key = book.key;
    fill_bookmark_position(session, probe);
    const std::string& key = book.key;
    for (auto it = session.marks.begin(); it != session.marks.end(); ++it) {
        if (it->key == key && it->ci == probe.ci && it->pi == probe.pi && it->page == probe.page &&
            ((it->scroll > probe.scroll) ? (it->scroll - probe.scroll) : (probe.scroll - it->scroll)) < 48) {
            session.marks.erase(it);
            toast(session, kMarkRemoved);
            persist_marks(session);
            render_marks(session);
            capture_reading_progress(session);
            return;
        }
    }
    add_bookmark(session, probe.ci, probe.pi, probe.ch, probe.page, probe.scroll);
    toast(session, kMarkAdded);
}

}  // namespace reader