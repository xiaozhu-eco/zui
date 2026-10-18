#include "ext/slots.h"
#include "plugins/apple_objc.h"
#include "plugins/tray/item.h"

#include <string>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace zui {
namespace {

#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE

int g_badge = 0;
bool g_badge_set = false;

bool set_badge(long count) {
    id app = apple_objc::msg<id>(apple_objc::cls("UIApplication"), apple_objc::sel("sharedApplication"));
    if (app == nullptr) {
        return false;
    }
    apple_objc::msg<void>(app, apple_objc::sel("setApplicationIconBadgeNumber:"), count);
    return true;
}

class IosTray final : public TrayService {
public:
    plugin::OsResult item(std::string_view tip, plugin::Handler on_activate) override {
        plugin::OsResult result;
        const tray_detail::Item prepared = tray_detail::prepare(tip);
        result.title = prepared.tip;
        if (!prepared.ready) {
            result.body = prepared.reason;
            return result;
        }
        if (!on_activate) {
            result.body = "tray activate handler is empty";
            return result;
        }
        result.body = "iOS has no system tray";
        return result;
    }

    plugin::OsResult badge(int count) override {
        plugin::OsResult result;
        result.ok = set_badge(count);
        if (result.ok) {
            g_badge = count;
            g_badge_set = true;
            result.title = std::to_string(count);
        }
        return result;
    }

    int badge_count() const override { return g_badge_set ? g_badge : 0; }
};

#endif

}  // namespace

void register_ios_tray(ExtensionRegistry& registry) {
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
    registry.add_tray(std::make_unique<IosTray>());
#else
    (void)registry;
#endif
}

}  // namespace zui
