#ifndef ZUI_HOST_H
#define ZUI_HOST_H

#include <zui/export.h>
#include <zui/host_types.h>
#include <zui/plugin.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace zui {

/// Process services for one App: dialogs, clipboard, paths, gamepads, and tasks.
class ZUI_EXPORT Host {
    friend class App;

public:
    /// Sets the application id used for user_data_dir / cache_dir. Overrides
    /// `<meta name="zui-app-id">` and the executable-stem fallback.
    void set_app_id(std::string_view id);

    /// Writable per-user data directory for this app id (created if needed).
    /// Safe to call before run(); lazily creates the platform backend.
    std::string user_data_dir() const;

    /// Writable cache directory for this app id (created if needed).
    std::string cache_dir() const;

    /// Native open-file dialog. The owner is the current window.
    /// When status != Ok, paths is empty.
    PickPaths pick_open(const FileDialogOptions& opt = {});

    /// Native save-file dialog. When status != Ok, path is empty.
    PickPath pick_save(const FileDialogOptions& opt = {});

    /// Native folder dialog. When status != Ok, path is empty.
    PickPath pick_folder(const FileDialogOptions& opt = {});

    using PickOpenHandler = std::function<void(PickPaths)>;
    using PickPathHandler = std::function<void(PickPath)>;

    /// Async open-file pick. Empty @p handler is a no-op.
    /// Desktop / sync platforms invoke @p handler on the same call stack with the
    /// sync pick_* result. A mobile host stores the handler until its system dialog returns.
    void pick_open_async(const FileDialogOptions& opt, PickOpenHandler handler);

    /// Async save-file pick. See pick_open_async.
    void pick_save_async(const FileDialogOptions& opt, PickPathHandler handler);

    /// Async folder pick. See pick_open_async.
    void pick_folder_async(const FileDialogOptions& opt, PickPathHandler handler);

    /// Runtime state of a declared mobile permission. The closed set is
    /// `Permission` in host_types.h. Unsupported on a platform that has no such
    /// notion (every desktop), or when the name is missing from the app's
    /// declaration (Android manifest / iOS usage description).
    PermissionState permission_state(Permission permission) const;

    using PermissionHandler = std::function<void(PermissionState)>;

    /// Asks the user for a declared permission. The handler runs once with the
    /// state afterwards. A desktop answers on the same call stack; a mobile OS
    /// answers after its dialog or settings page. Install-time names complete
    /// immediately. A blocked name opens system settings. While one request is
    /// pending, a second one is answered immediately with the current state.
    void request_permission(Permission permission, PermissionHandler handler);

    /// Reads the system text clipboard via the current window.
    /// Without a running window, status is Unsupported.
    ClipboardText clipboard_text();

    /// Writes UTF-8 text to the system clipboard via the current window.
    /// @return false when no window is running or the platform write failed.
    bool set_clipboard_text(std::string_view text);

    /// Reads the system image clipboard. Unsupported without a window. Does not change text.
    ClipboardImage clipboard_image();

    /// Writes the same owned tightly packed RGBA8 value `clipboard_image` returns.
    /// The text clipboard is left unchanged.
    /// @return false when no window is running, the buffer size does not match, or the
    ///         platform write failed.
    bool set_clipboard_image(const ClipboardImage& image);

    /// Opens @p uri with the OS default handler (http(s), mailto:, tel:, file:).
    /// Other schemes are rejected. @return false when unsupported or the launch failed.
    bool open_uri(std::string_view uri);

    /// One pad from the snapshot the gamepad service produced on the last frame.
    /// A slot past that snapshot (or a pad that is gone) comes back with `connected == false`.
    /// There is no gamepad event stream: read this where the frame already runs
    /// (a handler, a timer, or the app's own loop).
    GamepadState gamepad(std::size_t index) const;
    /// Gamepad slots the host reported on the last frame.
    std::size_t gamepad_count() const;

    /// Starts @p work on a background thread.
    ///
    /// `work` is a `TaskWork`. The worker calls `TaskSignal::cancelled` and
    /// `TaskSignal::set_progress` (a fraction in [0, 1]). It must not touch the document.
    /// `pump_frame` copies that fraction onto @p progress_id with `set(id, "value", ...)`,
    /// a scalar, not a model. `on_done` is a `MainThreadWork` and runs on the main thread
    /// only if the worker returns without `cancel_task`.
    /// @return 0 when @p work is empty. Zero is never a live task.
    TaskId start_task(TaskWork work, MainThreadWork on_done, std::string_view progress_id);

    /// Optional OS operations for this session. A default-constructed `HostServices`
    /// does not reach them.
    HostServices services() const;

    /// Sets the cancel flag and waits until @p work returns. Does not run the completion.
    /// @return false when @p id is unknown or the completion already ran.
    bool cancel_task(TaskId id);

    ~Host();

    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;
    Host(Host&&) = delete;
    Host& operator=(Host&&) = delete;

private:
    class Impl;
    explicit Host(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace zui

#endif  // ZUI_HOST_H
