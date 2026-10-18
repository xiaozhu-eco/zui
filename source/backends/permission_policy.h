#ifndef ZUI_BACKENDS_PERMISSION_POLICY_H
#define ZUI_BACKENDS_PERMISSION_POLICY_H

// Ask routing for the closed permission catalog. Android and iOS hosts follow
// these functions; a Windows test calls the same ones with plain inputs.

#include <zui/export.h>
#include <zui/host_types.h>

#include <string_view>

namespace zui {
namespace backends {

enum class PermissionAskKind { Immediate, Dialog, Settings };

struct PermissionAsk {
    PermissionAskKind kind = PermissionAskKind::Immediate;
    PermissionState state = PermissionState::Unsupported;
};

/// @p declared is whether the author list contains @p name. @p api_level is the
/// Android SDK. @p current is the state observed before this ask.
ZUI_EXPORT PermissionAsk permission_ask(std::string_view name, bool declared, int api_level,
                                        PermissionState current);

/// The authorization value an iOS framework reported, before it is mapped onto
/// PermissionState. Names with no durable status stay NotDetermined.
enum class IosAuth : int {
    NotDetermined = 0,
    Authorized = 1,
    Denied = 2,
    Restricted = 3,
    Limited = 4,
    WhenInUse = 5,
    /// EventKit write-only (iOS 17). Enough for calendar-write, not for calendar-read.
    WriteOnly = 6,
};

struct IosPermissionFacts {
    bool declared = false;
    IosAuth status = IosAuth::NotDetermined;
    bool hardware = false;
    /// Biometric evaluation has succeeded. Hardware availability alone is not this.
    bool evaluated = false;
};

struct IosPermissionChoice {
    PermissionState state = PermissionState::Unsupported;
    /// Start the system prompt (dialog or NFC / local-network sheet).
    bool prompt = false;
    /// Start a biometric evaluation. A grant is reported only after it succeeds.
    bool evaluate = false;
};

ZUI_EXPORT IosPermissionChoice ios_permission_choice(std::string_view name,
                                                    const IosPermissionFacts& facts, bool requesting);

/// True when @p name is its own token in the author list (`ZuiDeclaredPermissions`).
/// A shared usage-description key does not declare a name that is absent from the list.
ZUI_EXPORT bool permission_author_declared(std::string_view declared_list, std::string_view name);

}  // namespace backends
}  // namespace zui

#endif
