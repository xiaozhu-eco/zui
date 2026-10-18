#ifndef ZUI_SOURCE_APP_PERMISSION_REGISTRY_H
#define ZUI_SOURCE_APP_PERMISSION_REGISTRY_H

// The pending `Host::request_permission`. A mobile OS answers asynchronously, so the handler parks
// here until the backend calls back through the sink registered below.

#include <zui/app.h>

#include "backends/platform.h"

#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace zui {
namespace app_detail {

class PermissionRegistry {
public:
    /// The handler waiting for the OS dialog, and the name it asked for.
    Host::PermissionHandler pending;
    std::string pending_name;

    /// An answer the platform produced without a pending handler (an injected
    /// test answer, or a result that arrived after the window closed). Later
    /// queries see it.
    std::string answer_name;
    PermissionState answer_state = PermissionState::Unsupported;

    /// Drop the pending handler without calling it (App shutdown).
    void clear() {
        pending = {};
        pending_name.clear();
    }

    PermissionState state(backends::IPlatform* platform, std::string_view name) const {
        if (!answer_name.empty() && answer_name == name) {
            return answer_state;
        }
        if (platform == nullptr) {
            return PermissionState::Unsupported;
        }
        return platform->permission_state(name);
    }

    void request(backends::IPlatform* platform, std::string_view name, Host::PermissionHandler handler) {
        if (!handler) {
            return;
        }
        // A desktop, or a platform with no runtime permissions, answers here.
        if (platform == nullptr || !platform->permissions_are_async()) {
            handler(state(platform, name));
            return;
        }
        // One OS dialog at a time: a second ask reports the current state.
        if (pending) {
            handler(state(platform, name));
            return;
        }
        pending_name.assign(name.begin(), name.end());
        pending = std::move(handler);
        // A headless backend answers inside this call; complete() then clears
        // `pending` before begin_permission_request returns.
        platform->begin_permission_request(name);
    }

    void complete(std::string_view name, PermissionState value) {
        if (pending && pending_name == name) {
            Host::PermissionHandler handler = std::move(pending);
            pending = {};
            pending_name.clear();
            handler(value);
            return;
        }
        answer_name.assign(name.begin(), name.end());
        answer_state = value;
    }

    static void sink(void* ctx, std::string_view name, PermissionState value) {
        static_cast<PermissionRegistry*>(ctx)->complete(name, value);
    }

    void register_mobile_sink() { backends::mobile_set_permission_sink(this, &PermissionRegistry::sink); }
    void unregister_mobile_sink() { backends::mobile_clear_permission_sink(this); }
};

}  // namespace app_detail
}  // namespace zui

#endif
