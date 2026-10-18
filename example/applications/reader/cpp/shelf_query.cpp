#include "session_detail.h"

#include <cctype>
#include <string>

namespace reader {
namespace {

void fold_ascii(std::string& text) {
    for (char& c : text) {
        const auto u = static_cast<unsigned char>(c);
        if (u >= 'A' && u <= 'Z') {
            c = static_cast<char>(u - 'A' + 'a');
        }
    }
}

std::string trim_copy(std::string text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return text.substr(begin, end - begin);
}

}  // namespace

ShelfQuery ShelfQuery::read(zui::App& app) {
    ShelfQuery query;
    query.needle_ = trim_copy(app.document().get("shelf_query", "value"));
    fold_ascii(query.needle_);
    return query;
}

void ShelfQueryWatch::arm(Session& session) {
    if (armed_ || session.app == nullptr) {
        return;
    }
    armed_ = true;
    seen_ = ShelfQuery::read(*session.app).needle();
    zui::App* live = session.app;
    live->document().set_interval([this, &session] {
        session.app->document().defer([this, &session] { stage(session); });
    }, 0);
}

void ShelfQueryWatch::stage(Session& session) {
    session.app->document().defer([this, &session] { apply(session); });
}

void ShelfQueryWatch::apply(Session& session) {
    const std::string needle = ShelfQuery::read(*session.app).needle();
    if (needle == seen_) {
        return;
    }
    seen_ = needle;
    fill_shelf(session);
}

bool ShelfQuery::matches(const Book& book) const {
    if (needle_.empty()) {
        return true;
    }
    std::string blob = book.title;
    blob.push_back(' ');
    blob += book.author;
    fold_ascii(blob);
    return blob.find(needle_) != std::string::npos;
}

}  // namespace reader
