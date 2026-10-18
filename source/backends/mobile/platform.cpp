#include "backends/mobile_device.h"
#include "backends/platform.h"
#include "ext/slots.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zui {
namespace backends {
namespace {

/// Optional one-shot pick injection for host tests / future async completion
/// without changing A-tier. Consumed by the next mobile pick_* call of that kind.
PickPaths g_next_pick_open{};
bool g_has_next_pick_open = false;
PickPath g_next_pick_save{};
bool g_has_next_pick_save = false;
PickPath g_next_pick_folder{};
bool g_has_next_pick_folder = false;

struct AsyncPickSink {
    void* ctx = nullptr;
    MobilePickOpenFn on_open = nullptr;
    MobilePickPathFn on_save = nullptr;
    MobilePickPathFn on_folder = nullptr;
};

AsyncPickSink g_async_sink{};

/// Optional one-shot permission answer for host tests / the headless mobile
/// stub. A query or a request of that name sees it; nothing else does.
std::string g_permission_name{};
PermissionState g_permission_state = PermissionState::Unsupported;

struct AsyncPermissionSink {
    void* ctx = nullptr;
    MobilePermissionFn fn = nullptr;
};

AsyncPermissionSink g_permission_sink{};

/// Portable stub IPlatform for iOS / Android kinds.
///
/// On-device (future):
///   iOS data  → Application Support (NSSearchPath / FileManager)
///   iOS cache → Library/Caches
///   Android data  → Context.getFilesDir() / equivalent app-specific files
///   Android cache → Context.getCacheDir()
///   pick_* → UIDocumentPickerViewController / Intent ACTION_OPEN_DOCUMENT
///            (async OS UI; complete via mobile_complete_pick_* → App handlers)
///   open_uri → UIApplication openURL / Intent ACTION_VIEW (scheme whitelist)
///
/// Desktop / CI compile of these kinds uses temp sandbox placeholders so Windows
/// and desktop hosts can exercise the factory without device SDKs.
class MobilePlatform final : public IPlatform {
public:
    explicit MobilePlatform(const char* kind) : kind_(kind) {}

    bool picks_are_async() const override { return true; }

    PermissionState permission_state(std::string_view name) const override {
        return g_permission_name == name ? g_permission_state : PermissionState::Unsupported;
    }

    bool permissions_are_async() const override { return true; }

    void begin_permission_request(std::string_view name) override {
        // An injected answer completes this ask. With none, the handler stays
        // parked until mobile_complete_permission; permission_state stays Unsupported.
        if (g_permission_name == name) {
            mobile_complete_permission(name, g_permission_state);
        }
    }

    PickPaths pick_open(NativeWindowId, const FileDialogOptions&) override {
        if (g_has_next_pick_open) {
            g_has_next_pick_open = false;
            PickPaths out = std::move(g_next_pick_open);
            g_next_pick_open = {};
            return out;
        }
        // Stub: device builds will present a document picker and return Ok/Cancelled.
        return {PickStatus::Unsupported, {}};
    }

    PickPath pick_save(NativeWindowId, const FileDialogOptions&) override {
        if (g_has_next_pick_save) {
            g_has_next_pick_save = false;
            PickPath out = std::move(g_next_pick_save);
            g_next_pick_save = {};
            return out;
        }
        return {PickStatus::Unsupported, {}};
    }

    PickPath pick_folder(NativeWindowId, const FileDialogOptions&) override {
        if (g_has_next_pick_folder) {
            g_has_next_pick_folder = false;
            PickPath out = std::move(g_next_pick_folder);
            g_next_pick_folder = {};
            return out;
        }
        return {PickStatus::Unsupported, {}};
    }

    bool open_uri(std::string_view uri) override {
        const PlatformKind kind = kind_[0] == 'i' ? PlatformKind::Ios : PlatformKind::Android;
        return shell_open_uri(kind, uri);
    }

    std::string user_data_dir(std::string_view app_id) const override {
        const PlatformKind kind = kind_[0] == 'i' ? PlatformKind::Ios : PlatformKind::Android;
        return shell_user_data_dir(kind, app_id);
    }

