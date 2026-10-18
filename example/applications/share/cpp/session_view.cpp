#include "session.h"

#include "rows.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace share {
void Session::render_peers() {
    const std::size_t count = peers_.size();
    set_text("peer-count", std::to_string(count) + " 台在线");
    set_text("peer-count-side", std::to_string(count) + " 在线");
    set_visible("peers-empty", count == 0);
    PeerChipRows chips(peers_);
    app_.document().set_model("peer-chips", chips);
    PeerCardRows cards(peers_);
    app_.document().set_model("peer-side", cards);
    if (screen_ == Screen::Pick) {
        render_pick();
    }
}

void Session::render_browse() {
    set_text("me-name", config_.name);
    render_peers();
    render_files();
}

void Session::render_files() {
    browser_.refresh();  // first render (and any directory change) reads the listing
    const std::string label = browser_.label();
    if (label != path_shown_) {
        app_.document().set("path", "value", label);
        path_shown_ = label;
    }
    FileRows rows(browser_);
    app_.document().set_model("file-list", rows);
    render_selection();
    if (!storage_access_ok(app_.host())) {
        set_error("需要存储权限才能浏览和接收文件");
    } else if (!browser_.error().empty()) {
        set_error(browser_.error());
    }
}

void Session::render_selection() {
    set_text("sel-label", browser_.selection_label());
    set_visible("clear", browser_.selected_count() > 0);
    app_.document().set("share", "disabled", browser_.selected_count() > 0 ? "false" : "true");
}

void Session::render_pick() {
    set_text("pick-sum", browser_.selection_label());
    PeerCardRows cards(peers_);
    app_.document().set_model("pick-list", cards);
    set_visible("pick-empty", peers_.empty());
}

void Session::render_wait() {
    const XferView current = xfer();
    set_text("wait-avatar", first_glyph(current.peer_name));
    set_text("wait-title", "等待「" + current.peer_name + "」接受…");
    set_text("wait-count", std::to_string(browser_.selected_count()) + " 个项目");
    set_text("wait-size", "共 " + format_bytes(browser_.selected_bytes()));
    set_text("wait-hint", "同一批文件支持断点续传，中断后再次发送会接着传。");
}

void Session::render_transfer() {
    const XferView current = xfer();
    const double total = static_cast<double>(current.total);
    const double fraction = current.total == 0 ? 0.0 : static_cast<double>(current.done) / total;
    double elapsed = now_sec() - current.started;
    if (elapsed < 0.2) {
        elapsed = 0.2;
    }
    const double speed = static_cast<double>(current.session_bytes) / elapsed;

    set_text("xfer-kicker", current.sending ? "正在发送" : "正在接收");
    set_text("xfer-peer", current.peer_name);
    set_text("xfer-ip", current.peer_ip);
    const int percent = static_cast<int>(std::lround(fraction * 100.0));
    set_text("xfer-pct", std::to_string(percent) + "%");
    ProgressRows bar(fraction);
    app_.document().set_model("xfer-bar", bar);
    set_text("xfer-bytes", format_bytes(current.done) + "  /  " + format_bytes(current.total));
    if (current.done < current.total && speed > 1.0) {
        const long long remain = static_cast<long long>(static_cast<double>(current.total - current.done) / speed);
        set_text("xfer-rate", format_rate(speed) + "  ·  剩余约 " + std::to_string(remain) + " 秒");
    } else {
        set_text("xfer-rate", format_rate(speed));
    }
    set_text("xfer-file", current.current_file.empty() ? std::string("…") : current.current_file);
    DoneRows finished(current.finished);
    app_.document().set_model("done-list", finished);
    set_text("xfer-resume", "同批文件可断点续传。");
}

void Session::render_done() {
    const XferView current = xfer();
    double elapsed = now_sec() - current.started;
    if (elapsed < 0.2) {
        elapsed = 0.2;
    }
    const double speed = static_cast<double>(current.session_bytes) / elapsed;
    set_text("done-sub", std::string("已") + (current.sending ? "发送" : "接收") + "到「" + current.peer_name + "」");
    set_text("done-size", format_bytes(current.total));
    set_text("done-time", std::to_string(static_cast<long long>(elapsed + 0.5)) + " 秒");
    set_text("done-rate", format_rate(speed));
}

void Session::render_fail(const XferView& current) {
    if (current.phase == XferPhase::Rejected) {
        set_text("fail-title", "「" + current.peer_name + "」拒绝了这次分享");
        set_text("fail-sub", "对方没有接受这批文件");
    } else {
        set_text("fail-title", current.error.empty() ? std::string("出错了") : current.error);
        set_text("fail-sub", "传输没有完成");
    }
    set_visible("fail-retry", retry_available());
}

void Session::render_settings() {
    app_.document().set("set-name", "value", config_.name);
    app_.document().set("set-dir", "value", config_.save_dir);
    set_text("me-name", config_.name);
    refresh_storage_ui();
}

void Session::render_error() {
    const bool show = !error_.empty() && screen_ != Screen::Wait && screen_ != Screen::Transfer;
    set_text("app-error", show ? error_ : std::string());
    set_visible("app-error", show);
}

/* ---------------------------------------------------------------- handlers */

}  // namespace share
