#include "session.h"

#include "dates.h"

#include <cstdio>

namespace calendar {

void Session::mark_color(const std::string& color) {
    color_ = color;
    const char* ids[] = {"green", "clay", "sea", "gold"};
    for (const char* id : ids) {
        app->document().set(std::string("color-") + id, "checked", color == id ? "true" : "false");
    }
}

void Session::show_editor(const Ev* ev, const std::string& date, const std::string& start, const std::string& end) {
    editing_ = ev ? ev->id : "";
    zui::Document& doc = app->document();
    doc.set("editor-title", "text", ev ? "编辑日程" : "新建日程");
    doc.set("editor-error", "text", "");
    doc.set("f-title", "value", ev ? ev->title : "");
    doc.set("f-date", "value", ev ? ev->date : date);
    doc.set("f-start", "value", ev ? ev->start : start);
    doc.set("f-end", "value", ev ? ev->end : end);
    doc.set("f-place", "value", ev ? ev->place : "");
    mark_color(ev ? ev->color : "green");
    doc.set("editor-remove", "visible", ev ? "true" : "false");
    doc.set("editor", "visible", "true");
}

void Session::on_day_new(const zui::Event& event) {
    if (event.value().empty()) {
        return;
    }
    // Week columns are 24 stripes of 44px (board.css). The click's y is already
    // CSS px in the same space as measure()'s border box, so the hour is the
    // stripe under the pointer. Keyboard activation has no pointer and stays at 09:00.
    int hour = 9;
    if (const zui::Pointer* pointer = event.is_pointer()) {
        const std::string id(event.source());
        const auto box = app->document().measure(id);
        if (!id.empty() && box && box->laid) {
            const float local = pointer->y - box->border.y;
            int stripe = local < 0.f ? 0 : static_cast<int>(local / 44.f);
            if (stripe > 23) {
                stripe = 23;
            }
            hour = stripe;
        }
    }
    char start[8];
    char end[8];
    std::snprintf(start, sizeof(start), "%02d:00", hour);
    if (hour >= 23) {
        std::snprintf(end, sizeof(end), "23:59");
    } else {
        std::snprintf(end, sizeof(end), "%02d:00", hour + 1);
    }
    selected_ = std::string(event.value());
    render();
    show_editor(nullptr, selected_, start, end);
}

void Session::on_event(const zui::Event& event) {
    const Ev* ev = store_.find(std::string(event.data("id")));
    if (ev == nullptr) {
        return;
    }
    selected_ = ev->date;
    render();
    show_editor(ev, ev->date, ev->start, ev->end);
}

void Session::on_agenda(const zui::Event& event) { on_event(event); }

void Session::on_color(const zui::Event& event) {
    // The radio's value attribute is the colour name. The id prefix stays as a
    // fallback for a control that has no value.
    const std::string value(event.value());
    if (value == "green" || value == "clay" || value == "sea" || value == "gold") {
        mark_color(value);
        return;
    }
    const std::string source(event.source());
    if (source.rfind("color-", 0) == 0) {
        mark_color(source.substr(6));
    }
}

void Session::on_create(const zui::Event&) { show_editor(nullptr, selected_, "09:00", "10:00"); }

void Session::on_editor_cancel(const zui::Event&) {
    app->document().set("editor", "visible", "false");
    editing_.clear();
}

void Session::on_save(const zui::Event&) {
    zui::Document& doc = app->document();
    const std::string title = trim(doc.get("f-title", "value"));
    const std::string date = trim(doc.get("f-date", "value"));
    const std::string start = trim(doc.get("f-start", "value"));
    const std::string end = trim(doc.get("f-end", "value"));
    if (title.empty()) {
        doc.set("editor-error", "text", "请填写标题");
        return;
    }
    int y = 0, m = 0, d = 0;
    if (!parse_iso(date, y, m, d)) {
        doc.set("editor-error", "text", "日期写成 YYYY-MM-DD");
        return;
    }
    const int start_min = minutes_of(start);
    const int end_min = minutes_of(end);
    if (start_min < 0 || end_min < 0) {
        doc.set("editor-error", "text", "时间写成 HH:MM");
        return;
    }
    if (end_min <= start_min) {
        doc.set("editor-error", "text", "结束要晚于开始");
        return;
    }
    Ev next;
    next.id = editing_.empty() ? store_.uid() : editing_;
    next.title = title;
    next.date = iso_from(y, m, d);
    char sb[8], eb[8];
    std::snprintf(sb, sizeof(sb), "%02d:%02d", start_min / 60, start_min % 60);
    std::snprintf(eb, sizeof(eb), "%02d:%02d", end_min / 60, end_min % 60);
    next.start = sb;
    next.end = eb;
    next.place = trim(doc.get("f-place", "value"));
    next.color = color_.empty() ? "green" : color_;
    bool replaced = false;
    for (Ev& ev : store_.events()) {
        if (ev.id == editing_) {
            ev = next;
            replaced = true;
        }
    }
    if (!replaced) {
        store_.events().push_back(next);
    }
    selected_ = next.date;
    year_ = y;
    month_ = m - 1;
    doc.set("editor", "visible", "false");
    persist();
    render();
}

void Session::on_editor_remove(const zui::Event&) {
    const Ev* ev = store_.find(editing_);
    if (ev == nullptr) {
        return;
    }
    open_confirm(make_remove_event(ev->title));
}

}  // namespace calendar