    std::string cache_dir(std::string_view app_id) const override {
        const PlatformKind kind = kind_[0] == 'i' ? PlatformKind::Ios : PlatformKind::Android;
        return shell_cache_dir(kind, app_id);
    }

private:
    const char* kind_ = "ios";
};

}  // namespace

std::unique_ptr<IPlatform> create_ios_platform() {
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
    return create_ios_platform_device();
#else
    return std::make_unique<MobilePlatform>("ios");
#endif
}

std::unique_ptr<IPlatform> create_android_platform() {
#if defined(__ANDROID__)
    return create_android_platform_device();
#else
    return std::make_unique<MobilePlatform>("android");
#endif
}

bool mobile_consume_injected_open(PickPaths& out) {
    if (!g_has_next_pick_open) {
        return false;
    }
    g_has_next_pick_open = false;
    out = std::move(g_next_pick_open);
    g_next_pick_open = {};
    return true;
}

bool mobile_consume_injected_save(PickPath& out) {
    if (!g_has_next_pick_save) {
        return false;
    }
    g_has_next_pick_save = false;
    out = std::move(g_next_pick_save);
    g_next_pick_save = {};
    return true;
}

bool mobile_consume_injected_folder(PickPath& out) {
    if (!g_has_next_pick_folder) {
        return false;
    }
    g_has_next_pick_folder = false;
    out = std::move(g_next_pick_folder);
    g_next_pick_folder = {};
    return true;
}

void mobile_set_next_pick_open(PickPaths next) {
    g_next_pick_open = std::move(next);
    g_has_next_pick_open = true;
}

void mobile_set_next_pick_save(PickPath next) {
    g_next_pick_save = std::move(next);
    g_has_next_pick_save = true;
}

void mobile_set_next_pick_folder(PickPath next) {
    g_next_pick_folder = std::move(next);
    g_has_next_pick_folder = true;
}

void mobile_set_async_pick_sink(void* ctx, MobilePickOpenFn on_open, MobilePickPathFn on_save,
                                MobilePickPathFn on_folder) {
    g_async_sink.ctx = ctx;
    g_async_sink.on_open = on_open;
    g_async_sink.on_save = on_save;
    g_async_sink.on_folder = on_folder;
}

void mobile_clear_async_pick_sink(void* ctx) {
    if (g_async_sink.ctx == ctx) {
        g_async_sink = {};
    }
}

void mobile_set_next_permission(std::string_view name, PermissionState state) {
    g_permission_name.assign(name.begin(), name.end());
    g_permission_state = state;
}

void mobile_set_permission_sink(void* ctx, MobilePermissionFn fn) {
    g_permission_sink.ctx = ctx;
    g_permission_sink.fn = fn;
}

void mobile_clear_permission_sink(void* ctx) {
    if (g_permission_sink.ctx == ctx) {
        g_permission_sink = {};
    }
}

void mobile_complete_permission(std::string_view name, PermissionState state) {
    if (g_permission_sink.ctx != nullptr && g_permission_sink.fn != nullptr) {
        g_permission_sink.fn(g_permission_sink.ctx, name, state);
        return;
    }
    mobile_set_next_permission(name, state);
}

void mobile_complete_pick_open(PickPaths result) {
    if (g_async_sink.ctx != nullptr && g_async_sink.on_open != nullptr) {
        g_async_sink.on_open(g_async_sink.ctx, std::move(result));
        return;
    }
    mobile_set_next_pick_open(std::move(result));
}

void mobile_complete_pick_save(PickPath result) {
    if (g_async_sink.ctx != nullptr && g_async_sink.on_save != nullptr) {
        g_async_sink.on_save(g_async_sink.ctx, std::move(result));
        return;
    }
    mobile_set_next_pick_save(std::move(result));
}

void mobile_complete_pick_folder(PickPath result) {
    if (g_async_sink.ctx != nullptr && g_async_sink.on_folder != nullptr) {
        g_async_sink.on_folder(g_async_sink.ctx, std::move(result));
        return;
    }
    mobile_set_next_pick_folder(std::move(result));
}

}  // namespace backends
}  // namespace zui
