#include "session.h"

#include <zui/model.h>

#include <functional>
#include <string>
#include <utility>

namespace kanban {
namespace {

class CardRows : public zui::ListModel {
public:
    std::vector<const Card*> rows;
    std::size_t row_count() const override { return rows.size(); }
    std::string value(std::size_t row, std::string_view key) const override {
        const Card& card = *rows[row];
        if (key == "id") return card.id;
        if (key == "title") return card.title;
        if (key == "note") return card.note;
        return {};
    }
};

class BoardRows : public zui::ListModel {
public:
    explicit BoardRows(const BoardStore& store) : store_(&store) {}

    std::size_t row_count() const override { return store_->columns().size(); }
    std::string value(std::size_t row, std::string_view key) const override {
        const Column& column = store_->columns()[row];
        if (key == "id") return column.id;
        if (key == "name") return column.name;
        if (key == "count") return std::to_string(column.card_ids.size());
        return {};
    }
    void with_children(std::size_t row,
                       const std::function<void(const zui::ListModel&)>& visit) const override {
        CardRows cards;
        for (const std::string& id : store_->columns()[row].card_ids) {
            if (const Card* card = store_->find_card(id)) {
                cards.rows.push_back(card);
            }
        }
        visit(cards);
    }

private:
    const BoardStore* store_;
};

class PickRows : public zui::ListModel {
public:
    std::vector<std::vector<std::pair<std::string, std::string>>> rows;
    std::size_t row_count() const override { return rows.size(); }
    std::string value(std::size_t row, std::string_view key) const override {
        if (row >= rows.size()) return {};
        for (const auto& field : rows[row]) {
            if (field.first == key) return field.second;
        }
        return {};
    }
};

}  // namespace

Session::Session(zui::App& app, std::filesystem::path dir) : app_(app), dir_(std::move(dir)) {}

void Session::set_status(const std::string& text) { app_.document().set("status", "text", text); }

bool Session::persist() {
    const std::filesystem::path file = dir_ / "native-data" / "kanban.json";
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    if (!store_.save(file)) {
        set_status("没能写入本地记录");
        return false;
    }
    set_status("");
    return true;
}

void Session::render_board() {
    BoardRows board(store_);
    app_.document().set_model("board", board);
}

void Session::render_picks() {
    PickRows model;
    for (const Column& column : store_.columns()) {
        std::vector<std::pair<std::string, std::string>> row;
        row.emplace_back("id", column.id);
        row.emplace_back("name", column.name);
        model.rows.push_back(std::move(row));
    }
    // The closed control paints `text`. `set(value)` commits that from the matching option, and
    // no-ops when the id is already selected, so a renamed column still needs its label written.
    app_.document().set_model("f-column", model);
    const std::string& picked = editor_.picked();
    if (picked.empty()) {
        return;
    }
    app_.document().set("f-column", "value", picked);
    if (const Column* column = store_.find_column(picked)) {
        app_.document().set("f-column", "text", column->name);
    }
}

void Session::on_col_name(const zui::Event& event) {
    const std::string source(event.source());
    if (source.rfind("name-", 0) != 0) {
        return;
    }
    Column* column = store_.find_column(source.substr(5));
    const std::string next = trim(event.value());
    if (column == nullptr || next.empty()) {
        return;
    }
    column->name = next;
    persist();
}

void Session::on_add_card(const zui::Event& event) {
    // A keydown firing arrives for every key of the box; only Enter saves. A single-line <input>
    // has no default action to cancel (EVENT.md section 7), so nothing else is needed here.
    const zui::Key* key = event.is_key();
    if (key == nullptr || key->key != "Enter") {
        return;
    }
    const std::string source(event.source());
    if (source.rfind("add-", 0) != 0) {
        return;
    }
    const std::string title = trim(app_.document().get(source, "value"));
    if (title.empty()) {
        return;
    }
    if (!store_.add_card(source.substr(4), title)) {
        return;
    }
    persist();
    render_board();
}

void Session::on_add_column() {
    store_.add_column("新列");
    persist();
    render_board();
}

void Session::on_card_up(const zui::Event& event) {
    if (!store_.nudge_card(std::string(event.data("id")), -1)) {
        return;
    }
    persist();
    render_board();
}

void Session::on_card_down(const zui::Event& event) {
    if (!store_.nudge_card(std::string(event.data("id")), 1)) {
        return;
    }
    persist();
    render_board();
}

void Session::on_accel(zui::Event& event) {
    const zui::Key* key = event.is_key();
    if (key == nullptr || key->composing || key->key != "Escape") {
        return;
    }
    if (app_.document().get("confirm", "visible") == "true") {
        editor_.confirm_cancel(*this);
        return;
    }
    if (app_.document().get("editor", "visible") == "true") {
        editor_.cancel(*this);
    }
}

void Session::bind() {
    app_.document().bind("on_restore", [this](zui::Event&) { editor_.ask_restore(*this); });
    app_.document().bind("on_add_column", [this](zui::Event&) { on_add_column(); });
    app_.document().bind("on_col_name", [this](const zui::Event& event) { on_col_name(event); });
    app_.document().bind("on_col_remove", [this](const zui::Event& event) { editor_.ask_remove_column(*this, event); });
    app_.document().bind("on_card", [this](const zui::Event& event) { editor_.open(*this, std::string(event.data("id"))); });
    app_.document().bind("on_card_up", [this](const zui::Event& event) { on_card_up(event); });
    app_.document().bind("on_card_down", [this](const zui::Event& event) { on_card_down(event); });
    app_.document().bind("on_add_card", [this](const zui::Event& event) { on_add_card(event); });
    app_.document().bind("on_save", [this](zui::Event&) { editor_.save(*this); });
    app_.document().bind("on_editor_cancel", [this](zui::Event&) { editor_.cancel(*this); });
    app_.document().bind("on_editor_remove", [this](zui::Event&) { editor_.ask_remove_card(*this); });
    app_.document().bind("on_confirm_ok", [this](zui::Event&) { editor_.confirm_ok(*this); });
    app_.document().bind("on_confirm_cancel", [this](zui::Event&) { editor_.confirm_cancel(*this); });
    app_.document().bind("on_accel", [this](zui::Event& event) { on_accel(event); });
    app_.document().bind("on_drag_start", [this](zui::Event& event) { drag_.on_start(*this, event); });
    app_.document().bind("on_drag", [this](zui::Event& event) { drag_.on_move(*this, event); });
    app_.document().bind("on_drag_end", [this](zui::Event&) { drag_.on_end(*this); });
    app_.document().bind("on_drag_over", [this](zui::Event& event) { drag_.on_over(*this, event); });
    app_.document().bind("on_drop", [this](zui::Event& event) { drag_.on_drop(*this, event); });
}

void Session::show() {
    app_.window().set_title("小竹看板");
    app_.window().set_window_size(1200, 800);
    // The column control is `<select id="f-column">`. `#column-picks` is the old chip row.
    app_.document().set("column-picks", "visible", "false");
    if (store_.load(dir_ / "native-data" / "kanban.json") == LoadResult::Corrupt) {
        set_status("本地记录读不出来，先显示示例");
    }
    render_board();
}

}  // namespace kanban
