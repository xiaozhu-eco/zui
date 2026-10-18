#ifndef ZUI_HOST_TYPES_H
#define ZUI_HOST_TYPES_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace zui {

enum class PickStatus { Ok, Cancelled, Unsupported };

/// Closed set of mobile permission names. Camera / microphone / location keep
/// their original values. The packer catalog uses the same stable strings
/// `permission_name` returns.
enum class Permission {
    Camera = 0,
    Microphone,
    Location,
    Network,
    NetworkState,
    WifiState,
    ChangeWifi,
    Multicast,   // NEW - keep contiguous integers; permission_parse loops 0..FullScreenIntent
    Vibrate,
    Nfc,
    WakeLock,
    BootCompleted,
    Biometric,
    ModifyAudio,
    ForegroundService,
    ForegroundCamera,
    ForegroundMicrophone,
    ForegroundLocation,
    ForegroundPlayback,
    ForegroundSync,
    ForegroundConnected,
    HighSamplingRate,
    DetectScreenCapture,
    CoarseLocation,
    LocationBackground,
    Storage,
    StorageWrite,
    MediaImages,
    MediaVideo,
    MediaAudio,
    MediaVisualSelected,
    MediaLocation,
    Notifications,
    BluetoothScan,
    BluetoothConnect,
    BluetoothAdvertise,
    NearbyWifi,
    ContactsRead,
    ContactsWrite,
    Accounts,
    CalendarRead,
    CalendarWrite,
    PhoneCall,
    PhoneState,
    PhoneNumbers,
    CallLogRead,
    CallLogWrite,
    AnswerCalls,
    SmsSend,
    SmsReceive,
    SmsRead,
    BodySensors,
    BodySensorsBackground,
    ActivityRecognition,
    ManageStorage,
    Overlay,
    InstallPackages,
    IgnoreBattery,
    ExactAlarm,
    WriteSettings,
    FullScreenIntent,
};

inline const char* permission_name(Permission permission) noexcept {
    switch (permission) {
    case Permission::Camera:
        return "camera";
    case Permission::Microphone:
        return "microphone";
    case Permission::Location:
        return "location";
    case Permission::Network:
        return "network";
    case Permission::NetworkState:
        return "network-state";
    case Permission::WifiState:
        return "wifi-state";
    case Permission::ChangeWifi:
        return "change-wifi";
    case Permission::Multicast:
        return "multicast";
    case Permission::Vibrate:
        return "vibrate";
    case Permission::Nfc:
        return "nfc";
    case Permission::WakeLock:
        return "wake-lock";
    case Permission::BootCompleted:
        return "boot-completed";
    case Permission::Biometric:
        return "biometric";
    case Permission::ModifyAudio:
        return "modify-audio";
    case Permission::ForegroundService:
        return "foreground-service";
    case Permission::ForegroundCamera:
        return "foreground-camera";
    case Permission::ForegroundMicrophone:
        return "foreground-microphone";
    case Permission::ForegroundLocation:
        return "foreground-location";
    case Permission::ForegroundPlayback:
        return "foreground-playback";
    case Permission::ForegroundSync:
        return "foreground-sync";
    case Permission::ForegroundConnected:
        return "foreground-connected";
    case Permission::HighSamplingRate:
        return "high-sampling-rate";
    case Permission::DetectScreenCapture:
        return "detect-screen-capture";
    case Permission::CoarseLocation:
        return "coarse-location";
    case Permission::LocationBackground:
        return "location-background";
    case Permission::Storage:
        return "storage";
    case Permission::StorageWrite:
        return "storage-write";
    case Permission::MediaImages:
        return "media-images";
    case Permission::MediaVideo:
        return "media-video";
    case Permission::MediaAudio:
        return "media-audio";
    case Permission::MediaVisualSelected:
        return "media-visual-selected";
    case Permission::MediaLocation:
        return "media-location";
    case Permission::Notifications:
        return "notifications";
    case Permission::BluetoothScan:
        return "bluetooth-scan";
    case Permission::BluetoothConnect:
        return "bluetooth-connect";
    case Permission::BluetoothAdvertise:
        return "bluetooth-advertise";
    case Permission::NearbyWifi:
        return "nearby-wifi";
    case Permission::ContactsRead:
        return "contacts-read";
    case Permission::ContactsWrite:
        return "contacts-write";
    case Permission::Accounts:
        return "accounts";
    case Permission::CalendarRead:
        return "calendar-read";
    case Permission::CalendarWrite:
        return "calendar-write";
    case Permission::PhoneCall:
        return "phone-call";
    case Permission::PhoneState:
        return "phone-state";
    case Permission::PhoneNumbers:
        return "phone-numbers";
    case Permission::CallLogRead:
        return "call-log-read";
    case Permission::CallLogWrite:
        return "call-log-write";
    case Permission::AnswerCalls:
        return "answer-calls";
    case Permission::SmsSend:
        return "sms-send";
    case Permission::SmsReceive:
        return "sms-receive";
    case Permission::SmsRead:
        return "sms-read";
    case Permission::BodySensors:
        return "body-sensors";
    case Permission::BodySensorsBackground:
        return "body-sensors-background";
    case Permission::ActivityRecognition:
        return "activity-recognition";
    case Permission::ManageStorage:
        return "manage-storage";
    case Permission::Overlay:
        return "overlay";
    case Permission::InstallPackages:
        return "install-packages";
    case Permission::IgnoreBattery:
        return "ignore-battery";
    case Permission::ExactAlarm:
        return "exact-alarm";
    case Permission::WriteSettings:
        return "write-settings";
    case Permission::FullScreenIntent:
        return "full-screen-intent";
    }
    return "";
}

