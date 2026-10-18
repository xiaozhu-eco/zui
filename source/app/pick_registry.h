#ifndef ZUI_SOURCE_APP_PICK_REGISTRY_H
#define ZUI_SOURCE_APP_PICK_REGISTRY_H

// The pending `App::pick_*_async` requests. The host may answer asynchronously (the mobile
// `picks_are_async` path), so the handler and its options park here until the backend calls back
// through the sink registered below.

#include <zui/app.h>

#include "backends/platform.h"
#include "backends/window.h"

#include <functional>
#include <utility>

namespace zui {
namespace app_detail {

class PickRegistry {
public:
    /// Observes the app's active window so a completed pick can drop the input it queued while
    /// the host dialog was up.
    explicit PickRegistry(backends::IWindow*& active_window) : active_window_(active_window) {}

    Host::PickOpenHandler open;
    Host::PickPathHandler save;
    Host::PickPathHandler folder;
    FileDialogOptions open_opt;
    FileDialogOptions save_opt;
    FileDialogOptions folder_opt;

    /// Drop every pending handler without calling it (App shutdown).
    void clear() {
        open = {};
        save = {};
        folder = {};
    }

    void enter_modal() { ++modal_depth_; }
    void leave_modal() { --modal_depth_; }
    bool modal() const { return modal_depth_ > 0; }

    void complete_open(PickPaths result) {
        if (open) {
            Host::PickOpenHandler handler = std::move(open);
            open = {};
            open_opt = {};
            discard_after_pick();
            handler(std::move(result));
            return;
        }
        backends::mobile_set_next_pick_open(std::move(result));
    }

    void complete_save(PickPath result) {
        if (save) {
            Host::PickPathHandler handler = std::move(save);
            save = {};
            save_opt = {};
            discard_after_pick();
            handler(std::move(result));
            return;
        }
        backends::mobile_set_next_pick_save(std::move(result));
    }

    void complete_folder(PickPath result) {
        if (folder) {
            Host::PickPathHandler handler = std::move(folder);
            folder = {};
            folder_opt = {};
            discard_after_pick();
            handler(std::move(result));
            return;
        }
        backends::mobile_set_next_pick_folder(std::move(result));
    }

    static void sink_open(void* ctx, PickPaths result) {
        static_cast<PickRegistry*>(ctx)->complete_open(std::move(result));
    }
    static void sink_save(void* ctx, PickPath result) {
        static_cast<PickRegistry*>(ctx)->complete_save(std::move(result));
    }
    static void sink_folder(void* ctx, PickPath result) {
        static_cast<PickRegistry*>(ctx)->complete_folder(std::move(result));
    }

    void register_mobile_sink() {
        backends::mobile_set_async_pick_sink(this, &PickRegistry::sink_open,
                                             &PickRegistry::sink_save, &PickRegistry::sink_folder);
    }
    void unregister_mobile_sink() { backends::mobile_clear_async_pick_sink(this); }

private:
    void discard_after_pick() {
        if (active_window_ != nullptr) {
            active_window_->discard_pending_input();
        }
    }

    backends::IWindow*& active_window_;
    int modal_depth_ = 0;
};

}  // namespace app_detail
}  // namespace zui

#endif
