#include "session.h"

#include "dates.h"
#include "layout.h"
#include "rows.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace calendar {

void Session::render() {
    const std::tm today = today_tm();
    const std::string today_iso = iso_from(today.tm_year + 1900, today.tm_mon + 1, today.tm_mday);
    zui::Document& doc = app->document();
    mode_->apply_chrome(doc);
    doc.set("title", "text", mode_->title(year_, month_, selected_));

    const std::string week_start = start_of_week(selected_);
    const char* heads[] = {"一", "二", "三", "四", "五", "六", "日"};
    FieldRows month;
    for (const char* head : heads) {
        FieldRows::Row row;
        month.put(row, "kind", "mh");
        month.put(row, "date", "");
        month.put(row, "num", head);
        month.put(row, "today", "");
        for (const char* key : {"c0", "c1", "c2"}) {
            month.put(row, key, "");
        }
        for (const char* key : {"i0", "i1", "i2"}) {
            month.put(row, key, "");
        }
        for (const char* key : {"t0", "t1", "t2"}) {
            month.put(row, key, "");
        }
        for (const char* key : {"b0", "b1", "b2", "bm"}) {
            month.put(row, key, "is-blank");
        }
        month.put(row, "more", "");
        month.add(std::move(row));
    }
    const int lead = weekday_monday(iso_from(year_, month_ + 1, 1));
    const std::string grid_start = add_days(iso_from(year_, month_ + 1, 1), -lead);
    for (int i = 0; i < 42; ++i) {
        const std::string iso = add_days(grid_start, i);
        int y = 0, m = 0, d = 0;
        parse_iso(iso, y, m, d);
        const auto rows = store_.on_date(iso);
        std::string kind = "cell";
        if (m != month_ + 1) {
            kind += " is-out";
        }
        if (iso == selected_) {
            kind += " is-on";
        }
        FieldRows::Row row;
        month.put(row, "kind", kind);
        month.put(row, "date", iso);
        month.put(row, "num", std::to_string(d));
        month.put(row, "today", iso == today_iso ? "is-today" : "");
        for (int c = 0; c < 3; ++c) {
            const bool on = c < static_cast<int>(rows.size());
            month.put(row, "c" + std::to_string(c), on ? rows[static_cast<std::size_t>(c)]->color : "");
            // The chip carries `onclick="on_event"` + `data-id`, so it is its own hit target: the
            // click walk stops there instead of reaching the cell's `on_day` (EVENT.md section 5),
            // and `Event::data("id")` names the event. The browser's view.js builds a button per
            // chip for the same reason. A blank slot keeps an empty id but is `display: none`.
            month.put(row, "i" + std::to_string(c), on ? rows[static_cast<std::size_t>(c)]->id : "");
            month.put(row, "t" + std::to_string(c), on ? rows[static_cast<std::size_t>(c)]->title : "");
            month.put(row, "b" + std::to_string(c), on ? "" : "is-blank");
        }
        if (rows.size() > 3) {
            month.put(row, "bm", "");
            month.put(row, "more", "还有 " + std::to_string(rows.size() - 3) + " 项");
        } else {
            month.put(row, "bm", "is-blank");
            month.put(row, "more", "");
        }
        month.add(std::move(row));
    }
    doc.set_model("month-view", month);

    FieldRows hours;
    for (int hour = 0; hour < 24; ++hour) {
        FieldRows::Row row;
        char label[8];
        std::snprintf(label, sizeof(label), "%02d:00", hour);
        hours.put(row, "label", label);
        hours.put(row, "top", std::to_string(hour * 44));
        hours.put(row, "first", hour == 0 ? "hour-first" : "");
        hours.add(std::move(row));
    }
    doc.set_model("hours", hours);

    for (int i = 0; i < 7; ++i) {
        const std::string iso = add_days(week_start, i);
        int y = 0, m = 0, d = 0;
        parse_iso(iso, y, m, d);
        const std::string id = std::to_string(i);
        doc.set("dow-name-" + id, "text", heads[i]);
        doc.set("dow-num-" + id, "text", std::to_string(d));
        doc.set("dow-" + id, "value", iso);
        doc.set("day-" + id, "value", iso);
        if (iso == selected_) {
            doc.add_class("dow-" + id, "is-on");
            doc.add_class("day-" + id, "is-on");
        } else {
            doc.remove_class("dow-" + id, "is-on");
            doc.remove_class("day-" + id, "is-on");
        }
        if (iso == today_iso) {
            doc.add_class("dow-num-" + id, "is-today");
        } else {
            doc.remove_class("dow-num-" + id, "is-today");
        }
        FieldRows blocks;
        for (const Placed& block : layout_day(store_.on_date(iso))) {
            FieldRows::Row row;
            char style[180];
            std::snprintf(style, sizeof(style),
                          "top:%.0fpx;height:%.0fpx;left:calc(%.4f%% + 2px);width:calc(%.4f%% - 4px)",
                          block.top, std::max(20.0, block.height - 2.0), block.left, block.width);
            blocks.put(row, "id", block.ev->id);
            blocks.put(row, "color", block.ev->color.empty() ? "green" : block.ev->color);
            blocks.put(row, "style", style);
            blocks.put(row, "title", block.ev->title);
            blocks.put(row, "when", block.ev->start + "–" + block.ev->end);
            blocks.add(std::move(row));
        }
        doc.set_model("day-" + id, blocks);
    }
    if (mode_->is_week() && !week_scrolled_) {
        doc.set("week-scroll", "scroll_top", "352");
        week_scrolled_ = true;
    }

    int sy = 0, sm = 0, sd = 0;
    parse_iso(selected_, sy, sm, sd);
    const char* week_names[] = {"周一", "周二", "周三", "周四", "周五", "周六", "周日"};
    doc.set("agenda-title", "text",
            std::to_string(sm) + "月" + std::to_string(sd) + "日 " + week_names[weekday_monday(selected_)]);
    const auto agenda_rows = store_.on_date(selected_);
    doc.set("agenda-empty", "visible", agenda_rows.empty() ? "true" : "false");
    FieldRows agenda;
    for (const Ev* ev : agenda_rows) {
        FieldRows::Row row;
        agenda.put(row, "id", ev->id);
        agenda.put(row, "color", ev->color.empty() ? "green" : ev->color);
        agenda.put(row, "time", ev->start + "–" + ev->end);
        agenda.put(row, "title", ev->title);
        agenda.put(row, "place", ev->place);
        agenda.add(std::move(row));
    }
    doc.set_model("agenda-list", agenda);
}

}  // namespace calendar
