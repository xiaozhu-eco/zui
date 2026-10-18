#ifndef ZUI_BACKENDS_PLATFORM_H
#define ZUI_BACKENDS_PLATFORM_H

#include <zui/export.h>
#include <zui/host_surface.h>
#include <zui/host_types.h>

#include <memory>
#include <string>
#include <string_view>

namespace zui {
namespace backends {

/// Process-scoped host services (dialogs, URI, app data dirs). Window-bound OS
/// belongs on IWindow. pick_* take a per-call owner token; this object does not
/// cache windows. Include host_types.h and host_surface.h; never app.h.
class IPlatform {
public:
    virtual ~IPlatform() = default;

    virtual PickPaths pick_open(NativeWindowId owner, const FileDialogOptions& opt) = 0;
    virtual PickPath pick_save(NativeWindowId owner, const FileDialogOptions& opt) = 0;
    virtual PickPath pick_folder(NativeWindowId owner, const FileDialogOptions& opt) = 0;
    virtual bool open_uri(std::string_view uri) = 0;
    virtual std::string user_data_dir(std::string_view app_id) const = 0;
    virtual std::string cache_dir(std::string_view app_id) const = 0;

    /// Runtime permission state, by stable name. Unsupported by default: only a
    /// platform whose OS has runtime permissions overrides this.
    virtual PermissionState permission_state(std::string_view name) const {
        (void)name;
        return PermissionState::Unsupported;
    }

    /// Mobile OS permission dialogs are async. App::request_permission uses this
    /// to decide whether to answer on the call stack or wait for
    /// mobile_complete_permission.
    virtual bool permissions_are_async() const { return false; }

    /// Present the OS permission dialog and finish later via
    /// mobile_complete_permission. Called only when permissions_are_async().
    virtual void begin_permission_request(std::string_view name) { (void)name; }

    /// Mobile OS pickers are async; desktop dialogs are sync. App::pick_*_async
    /// uses this to decide whether to complete on the call stack or wait for
    /// mobile_complete_pick_*.
    virtual bool picks_are_async() const { return false; }

    /// Present the OS picker and finish later via mobile_complete_pick_*.
    /// App::pick_*_async calls this only when picks_are_async() is true.
    /// The default presents nothing, so a headless stub stays pending until
    /// mobile_complete_pick_* (host tests). Device backends override it.
    virtual void begin_pick_open(NativeWindowId, const FileDialogOptions&) {}
    virtual void begin_pick_save(NativeWindowId, const FileDialogOptions&) {}
    virtual void begin_pick_folder(NativeWindowId, const FileDialogOptions&) {}
};

std::unique_ptr<IPlatform> create_noop_platform();
std::unique_ptr<IPlatform> create_win32_platform();
std::unique_ptr<IPlatform> create_linux_platform();
std::unique_ptr<IPlatform> create_macos_platform();
std::unique_ptr<IPlatform> create_ios_platform();
std::unique_ptr<IPlatform> create_android_platform();

/// Backends / tests only: next mobile pick_* returns @p once (then clears).
/// Lets host tests exercise Ok/Cancelled without device UI; A-tier unchanged.
ZUI_EXPORT void mobile_set_next_pick_open(PickPaths next);
ZUI_EXPORT void mobile_set_next_pick_save(PickPath next);
ZUI_EXPORT void mobile_set_next_pick_folder(PickPath next);

/// Tests only: the noop platform answers the next pick_* with @p once (then clears), so a
/// headless case can drive the Ok branch of an app's pick flow - the default answer is
/// Unsupported, which leaves the app on its no-dialog fallback.
ZUI_EXPORT void noop_set_next_pick_open(PickPaths next);
ZUI_EXPORT void noop_set_next_pick_save(PickPath next);

/// Native embedder / tests: complete a pending App::pick_*_async, or if none is
/// pending behave like mobile_set_next_pick_* for the next sync pick_*.
ZUI_EXPORT void mobile_complete_pick_open(PickPaths result);
ZUI_EXPORT void mobile_complete_pick_save(PickPath result);
ZUI_EXPORT void mobile_complete_pick_folder(PickPath result);

/// App registers a sink so mobile_complete_* can reach pending async handlers.
/// Pass ctx=nullptr (and null fns) to clear. Only the matching ctx clears.
using MobilePickOpenFn = void (*)(void* ctx, PickPaths result);
using MobilePickPathFn = void (*)(void* ctx, PickPath result);
ZUI_EXPORT void mobile_set_async_pick_sink(void* ctx, MobilePickOpenFn on_open,
                                           MobilePickPathFn on_save, MobilePickPathFn on_folder);
ZUI_EXPORT void mobile_clear_async_pick_sink(void* ctx);

/// Backends / tests only: the answer the next permission_state() returns, and
/// the completion of a pending request_permission. A-tier unchanged.
ZUI_EXPORT void mobile_set_next_permission(std::string_view name, PermissionState state);
ZUI_EXPORT void mobile_complete_permission(std::string_view name, PermissionState state);

/// App registers a sink so mobile_complete_permission can reach the handler.
using MobilePermissionFn = void (*)(void* ctx, std::string_view name, PermissionState state);
ZUI_EXPORT void mobile_set_permission_sink(void* ctx, MobilePermissionFn fn);
ZUI_EXPORT void mobile_clear_permission_sink(void* ctx);

/// Empty kind → getenv ZUI_PLATFORM; else OS default: win32 / linux / macos /
/// ios (TARGET_OS_IPHONE) / android (__ANDROID__).
/// "noop" → NoopPlatform (tests). "native" → current OS platform.
/// Unknown / wrong-OS desktop kinds fall back to NoopPlatform.
/// "ios" / "android" on a desktop host resolve to headless stubs (temp dirs; pick Unsupported).
/// When this binary is built for iOS or Android, those kinds resolve to the device IPlatform.
ZUI_EXPORT std::unique_ptr<IPlatform> create_platform(std::string_view kind);

}  // namespace backends
}  // namespace zui

#endif
