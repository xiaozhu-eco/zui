#include "session.h"

#include "clock.h"

namespace chat {

std::filesystem::path Session::data_file() const { return dir / "native-data" / "chat.json"; }

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
    if (store_.load(data_file()) == LoadResult::Corrupt) {
        set_status("本地记录读不出来，先显示示例");
    }
}

void Session::render() {
    render_list(true);
    render_log();
}

void Session::send_text(std::string text) {
    text = trim(text);
    if (text.empty() || active_.empty()) {
        return;
    }
    store_.messages().push_back({store_.uid("m"), active_, true, text, now_ms()});
    app->document().set("draft", "value", "");
    persist();
    render();
}

void Session::open_confirm(std::unique_ptr<ConfirmCommand> command) {
    confirm_ = std::move(command);
    app->document().set("confirm-body", "text", confirm_->body());
    app->document().set("confirm-ok", "text", confirm_->ok_label());
    app->document().set("confirm", "visible", "true");
}

void Session::on_query(const zui::Event& event) {
    query_ = trim(event.value());
    // Filtering the list does not restick the open thread and does not change which thread is open.
    render_list(false);
}

void Session::on_conv(const zui::Event& event) {
    active_ = std::string(event.data("id"));
    render();
}

void Session::on_create(const zui::Event&) {
    app->document().set("creator-error", "text", "");
    app->document().set("creator-name", "value", "");
    app->document().set("creator", "visible", "true");
}

void Session::on_creator_cancel(const zui::Event&) { app->document().set("creator", "visible", "false"); }

void Session::on_creator_save(const zui::Event&) {
    const std::string name = trim(app->document().get("creator-name", "value"));
    if (name.empty()) {
        app->document().set("creator-error", "text", "请填写名字");
        return;
    }
    Convo convo;
    convo.id = store_.uid("c");
    convo.name = name;
    convo.created = now_ms();
    store_.convos().push_back(convo);
    active_ = convo.id;
    app->document().set("creator", "visible", "false");
    persist();
    render();
}

void Session::on_restore(const zui::Event&) { open_confirm(make_restore_samples()); }

void Session::on_remove(const zui::Event&) {
    const Convo* convo = store_.find_convo(active_);
    if (convo == nullptr) {
        return;
    }
    open_confirm(make_remove_convo(convo->name));
}

void Session::on_send(const zui::Event&) { send_text(app->document().get("draft", "value")); }

/// Enter sends, Shift+Enter (and the Enter that confirms an IME composition) keeps its own
/// meaning. The engine's default action for Enter in a <textarea> is the line break Chrome
/// inserts, so sending is expressed by cancelling it.
void Session::on_draft_key(zui::Event& event) {
    const zui::Key* key = event.is_key();
    if (key == nullptr || key->key != "Enter" || key->shift || key->composing) {
        return;
    }
    event.prevent_default();
    send_text(app->document().get("draft", "value"));
}

void Session::on_jump(const zui::Event&) { stick_bottom(); }

void Session::on_log_scroll(const zui::Event&) { update_jump(); }

void Session::on_confirm_cancel(const zui::Event&) {
    confirm_.reset();
    app->document().set("confirm", "visible", "false");
}

void Session::on_accel(zui::Event& event) {
    const zui::Key* key = event.is_key();
    if (key == nullptr || key->key != "Escape") {
        return;
    }
    if (app->document().get("confirm", "visible") == "true") {
        on_confirm_cancel(event);
        return;
    }
    if (app->document().get("creator", "visible") == "true") {
        app->document().set("creator", "visible", "false");
    }
}

void Session::on_confirm_ok(const zui::Event&) {
    std::unique_ptr<ConfirmCommand> command = std::move(confirm_);
    app->document().set("confirm", "visible", "false");
    if (!command) {
        return;
    }
    ChatContext ctx{store_, active_};
    command->execute(ctx);
    persist();
    render();
}

void Session::bind() {
    app->document().bind("on_query", [this](const zui::Event& event) { on_query(event); });
    app->document().bind("on_conv", [this](const zui::Event& event) { on_conv(event); });
    app->document().bind("on_create", [this](const zui::Event& event) { on_create(event); });
    app->document().bind("on_restore", [this](const zui::Event& event) { on_restore(event); });
    app->document().bind("on_remove", [this](const zui::Event& event) { on_remove(event); });
    app->document().bind("on_send", [this](const zui::Event& event) { on_send(event); });
    app->document().bind("on_draft_key", [this](zui::Event& event) { on_draft_key(event); });
    app->document().bind("on_jump", [this](const zui::Event& event) { on_jump(event); });
    app->document().bind("on_log_scroll", [this](const zui::Event& event) { on_log_scroll(event); });
    app->document().bind("on_creator_cancel", [this](const zui::Event& event) { on_creator_cancel(event); });
    app->document().bind("on_creator_save", [this](const zui::Event& event) { on_creator_save(event); });
    app->document().bind("on_confirm_ok", [this](const zui::Event& event) { on_confirm_ok(event); });
    app->document().bind("on_confirm_cancel", [this](const zui::Event& event) { on_confirm_cancel(event); });
    app->document().bind("on_accel", [this](zui::Event& event) { on_accel(event); });
}

void Session::show() {
    app->window().set_title("小竹会话");
    app->window().set_window_size(1000, 720);
    load();
    active_ = store_.convos().empty() ? std::string{} : store_.convos().front().id;
    render();
}

}  // namespace chat
