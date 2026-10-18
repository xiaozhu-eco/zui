#include "session.h"

namespace writer {

Session::Session() : view_(make_desk_view("tree")) {}

void Session::set_status(const std::string& text) {
    if (app != nullptr) {
        app->document().set("status", "text", text.empty() ? " " : text);
    }
}

void Session::set_storage_status(const std::string& text) {
    if (app != nullptr) {
        app->document().set("storage-status", "text", text.empty() ? " " : text);
    }
}

void Session::flash_autosave() {
    if (app == nullptr) {
        return;
    }
    app->document().set("autosave", "visible", "true");
    if (autosave_ != 0) {
        app->document().clear_timer(autosave_);
    }
    autosave_ = app->document().set_timeout([this] {
        autosave_ = 0;
        if (app != nullptr) {
            app->document().set("autosave", "visible", "false");
        }
    }, 700);
}

void Session::on_settings() {
    if (app == nullptr) {
        return;
    }
    const bool open = app->document().get("storage-bar", "visible") == "true";
    app->document().set("storage-bar", "visible", open ? "false" : "true");
}

void Session::on_text_wheel(zui::Event& event) {
    const zui::Wheel* wheel = event.is_wheel();
    if (wheel == nullptr || !wheel->ctrl) {
        return;
    }
    event.prevent_default();
    if (wheel->delta_y < 0 && text_ < 8) {
        ++text_;
    } else if (wheel->delta_y > 0 && text_ > 0) {
        --text_;
    }
    apply_text_size();
}

void Session::on_tree_toggle() {
    if (app == nullptr) {
        return;
    }
    const std::string classes = app->document().get("app", "class");
    if (classes.find("show-tree") != std::string::npos) {
        app->document().remove_class("app", "show-tree");
    } else {
        app->document().add_class("app", "show-tree");
    }
}

void Session::bind() {
    if (app == nullptr) {
        return;
    }
    zui::Document& doc = app->document();
    doc.bind("on_new_piece", [this](zui::Event&) { on_new_piece(); });
    doc.bind("on_new_folder", [this](zui::Event&) { on_new_folder(); });
    doc.bind("on_open", [this](zui::Event& event) {
        // publish_tree replaces the clicked twig. Cancelling the default keeps the
        // click from using that node after the model rebuild.
        event.prevent_default();
        on_open(event);
    });
    doc.bind("on_rename", [this](zui::Event&) { on_rename(); });
    doc.bind("on_editor", [this](zui::Event& event) { on_editor(event); });
    doc.bind("on_mode_read", [this](zui::Event&) { on_mode_read(); });
    doc.bind("on_mode_edit", [this](zui::Event&) { on_mode_edit(); });
    doc.bind("on_delete", [this](zui::Event&) { on_delete(); });
    doc.bind("on_save", [this](zui::Event&) { on_save(); });
    doc.bind("on_storage", [this](zui::Event&) { on_storage(); });
    doc.bind("on_view_tree", [this](zui::Event&) { set_view("tree"); });
    doc.bind("on_view_calendar", [this](zui::Event&) { set_view("calendar"); });
    doc.bind("on_view_history", [this](zui::Event&) { set_view("history"); });
    doc.bind("on_snap", [this](zui::Event& event) { on_snap(event); });
    doc.bind("on_tree_toggle", [this](zui::Event&) { on_tree_toggle(); });
    doc.bind("on_settings", [this](zui::Event&) { on_settings(); });
    doc.bind("on_text_wheel", [this](zui::Event& event) { on_text_wheel(event); });
}

void Session::show() {
    if (autosave_ != 0 && app != nullptr) {
        app->document().clear_timer(autosave_);
    }
    autosave_ = 0;
    store_.clear();
    active_.clear();
    read_ = false;
    view_ = make_desk_view("tree");
    text_ = 3;
    follow_remembered_dir();
    store_.load(dir);
    present_loaded();
    if (app != nullptr) {
        app->document().set("storage-path", "value", dir.string());
        app->document().set("history-body", "text", " ");
    }
    set_storage_status(dir.empty() ? " " : dir.string());
    set_status(store_.docs().empty() ? "新建一篇文稿开始写。" : "已载入文稿");
    apply_text_size();
    if (app != nullptr) {
        app->window().set_title("小竹写作");
    }
}

}  // namespace writer
