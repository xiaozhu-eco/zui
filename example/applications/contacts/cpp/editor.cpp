#include "session.h"

#include "text.h"

namespace contacts {

void Session::mark_group(const std::string& group) {
    group_ = group;
    // The shipped page hides #group-picks and leaves these chips off. The browser's
    // `on_group` is a no-op; the reachable control is <select id="f-group">. Painting
    // `is-on` here would disagree with that recording.
    const char* ids[] = {"work", "family", "friend", "other"};
    for (const char* id : ids) {
        app->document().remove_class(std::string("g-") + id, "is-on");
    }
}

void Session::open_editor(const Person* person) {
    editing_ = person ? person->id : "";
    zui::Document& doc = app->document();
    doc.set("editor-title", "text", person ? "编辑联系人" : "新建联系人");
    doc.set("editor-error", "text", "");
    doc.set("f-name", "value", person ? person->name : "");
    doc.set("f-org", "value", person ? person->org : "");
    doc.set("f-title", "value", person ? person->title : "");
    doc.set("f-phone", "value", person ? person->phone : "");
    doc.set("f-email", "value", person ? person->email : "");
    doc.set("f-city", "value", person ? person->city : "");
    doc.set("f-note", "value", person ? person->note : "");
    doc.set("f-star", "checked", person && person->star ? "true" : "false");
    const std::string group = person ? person->group : "other";
    doc.set("f-group", "value", group);
    mark_group(group);
    doc.set("editor", "visible", "true");
}

void Session::close_editor() {
    app->document().set("editor", "visible", "false");
    editing_.clear();
}

void Session::on_group(const zui::Event& event) {
    const std::string group(event.data("id"));
    if (group == "work" || group == "family" || group == "friend" || group == "other") {
        mark_group(group);
    }
}

void Session::on_editor_cancel(const zui::Event&) { close_editor(); }

void Session::on_save(const zui::Event&) {
    zui::Document& doc = app->document();
    const std::string name = trim(doc.get("f-name", "value"));
    const std::string email = trim(doc.get("f-email", "value"));
    if (name.empty()) {
        doc.set("editor-error", "text", "请填写姓名");
        return;
    }
    if (!email.empty() && !valid_email(email)) {
        doc.set("editor-error", "text", "邮箱格式不对");
        return;
    }
    Person next;
    next.id = editing_.empty() ? store_.uid() : editing_;
    next.name = name;
    next.org = trim(doc.get("f-org", "value"));
    next.title = trim(doc.get("f-title", "value"));
    next.phone = trim(doc.get("f-phone", "value"));
    next.email = email;
    next.city = trim(doc.get("f-city", "value"));
    next.note = trim(doc.get("f-note", "value"));
    const std::string group = doc.get("f-group", "value");
    next.group = group.empty() ? (group_.empty() ? "other" : group_) : group;
    next.star = doc.get("f-star", "checked") == "true";
    if (Person* existing = store_.find(editing_)) {
        *existing = next;
    } else {
        store_.people().insert(store_.people().begin(), next);
    }
    selected_ = next.id;
    dirty_ = true;
    persist();
    close_editor();
    render();
}

void Session::on_create(const zui::Event&) { open_editor(nullptr); }

void Session::on_edit(const zui::Event&) { open_editor(store_.find(selected_)); }

void Session::on_open_uri(zui::Event& event) {
    const std::string id(event.source());
    if (id.empty()) {
        return;
    }
    const std::string uri = app->document().get(id, "href");
    if (uri.empty()) {
        return;
    }
    app->host().open_uri(uri);
    // The engine also follows `<a href>` unless the click is cancelled.
    event.prevent_default();
}

}  // namespace contacts
