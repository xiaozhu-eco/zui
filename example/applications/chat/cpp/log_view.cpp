#include "session.h"

#include "clock.h"
#include "rows.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace chat {

void Session::update_jump() {
    if (active_.empty()) {
        app->document().set("jump", "visible", "false");
        return;
    }
    const float top = std::strtof(app->document().get("log", "scroll_top").c_str(), nullptr);
    // The maximum is the one `set_scroll` clamps to: rounded scrollHeight minus the padding-box
    // view. A process-wide high-water mark stays above a shorter layout and leaves 「回到底部」
    // showing while the thread is already pinned.
    float max_top = 0.f;
    if (const auto box = app->document().measure("log")) {
        max_top = std::max(0.f, std::round(box->scroll_height) - std::round(box->client_height));
    }
    const bool near_bottom = top >= max_top - 64.f;
    app->document().set("jump", "visible", near_bottom ? "false" : "true");
}

void Session::stick_bottom() {
    app->document().set("log", "scroll_top", "100000000");
    app->document().set("jump", "visible", "false");
}

void Session::render_log() {
    const Convo* convo = store_.find_convo(active_);
    const bool show = convo != nullptr;
    app->document().set("thread-head", "visible", show ? "true" : "false");
    app->document().set("composer", "visible", show ? "true" : "false");
    app->document().set("thread-empty", "visible", show ? "false" : "true");
    if (!show) {
        app->document().set("jump", "visible", "false");
    }
    app->document().set("thread-name", "text", show ? convo->name : "");

    std::vector<const Message*> rows;
    for (const Message& message : store_.messages()) {
        if (message.conv_id == active_) {
            rows.push_back(&message);
        }
    }
    std::sort(rows.begin(), rows.end(), [](const Message* a, const Message* b) { return a->ts < b->ts; });

    FieldRows model;
    std::string prev;
    for (const Message* message : rows) {
        const std::string day = day_label(message->ts);
        if (day != prev) {
            FieldRows::Row sep;
            model.put(sep, "kind", "day-sep");
            model.put(sep, "sep", day);
            model.put(sep, "text", "");
            model.put(sep, "time", "");
            model.add(std::move(sep));
            prev = day;
        }
        FieldRows::Row row;
        model.put(row, "kind", message->mine ? "msg mine" : "msg");
        model.put(row, "sep", "");
        model.put(row, "text", message->text);
        model.put(row, "time", clock_label(message->ts));
        model.add(std::move(row));
    }
    app->document().set_model("log", model);
    if (show) {
        stick_bottom();
    }
}

}  // namespace chat
