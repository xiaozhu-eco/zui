#include "session.h"

#include "text.h"

namespace contacts {

std::filesystem::path Session::data_file() const { return dir / "native-data" / "contacts.json"; }

void Session::set_status(const std::string& text) { app->document().set("status", "text", text); }

bool Session::persist() {
    if (!store_.save(data_file())) {
        set_status("没能写入本地记录");
        return false;
    }
    set_status("");
    return true;
}

void Session::load() {
    const LoadResult kind = store_.load(data_file());
    if (kind == LoadResult::Missing) {
        dirty_ = false;
    } else if (kind == LoadResult::Corrupt) {
        set_status("本地记录读不出来，先显示示例");
    }
}

void Session::open_confirm(std::unique_ptr<ConfirmCommand> command) {
    confirm_ = std::move(command);
    app->document().set("confirm-body", "text", confirm_->body());
    app->document().set("confirm-ok", "text", confirm_->ok_label());
    app->document().set("confirm", "visible", "true");
}

void Session::on_query(const zui::Event& event) {
    query_ = trim(event.value());
    show_list();
}

void Session::on_star(const zui::Event& event) {
    star_only_ = event.value() == "true";
    show_list();
}

void Session::on_filter(const zui::Event& event) {
    filter_ = std::string(event.data("id"));
    if (filter_.empty()) {
        filter_ = "all";
    }
    render();
}

void Session::on_person(const zui::Event& event) {
    selected_ = std::string(event.data("id"));
    show_list();
}

void Session::on_delete(const zui::Event&) {
    const Person* person = store_.find(selected_);
    if (person == nullptr) {
        return;
    }
    open_confirm(make_delete_person(person->name));
}

void Session::on_restore(const zui::Event&) { open_confirm(make_restore_samples()); }

void Session::on_confirm_cancel(const zui::Event&) {
    confirm_.reset();
    app->document().set("confirm", "visible", "false");
}

void Session::on_confirm_ok(const zui::Event&) {
    std::unique_ptr<ConfirmCommand> command = std::move(confirm_);
    app->document().set("confirm", "visible", "false");
    if (!command) {
        return;
    }
    ConfirmContext ctx{store_, selected_, dirty_};
    if (!command->execute(ctx)) {
        return;
    }
    persist();
    render();
}

void Session::on_accel(zui::Event& event) {
    const zui::Key* key = event.is_key();
    if (key == nullptr || key->composing || key->key != "Escape") {
        return;
    }
    if (app->document().get("confirm", "visible") == "true") {
        on_confirm_cancel(event);
        return;
    }
    if (app->document().get("editor", "visible") == "true") {
        on_editor_cancel(event);
    }
}

void Session::bind() {
    zui::Document& doc = app->document();
    doc.bind("on_query", [this](const zui::Event& event) { on_query(event); });
    doc.bind("on_star", [this](const zui::Event& event) { on_star(event); });
    doc.bind("on_filter", [this](const zui::Event& event) { on_filter(event); });
    doc.bind("on_person", [this](const zui::Event& event) { on_person(event); });
    doc.bind("on_create", [this](const zui::Event& event) { on_create(event); });
    doc.bind("on_restore", [this](const zui::Event& event) { on_restore(event); });
    doc.bind("on_edit", [this](const zui::Event& event) { on_edit(event); });
    doc.bind("on_delete", [this](const zui::Event& event) { on_delete(event); });
    doc.bind("on_open_uri", [this](zui::Event& event) { on_open_uri(event); });
    doc.bind("on_group", [this](const zui::Event& event) { on_group(event); });
    doc.bind("on_save", [this](const zui::Event& event) { on_save(event); });
    doc.bind("on_editor_cancel", [this](const zui::Event& event) { on_editor_cancel(event); });
    doc.bind("on_confirm_ok", [this](const zui::Event& event) { on_confirm_ok(event); });
    doc.bind("on_confirm_cancel", [this](const zui::Event& event) { on_confirm_cancel(event); });
    doc.bind("on_accel", [this](zui::Event& event) { on_accel(event); });
}

void Session::show() {
    app->window().set_title("小竹通讯录");
    app->window().set_window_size(1000, 700);
    // Engine <select> popup works (HOST PR-15); hide the chip-row fallback used before.
    app->document().set("group-picks", "visible", "false");
    load();
    if (!store_.people().empty()) {
        selected_ = store_.people().front().id;
    }
    render();
    detail_name_.arm(*app);
}

}  // namespace contacts
