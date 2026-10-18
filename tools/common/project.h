#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace zui_tool {

// One mobile permission the author asked for. `name` is the stable id the
// tools know. `android` is the platform permission that name becomes. `reason`
// is the sentence a person reads. The list is the only permission source.
struct Permission {
    std::string name;
    std::string android;
    std::string reason;
};

// The only mobile-permission vocabulary. The wizard prints it. The manifest
// requests `android` for the names the author kept, and nothing else. Stable
// `name` values match zui::Permission / permission_name in include/zui/host_types.h.
struct CatalogPermission {
    const char* name;
    const char* android;
    const char* reason;
    /// The OS asks the user at run time (Android 6+ dangerous permissions).
    bool runtime;
    /// The OS has no dialog: the app opens a system settings page instead.
    bool settings;
    /// A hardware feature this permission implies; the manifest declares it as
    /// not required, so the store does not filter the app out.
    const char* feature;
    /// Emitted as android:maxSdkVersion when not zero: the permission was
    /// replaced on newer API levels.
    int max_sdk;
    /// The older Android permission of the same capability, with the API level
    /// it applies up to. Empty when the permission is not versioned.
    const char* legacy;
    int legacy_max_sdk;
    /// iOS / macOS Info.plist usage-description key, or null.
    const char* ios_key;
    /// A second usage-description key the same capability needs, or null.
    const char* ios_key_alt;
};

