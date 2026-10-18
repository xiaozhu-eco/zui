#ifndef SHARE_SESSION_H
#define SHARE_SESSION_H

#include "browser.h"
#include "gesture.h"
#include "net.h"
#include "util.h"

#include <zui/app.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace share {

/// Which section is showing. The value maps to the `screen-<name>` class on `#app`.
enum class Screen { Name, Browse, Pick, Wait, Fail, Transfer, Done, Settings };

/// The class token `#app` carries while @p screen is showing.
const char* screen_class(Screen screen);

/// One running 小竹分享: the config, the file browser, and the network service, wired to the
/// document's `bind` names. The network runs on its own threads; `poll()` (a main-loop interval)
/// is the only place that reads it and writes the DOM, so every `App` call stays on one thread.
class Session {
public:
    Session(zui::App& app);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    void bind();
    void show();
    /// Stops the network threads. Safe to call more than once.
    void stop();

    zui::App& app() { return app_; }
    Config& config() { return config_; }
    FileBrowser& browser() { return browser_; }
    const std::vector<Peer>& peers() const { return peers_; }
    const std::string& error() const { return error_; }

    /// Switches to @p screen and repaints it.
    void go(Screen screen);
    /// Shows or hides the incoming-share overlay (the `incoming` class on `#app`).
    void show_incoming(bool on);
    /// Reads the network state and moves the UI along. Main-loop interval only.
    void poll();

    void render_browse();
    void render_files();
    void render_selection();
    void render_pick();
    void render_wait();
    void render_transfer();
    void render_done();
    void render_fail(const XferView& xfer);
    void render_settings();
    void render_error();
    void render_peers();

    /// Writes `config.txt`. False leaves `error()` set.
    bool persist();
    /// Sets @p message as the app-level error line (pass an empty string to clear).
    void set_error(std::string message);

private:
    const Peer* find_peer(std::string_view id) const;
    XferView xfer() const;

    void on_name(const zui::Event& event);
    void on_start();
    void on_up();
    void on_refresh();
    void on_path(const zui::Event& event);
    void on_row(const zui::Event& event);
    void on_share();
    bool try_offer(const Peer& peer);  // expand -> net_offer -> retry_peer_ / error / go(Wait)
    void on_peer(const zui::Event& event);
    void on_pick(const zui::Event& event);
    void on_accept();
    void on_reject();
    void on_save_name();
    void on_save_dir();
    void on_pick_save_dir();
    void apply_save_dir(std::string next);
    void on_request_storage();
    void refresh_storage_ui();
    void on_back();
    void on_fail_retry();
    void on_done_open();
    void clear_retry_peer();
    bool retry_available() const;  // retry_peer_ valid && browser_.selected_count() > 0
    void on_accel(zui::Event& event);
    void on_gesture_down(const zui::Event& event);
    void on_gesture_files_down(const zui::Event& event);
    void on_gesture_incoming_down(const zui::Event& event);
    void on_gesture_up(zui::Event& event);
    void on_gesture_cancel(zui::Event& event);
    void dispatch_gesture(GestureAction action, zui::Event& event);

    void toast(std::string_view title, std::string_view body);
    void maybe_notify_incoming(const IncomingOffer& offer);
    void maybe_notify_xfer_terminal(const XferView& view);

    void set_text(std::string_view id, const std::string& text);
    void set_visible(std::string_view id, bool on);

    zui::App& app_;
    Config config_;
    FileBrowser browser_;
    std::vector<Peer> peers_;
    Screen screen_ = Screen::Name;
    bool incoming_on_ = false;
    XferPhase phase_ = XferPhase::Idle;
    std::uint64_t notified_offer_id_ = 0;  // 0 = none; dedupe incoming notify across polls
    std::string error_;
    Peer retry_peer_{};  // id==0 means none; after successful offer, full snapshot
    /// What the toolbar text field was last written with, so typing is not clobbered.
    std::string path_shown_;
    zui::TimerId poll_timer_ = 0;
    GestureTracker gesture_;
};

}  // namespace share

#endif  // SHARE_SESSION_H
