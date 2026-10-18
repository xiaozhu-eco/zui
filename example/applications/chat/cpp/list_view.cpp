#include "session.h"

#include "clock.h"
#include "rows.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

namespace chat {

namespace {

// The browser leaves #conv-empty hidden and paints a paragraph of its own.
// Handlers read this id before deferred work runs; the next frame hides it.
// The note appended under the list is the paragraph that stays on screen.
void publish_empty(zui::App& app, const std::filesystem::path& dir, const std::string& text) {
    app.document().set("conv-empty", "text", text);
    app.document().set("conv-empty", "visible", "true");
    zui::App* live = &app;
    app.document().defer([live] { live->document().set("conv-empty", "visible", "false"); });
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path path = dir / ".conv-note.html";
    {
        std::ofstream out(path, std::ios::binary);
        if (!out) {
            return;
        }
        out << "<p class=\"conv-empty\" id=\"conv-note\"></p>";
    }
    app.document().load_fragment(path.string(), "conv-list");
    app.document().set("conv-note", "text", text);
}

}  // namespace

void Session::render_list(bool retarget) {
    const std::string needle = fold_ascii(query_);
    std::vector<const Convo*> rows;
    for (const Convo& convo : store_.convos()) {
        if (!needle.empty()) {
            const std::string name = fold_ascii(convo.name);
            const Message* last = store_.last_of(convo.id);
            const std::string preview = fold_ascii(last ? last->text : "");
            if (name.find(needle) == std::string::npos && preview.find(needle) == std::string::npos) {
                continue;
            }
        }
        rows.push_back(&convo);
    }
    std::sort(rows.begin(), rows.end(), [&](const Convo* a, const Convo* b) {
        const Message* la = store_.last_of(a->id);
        const Message* lb = store_.last_of(b->id);
        const long long ta = la ? la->ts : a->created;
        const long long tb = lb ? lb->ts : b->created;
        return ta > tb;
    });
    bool active_visible = false;
    for (const Convo* convo : rows) {
        if (convo->id == active_) {
            active_visible = true;
        }
    }
    // A search that hides the open thread leaves it open. Switching, deleting, and the first
    // paint are the places that pick a new thread.
    if (retarget && !active_visible) {
        active_ = rows.empty() ? std::string{} : rows.front()->id;
    }

    FieldRows model;
    for (const Convo* convo : rows) {
        const Message* last = store_.last_of(convo->id);
        FieldRows::Row row;
        model.put(row, "id", convo->id);
        model.put(row, "on", convo->id == active_ ? "is-on" : "");
        model.put(row, "initial", first_char(convo->name));
        model.put(row, "name", convo->name);
        model.put(row, "when",
                  last ? (day_label(last->ts) == "今天" ? clock_label(last->ts) : day_label(last->ts)) : "");
        model.put(row, "preview", last ? last->text : "还没有消息");
        model.add(std::move(row));
    }
    app->document().set_model("conv-list", model);
    if (rows.empty()) {
        publish_empty(*app, dir, store_.convos().empty() ? "还没有会话" : "没有符合的会话");
    } else {
        app->document().set("conv-empty", "visible", "false");
    }
}

}  // namespace chat