inline constexpr CatalogPermission kPermissionCatalog[] = {
    {"network", "android.permission.INTERNET", "Open network connections", false, false, nullptr, 0,
     nullptr, 0, nullptr, nullptr},
    {"network-state", "android.permission.ACCESS_NETWORK_STATE",
     "Read whether a network is connected", false, false, nullptr, 0, nullptr, 0, nullptr, nullptr},
    {"wifi-state", "android.permission.ACCESS_WIFI_STATE", "Read Wi-Fi connection state", false,
     false, "android.hardware.wifi", 0, nullptr, 0, nullptr, nullptr},
    {"change-wifi", "android.permission.CHANGE_WIFI_STATE", "Change Wi-Fi state", false, false,
     "android.hardware.wifi", 0, nullptr, 0, nullptr, nullptr},
    {"multicast", "android.permission.CHANGE_WIFI_MULTICAST_STATE",
     "Receive multicast packets for LAN discovery", false,
     false, "android.hardware.wifi", 0, nullptr, 0, nullptr, nullptr},
    {"vibrate", "android.permission.VIBRATE", "Vibrate the device", false, false, nullptr, 0,
     nullptr, 0, nullptr, nullptr},
    {"nfc", "android.permission.NFC", "Read and write NFC tags", false, false, "android.hardware.nfc",
     0, nullptr, 0, "NFCReaderUsageDescription", nullptr},
    {"wake-lock", "android.permission.WAKE_LOCK", "Keep the CPU awake", false, false, nullptr, 0,
     nullptr, 0, nullptr, nullptr},
    {"boot-completed", "android.permission.RECEIVE_BOOT_COMPLETED",
     "Run after the device boots", false, false, nullptr, 0, nullptr, 0, nullptr, nullptr},
    {"biometric", "android.permission.USE_BIOMETRIC", "Use fingerprint or face unlock", false, false,
     nullptr, 0, "android.permission.USE_FINGERPRINT", 28, "NSFaceIDUsageDescription", nullptr},
    {"modify-audio", "android.permission.MODIFY_AUDIO_SETTINGS", "Change audio settings", false,
     false, nullptr, 0, nullptr, 0, nullptr, nullptr},
    {"foreground-service", "android.permission.FOREGROUND_SERVICE", "Run a foreground service",
     false, false, nullptr, 0, nullptr, 0, nullptr, nullptr},
    {"foreground-camera", "android.permission.FOREGROUND_SERVICE_CAMERA",
     "Run a camera foreground service", false, false, nullptr, 0, nullptr, 0, nullptr, nullptr},
    {"foreground-microphone", "android.permission.FOREGROUND_SERVICE_MICROPHONE",
     "Run a microphone foreground service", false, false, nullptr, 0, nullptr, 0, nullptr, nullptr},
    {"foreground-location", "android.permission.FOREGROUND_SERVICE_LOCATION",
     "Run a location foreground service", false, false, nullptr, 0, nullptr, 0, nullptr, nullptr},
    {"foreground-playback", "android.permission.FOREGROUND_SERVICE_MEDIA_PLAYBACK",
     "Run a media-playback foreground service", false, false, nullptr, 0, nullptr, 0, nullptr,
     nullptr},
    {"foreground-sync", "android.permission.FOREGROUND_SERVICE_DATA_SYNC",
     "Run a data-sync foreground service", false, false, nullptr, 0, nullptr, 0, nullptr, nullptr},
    {"foreground-connected", "android.permission.FOREGROUND_SERVICE_CONNECTED_DEVICE",
     "Run a connected-device foreground service", false, false, nullptr, 0, nullptr, 0, nullptr,
     nullptr},
    {"high-sampling-rate", "android.permission.HIGH_SAMPLING_RATE_SENSORS",
     "Read sensors at a high sampling rate", false, false, nullptr, 0, nullptr, 0, nullptr, nullptr},
    {"detect-screen-capture", "android.permission.DETECT_SCREEN_CAPTURE",
     "Learn when the screen is captured", false, false, nullptr, 0, nullptr, 0, nullptr, nullptr},
    {"camera", "android.permission.CAMERA", "Capture photos and video", true, false,
     "android.hardware.camera", 0, nullptr, 0, "NSCameraUsageDescription", nullptr},
    {"microphone", "android.permission.RECORD_AUDIO", "Record audio", true, false,
     "android.hardware.microphone", 0, nullptr, 0, "NSMicrophoneUsageDescription", nullptr},
    {"location", "android.permission.ACCESS_FINE_LOCATION", "Read a precise location", true, false,
     "android.hardware.location.gps", 0, nullptr, 0, "NSLocationWhenInUseUsageDescription", nullptr},
    {"coarse-location", "android.permission.ACCESS_COARSE_LOCATION",
     "Read an approximate location", true, false, "android.hardware.location", 0, nullptr, 0,
     "NSLocationWhenInUseUsageDescription", nullptr},
    {"location-background", "android.permission.ACCESS_BACKGROUND_LOCATION",
     "Read location while in the background", true, false, "android.hardware.location.gps", 0,
     nullptr, 0, "NSLocationAlwaysAndWhenInUseUsageDescription",
     "NSLocationWhenInUseUsageDescription"},
    {"storage", "android.permission.READ_EXTERNAL_STORAGE", "Read files stored on the device", true,
     false, nullptr, 32, nullptr, 0, nullptr, nullptr},
    {"storage-write", "android.permission.WRITE_EXTERNAL_STORAGE",
     "Write files stored on the device", true, false, nullptr, 29, nullptr, 0,
     "NSPhotoLibraryAddUsageDescription", nullptr},
    {"media-images", "android.permission.READ_MEDIA_IMAGES", "Read images on the device", true,
     false, nullptr, 0, nullptr, 0, "NSPhotoLibraryUsageDescription", nullptr},
    {"media-video", "android.permission.READ_MEDIA_VIDEO", "Read video on the device", true, false,
     nullptr, 0, nullptr, 0, "NSPhotoLibraryUsageDescription", nullptr},
    {"media-audio", "android.permission.READ_MEDIA_AUDIO", "Read audio on the device", true, false,
     nullptr, 0, nullptr, 0, "NSAppleMusicUsageDescription", nullptr},
    {"media-visual-selected", "android.permission.READ_MEDIA_VISUAL_USER_SELECTED",
     "Read photos the user selected", true, false, nullptr, 0, nullptr, 0,
     "NSPhotoLibraryUsageDescription", nullptr},
    {"media-location", "android.permission.ACCESS_MEDIA_LOCATION",
     "Read location metadata of media", true, false, nullptr, 0, nullptr, 0, nullptr, nullptr},
    {"notifications", "android.permission.POST_NOTIFICATIONS", "Post a notification", true, false,
     nullptr, 0, nullptr, 0, nullptr, nullptr},
    {"bluetooth-scan", "android.permission.BLUETOOTH_SCAN", "Find nearby Bluetooth devices", true,
     false, "android.hardware.bluetooth_le", 0, "android.permission.BLUETOOTH_ADMIN", 30,
     "NSBluetoothAlwaysUsageDescription", "NSBluetoothPeripheralUsageDescription"},
    {"bluetooth-connect", "android.permission.BLUETOOTH_CONNECT",
     "Talk to paired Bluetooth devices", true, false, "android.hardware.bluetooth_le", 0,
     "android.permission.BLUETOOTH", 30, "NSBluetoothAlwaysUsageDescription",
     "NSBluetoothPeripheralUsageDescription"},
    {"bluetooth-advertise", "android.permission.BLUETOOTH_ADVERTISE",
     "Advertise this device over Bluetooth", true, false, "android.hardware.bluetooth_le", 0,
     "android.permission.BLUETOOTH_ADMIN", 30, "NSBluetoothAlwaysUsageDescription",
     "NSBluetoothPeripheralUsageDescription"},
    {"nearby-wifi", "android.permission.NEARBY_WIFI_DEVICES", "Find nearby Wi-Fi devices", true,
     false, "android.hardware.wifi", 0, nullptr, 0, "NSLocalNetworkUsageDescription", nullptr},
    {"contacts-read", "android.permission.READ_CONTACTS", "Read the contacts list", true, false,
     nullptr, 0, nullptr, 0, "NSContactsUsageDescription", nullptr},
    {"contacts-write", "android.permission.WRITE_CONTACTS", "Change the contacts list", true, false,
     nullptr, 0, nullptr, 0, "NSContactsUsageDescription", nullptr},
    {"accounts", "android.permission.GET_ACCOUNTS", "Read accounts on the device", true, false,
     nullptr, 0, nullptr, 0, nullptr, nullptr},
    {"calendar-read", "android.permission.READ_CALENDAR", "Read the calendar", true, false, nullptr,
     0, nullptr, 0, "NSCalendarsUsageDescription", "NSCalendarsFullAccessUsageDescription"},
    {"calendar-write", "android.permission.WRITE_CALENDAR", "Change the calendar", true, false,
     nullptr, 0, nullptr, 0, "NSCalendarsUsageDescription", "NSCalendarsFullAccessUsageDescription"},
    {"phone-call", "android.permission.CALL_PHONE", "Place a phone call", true, false,
     "android.hardware.telephony", 0, nullptr, 0, nullptr, nullptr},
    {"phone-state", "android.permission.READ_PHONE_STATE", "Read phone state", true, false,
     "android.hardware.telephony", 0, nullptr, 0, nullptr, nullptr},
    {"phone-numbers", "android.permission.READ_PHONE_NUMBERS", "Read the device phone numbers", true,
     false, "android.hardware.telephony", 0, nullptr, 0, nullptr, nullptr},
    {"call-log-read", "android.permission.READ_CALL_LOG", "Read the call log", true, false,
     "android.hardware.telephony", 0, nullptr, 0, nullptr, nullptr},
    {"call-log-write", "android.permission.WRITE_CALL_LOG", "Change the call log", true, false,
     "android.hardware.telephony", 0, nullptr, 0, nullptr, nullptr},
    {"answer-calls", "android.permission.ANSWER_PHONE_CALLS", "Answer a phone call", true, false,
     "android.hardware.telephony", 0, nullptr, 0, nullptr, nullptr},
    {"sms-send", "android.permission.SEND_SMS", "Send an SMS message", true, false,
     "android.hardware.telephony", 0, nullptr, 0, nullptr, nullptr},
    {"sms-receive", "android.permission.RECEIVE_SMS", "Receive an SMS message", true, false,
     "android.hardware.telephony", 0, nullptr, 0, nullptr, nullptr},
    {"sms-read", "android.permission.READ_SMS", "Read SMS messages", true, false,
     "android.hardware.telephony", 0, nullptr, 0, nullptr, nullptr},
    {"body-sensors", "android.permission.BODY_SENSORS", "Read body sensors", true, false, nullptr, 0,
     nullptr, 0, nullptr, nullptr},
    {"body-sensors-background", "android.permission.BODY_SENSORS_BACKGROUND",
     "Read body sensors in the background", true, false, nullptr, 0, nullptr, 0, nullptr, nullptr},
    {"activity-recognition", "android.permission.ACTIVITY_RECOGNITION",
     "Read physical activity", true, false, nullptr, 0, nullptr, 0, "NSMotionUsageDescription",
     nullptr},
    {"manage-storage", "android.permission.MANAGE_EXTERNAL_STORAGE",
     "Access all files on the device", false, true, nullptr, 0, nullptr, 0, nullptr, nullptr},
    {"overlay", "android.permission.SYSTEM_ALERT_WINDOW", "Draw over other apps", false, true,
     nullptr, 0, nullptr, 0, nullptr, nullptr},
    {"install-packages", "android.permission.REQUEST_INSTALL_PACKAGES",
     "Install other packages", false, true, nullptr, 0, nullptr, 0, nullptr, nullptr},
    {"ignore-battery", "android.permission.REQUEST_IGNORE_BATTERY_OPTIMIZATIONS",
     "Ignore battery optimizations", false, true, nullptr, 0, nullptr, 0, nullptr, nullptr},
    {"exact-alarm", "android.permission.SCHEDULE_EXACT_ALARM", "Schedule exact alarms", false, true,
     nullptr, 0, nullptr, 0, nullptr, nullptr},
    {"write-settings", "android.permission.WRITE_SETTINGS", "Change system settings", false, true,
     nullptr, 0, nullptr, 0, nullptr, nullptr},
    {"full-screen-intent", "android.permission.USE_FULL_SCREEN_INTENT",
     "Show a full-screen intent", false, true, nullptr, 0, nullptr, 0, nullptr, nullptr},
};

