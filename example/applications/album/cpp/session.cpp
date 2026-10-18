#include "session.h"

#include <cstdlib>

namespace album {

ConfirmContext Session::context() { return {store_, light_, current_, dir}; }

std::filesystem::path Session::data_file() const { return dir / "native-data" / "album.json"; }

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

bool Session::is_visible(const char* id) const { return app->document().get(id, "visible") == "true"; }

float Session::scroll_of(const char* field) const {
    const std::string raw = app->document().get("stage", field);
    if (raw.empty()) {
        return 0.f;
    }
    char* end = nullptr;
    const float value = std::strtof(raw.c_str(), &end);
    return end == raw.c_str() ? 0.f : value;
}

void Session::open_confirm(std::unique_ptr<ConfirmCommand> command) {
    app->document().set("confirm-body", "text", command->body());
    app->document().set("confirm-ok", "text", command->ok_label());
    app->document().set("confirm", "visible", "true");
    confirm_ = std::move(command);
}

void Session::on_confirm_cancel(const zui::Event&) {
    confirm_.reset();
    app->document().set("confirm", "visible", "false");
}

void Session::on_confirm_ok(const zui::Event&) {
    std::unique_ptr<ConfirmCommand> command = std::move(confirm_);
    app->document().set("confirm", "visible", "false");
    if (command) {
        ConfirmContext ctx = context();
        command->execute(ctx);
        persist();
        render();
    }
}

void Session::on_add_album(const zui::Event&) {
    app->document().set("album-form-error", "text", "");
    app->document().set("album-form-name", "value", "");
    app->document().set("album-dialog", "visible", "true");
}

void Session::on_album_cancel(const zui::Event&) { app->document().set("album-dialog", "visible", "false"); }

void Session::on_album_save(const zui::Event&) {
    const std::string name = trim(app->document().get("album-form-name", "value"));
    if (name.empty()) {
        app->document().set("album-form-error", "text", "请填写名称");
        return;
    }
    Album album{store_.uid("a"), name};
    store_.albums().push_back(album);
    current_ = album.id;
    light_.clear();
    app->document().set("album-dialog", "visible", "false");
    persist();
    render();
}

void Session::on_delete_album(const zui::Event&) {
    const Album* album = store_.find_album(current_);
    if (album == nullptr || album->id == "loose") {
        return;
    }
    open_confirm(make_delete_album(album->name));
}

void Session::on_light_delete(const zui::Event&) {
    const Photo* photo = store_.find_photo(light_.photo_id());
    if (photo == nullptr) {
        return;
    }
    open_confirm(make_delete_photo(photo->name));
}

/// 信息: the bar, the filmstrip and the caption/album form are one disclosure over the photo.
/// It opens with the `i` key; the panel's own 信息 control closes it again.
void Session::toggle_info() {
    info_ = !info_;
    app->document().set("light-inspector", "checked", info_ ? "true" : "false");
}

void Session::on_restore(const zui::Event&) { open_confirm(make_restore_samples()); }

void Session::on_accel(zui::Event& event) {
    const zui::Key* key = event.is_key();
    if (key == nullptr || key->composing) {
        return;
    }
    if (key->key == "Escape") {
        if (is_visible("confirm")) {
            on_confirm_cancel(event);
            return;
        }
        if (is_visible("album-dialog")) {
            on_album_cancel(event);
            return;
        }
        if (is_visible("lightbox")) {
            on_light_close(event);
        }
        return;
    }
    if (!is_visible("lightbox") || is_visible("confirm") || is_visible("album-dialog")) {
        return;
    }
    if (key->alt || key->meta) {
        return;
    }
    if (!key->ctrl && !key->shift && key->key == "ArrowLeft") {
        on_prev(event);
        return;
    }
    if (!key->ctrl && !key->shift && key->key == "ArrowRight") {
        on_next(event);
        return;
    }
    if (!key->ctrl && !key->shift && (key->key == "Delete" || key->key == "Backspace")) {
        on_light_delete(event);
        return;
    }
    if (key->key == "i") {
        toggle_info();
        return;
    }
    if (key->key == "+" || key->key == "=") {
        on_zoom_in(event);
        return;
    }
    if (key->key == "-" || key->key == "_") {
        on_zoom_out(event);
    }
}

void Session::bind() {
    app->document().bind("on_restore", [this](const zui::Event& event) { on_restore(event); });
    app->document().bind("on_add_album", [this](const zui::Event& event) { on_add_album(event); });
    app->document().bind("on_import", [this](zui::Event& event) { on_import(event); });
    app->document().bind("on_album", [this](const zui::Event& event) { on_album(event); });
    app->document().bind("on_album_name", [this](const zui::Event& event) { on_album_name(event); });
    app->document().bind("on_delete_album", [this](const zui::Event& event) { on_delete_album(event); });
    app->document().bind("on_photo", [this](const zui::Event& event) { on_photo(event); });
    app->document().bind("on_light_back", [this](const zui::Event& event) { on_light_back(event); });
    app->document().bind("on_light_close", [this](const zui::Event& event) { on_light_close(event); });
    app->document().bind("on_prev", [this](const zui::Event& event) { on_prev(event); });
    app->document().bind("on_next", [this](const zui::Event& event) { on_next(event); });
    app->document().bind("on_zoom_in", [this](const zui::Event& event) { on_zoom_in(event); });
    app->document().bind("on_zoom_out", [this](const zui::Event& event) { on_zoom_out(event); });
    app->document().bind("on_wheel", [this](zui::Event& event) { on_wheel(event); });
    app->document().bind("on_stage_down", [this](const zui::Event& event) { on_stage_down(event); });
    app->document().bind("on_stage_move", [this](const zui::Event& event) { on_stage_move(event); });
    app->document().bind("on_stage_up", [this](const zui::Event& event) { on_stage_up(event); });
    app->document().bind("on_light_album", [this](const zui::Event& event) { on_light_album(event); });
    app->document().bind("on_note", [this](const zui::Event& event) { on_note(event); });
    app->document().bind("on_note_key", [this](zui::Event& event) { on_note_key(event); });
    app->document().bind("on_accel", [this](zui::Event& event) { on_accel(event); });
    app->document().bind("on_pick_album", [this](const zui::Event& event) { on_pick_album(event); });
    app->document().bind("on_light_delete", [this](const zui::Event& event) { on_light_delete(event); });
    app->document().bind("on_album_cancel", [this](const zui::Event& event) { on_album_cancel(event); });
    app->document().bind("on_album_save", [this](const zui::Event& event) { on_album_save(event); });
    app->document().bind("on_confirm_ok", [this](const zui::Event& event) { on_confirm_ok(event); });
    app->document().bind("on_confirm_cancel", [this](const zui::Event& event) { on_confirm_cancel(event); });
}

void Session::show() {
    current_ = "all";
    light_.clear();
    info_ = false;
    confirm_.reset();
    app->window().set_title("小竹相册");
    app->window().set_window_size(1100, 720);
    app->document().set("album-picks", "visible", "false");
    app->document().set("light-inspector", "checked", "false");
    load();
    render();
}

namespace {

bool image_path(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    for (char& c : ext) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".svg";
}

}  // namespace

bool start(Session& session, int argc, char** argv) {
    if (session.app == nullptr || argv == nullptr || argc < 1 || argv[0] == nullptr) {
        return false;
    }
    const std::filesystem::path exe_dir = std::filesystem::absolute(argv[0]).parent_path();
    std::filesystem::path page = exe_dir / "index.html";
    std::filesystem::path content;
    if (argc > 1 && argv[1] != nullptr && argv[1][0] != '\0') {
        const std::filesystem::path argument(argv[1]);
        if (image_path(argument)) {
            content = argument;
        } else {
            page = argument;
        }
    }
    if (session.dir.empty()) {
        session.dir = session.app->host().user_data_dir();
    }
    if (!session.app->document().load(page.string())) {
        return false;
    }
    session.bind();
    session.show();
    if (content.empty()) {
        return true;
    }
    return session.import_files({content}) > 0;
}

}  // namespace album
