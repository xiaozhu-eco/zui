#include "ext/slots.h"
#include "plugins/apple_objc.h"
#include "plugins/tray/item.h"

#include <string>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace zui {
namespace {

#if defined(__APPLE__) && !(defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)

int g_badge = 0;
bool g_badge_set = false;
plugin::Handler g_activate;
id g_status_item = nullptr;
id g_status_target = nullptr;

void on_status_activate(id, SEL) {
    if (g_activate) {
        g_activate();
    }
}

Class status_target_class() {
    Class existing = reinterpret_cast<Class>(apple_objc::cls("ZuiStatusTarget"));
    if (existing != nullptr) {
        return existing;
    }
    Class pair = objc_allocateClassPair(reinterpret_cast<Class>(apple_objc::cls("NSObject")), "ZuiStatusTarget", 0);
    if (pair == nullptr) {
        return nullptr;
    }
    class_addMethod(pair, apple_objc::sel("onActivate"), reinterpret_cast<IMP>(on_status_activate), "v@:");
    objc_registerClassPair(pair);
    return pair;
}

class MacTray final : public TrayService {
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
        g_activate = std::move(on_activate);
        id bar = apple_objc::msg<id>(apple_objc::cls("NSStatusBar"), apple_objc::sel("systemStatusBar"));
        if (bar == nullptr) {
            result.body = "NSStatusBar is not available";
            return result;
        }
        if (g_status_item == nullptr) {
            id created = apple_objc::msg<id>(bar, apple_objc::sel("statusItemWithLength:"), static_cast<double>(-1));
            if (created == nullptr) {
                result.body = "status item was not created";
                return result;
            }
            g_status_item = apple_objc::msg<id>(created, apple_objc::sel("retain"));
        }
        Class target_cls = status_target_class();
        if (g_status_target == nullptr && target_cls != nullptr) {
            id created = apple_objc::msg<id>(apple_objc::msg<id>(reinterpret_cast<id>(target_cls), apple_objc::sel("alloc")),
                                            apple_objc::sel("init"));
            if (created != nullptr) {
                g_status_target = apple_objc::msg<id>(created, apple_objc::sel("retain"));
            }
        }
        id button = apple_objc::msg<id>(g_status_item, apple_objc::sel("button"));
        id control = button != nullptr ? button : g_status_item;
        apple_objc::msg<void>(control, apple_objc::sel("setTitle:"), apple_objc::ns_string_kept(prepared.tip));
        if (g_status_target == nullptr) {
            result.body = "status item target was not created";
            return result;
        }
        apple_objc::msg<void>(control, apple_objc::sel("setTarget:"), g_status_target);
        apple_objc::msg<void>(control, apple_objc::sel("setAction:"), apple_objc::sel("onActivate"));
        result.ok = true;
        return result;
    }

    plugin::OsResult badge(int count) override {
        plugin::OsResult result;
        id app = apple_objc::msg<id>(apple_objc::cls("NSApplication"), apple_objc::sel("sharedApplication"));
        id tile = apple_objc::msg<id>(app, apple_objc::sel("dockTile"));
        if (tile == nullptr) {
            result.body = "dock tile is not available";
            return result;
        }
        const std::string label = count > 0 ? std::to_string(count) : std::string();
        apple_objc::msg<void>(tile, apple_objc::sel("setBadgeLabel:"), apple_objc::ns_string_kept(label));
        g_badge = count;
        g_badge_set = true;
        result.ok = true;
        result.title = std::to_string(count);
        return result;
    }

    int badge_count() const override { return g_badge_set ? g_badge : 0; }
};

#endif

}  // namespace

void register_macos_tray(ExtensionRegistry& registry) {
#if defined(__APPLE__) && !(defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    registry.add_tray(std::make_unique<MacTray>());
#else
    (void)registry;
#endif
}

}  // namespace zui
