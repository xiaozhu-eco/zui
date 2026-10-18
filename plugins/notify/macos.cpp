#include "backends/posix_platform_util.h"
#include "ext/slots.h"
#include "plugins/apple_objc.h"

#include <string>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace zui {
namespace {

#if defined(__APPLE__) && !(defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)

class MacNotify final : public NotifyService {
public:
    plugin::OsResult notify(std::string_view title, std::string_view body) override {
        plugin::OsResult result;
        const std::string t(title);
        const std::string b(body);
        id center = apple_objc::msg<id>(apple_objc::cls("NSUserNotificationCenter"),
                                        apple_objc::sel("defaultUserNotificationCenter"));
        id note = apple_objc::msg<id>(apple_objc::msg<id>(apple_objc::cls("NSUserNotification"), apple_objc::sel("alloc")),
                                      apple_objc::sel("init"));
        if (center != nullptr && note != nullptr) {
            apple_objc::msg<void>(note, apple_objc::sel("setTitle:"), apple_objc::ns_string_kept(t));
            apple_objc::msg<void>(note, apple_objc::sel("setInformativeText:"), apple_objc::ns_string_kept(b));
            apple_objc::msg<void>(center, apple_objc::sel("deliverNotification:"), note);
            result.ok = true;
            result.title = t;
            result.body = b;
            return result;
        }
        const std::string script = "display notification " + backends::posix_util::applescript_string(b) +
                                   " with title " + backends::posix_util::applescript_string(t);
        std::string out;
        int code = -1;
        result.ok = backends::posix_util::run_command_capture(
            "osascript -e " + backends::posix_util::shell_quote(script), out, code);
        if (result.ok) {
            result.title = t;
            result.body = b;
        }
        return result;
    }
};

#endif

}  // namespace

void register_macos_notify(ExtensionRegistry& registry) {
#if defined(__APPLE__) && !(defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    registry.add_notify(std::make_unique<MacNotify>());
#else
    (void)registry;
#endif
}

}  // namespace zui
