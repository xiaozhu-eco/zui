#include "editor.h"

#include <string>

#include "document.h"
#include "util.h"

namespace zmd {

Editor::Editor(zui::Document& page, Document& doc) : page_(page), doc_(doc) {}

void Editor::bind_surface(std::string_view id) {
    surface_ = std::string(id);
}

std::string Editor::text() const {
    return page_.get(surface_, "value");
}

std::size_t Editor::caret() const {
    std::string raw = page_.get(surface_, "caret");
    if (raw.empty()) {
        return 0;
    }
    try {
        return static_cast<std::size_t>(std::stoul(raw));
    } catch (...) {
        return 0;
    }
}

std::string Editor::selection() const {
    return page_.get(surface_, "selection");
}

Editor::SelectionRange Editor::selection_range() const {
    const std::string start = page_.get(surface_, "selection_start");
    const std::string end = page_.get(surface_, "selection_end");
    if (start.empty() || end.empty()) {
        return {};
    }
    return SelectionRange{static_cast<std::size_t>(std::strtoull(start.c_str(), nullptr, 10)),
                          static_cast<std::size_t>(std::strtoull(end.c_str(), nullptr, 10)), true};
}

void Editor::set_selection(std::size_t start, std::size_t end) {
    page_.set(surface_, "select", std::to_string(start) + "," + std::to_string(end));
}

void Editor::insert(std::string_view text) {
    page_.set(surface_, "insert", text);
}

void Editor::commit(std::string next, std::size_t caret) {
    doc_.set_source(next, true);
    page_.set(surface_, "value", next);
    set_selection(caret, caret);
}

std::string_view Editor::current_line(const std::string& text, std::size_t offset) const {
    std::size_t begin = util::line_start(text, offset);
    std::size_t end = util::line_end(text, offset);
    return std::string_view(text).substr(begin, end - begin);
}

void Editor::replace_line(std::string_view next) {
    const std::string source = this->text();
    const std::size_t offset = caret();
    const std::size_t begin = util::line_start(source, offset);
    const std::size_t end = util::line_end(source, offset);
    std::string out;
    out.reserve(source.size() + next.size());
    out.append(source, 0, begin);
    out.append(next);
    out.append(source, end, std::string::npos);
    commit(std::move(out), begin + next.size());
}

bool Editor::apply_block_rule() {
    const std::string source = this->text();
    const std::size_t offset = caret();
    std::string_view line = current_line(source, offset);
    if (line.find('\r') != std::string_view::npos) {
        line.remove_suffix(1);
    }
    // Only the text before the caret counts: typing " " after "#" is the trigger.
    std::string before(line.substr(0, offset - util::line_start(source, offset)));
    std::size_t hashes = 0;
    while (hashes < before.size() && before[hashes] == '#') {
        ++hashes;
    }
    if (hashes > 0 && hashes <= 6 && before.size() == hashes + 1 && before[hashes] == ' ') {
        replace_line(line.substr(hashes + 1));
        return true;
    }
    if (before == "- " || before == "* " || before == "+ ") {
        replace_line(std::string("• ") + std::string(line.substr(2)));
        return true;
    }
    if (before == "> ") {
        replace_line(std::string("❯ ") + std::string(line.substr(2)));
        return true;
    }
    return false;
}

void Editor::set_heading(int depth) {
    const std::string source = this->text();
    const std::size_t offset = caret();
    std::string_view line = current_line(source, offset);
    std::size_t hashes = 0;
    while (hashes < line.size() && line[hashes] == '#') {
        ++hashes;
    }
    std::string_view body = util::trim(line.substr(hashes));
    if (hashes == static_cast<std::size_t>(depth)) {
        replace_line(body);  // same level again clears it
        return;
    }
    replace_line(std::string(depth, '#') + " " + std::string(body));
}

void Editor::toggle_line_prefix(std::string_view prefix) {
    const std::string source = this->text();
    std::string_view line = current_line(source, caret());
    if (util::starts_with(line, prefix)) {
        replace_line(line.substr(prefix.size()));
        return;
    }
    replace_line(std::string(prefix) + std::string(line));
}

void Editor::toggle_task() {
    const std::string source = this->text();
    std::string_view line = current_line(source, caret());
    if (util::starts_with(line, "- [ ] ") || util::starts_with(line, "- [x] ")) {
        replace_line(line.substr(6));
        return;
    }
    if (util::starts_with(line, "- ")) {
        replace_line("- [ ] " + std::string(line.substr(2)));
        return;
    }
    replace_line("- [ ] " + std::string(line));
}

void Editor::insert_line(std::string_view text) {
    const std::string body = this->text();
    const std::size_t offset = caret();
    const std::size_t end = util::line_end(body, offset);
    std::string out = body;
    out.insert(end, "\n" + std::string(text));
    commit(std::move(out), end + 1 + text.size());
}

void Editor::wrap(std::string_view before, std::string_view after,
                  std::string_view placeholder) {
    const std::string selected = page_.get(surface_, "selection");
    if (selected.empty()) {
        insert(std::string(before) + std::string(placeholder) + std::string(after));
        return;
    }
    // The engine replaces the selection with `insert`, so the wrap is one call.
    insert(std::string(before) + selected + std::string(after));
}

}  // namespace zmd

