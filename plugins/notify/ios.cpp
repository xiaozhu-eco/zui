#include "ext/slots.h"
#include "plugins/apple_objc.h"

#include <string>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace zui {
namespace {

#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE

class IosNotify final : public NotifyService {
public:
    plugin::OsResult notify(std::string_view title, std::string_view body) override {
        plugin::OsResult result;
        const std::string t(title);
        const std::string b(body);
        id center = apple_objc::msg<id>(apple_objc::cls("UNUserNotificationCenter"),
                                        apple_objc::sel("currentNotificationCenter"));
        id content = apple_objc::msg<id>(
            apple_objc::msg<id>(apple_objc::cls("UNMutableNotificationContent"), apple_objc::sel("alloc")),
            apple_objc::sel("init"));
        if (center == nullptr || content == nullptr) {
            return result;
        }
        apple_objc::msg<void>(content, apple_objc::sel("setTitle:"), apple_objc::ns_string_kept(t));
        apple_objc::msg<void>(content, apple_objc::sel("setBody:"), apple_objc::ns_string_kept(b));
        id trigger = apple_objc::msg<id>(apple_objc::cls("UNTimeIntervalNotificationTrigger"),
                                         apple_objc::sel("triggerWithTimeInterval:repeats:"), 0.1, false);
        const std::string ident = "zui";
        id request = apple_objc::msg<id>(
            apple_objc::cls("UNNotificationRequest"), apple_objc::sel("requestWithIdentifier:content:trigger:"),
            apple_objc::ns_string_kept(ident), content, trigger);
        apple_objc::msg<void>(center, apple_objc::sel("addNotificationRequest:withCompletionHandler:"), request, nullptr);
        result.ok = request != nullptr;
        if (result.ok) {
            result.title = t;
            result.body = b;
        }
        return result;
    }
};

#endif

}  // namespace

void register_ios_notify(ExtensionRegistry& registry) {
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
    registry.add_notify(std::make_unique<IosNotify>());
#else
    (void)registry;
#endif
}

}  // namespace zui
