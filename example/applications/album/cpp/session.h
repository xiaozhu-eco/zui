#ifndef ALBUM_SESSION_H
#define ALBUM_SESSION_H

#include "commands.h"
#include "lightbox.h"
#include "store.h"

#include <zui/app.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace album {

/// One running 小竹相册. It owns the library, the lightbox, and the pending confirm command,
/// and it is the only object `Document::bind` closes over. It is not movable: those callbacks keep
/// its address.
class Session {
public:
    Session() = default;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;

    zui::App* app = nullptr;
    std::filesystem::path dir;

    void bind();
    void show();
    /// Copies PNG/JPEG/SVG files into this library. Returns how many were added.
    int import_files(const std::vector<std::filesystem::path>& paths);

    AlbumStore& store() { return store_; }
    const std::string& current() const { return current_; }

private:
    ConfirmContext context();
    std::filesystem::path data_file() const;
    void set_status(const std::string& text);
    bool persist();
    void load();
    bool is_visible(const char* id) const;
    float scroll_of(const char* field) const;

    void render();
    void render_light();
    void render_picks();
    void open_confirm(std::unique_ptr<ConfirmCommand> command);
    void step(int delta);
    void set_zoom(int next);
    void close_light();
    void toggle_info();
    void assign_photo_album(const std::string& next);

    void on_album(const zui::Event& event);
    void on_album_name(const zui::Event& event);
    void on_photo(const zui::Event& event);
    void on_light_back(const zui::Event&);
    void on_light_close(const zui::Event&);
    void on_prev(const zui::Event&);
    void on_next(const zui::Event&);
    void on_zoom_in(const zui::Event&);
    void on_zoom_out(const zui::Event&);
    void on_wheel(zui::Event& event);
    void on_stage_down(const zui::Event& event);
    void on_stage_move(const zui::Event& event);
    void on_stage_up(const zui::Event&);
    void on_light_album(const zui::Event& event);
    void on_note(const zui::Event& event);
    void on_note_key(zui::Event& event);
    void on_pick_album(const zui::Event& event);
    void on_accel(zui::Event& event);
    void on_add_album(const zui::Event&);
    void on_album_cancel(const zui::Event&);
    void on_album_save(const zui::Event&);
    void on_delete_album(const zui::Event&);
    void on_light_delete(const zui::Event&);
    void on_restore(const zui::Event&);
    void on_import(zui::Event&);
    void on_confirm_ok(const zui::Event&);
    void on_confirm_cancel(const zui::Event&);

    AlbumStore store_;
    Lightbox light_;
    std::string current_ = "all";
    bool info_ = false;
    std::unique_ptr<ConfirmCommand> confirm_;
};

inline void bind_actions(Session& session) { session.bind(); }
inline void show(Session& session) { session.show(); }

/// Loads the album page (argv is an image only when it should be imported) and imports that image.
bool start(Session& session, int argc, char** argv);

}  // namespace album

#endif
