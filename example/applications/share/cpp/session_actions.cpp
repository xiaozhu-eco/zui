#include "session.h"

#include "rows.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace share {
void Session::on_name(const zui::Event& event) {
    // The field is the live value; the config is committed by 开始 / 保存名字.
    const std::string next = trim(event.value());
    if (!next.empty()) {
        set_text("me-name", next);
    }
}

void Session::on_start() {
    const std::string name = trim(app_.document().get("name-input", "value"));
    if (name.empty()) {
        set_error("名字不能为空");
        return;
    }
    config_.name = name;
    create_dir_all(config_.save_dir);
    persist();
    if (!net_start(config_.id, config_.name, config_.save_dir)) {
        set_error("无法监听端口，检查是否已有实例在运行");
        return;
    }
    set_error("");
    if (!storage_access_ok(app_.host())) {
        set_error("需要存储权限才能浏览和接收文件");
    }
    go(Screen::Browse);
}

void Session::on_up() {
    if (browser_.up()) {
        set_error("");
        render_files();
    }
}

void Session::on_refresh() {
    browser_.refresh(true);
    render_files();
}

void Session::on_path(const zui::Event& event) {
    const std::string next = trim(event.value());
    if (!next.empty() && !browser_.open(next)) {
        set_error(browser_.error());
        return;
    }
    set_error("");
    render_files();
}

void Session::on_row(const zui::Event& event) {
    const std::string path(event.data("path"));
    if (path.empty()) {
        return;
    }
    const DirEnt* hit = nullptr;
    for (const DirEnt& entry : browser_.entries()) {
        if (entry.path == path) {
            hit = &entry;
            break;
        }
    }
    if (hit == nullptr) {
        return;
    }
    if (hit->is_dir) {
        if (!browser_.open(hit->path)) {
            set_error(browser_.error());
            return;
        }
        set_error("");
        render_files();
        return;
    }
    browser_.toggle(hit->path);
    render_files();
}

void Session::on_share() {
    if (browser_.selected_count() == 0) {
        return;
    }
    set_error("");
    go(Screen::Pick);
}

bool Session::try_offer(const Peer& peer) {
    auto files = browser_.expand();
    if (files.empty()) {
        set_error("没有可发送的文件");
        return false;
    }
    if (!net_offer(peer, std::move(files))) {
        // Task 4 replaces this branch with pending_peer_ + stronger copy.
        set_error("当前已有传输在进行");
        return false;
    }
    retry_peer_ = peer;
    set_error("");
    go(Screen::Wait);
    return true;
}

void Session::on_pick(const zui::Event& event) {
    const Peer* peer = find_peer(event.data("id"));
    if (peer == nullptr) {
        return;
    }
    (void)try_offer(*peer);
}

void Session::on_peer(const zui::Event& event) {
    const Peer* peer = find_peer(event.data("id"));
    if (peer == nullptr) {
        return;
    }
    if (browser_.selected_count() == 0) {
        // Task 2: go(Pick) + pick_highlight_id_
        return;
    }
    (void)try_offer(*peer);
}

void Session::on_accept() {
    const std::vector<IncomingOffer> offers = net_incoming();
    if (offers.empty()) {
        return;
    }
    net_respond(offers.front().offer_id, true);
    show_incoming(false);
    go(Screen::Transfer);
}

void Session::on_reject() {
    const std::vector<IncomingOffer> offers = net_incoming();
    show_incoming(false);
    if (!offers.empty()) {
        net_respond(offers.front().offer_id, false);
    }
}

void Session::on_save_name() {
    const std::string next = trim(app_.document().get("set-name", "value"));
    if (next.empty()) {
        set_error("名字不能为空");
        return;
    }
    config_.name = next;
    persist();
    net_set_name(next);
    set_text("me-name", next);
    set_error("");
}

void Session::apply_save_dir(std::string next) {
    next = try_resolve_save_dir(next);
    if (next.empty()) {
        set_error("无法使用该目录，请选内部存储下文件夹或先授予所有文件访问权");
        return;
    }
#if defined(__ANDROID__)
    if (!storage_access_ok(app_.host())) {
        set_error("需要存储权限才能设置接收目录");
        return;
    }
#endif
    if (!create_dir_all(next)) {
        set_error("无法创建该目录");
        return;
    }
    config_.save_dir = next;
    persist();
    net_set_save_dir(next);
    app_.document().set("set-dir", "value", next);
    set_error("");
}