inline bool permission_parse(std::string_view name, Permission& out) noexcept {
    const char* text = nullptr;
    for (int i = 0; i <= static_cast<int>(Permission::FullScreenIntent); ++i) {
        const auto value = static_cast<Permission>(i);
        text = permission_name(value);
        if (text[0] != '\0' && name == text) {
            out = value;
            return true;
        }
    }
    return false;
}

/// Runtime state of a mobile permission. A platform with no such notion (every
/// desktop) is Unsupported.
enum class PermissionState {
    Unsupported,   ///< the platform has none
    NotRequested,  ///< declared, and the user has not been asked yet
    Granted,       ///< usable now
    Denied,        ///< refused, and asking again shows the system dialog
    Blocked,       ///< refused for good; only the system settings can grant it
};

/// Multi-path pick result. When status != Ok, paths is empty.
struct PickPaths {
    PickStatus status = PickStatus::Unsupported;
    std::vector<std::string> paths;
};

/// Single-path pick result. When status != Ok, path is empty.
struct PickPath {
    PickStatus status = PickStatus::Unsupported;
    std::string path;
};

enum class ClipboardStatus { Ok, Unsupported };

/// Clipboard read result. When status == Ok, text may still be empty.
struct ClipboardText {
    ClipboardStatus status = ClipboardStatus::Unsupported;
    std::string text;
};

/// Image clipboard, separate from text. Tightly packed RGBA8, top-left origin.
/// `status == Ok` with a positive size means `rgba` is `width * height * 4` bytes.
struct ClipboardImage {
    ClipboardStatus status = ClipboardStatus::Unsupported;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

/// Block alignment for `Document::scroll_into_view` (CSSOM View `scrollIntoView`).
enum class ScrollAlign { Start, Center, End, Nearest };

struct FileFilter {
    std::string label;
    std::string patterns;
};

struct FileDialogOptions {
    std::string title;
    std::string default_name;
    std::vector<FileFilter> filters;
    bool multi = false;
};

/// Runtime window icon. Tightly packed RGBA8888, top-left origin, `width * height * 4` bytes.
/// This is the live title-bar / taskbar icon, not an executable or installer image.
struct WindowIcon {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;

