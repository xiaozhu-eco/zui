#include "session.h"

#include "native_file.h"

#include <cstdlib>
#include <ctime>
#include <system_error>

namespace album {
namespace {

std::string lower_ext(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    for (char& c : ext) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return ext;
}

}  // namespace

void Session::on_photo(const zui::Event& event) {
    light_.open(std::string(event.data("id")));
    render_light();
}

void Session::close_light() {
    light_.clear();
    app->document().set("lightbox", "visible", "false");
}

void Session::on_light_close(const zui::Event&) { close_light(); }

/// With 信息 closed, the photo is the whole page and a click anywhere on it leaves. The two
/// paging arrows and the panel's own controls declare their own `onclick`, and the engine fires
/// only the innermost owner, so their clicks never reach here; a press that panned does not
/// close either (`on_stage_*`), and while the panel is up its controls keep the clicks.
void Session::on_light_back(const zui::Event&) {
    if (light_.dragged() || info_) {
        return;
    }
    close_light();
}

void Session::step(int delta) {
    const std::vector<Photo*> rows = store_.visible(current_);
    if (!light_.step(rows, delta)) {
        return;
    }
    render_light();
}

void Session::on_prev(const zui::Event&) { step(-1); }
void Session::on_next(const zui::Event&) { step(1); }

void Session::set_zoom(int next) {
    light_.set_zoom(next);
    const Photo* photo = store_.find_photo(light_.photo_id());
    if (photo != nullptr) {
        light_.paint(*app, *photo, dir);
    }
}

void Session::on_zoom_in(const zui::Event&) { set_zoom(light_.zoom() + 1); }
void Session::on_zoom_out(const zui::Event&) { set_zoom(light_.zoom() - 1); }

void Session::on_stage_down(const zui::Event& event) {
    const zui::Pointer* pointer = event.is_pointer();
    if (pointer == nullptr || light_.zoom() <= 0 || !is_visible("lightbox")) {
        return;
    }
    if (!pointer->button.empty() && pointer->button != "left") {
        return;
    }
    light_.pointer_down(pointer->x, pointer->y, scroll_of("scroll_left"), scroll_of("scroll_top"));
}

void Session::on_stage_move(const zui::Event& event) {
    const zui::Pointer* pointer = event.is_pointer();
    if (pointer == nullptr) {
        return;
    }
    light_.pointer_move(*app, pointer->x, pointer->y);
}

void Session::on_stage_up(const zui::Event&) { light_.pointer_up(); }

void Session::on_wheel(zui::Event& event) {
    const zui::Wheel* wheel = event.is_wheel();
    if (wheel == nullptr || (!wheel->ctrl && !wheel->meta) || !is_visible("lightbox")) {
        return;
    }
    event.prevent_default();
    set_zoom(light_.zoom() + (wheel->delta_y < 0 ? 1 : -1));
}

void Session::on_note(const zui::Event& event) {
    Photo* photo = store_.find_photo(light_.photo_id());
    if (photo == nullptr) {
        return;
    }
    photo->note = std::string(event.value());
    persist();
}

void Session::on_note_key(zui::Event& event) {
    const zui::Key* key = event.is_key();
    if (key == nullptr || key->composing || key->key != "Escape") {
        return;
    }
    if (is_visible("confirm")) {
        on_confirm_cancel(event);
        return;
    }
    on_light_close(event);
}

int Session::import_files(const std::vector<std::filesystem::path>& paths) {
    std::error_code error;
    std::filesystem::create_directories(dir / "album-photos", error);
    int added = 0;
    std::string skipped;
    for (const std::filesystem::path& path : paths) {
        const std::string ext = lower_ext(path);
        if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".svg") {
            skipped = path.filename().u8string() + " 不是 PNG/JPEG/SVG，相册只导入这几种";
            continue;
        }
        std::error_code size_error;
        (void)std::filesystem::file_size(path, size_error);
        if (size_error) {
            skipped = "读不到 " + path.filename().u8string();
            continue;
        }
        const std::string id = store_.uid("p");
        const std::filesystem::path dest = dir / "album-photos" / (id + ext);
        std::filesystem::copy_file(path, dest, std::filesystem::copy_options::overwrite_existing, error);
        if (error) {
            skipped = "没能复制 " + path.filename().u8string();
            continue;
        }
        Photo photo;
        photo.id = id;
        photo.album_id = (current_ == "all" || store_.find_album(current_) == nullptr) ? "loose" : current_;
        photo.name = path.stem().u8string();
        photo.file = "album-photos/" + id + ext;
        photo.added = std::time(nullptr) * 1000LL + added;
        store_.photos().push_back(std::move(photo));
        ++added;
    }
    if (added) {
        persist();
    }
    if (!skipped.empty()) {
        set_status(skipped);
    }
    render();
    return added;
}

void Session::on_import(zui::Event& event) {
    // The label also points at `<input type=file>`. Cancelling the click keeps that control from
    // opening a second dialog that only remembers filenames.
    event.prevent_default();
    std::vector<std::filesystem::path> picked;
    if (!pick_images(*app, picked)) {
#ifndef _WIN32
        set_status("这个系统没有文件对话框");
#endif
        return;
    }
    import_files(picked);
}

}  // namespace album