inline const CatalogPermission* find_catalog_permission(const std::string& name) {
    for (const CatalogPermission& known : kPermissionCatalog) {
        if (name == known.name) {
            return &known;
        }
    }
    return nullptr;
}

inline std::string catalog_permission_names() {
    std::string out;
    std::string line = "  ";
    const std::size_t count = sizeof(kPermissionCatalog) / sizeof(kPermissionCatalog[0]);
    for (std::size_t i = 0; i < count; ++i) {
        std::string item = kPermissionCatalog[i].name;
        if (i + 1 != count) {
            item += ", ";
        }
        if (line.size() + item.size() > 78 && line.size() > 2) {
            out += line;
            out += '\n';
            line = "  ";
        }
        line += item;
    }
    out += line;
    return out;
}

// In-memory project. No filesystem and no prompts. Templates turn it into a
// tree; the writer only persists that tree.
// What an APK needs beyond the identity: the platform's own rules, not a second
// capability file. Everything here has a default for a plain app.
struct AndroidOptions {
    std::string target;  // an existing zui build target that produces libmain.so
    int min_sdk = 24;
    int target_sdk = 36;
    std::string orientation;  // sensor, portrait, landscape, or empty for the system default
    std::vector<std::string> abis;
    std::string keystore;  // project-relative .jks, or empty for a signing key of this build
    std::string key_alias;
    bool debuggable = false;
};

