#include "session.h"

#include "rows.h"
#include "textutil.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace writer {

void Session::publish_today() {
    if (app == nullptr) {
        return;
    }
    const std::string day = today();
    int chars = 0;
    for (const Doc& doc : store_.docs()) {
        if (doc.folder || (doc.edited != day && doc.created != day)) {
            continue;
        }
        chars += utf8_len(doc.body);
    }
    const int pct = std::min(100, chars * 100 / kGoalChars);
    const int step = pct / 10;
    app->document().set("today-info", "text", "今天 " + std::to_string(chars) + " 字");
    app->document().set("goal-pct", "text", std::to_string(pct) + "%");
    for (int i = 0; i <= 10; ++i) {
        const std::string name = "w" + std::to_string(i);
        if (i == step) {
            app->document().add_class("goal-fill", name);
        } else {
            app->document().remove_class("goal-fill", name);
        }
    }
}

void Session::publish_outline() {
    if (app == nullptr) {
        return;
    }
    const Doc* doc = store_.find(active_);
    if (doc == nullptr || doc->folder) {
        app->document().set("outline-body", "text", " ");
        return;
    }
    std::string listed;
    std::size_t at = 0;
    while (at <= doc->body.size()) {
        const std::size_t next = doc->body.find('\n', at);
        const std::string line = doc->body.substr(at, next == std::string::npos ? std::string::npos : next - at);
        int level = 1;
        std::string title;
        if (heading_line(line, level, title)) {
            listed.append(static_cast<std::size_t>(level - 1) * 2, ' ');
            listed += title;
            listed += "\n";
        }
        if (next == std::string::npos) {
            break;
        }
        at = next + 1;
    }
    if (listed.empty()) {
        listed = " ";
    }
    app->document().set("outline-body", "text", listed);
}

void Session::apply_text_size() {
    if (app == nullptr) {
        return;
    }
    for (int i = 0; i <= 8; ++i) {
        const std::string name = "tx" + std::to_string(i);
        if (i == text_) {
            app->document().add_class("app", name);
        } else {
            app->document().remove_class("app", name);
        }
    }
}

void Session::publish_tree() {
    if (app == nullptr) {
        return;
    }
    std::vector<std::pair<const Doc*, int>> order;
    const auto collect = [&](auto&& self, const std::string& parent, int depth) -> void {
        for (const Doc& doc : store_.docs()) {
            if (doc.parent != parent) {
                continue;
            }
            order.emplace_back(&doc, depth);
            self(self, doc.id, depth + 1);
        }
    };
    collect(collect, "", 0);
    FieldRows model;
    for (const auto& item : order) {
        const Doc* doc = item.first;
        const int depth = std::min(item.second, 6);
        FieldRows::Row row;
        model.put(row, "id", doc->id);
        model.put(row, "name", doc->name);
        model.put(row, "kind", doc->folder ? "folder" : "piece");
        model.put(row, "depth", "d" + std::to_string(depth));
        model.put(row, "on", doc->id == active_ ? "is-on" : "");
        model.add(std::move(row));
    }
    app->document().set_model("tree", model);
}

void Session::publish_doc() {
    if (app == nullptr) {
        return;
    }
    const Doc* doc = store_.find(active_);
    app->document().set("doc-tools", "visible", doc == nullptr ? "false" : "true");
    if (doc == nullptr) {
        app->document().set("doc-title", "text", "未选择");
        app->document().set("rename-name", "value", "");
        app->document().set("editor", "value", "");
        app->document().set("editor", "disabled", "false");
        app->document().set("reader", "text", " ");
        return;
    }
    app->document().set("doc-title", "text", doc->name);
    app->document().set("rename-name", "value", doc->name);
    if (doc->folder) {
        app->document().set("editor", "value", "");
        app->document().set("editor", "disabled", "true");
        app->document().set("reader", "text", "文件夹「" + doc->name + "」");
        return;
    }
    const std::string shown = doc->body.empty() ? " " : doc->body;
    app->document().set("reader", "text", shown);
    if (!read_) {
        app->document().set("editor", "disabled", "false");
        app->document().set("editor", "value", doc->body);
    }
}

void Session::apply_mode() {
    if (app == nullptr) {
        return;
    }
    const Doc* doc = store_.find(active_);
    if (read_) {
        app->document().set("editor", "visible", "false");
        app->document().set("editor", "disabled", "true");
        app->document().set("reader", "visible", "true");
        app->document().add_class("mode-read", "is-on");
        app->document().remove_class("mode-edit", "is-on");
    } else {
        app->document().set("reader", "visible", "false");
        app->document().set("editor", "visible", "true");
        app->document().set("editor", "disabled", (doc != nullptr && doc->folder) ? "true" : "false");
        app->document().add_class("mode-edit", "is-on");
        app->document().remove_class("mode-read", "is-on");
    }
    publish_doc();
}

void Session::publish_calendar() {
    if (app == nullptr) {
        return;
    }
    const Ymd now = today_ymd();
    app->document().set("cal-title", "text", std::to_string(now.year) + "年" + std::to_string(now.month) + "月");
    const int lead = monday_index(now.year, now.month, 1);
    const int count = days_in_month(now.year, now.month);
    FieldRows model;
    for (int i = 0; i < lead; ++i) {
        FieldRows::Row row;
        model.put(row, "label", " ");
        model.put(row, "mark", "pad");
        model.add(std::move(row));
    }
    for (int day = 1; day <= count; ++day) {
        const std::string date = ymd_text(Ymd{now.year, now.month, day});
        std::string names;
        for (const Doc& doc : store_.docs()) {
            if (doc.folder) {
                continue;
            }
            if (doc.created != date && doc.edited != date) {
                continue;
            }
            if (!names.empty()) {
                names += " ";
            }
            names += doc.name;
        }
        FieldRows::Row row;
        model.put(row, "label", names.empty() ? std::to_string(day) : std::to_string(day) + " " + names);
        model.put(row, "mark", names.empty() ? "" : "has");
        model.add(std::move(row));
    }
    app->document().set_model("cal-grid", model);
}

void Session::publish_history() {
    if (app == nullptr) {
        return;
    }
    const Doc* doc = store_.find(active_);
    FieldRows model;
    if (doc != nullptr) {
        for (const Snap& snap : doc->history) {
            FieldRows::Row row;
            model.put(row, "id", snap.id);
            model.put(row, "label", snap.label.empty() ? snap.id : snap.label);
            model.add(std::move(row));
        }
    }
    app->document().set_model("history-list", model);
    if (doc == nullptr || doc->folder) {
        app->document().set("history-note", "text", "打开一篇文稿后可以回看保存过的版本。");
    } else if (doc->history.empty()) {
        app->document().set("history-note", "text", "还没有更早的版本。再保存一次修改就会留下快照。");
    } else {
        app->document().set("history-note", "text", "点一条快照查看当时的正文。编辑区仍是当前文字。");
    }
}

void Session::set_view(const std::string& view) {
    view_ = make_desk_view(view);
    if (app == nullptr) {
        return;
    }
    view_->apply_chrome(app->document());
    if (view_->paints_calendar()) {
        publish_calendar();
    }
    if (view_->paints_history()) {
        publish_history();
    }
}

void Session::open_doc(const std::string& id) {
    active_ = id;
    publish_tree();
    apply_mode();
    if (view_->id() == std::string("history")) {
        publish_history();
    }
    if (view_->id() == std::string("calendar")) {
        publish_calendar();
    }
}

}  // namespace writer
