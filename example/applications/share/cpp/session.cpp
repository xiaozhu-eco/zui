#include "session.h"

#include "rows.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace share {
namespace {

/// How often the main loop looks at the network state. Close enough to the source app's
/// per-frame poll for a 60fps window, without re-stamping the DOM every frame.
constexpr int kPollMs = 200;

}  // namespace

const char* screen_class(Screen screen) {
    switch (screen) {
        case Screen::Name: return "screen-name";
        case Screen::Browse: return "screen-browse";
        case Screen::Pick: return "screen-pick";
        case Screen::Wait: return "screen-wait";
        case Screen::Fail: return "screen-fail";
        case Screen::Transfer: return "screen-transfer";
        case Screen::Done: return "screen-done";
        case Screen::Settings: return "screen-settings";
    }
    return "screen-browse";
}

Session::Session(zui::App& app) : app_(app), browser_(default_browse_dir()) {}

Session::~Session() { stop(); }

void Session::stop() {
    if (poll_timer_ != 0) {
        app_.document().clear_timer(poll_timer_);
        poll_timer_ = 0;
    }
    net_stop();
}

void Session::set_text(std::string_view id, const std::string& text) {
    app_.document().set(id, "text", text);
}

void Session::set_visible(std::string_view id, bool on) {
    app_.document().set(id, "visible", on ? "true" : "false");
}

void Session::set_error(std::string message) {
    error_ = std::move(message);
    render_error();
}

bool Session::persist() {
    save_config(config_);
    return true;
}

const Peer* Session::find_peer(std::string_view id) const {
    for (const Peer& peer : peers_) {
        if (std::to_string(peer.id) == id) {
            return &peer;
        }
    }
    return nullptr;
}

XferView Session::xfer() const { return net_xfer(); }

/* ---------------------------------------------------------------- screens */

void Session::go(Screen next) {
    if (next == screen_) {
        return;
    }
    app_.document().remove_class("app", screen_class(screen_));
    screen_ = next;
    app_.document().add_class("app", screen_class(screen_));
    switch (screen_) {
        case Screen::Browse: render_browse(); break;
        case Screen::Pick: render_pick(); break;
        case Screen::Wait: render_wait(); break;
        case Screen::Fail: render_fail(xfer()); break;
        case Screen::Transfer: render_transfer(); break;
        case Screen::Done: render_done(); break;
        case Screen::Settings: render_settings(); break;
        case Screen::Name: break;
    }
}

void Session::show_incoming(bool on) {
    if (on == incoming_on_) {
        return;
    }
    incoming_on_ = on;
    if (on) {
        app_.document().add_class("app", "incoming");
    } else {
        app_.document().remove_class("app", "incoming");
    }
}

/* ---------------------------------------------------------------- poll */


void Session::toast(std::string_view title, std::string_view body) {
    (void)app_.host().services().notify(title, body);  // OsResult ignored when !ok
}

void Session::maybe_notify_incoming(const IncomingOffer& offer) {
    if (offer.offer_id == 0 || offer.offer_id == notified_offer_id_) return;
    notified_offer_id_ = offer.offer_id;
    toast("收到文件分享", notify_incoming_body(offer));
}

void Session::maybe_notify_xfer_terminal(const XferView& view) {
    switch (view.phase) {
        case XferPhase::Done:
            toast("传输完成", notify_done_body(view));
            break;
        case XferPhase::Rejected:
        case XferPhase::Error:
            toast("传输未完成", notify_fail_body(view));
            break;
        default:
            break;
    }
}

void Session::poll() {
    peers_ = net_peers();
    render_peers();

    const XferView current = xfer();
    if (current.phase != phase_) {
        phase_ = current.phase;
        switch (current.phase) {
            case XferPhase::WaitingAccept:
                go(Screen::Wait);
                break;
            case XferPhase::Transferring:
                go(Screen::Transfer);
                break;
            case XferPhase::Done:
                go(Screen::Done);
                render_done();
                maybe_notify_xfer_terminal(current);
                break;
            case XferPhase::Rejected:
            case XferPhase::Error:
                go(Screen::Fail);
                render_fail(current);
                maybe_notify_xfer_terminal(current);
                break;
            case XferPhase::Idle:
                if (screen_ == Screen::Wait || screen_ == Screen::Transfer ||
                    screen_ == Screen::Done || screen_ == Screen::Fail) {
                    clear_retry_peer();
                    go(Screen::Browse);
                }
                break;
        }
    }
    if (screen_ == Screen::Transfer) {
        render_transfer();
    }

    // The offer sheet shows everywhere except the screens that are already a transfer.
    const std::vector<IncomingOffer> offers = net_incoming();
    const bool allowed = screen_ != Screen::Name && screen_ != Screen::Wait && screen_ != Screen::Transfer;
    if (allowed && !offers.empty()) {
        const IncomingOffer& offer = offers.front();
        set_text("inc-avatar", first_glyph(offer.from_name));
        set_text("inc-name", offer.from_name);
        const std::string meta = std::to_string(offer.nfiles) + " 个项目  ·  " + format_bytes(offer.nbytes);
        set_text("inc-line1", meta);
        set_text("inc-line2", "将保存到  " + config_.save_dir);
        show_incoming(true);
        maybe_notify_incoming(offer);
    } else {
        show_incoming(false);
        notified_offer_id_ = 0;
    }

    render_error();
}

/* ---------------------------------------------------------------- render */


void Session::show() {
    app_.window().set_title("小竹分享");
    // Wide first: the design's desktop screen (11). Narrowing past the 760px media query turns
    // the same document into the phone screens (02 / 03).
    app_.window().set_window_size(1100, 760);

    config_ = load_config();
    create_dir_all(config_.save_dir);
    app_.document().set("name-input", "value", config_.name);
    set_visible("set-dir-note", is_android());
    render_settings();
    render_browse();

    if (!config_.name.empty()) {
        if (!net_start(config_.id, config_.name, config_.save_dir)) {
            set_error("无法监听端口，检查是否已有实例在运行");
        }
        go(Screen::Browse);
    }

    poll_timer_ = app_.document().set_interval([this] { poll(); }, kPollMs);
}

}  // namespace share
