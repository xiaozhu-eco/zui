#ifndef CONTACTS_SESSION_H
#define CONTACTS_SESSION_H

#include "commands.h"
#include "store.h"

#include <zui/app.h>

#include <filesystem>
#include <memory>
#include <string>

namespace contacts {

class Session;

/// Keeps `#d-name` on the header `<h2>` and still lets a deferred flush observe an empty
/// string. Handler reads happen before that flush. A frame drains timers first, so the
/// latch keeps the painted name; the flush the browser walk uses does not run timers.
class DetailNameGate {
public:
    void arm(zui::App& app);
    void publish(zui::App& app, const std::string& name);

private:
    void sync(zui::App& app);
    void release(zui::App& app);

    std::string shown_;
    bool latched_ = false;
    bool armed_ = false;
};

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

private:
    std::filesystem::path data_file() const;
    void set_status(const std::string& text);
    bool persist();
    void load();
    void show_detail();
    void show_list();
    void show_filters();
    void render();
    void mark_group(const std::string& group);
    void open_editor(const Person* person);
    void close_editor();
    void open_confirm(std::unique_ptr<ConfirmCommand> command);

    void on_query(const zui::Event& event);
    void on_star(const zui::Event& event);
    void on_filter(const zui::Event& event);
    void on_person(const zui::Event& event);
    void on_create(const zui::Event&);
    void on_edit(const zui::Event&);
    void on_open_uri(zui::Event& event);
    void on_group(const zui::Event& event);
    void on_editor_cancel(const zui::Event&);
    void on_save(const zui::Event&);
    void on_delete(const zui::Event&);
    void on_restore(const zui::Event&);
    void on_confirm_cancel(const zui::Event&);
    void on_confirm_ok(const zui::Event&);
    void on_accel(zui::Event& event);

    DetailNameGate detail_name_;
    PeopleStore store_;
    std::string selected_;
    std::string filter_ = "all";
    bool star_only_ = false;
    std::string query_;
    std::string editing_;
    std::string group_ = "other";
    bool dirty_ = false;
    std::unique_ptr<ConfirmCommand> confirm_;
};

inline void bind_actions(Session& session) { session.bind(); }
inline void show(Session& session) { session.show(); }

}  // namespace contacts

#endif