void Session::on_save_dir() {
    const std::string next = trim(app_.document().get("set-dir", "value"));
    if (next.empty()) {
        set_error("目录不能为空");
        return;
    }
    apply_save_dir(next);
}

void Session::on_pick_save_dir() {
    zui::FileDialogOptions opt;
    opt.title = "选择接收目录";
    app_.host().pick_folder_async(opt, [this](zui::PickPath picked) {
        if (picked.status == zui::PickStatus::Cancelled) return;
        if (picked.status == zui::PickStatus::Unsupported) {
            set_error("当前平台不支持目录选择，请手动输入或使用当前浏览目录");
            return;
        }
        // Ok — may be filesystem path or content:// from ZuiHost.pathOrUri
        apply_save_dir(std::move(picked.path));
    });
}

void Session::on_request_storage() {
    request_storage_access(app_.host(), [this](zui::PermissionState) {
        refresh_storage_ui();
        if (storage_access_ok(app_.host())) {
            set_error("");
            if (screen_ == Screen::Browse) {
                browser_.refresh(true);
                render_files();
            }
        }
    });
}

void Session::refresh_storage_ui() {
    const zui::PermissionState state =
        app_.host().permission_state(zui::Permission::ManageStorage);
    const char* status = "";
    switch (state) {
        case zui::PermissionState::Unsupported:
            status = "当前平台无需额外存储权限";
            break;
        case zui::PermissionState::NotRequested:
            status = "尚未授权所有文件访问";
            break;
        case zui::PermissionState::Granted:
            status = "已授予存储权限";
            break;
        case zui::PermissionState::Denied:
            status = "已拒绝；可再次请求";
            break;
        case zui::PermissionState::Blocked:
            status = "已永久拒绝；请在系统设置中开启";
            break;
    }
    set_text("storage-perm-status", status);
    const bool show_request =
        state != zui::PermissionState::Granted && state != zui::PermissionState::Unsupported;
    set_visible("request-storage", show_request);
}

void Session::on_back() {
    switch (screen_) {
        case Screen::Pick:
        case Screen::Settings:
            set_error("");
            go(Screen::Browse);
            break;
        case Screen::Fail:
            net_clear_xfer();
            phase_ = XferPhase::Idle;
            clear_retry_peer();
            set_error("");
            go(Screen::Browse);
            break;
        case Screen::Browse:
            on_up();
            break;
        default:
            break;
    }
}


void Session::on_gesture_down(const zui::Event& event) {
    gesture_.on_down(event, /*incoming=*/false, /*files=*/false);
}

void Session::on_gesture_files_down(const zui::Event& event) {
    gesture_.on_down(event, /*incoming=*/false, /*files=*/true);
}

void Session::on_gesture_incoming_down(const zui::Event& event) {
    gesture_.on_down(event, /*incoming=*/true, /*files=*/false);
}

void Session::on_gesture_cancel(zui::Event&) {
    gesture_.reset();
}

void Session::dispatch_gesture(GestureAction action, zui::Event& event) {
    switch (action) {
        case GestureAction::Refresh:
            event.prevent_default();
            on_refresh();
            break;
        case GestureAction::Back:
            event.prevent_default();
            on_back();
            break;
        case GestureAction::Reject:
            event.prevent_default();
            on_reject();
            break;
        case GestureAction::None:
            break;
    }
}

void Session::on_gesture_up(zui::Event& event) {
    dispatch_gesture(gesture_.on_up(event), event);
}

void Session::on_accel(zui::Event& event) {
    const zui::Key* key = event.is_key();
    if (key == nullptr || key->composing || key->key != "Escape") {
        return;
    }
    if (incoming_on_) {
        on_reject();
        return;
    }
    on_back();
}


void Session::clear_retry_peer() {
    retry_peer_ = Peer{};
}

bool Session::retry_available() const {
    return retry_peer_.id != 0 && browser_.selected_count() > 0;
}

void Session::on_fail_retry() {
    if (!retry_available()) {
        return;
    }
    std::vector<ShareFile> files = browser_.expand();
    if (files.empty()) {
        set_visible("fail-retry", false);
        return;
    }
    if (!net_offer(retry_peer_, std::move(files))) {
        set_error("当前已有传输在进行");
        render_fail(xfer());
        return;
    }
    set_error("");
    go(Screen::Wait);
}


