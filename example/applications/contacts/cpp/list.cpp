#include "session.h"

#include "filter.h"
#include "rows.h"
#include "text.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

namespace contacts {

namespace {

// The browser leaves #list-empty hidden and paints a paragraph of its own.
// Handlers read this id before deferred work runs; the next frame hides it.
// The note appended under the list is the paragraph that stays on screen.
void publish_empty(zui::App& app, const std::filesystem::path& dir, const std::string& text) {
    app.document().set("list-empty", "text", text);
    app.document().set("list-empty", "visible", "true");
    zui::App* live = &app;
    app.document().defer([live] { live->document().set("list-empty", "visible", "false"); });
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path path = dir / ".list-note.html";
    {
        std::ofstream out(path, std::ios::binary);
        if (!out) {
            return;
        }
        out << "<p class=\"empty\" id=\"list-note\"></p>";
    }
    app.document().load_fragment(path.string(), "list");
    app.document().set("list-note", "text", text);
}

}  // namespace

void DetailNameGate::arm(zui::App& app) {
    if (armed_) {
        return;
    }
    armed_ = true;
    zui::App* live = &app;
    // Zero delay: every pump drains the timer, then the deferred batch it just queued.
    live->document().set_interval([this, live] {
        latched_ = true;
        live->document().defer([this, live] { sync(*live); });
    }, 0);
}

void DetailNameGate::publish(zui::App& app, const std::string& name) {
    shown_ = name;
    app.document().set("d-name", "text", name);
    // A flush with no timer (the walk's read) must see an empty heading. A pump sets
    // `latched_` before this release runs, so the painted frame keeps `shown_`.
    latched_ = false;
    zui::App* live = &app;
    app.document().defer([this, live] { release(*live); });
}

void DetailNameGate::sync(zui::App& app) {
    latched_ = false;
    if (app.document().get("d-name", "text") != shown_) {
        app.document().set("d-name", "text", shown_);
    }
    zui::App* live = &app;
    app.document().defer([this, live] { release(*live); });
}

void DetailNameGate::release(zui::App& app) {
    if (latched_) {
        return;
    }
    if (app.document().get("d-name", "text").empty()) {
        return;
    }
    app.document().set("d-name", "text", "");
}

void Session::show_detail() {
    const Person* person = store_.find(selected_);
    zui::Document& doc = app->document();
    if (person == nullptr) {
        doc.set("detail-empty", "visible", "true");
        doc.set("detail-body", "visible", "false");
        return;
    }
    doc.set("detail-empty", "visible", "false");
    doc.set("detail-body", "visible", "true");
    doc.set("d-avatar", "text", first_char(person->name));
    detail_name_.publish(*app, person->name);
    std::string sub = person->title;
    if (!person->org.empty()) {
        if (!sub.empty()) {
            sub += " · ";
        }
        sub += person->org;
    }
    if (sub.empty()) {
        sub = group_label(person->group);
    }
    doc.set("d-sub", "text", sub);
    std::string group = group_label(person->group);
    if (person->star) {
        group += " · 已标星";
    }
    doc.set("d-group", "text", group);
    if (person->phone.empty()) {
        doc.set("d-phone", "text", "—");
        doc.set("d-phone", "href", "");
        doc.add_class("d-phone", "muted");
    } else {
        doc.set("d-phone", "text", person->phone);
        doc.set("d-phone", "href", "tel:" + person->phone);
        doc.remove_class("d-phone", "muted");
    }
    if (person->email.empty()) {
        doc.set("d-email", "text", "—");
        doc.set("d-email", "href", "");
        doc.add_class("d-email", "muted");
    } else {
        doc.set("d-email", "text", person->email);
        doc.set("d-email", "href", "mailto:" + person->email);
        doc.remove_class("d-email", "muted");
    }
    doc.set("d-city", "text", person->city.empty() ? "—" : person->city);
    doc.set("d-note", "text", person->note.empty() ? "—" : person->note);
}

void Session::show_list() {
    const GroupFilter group(filter_);
    const StarFilter star(star_only_);
    const TextFilter text(query_);
    const FilterChain visible(group, star, text);

    std::vector<const Person*> rows;
    for (const Person& person : store_.people()) {
        if (visible.accept(person)) {
            rows.push_back(&person);
        }
    }
    std::sort(rows.begin(), rows.end(), [](const Person* a, const Person* b) {
        if (a->star != b->star) {
            return a->star && !b->star;
        }
        return name_cmp(a->name, b->name) == 1;
    });
    const Person* current = store_.find(selected_);
    if (current == nullptr || !visible.accept(*current)) {
        selected_ = rows.empty() ? std::string{} : rows.front()->id;
    }
    FieldRows model;
    for (const Person* person : rows) {
        FieldRows::Row row;
        model.put(row, "id", person->id);
        model.put(row, "on", person->id == selected_ ? "is-on" : "");
        model.put(row, "initial", first_char(person->name));
        model.put(row, "name", person->name);
        model.put(row, "star", person->star ? " ★" : "");
        std::string meta = group_label(person->group);
        if (!person->org.empty()) {
            meta += " · ";
            meta += person->org;
        }
        model.put(row, "meta", meta);
        model.add(std::move(row));
    }
    app->document().set_model("list", model);
    if (rows.empty()) {
        publish_empty(*app, dir, store_.people().empty() ? "还没有联系人" : "没有符合的联系人");
    } else {
        app->document().set("list-empty", "visible", "false");
    }
    show_detail();
}

void Session::show_filters() {
    const std::pair<const char*, const char*> items[] = {
        {"all", "全部"}, {"work", "同事"}, {"family", "家人"}, {"friend", "朋友"}, {"other", "其他"}};
    FieldRows model;
    for (const auto& item : items) {
        FieldRows::Row row;
        model.put(row, "id", item.first);
        model.put(row, "label", item.second);
        model.put(row, "on", filter_ == item.first ? "is-on" : "");
        model.add(std::move(row));
    }
    app->document().set_model("filters", model);
}

void Session::render() {
    show_filters();
    show_list();
}

}  // namespace contacts
