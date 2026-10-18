#include "session.h"

#include "textutil.h"

#include <algorithm>
#include <functional>

namespace writer {

std::string Session::parent_for_new() const {
    const Doc* doc = store_.find(active_);
    if (doc == nullptr) {
        return {};
    }
    if (doc->folder) {
        return doc->id;
    }
    return doc->parent;
}

void Session::on_new_piece() {
    Doc doc;
    doc.id = store_.fresh_id();
    doc.parent = parent_for_new();
    doc.name = "未命名文稿 " + doc.id;
    doc.folder = false;
    doc.created = today();
    doc.edited = doc.created;
    store_.docs().push_back(std::move(doc));
    open_doc(store_.docs().back().id);
    store_.save(dir);
    set_status("已新建文稿");
}

void Session::on_new_folder() {
    Doc doc;
    doc.id = store_.fresh_id();
    doc.parent = parent_for_new();
    doc.name = "新建文件夹 " + doc.id;
    doc.folder = true;
    doc.created = today();
    doc.edited = doc.created;
    store_.docs().push_back(std::move(doc));
    open_doc(store_.docs().back().id);
    store_.save(dir);
    set_status("已新建文件夹");
}

void Session::on_open(const zui::Event& event) {
    std::string id(event.data("id"));
    if (id.empty()) {
        const std::string source(event.source());
        const std::string prefix = "node-";
        if (source.compare(0, prefix.size(), prefix) == 0) {
            id = source.substr(prefix.size());
        }
    }
    if (store_.find(id) == nullptr) {
        return;
    }
    open_doc(id);
    const Doc* doc = store_.find(id);
    if (doc != nullptr && !doc->folder) {
        set_status("已打开「" + doc->name + "」");
    }
    publish_today();
    publish_outline();
}

void Session::on_rename() {
    if (app == nullptr) {
        return;
    }
    Doc* doc = store_.find(active_);
    if (doc == nullptr) {
        set_status("先选择一篇文稿或文件夹");
        return;
    }
    const std::string name = trim(app->document().get("rename-name", "value"));
    if (name.empty()) {
        set_status("名称不能为空");
        return;
    }
    doc->name = name;
    doc->edited = today();
    publish_tree();
    publish_doc();
    store_.save(dir);
    set_status("已重命名");
}

void Session::on_editor(const zui::Event& event) {
    if (read_) {
        return;
    }
    Doc* doc = store_.find(active_);
    if (doc != nullptr && doc->folder) {
        return;
    }
    if (doc == nullptr) {
        on_new_piece();
        doc = store_.find(active_);
    }
    if (doc == nullptr || doc->folder) {
        return;
    }
    doc->body = std::string(event.value());
    doc->edited = today();
    if (app != nullptr) {
        app->document().set("editor", "value", doc->body);
        app->document().set("reader", "text", doc->body.empty() ? " " : doc->body);
    }
    publish_today();
    publish_outline();
}

void Session::on_mode_read() {
    read_ = true;
    Doc* doc = store_.find(active_);
    if (doc != nullptr && !doc->folder && app != nullptr) {
        doc->body = app->document().get("editor", "value");
    }
    apply_mode();
}

void Session::on_mode_edit() {
    read_ = false;
    apply_mode();
}

void Session::on_delete() {
    if (active_.empty()) {
        set_status("没有可删除的条目");
        return;
    }
    std::vector<std::string> drop;
    std::function<void(const std::string&)> walk = [&](const std::string& id) {
        drop.push_back(id);
        for (const Doc& doc : store_.docs()) {
            if (doc.parent == id) {
                walk(doc.id);
            }
        }
    };
    walk(active_);
    for (const std::string& id : drop) {
        const auto it = std::find_if(store_.docs().begin(), store_.docs().end(),
                                     [&](const Doc& doc) { return doc.id == id; });
        if (it == store_.docs().end()) {
            continue;
        }
        store_.erase_files(dir, *it);
        store_.docs().erase(it);
    }
    active_.clear();
    if (!store_.docs().empty()) {
        active_ = store_.docs().front().id;
    }
    open_doc(active_);
    store_.save(dir);
    set_status("已删除");
    publish_today();
    publish_outline();
}

void Session::on_save() {
    Doc* doc = store_.find(active_);
    if (doc == nullptr || doc->folder) {
        set_status("文件夹不用保存正文");
        return;
    }
    if (app != nullptr && !read_) {
        doc->body = app->document().get("editor", "value");
    }
    if (doc->saved_body != doc->body && !doc->saved_body.empty()) {
        Snap snap;
        snap.id = store_.fresh_snap_id();
        snap.text = doc->saved_body;
        snap.label = doc->edited + " " + snap.id;
        doc->history.push_back(std::move(snap));
    }
    doc->saved_body = doc->body;
    doc->edited = today();
    store_.save(dir);
    if (app != nullptr) {
        app->document().set("reader", "text", doc->body.empty() ? " " : doc->body);
    }
    if (std::string(view_->id()) == "history") {
        publish_history();
    }
    if (std::string(view_->id()) == "calendar") {
        publish_calendar();
    }
    set_status("已保存");
    publish_today();
    publish_outline();
    flash_autosave();
}

void Session::on_snap(const zui::Event& event) {
    std::string id(event.data("id"));
    if (id.empty()) {
        const std::string source(event.source());
        const std::string prefix = "snap-";
        if (source.compare(0, prefix.size(), prefix) == 0) {
            id = source.substr(prefix.size());
        }
    }
    const Doc* doc = store_.find(active_);
    if (doc == nullptr || app == nullptr) {
        return;
    }
    for (const Snap& snap : doc->history) {
        if (snap.id != id) {
            continue;
        }
        app->document().set("history-body", "text", snap.text.empty() ? " " : snap.text);
        app->document().set("history-note", "text", "这是较早的版本，和当前正文不同。");
        return;
    }
}

}  // namespace writer
