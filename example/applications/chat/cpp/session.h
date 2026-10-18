#ifndef CHAT_SESSION_H
#define CHAT_SESSION_H

#include "commands.h"
#include "store.h"

#include <zui/app.h>

#include <filesystem>
#include <memory>
#include <string>

namespace chat {

/// One running 小竹会话. Bind callbacks keep this address, so the session is not movable.
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

    void render_list(bool retarget);
    void render_log();
    void render();
    void stick_bottom();
    void update_jump();
    void send_text(std::string text);
    void open_confirm(std::unique_ptr<ConfirmCommand> command);

    void on_query(const zui::Event& event);
    void on_conv(const zui::Event& event);
    void on_create(const zui::Event&);
    void on_creator_cancel(const zui::Event&);
    void on_creator_save(const zui::Event&);
    void on_restore(const zui::Event&);
    void on_remove(const zui::Event&);
    void on_send(const zui::Event&);
    void on_draft_key(zui::Event& event);
    void on_jump(const zui::Event&);
    void on_log_scroll(const zui::Event&);
    void on_confirm_cancel(const zui::Event&);
    void on_confirm_ok(const zui::Event&);
    void on_accel(zui::Event& event);

    ChatStore store_;
    std::string active_;
    std::string query_;
    std::unique_ptr<ConfirmCommand> confirm_;
};

inline void bind_actions(Session& session) { session.bind(); }
inline void show(Session& session) { session.show(); }

}  // namespace chat

#endif