void Session::on_done_open() {
    const std::string uri = path_to_file_uri(config_.save_dir);
    if (uri.empty()) {
        return;
    }
    (void)app_.host().open_uri(uri);  // failure: no modal; #done-ok still works
}

/* ---------------------------------------------------------------- wiring */

void Session::bind() {
    app_.document().bind("on_accel", [this](zui::Event& event) { on_accel(event); });
    app_.document().bind("on_name", [this](const zui::Event& event) { on_name(event); });
    app_.document().bind("on_name_key", [this](const zui::Event& event) {
        const zui::Key* key = event.is_key();
        if (key != nullptr && key->key == "Enter") {
            on_start();
        }
    });
    app_.document().bind("on_start", [this](zui::Event&) { on_start(); });
    app_.document().bind("on_settings", [this](zui::Event&) {
        set_error("");
        go(Screen::Settings);
    });
    app_.document().bind("on_back", [this](zui::Event&) { on_back(); });
    app_.document().bind("on_up", [this](zui::Event&) { on_up(); });
    app_.document().bind("on_refresh", [this](zui::Event&) { on_refresh(); });
    app_.document().bind("on_path", [this](const zui::Event& event) { on_path(event); });
    app_.document().bind("on_row", [this](zui::Event& event) {
        // render_files replaces the clicked row. Cancelling the default keeps the
        // click from using that node after the model rebuild.
        event.prevent_default();
        on_row(event);
    });
    app_.document().bind("on_clear", [this](zui::Event&) {
        browser_.clear_selection();
        render_files();
    });
    app_.document().bind("on_share", [this](zui::Event&) { on_share(); });
    app_.document().bind("on_peer", [this](const zui::Event& event) { on_peer(event); });
    app_.document().bind("on_pick", [this](zui::Event& event) {
        event.prevent_default();
        on_pick(event);
    });
    app_.document().bind("on_wait_cancel", [this](zui::Event&) {
        net_cancel();
        net_clear_xfer();
        phase_ = XferPhase::Idle;
        clear_retry_peer();
        go(Screen::Browse);
    });
    app_.document().bind("on_xfer_cancel", [this](zui::Event&) {
        net_cancel();
        // A live socket stays up for its thread to record the failure. With nothing connected,
        // the click leaves the transfer the same way the page script does: back to the browser.
        if (net_xfer().phase != XferPhase::Idle) {
            return;
        }
        phase_ = XferPhase::Idle;
        clear_retry_peer();
        set_error("");
        go(Screen::Browse);
    });
    app_.document().bind("on_done_open", [this](zui::Event&) { on_done_open(); });
    app_.document().bind("on_done", [this](zui::Event&) {
        net_clear_xfer();
        phase_ = XferPhase::Idle;
        clear_retry_peer();
        go(Screen::Browse);
    });
    app_.document().bind("on_fail_retry", [this](zui::Event&) { on_fail_retry(); });
    app_.document().bind("on_accept", [this](zui::Event&) { on_accept(); });
    app_.document().bind("on_reject", [this](zui::Event&) { on_reject(); });
    app_.document().bind("on_save_name", [this](zui::Event&) { on_save_name(); });
    app_.document().bind("on_save_dir", [this](zui::Event&) { on_save_dir(); });
    app_.document().bind("on_pick_save_dir", [this](zui::Event&) { on_pick_save_dir(); });
    app_.document().bind("on_request_storage", [this](zui::Event&) { on_request_storage(); });
    app_.document().bind("on_use_cwd", [this](zui::Event&) {
        app_.document().set("set-dir", "value", browser_.label());
    });
    app_.document().bind("on_gesture_down", [this](const zui::Event& event) {
        on_gesture_down(event);
    });
    app_.document().bind("on_gesture_files_down", [this](const zui::Event& event) {
        on_gesture_files_down(event);
    });
    app_.document().bind("on_gesture_incoming_down", [this](const zui::Event& event) {
        on_gesture_incoming_down(event);
    });
    app_.document().bind("on_gesture_up", [this](zui::Event& event) { on_gesture_up(event); });
    app_.document().bind("on_gesture_cancel", [this](zui::Event& event) {
        on_gesture_cancel(event);
    });
}


}  // namespace share