    /// True when both edges are positive and `rgba` is exactly `width * height * 4` bytes.
    bool valid() const noexcept {
        if (width <= 0 || height <= 0) {
            return false;
        }
        const std::size_t w = static_cast<std::size_t>(width);
        const std::size_t h = static_cast<std::size_t>(height);
        if (w > (static_cast<std::size_t>(-1) / 4) / h) {
            return false;
        }
        return rgba.size() == w * h * 4;
    }
};

/// Which display the window is on. `known` is false when the host cannot say.
struct DisplayIdentity {
    bool known = false;
    std::string id;
    int index = -1;
    int width_px = 0;
    int height_px = 0;
    float scale = 1.f;
};

/// Worker-facing cancel and scalar progress for one background task.
///
/// The worker calls `cancelled` and `set_progress`. It must not touch the document.
/// The pointers that back this signal stay inside the task service.
class TaskSignal {
public:
    TaskSignal() = default;
    TaskSignal(std::atomic<bool>* cancel, std::atomic<float>* progress) noexcept
        : cancel_(cancel), progress_(progress) {}

    bool cancelled() const noexcept {
        return cancel_ != nullptr && cancel_->load(std::memory_order_acquire);
    }

    void set_progress(float fraction) const noexcept {
        if (progress_ == nullptr) {
            return;
        }
        if (fraction < 0.f) {
            fraction = 0.f;
        }
        if (fraction > 1.f) {
            fraction = 1.f;
        }
        progress_->store(fraction, std::memory_order_release);
    }

private:
    std::atomic<bool>* cancel_ = nullptr;
    std::atomic<float>* progress_ = nullptr;
};

using TaskId = std::uint64_t;

/// Work that runs on a background thread. It is not a main-thread completion:
/// a no-argument callable does not construct one of these.
class TaskWork {
public:
    TaskWork() = default;

    template <class F,
              class = std::enable_if_t<std::conjunction_v<
                  std::negation<std::is_same<std::decay_t<F>, TaskWork>>,
                  std::is_invocable<std::decay_t<F>&, const TaskSignal&>>>>
    TaskWork(F&& fn) : fn_(std::forward<F>(fn)) {}

    void operator()(const TaskSignal& signal) const {
        if (fn_) {
            fn_(signal);
        }
    }

    explicit operator bool() const noexcept { return static_cast<bool>(fn_); }

private:
    std::function<void(const TaskSignal&)> fn_;
};

/// Completion that runs on the main thread. A callable that accepts `TaskSignal`
/// does not construct one of these.
class MainThreadWork {
public:
    MainThreadWork() = default;

    template <class F,
              class = std::enable_if_t<std::conjunction_v<
                  std::negation<std::is_same<std::decay_t<F>, MainThreadWork>>,
                  std::is_invocable<std::decay_t<F>&>,
                  std::negation<std::is_invocable<std::decay_t<F>&, const TaskSignal&>>>>>
    MainThreadWork(F&& fn) : fn_(std::forward<F>(fn)) {}

    void operator()() const {
        if (fn_) {
            fn_();
        }
    }

    explicit operator bool() const noexcept { return static_cast<bool>(fn_); }

private:
    std::function<void()> fn_;
};

/// W3C Gamepad API counts: 4 axes (left/right stick) and 17 standard-mapping buttons.
constexpr int kGamepadAxisCount = 4;
constexpr int kGamepadButtonCount = 17;

struct GamepadButton {
    bool pressed = false;
    /// Analog travel 0..1; a digital button reports 0 or 1.
    float value = 0.f;
};

/// One pad in the W3C Gamepad shape. Gamepads are polled, never event-driven.
/// `Host::gamepad` returns the pad from the snapshot the gamepad service produced.
struct GamepadState {
    bool connected = false;
    /// `Gamepad.id`: the OS name of the pad.
    std::string id;
    /// `Gamepad.axes` order: 0/1 left stick x/y, 2/3 right stick x/y, each -1..1.
    float axes[kGamepadAxisCount] = {};
    /// `Gamepad.buttons` in standard-mapping order: 0 south/A, 1 east/B, 2 west/X, 3 north/Y,
    /// 4 LB, 5 RB, 6 LT, 7 RT, 8 back, 9 start, 10 LStick, 11 RStick, 12 dpad up, 13 down,
    /// 14 left, 15 right, 16 guide.
    GamepadButton buttons[kGamepadButtonCount] = {};
};

}  // namespace zui

#endif  // ZUI_HOST_TYPES_H
