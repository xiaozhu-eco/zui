#ifndef CALENDAR_SESSION_H
#define CALENDAR_SESSION_H

#include "commands.h"
#include "span.h"
#include "store.h"

#include <zui/app.h>

#include <filesystem>
#include <memory>
#include <string>

namespace calendar {

class Session {
public:
    Session();
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
    void place_on(const std::string& iso);
    void render();
    void shift(int step);
    void mark_color(const std::string& color);
    void show_editor(const Ev* ev, const std::string& date, const std::string& start, const std::string& end);
    void open_confirm(std::unique_ptr<ConfirmCommand> command);

    void on_today(const zui::Event&);
    void on_prev(const zui::Event&);
    void on_next(const zui::Event&);
    void on_month(const zui::Event&);
    void on_week(const zui::Event&);
    void on_day(const zui::Event& event);
    void on_week_day(const zui::Event& event);
    void on_day_new(const zui::Event& event);
    void on_event(const zui::Event& event);
    void on_agenda(const zui::Event& event);
    void on_color(const zui::Event& event);
    void on_create(const zui::Event&);
    void on_editor_cancel(const zui::Event&);
    void on_save(const zui::Event&);
    void on_editor_remove(const zui::Event&);
    void on_restore(const zui::Event&);
    void on_confirm_cancel(const zui::Event&);
    void on_confirm_ok(const zui::Event&);
    void on_accel(zui::Event& event);

    EventStore store_;
    std::unique_ptr<SpanMode> mode_;
    std::string selected_;
    int year_ = 2026;
    int month_ = 0;
    bool week_scrolled_ = false;
    std::string editing_;
    std::string color_ = "green";
    std::unique_ptr<ConfirmCommand> confirm_;
};

inline void bind_actions(Session& session) { session.bind(); }
inline void show(Session& session) { session.show(); }

}  // namespace calendar

#endif
