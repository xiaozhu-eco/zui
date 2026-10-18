#ifndef WRITER_SESSION_H
#define WRITER_SESSION_H

#include "desk.h"
#include "store.h"

#include <zui/app.h>

#include <filesystem>
#include <memory>
#include <string>

namespace writer {

class Session {
public:
    Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;

    zui::App* app = nullptr;
    // Active library directory. `show` may replace this with the folder the
    // user last chose, which is recorded under `home`.
    std::filesystem::path dir;
    // Default data directory (`App::user_data_dir` in the program). The
    // remembered storage path is a file here, so a later process can open it.
    std::filesystem::path home;

    void bind();
    void show();

private:
    static constexpr int kGoalChars = 1000;

    void set_status(const std::string& text);
    void set_storage_status(const std::string& text);
    void publish_today();
    void publish_outline();
    void apply_text_size();
    void flash_autosave();
    std::string parent_for_new() const;
    void publish_tree();
    void publish_doc();
    void apply_mode();
    void publish_calendar();
    void publish_history();
    void set_view(const std::string& view);
    void open_doc(const std::string& id);
    void on_new_piece();
    void on_new_folder();
    void on_open(const zui::Event& event);
    void on_rename();
    void on_editor(const zui::Event& event);
    void on_mode_read();
    void on_mode_edit();
    void on_delete();
    void on_save();
    bool page_missing(const char* id) const;
    void mount_pages();
    void present_loaded();
    void remember_dir(const std::filesystem::path& next);
    void follow_remembered_dir();
    void on_storage();
    void on_snap(const zui::Event& event);
    void on_settings();
    void on_text_wheel(zui::Event& event);
    void on_tree_toggle();

    DocStore store_;
    std::unique_ptr<DeskView> view_;
    std::string active_;
    bool read_ = false;
    int text_ = 3;
    zui::TimerId autosave_ = 0;
};

inline void bind_actions(Session& session) { session.bind(); }
inline void show(Session& session) { session.show(); }

}  // namespace writer

#endif
