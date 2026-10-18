#include "ext/slots.h"
#include "plugins/hotkey/chord.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace zui {
namespace {

#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE

class IosHotkey final : public HotkeyService {
public:
    // iOS key commands are not global. Reporting success for a fixed chord would
    // ignore the caller. The chord is still encoded so the key is in the result.
    plugin::OsResult global_hotkey(const plugin::Hotkey& key, plugin::Handler) override {
        const hotkey_chord::Chord chord = hotkey_chord::encode(key);
        plugin::OsResult result;
        result.title = chord.label;
        if (!chord.ios_input && !chord.reason.empty()) {
            result.body = chord.reason;
            return result;
        }
        result.body = "iOS has no global hotkey";
        return result;
    }
};

#endif

}  // namespace

void register_ios_hotkey(ExtensionRegistry& registry) {
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
    registry.add_hotkey(std::make_unique<IosHotkey>());
#else
    (void)registry;
#endif
}

}  // namespace zui
