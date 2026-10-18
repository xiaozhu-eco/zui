#include "backends/permission_policy.h"

namespace zui {
namespace backends {
namespace {

struct Row {
    const char* name;
    bool runtime;
    bool settings;
    int floor_api;
    PermissionState below_floor;
    bool ios_usage;
    bool ios_notify;
    bool ios_biometric;
    bool ios_prompt_only;
    bool ios_activity;
};

// Same closed catalog as tools/common/project.h: runtime, settings, the API
// floor, and whether iOS has a usage-description key.
constexpr Row kRows[] = {
    {"network", false, false, 0, PermissionState::Unsupported, false, false, false, false, false},
    {"network-state", false, false, 0, PermissionState::Unsupported, false, false, false, false, false},
    {"wifi-state", false, false, 0, PermissionState::Unsupported, false, false, false, false, false},
    {"change-wifi", false, false, 0, PermissionState::Unsupported, false, false, false, false, false},
    {"multicast", false, false, 0, PermissionState::Unsupported, false, false, false, false, false},
    {"vibrate", false, false, 0, PermissionState::Unsupported, false, false, false, false, false},
    {"nfc", false, false, 0, PermissionState::Unsupported, true, false, false, true, false},
    {"wake-lock", false, false, 0, PermissionState::Unsupported, false, false, false, false, false},
    {"boot-completed", false, false, 0, PermissionState::Unsupported, false, false, false, false, false},
    {"biometric", false, false, 0, PermissionState::Unsupported, true, false, true, false, false},
    {"modify-audio", false, false, 0, PermissionState::Unsupported, false, false, false, false, false},
    {"foreground-service", false, false, 0, PermissionState::Unsupported, false, false, false, false, false},
    {"foreground-camera", false, false, 0, PermissionState::Unsupported, false, false, false, false, false},
    {"foreground-microphone", false, false, 0, PermissionState::Unsupported, false, false, false, false, false},
    {"foreground-location", false, false, 0, PermissionState::Unsupported, false, false, false, false, false},
    {"foreground-playback", false, false, 0, PermissionState::Unsupported, false, false, false, false, false},
    {"foreground-sync", false, false, 0, PermissionState::Unsupported, false, false, false, false, false},
    {"foreground-connected", false, false, 0, PermissionState::Unsupported, false, false, false, false, false},
    {"high-sampling-rate", false, false, 0, PermissionState::Unsupported, false, false, false, false, false},
    {"detect-screen-capture", false, false, 0, PermissionState::Unsupported, false, false, false, false, false},
    {"camera", true, false, 0, PermissionState::Unsupported, true, false, false, false, false},
    {"microphone", true, false, 0, PermissionState::Unsupported, true, false, false, false, false},
    {"location", true, false, 0, PermissionState::Unsupported, true, false, false, false, false},
    {"coarse-location", true, false, 0, PermissionState::Unsupported, true, false, false, false, false},
    {"location-background", true, false, 0, PermissionState::Unsupported, true, false, false, false, false},
    {"storage", true, false, 0, PermissionState::Unsupported, false, false, false, false, false},
    {"storage-write", true, false, 0, PermissionState::Unsupported, true, false, false, false, false},
    {"media-images", true, false, 0, PermissionState::Unsupported, true, false, false, false, false},
    {"media-video", true, false, 0, PermissionState::Unsupported, true, false, false, false, false},
    {"media-audio", true, false, 0, PermissionState::Unsupported, true, false, false, false, false},
    {"media-visual-selected", true, false, 0, PermissionState::Unsupported, true, false, false, false, false},
    {"media-location", true, false, 0, PermissionState::Unsupported, false, false, false, false, false},
    {"notifications", true, false, 0, PermissionState::Unsupported, false, true, false, false, false},
    {"bluetooth-scan", true, false, 0, PermissionState::Unsupported, true, false, false, false, false},
    {"bluetooth-connect", true, false, 0, PermissionState::Unsupported, true, false, false, false, false},
    {"bluetooth-advertise", true, false, 0, PermissionState::Unsupported, true, false, false, false, false},
    {"nearby-wifi", true, false, 0, PermissionState::Unsupported, true, false, false, true, false},
    {"contacts-read", true, false, 0, PermissionState::Unsupported, true, false, false, false, false},
    {"contacts-write", true, false, 0, PermissionState::Unsupported, true, false, false, false, false},
    {"accounts", true, false, 0, PermissionState::Unsupported, false, false, false, false, false},
    {"calendar-read", true, false, 0, PermissionState::Unsupported, true, false, false, false, false},
    {"calendar-write", true, false, 0, PermissionState::Unsupported, true, false, false, false, false},
    {"phone-call", true, false, 0, PermissionState::Unsupported, false, false, false, false, false},
    {"phone-state", true, false, 0, PermissionState::Unsupported, false, false, false, false, false},
    {"phone-numbers", true, false, 0, PermissionState::Unsupported, false, false, false, false, false},
    {"call-log-read", true, false, 0, PermissionState::Unsupported, false, false, false, false, false},
    {"call-log-write", true, false, 0, PermissionState::Unsupported, false, false, false, false, false},
    {"answer-calls", true, false, 0, PermissionState::Unsupported, false, false, false, false, false},
    {"sms-send", true, false, 0, PermissionState::Unsupported, false, false, false, false, false},
    {"sms-receive", true, false, 0, PermissionState::Unsupported, false, false, false, false, false},
    {"sms-read", true, false, 0, PermissionState::Unsupported, false, false, false, false, false},
    {"body-sensors", true, false, 0, PermissionState::Unsupported, false, false, false, false, false},
    {"body-sensors-background", true, false, 0, PermissionState::Unsupported, false, false, false, false, false},
    {"activity-recognition", true, false, 0, PermissionState::Unsupported, true, false, false, false, true},
    {"manage-storage", false, true, 30, PermissionState::Unsupported, false, false, false, false, false},
    {"overlay", false, true, 0, PermissionState::Unsupported, false, false, false, false, false},
    {"install-packages", false, true, 26, PermissionState::Granted, false, false, false, false, false},
    {"ignore-battery", false, true, 0, PermissionState::Unsupported, false, false, false, false, false},
    {"exact-alarm", false, true, 31, PermissionState::Granted, false, false, false, false, false},
    {"write-settings", false, true, 0, PermissionState::Unsupported, false, false, false, false, false},
    {"full-screen-intent", false, true, 34, PermissionState::Granted, false, false, false, false, false},
};

const Row* find_row(std::string_view name) {
    for (const Row& row : kRows) {
        if (name == row.name) {
            return &row;
        }
    }
    return nullptr;
}

PermissionAsk immediate(PermissionState state) {
    PermissionAsk out;
    out.kind = PermissionAskKind::Immediate;
    out.state = state;
    return out;
}

IosPermissionChoice ios_done(PermissionState state) {
    IosPermissionChoice out;
    out.state = state;
    return out;
}

PermissionState ios_standard_state(std::string_view name, IosAuth status) {
    if (status == IosAuth::Authorized) {
        return PermissionState::Granted;
    }
    if (status == IosAuth::WhenInUse) {
        return name == "location-background" ? PermissionState::Denied : PermissionState::Granted;
    }
    if (status == IosAuth::Limited) {
        return name == "media-visual-selected" ? PermissionState::Granted : PermissionState::Denied;
    }
    if (status == IosAuth::Denied || status == IosAuth::Restricted) {
        return PermissionState::Blocked;
    }
    return PermissionState::NotRequested;
}

}  // namespace

PermissionAsk permission_ask(std::string_view name, bool declared, int api_level, PermissionState current) {
    if (!declared) {
        return immediate(PermissionState::Unsupported);
    }
    const Row* row = find_row(name);
    if (row == nullptr) {
        return immediate(PermissionState::Unsupported);
    }
    if (row->floor_api > 0 && api_level < row->floor_api) {
        return immediate(row->below_floor);
    }
    if (current == PermissionState::Granted || current == PermissionState::Unsupported) {
        return immediate(current);
    }
    if (row->settings && current != PermissionState::Granted) {
        PermissionAsk out;
        out.kind = PermissionAskKind::Settings;
        out.state = current;
        return out;
    }
    if (current == PermissionState::Blocked) {
        PermissionAsk out;
        out.kind = PermissionAskKind::Settings;
        out.state = current;
        return out;
    }
    if (row->runtime &&
        (current == PermissionState::NotRequested || current == PermissionState::Denied)) {
        PermissionAsk out;
        out.kind = PermissionAskKind::Dialog;
        out.state = current;
        return out;
    }
    if (!row->runtime && !row->settings) {
        return immediate(PermissionState::Granted);
    }
    return immediate(current);
}

IosPermissionChoice ios_permission_choice(std::string_view name, const IosPermissionFacts& facts,
                                         bool requesting) {
    const Row* row = find_row(name);
    if (row == nullptr || (!row->ios_usage && !row->ios_notify)) {
        return ios_done(PermissionState::Unsupported);
    }
    if (!facts.declared) {
        return ios_done(PermissionState::Unsupported);
    }
    if (row->ios_biometric) {
        // A finished evaluation wins over canEvaluatePolicy. Lockout makes that
        // call return false, and treating it as "no hardware" would report
        // Unsupported instead of Blocked.
        if (facts.evaluated || facts.status == IosAuth::Authorized) {
            return ios_done(PermissionState::Granted);
        }
        if (facts.status == IosAuth::Restricted) {
            return ios_done(PermissionState::Blocked);
        }
        if (facts.status == IosAuth::Denied) {
            IosPermissionChoice out;
            out.state = PermissionState::Denied;
            out.evaluate = requesting;
            return out;
        }
        if (!facts.hardware) {
            return ios_done(PermissionState::Unsupported);
        }
        IosPermissionChoice out;
        out.state = PermissionState::NotRequested;
        out.evaluate = requesting;
        return out;
    }
    if (row->ios_prompt_only) {
        // NFC and local network have no durable grant. A finished prompt is a
        // denial that can be asked again; it does not stay NotRequested, and it
        // is not reported as Granted.
        IosPermissionChoice out;
        if (facts.status == IosAuth::Restricted) {
            out.state = PermissionState::Blocked;
        } else if (facts.status == IosAuth::Denied) {
            out.state = PermissionState::Denied;
            out.prompt = requesting;
        } else {
            out.state = PermissionState::NotRequested;
            out.prompt = requesting;
        }
        return out;
    }
    if (row->ios_activity && !facts.hardware) {
        return ios_done(PermissionState::Unsupported);
    }
    if ((name == "calendar-read" || name == "calendar-write") && facts.status == IosAuth::WriteOnly) {
        IosPermissionChoice out;
        if (name == "calendar-write") {
            out.state = PermissionState::Granted;
        } else {
            out.state = PermissionState::Denied;
            out.prompt = requesting;
        }
        return out;
    }
    IosPermissionChoice out;
    out.state = ios_standard_state(name, facts.status);
    out.prompt = requesting && out.state == PermissionState::NotRequested;
    return out;
}

bool permission_author_declared(std::string_view declared_list, std::string_view name) {
    if (name.empty()) {
        return false;
    }
    std::size_t at = 0;
    while (at < declared_list.size()) {
        const std::size_t end = declared_list.find(' ', at);
        const std::size_t stop = end == std::string_view::npos ? declared_list.size() : end;
        if (declared_list.substr(at, stop - at) == name) {
            return true;
        }
        if (end == std::string_view::npos) {
            break;
        }
        at = end + 1;
    }
    return false;
}

}  // namespace backends
}  // namespace zui