// What a Windows package puts in the executable's version resource and how the
// release is signed. The password never lives here: it comes from the
// environment.
struct WindowsOptions {
    std::string company;
    std::string copyright;
    std::string description;
    std::string certificate;   // a .pfx in the project, or the SHA-1 of a store certificate
    std::string timestamp_url; // defaults to a public timestamp service
};

// Apple packaging: what a .app needs beyond the identity, for macOS and iOS.
// Secrets (the notarization password) stay in the environment.
struct AppleOptions {
    std::string identity;       // the codesign identity, e.g. "Developer ID Application: ..."
    std::string category;       // LSApplicationCategoryType (macOS)
    std::string provisioning;   // iOS provisioning profile, project-relative
    std::string entitlements;   // entitlements plist, project-relative
    std::string notary_profile; // notarytool keychain profile (macOS)
    std::vector<std::string> orientations;  // iOS UIInterfaceOrientation names
    bool dmg = true;            // macOS: also write a .dmg when hdiutil is there
};

struct Project {
    std::string name;
    std::string version;
    std::string identifier;
    std::string icon;
    std::string page;
    std::string entry;
    std::string target;
    std::vector<std::string> targets;
    std::vector<std::string> files;
    std::vector<Permission> permissions;
    AndroidOptions android;
    WindowsOptions windows;
    AppleOptions apple;
};

inline constexpr const char* kDeclFile = "zui.json";

// Schema of the project file this tool writes. A project written before the
// key existed is schema 1; a newer schema is refused instead of read wrong.
inline constexpr int kSchemaVersion = 2;
inline constexpr const char* kSchemaKey = "schema";

bool valid_name(const std::string& name);
bool valid_version(const std::string& version);
bool valid_identifier(const std::string& identifier);
bool valid_rel_path(const std::string& path);
bool known_target(const std::string& id);
bool has_target(const Project& project, const std::string& id);

// Fills `android` and, when `reason` is empty, the catalog reason. Unknown
// names fail and name the permission.
bool resolve_permission(const std::string& name, const std::string& reason, Permission& out,
                        std::string& error);

// Schema only: identity, paths, targets, and the permission list. A listed
// android string that is not the catalog value for that name is contradictory.
bool valid_project(const Project& project, std::string& error);

std::string project_json(const Project& project);
bool parse_project(const std::string& json, Project& project, std::string& error);

int version_code(const std::string& version);

}  // namespace zui_tool
