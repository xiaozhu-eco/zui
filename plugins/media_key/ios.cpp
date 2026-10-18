#include "ext/slots.h"
#include "plugins/apple_objc.h"

#include <string>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace zui {
namespace {

#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE

plugin::Handler g_play;
plugin::Handler g_next;
plugin::Handler g_prev;

void on_play(id, SEL) {
    if (g_play) {
        g_play();
    }
}
void on_next(id, SEL) {
    if (g_next) {
        g_next();
    }
}
void on_prev(id, SEL) {
    if (g_prev) {
        g_prev();
    }
}

Class remote_target_class() {
    Class existing = reinterpret_cast<Class>(apple_objc::cls("ZuiIosRemote"));
    if (existing != nullptr) {
        return existing;
    }
    Class pair = objc_allocateClassPair(reinterpret_cast<Class>(apple_objc::cls("NSObject")), "ZuiIosRemote", 0);
    if (pair == nullptr) {
        return nullptr;
    }
    class_addMethod(pair, apple_objc::sel("onPlay"), reinterpret_cast<IMP>(on_play), "v@:");
    class_addMethod(pair, apple_objc::sel("onNext"), reinterpret_cast<IMP>(on_next), "v@:");
    class_addMethod(pair, apple_objc::sel("onPrev"), reinterpret_cast<IMP>(on_prev), "v@:");
    objc_registerClassPair(pair);
    return pair;
}

class IosMedia final : public MediaKeyService {
public:
    plugin::OsResult media_key(plugin::MediaKey action, plugin::Handler on_fire) override {
        const std::string_view which = plugin::media_key_name(action);
        plugin::OsResult result;
        if (which != "play" && which != "next" && which != "previous") {
            result.body = "media key is not play, next, or previous";
            return result;
        }
        if (!on_fire) {
            result.title.assign(which.begin(), which.end());
            result.body = "media-key handler is empty";
            return result;
        }
        const char* command = nullptr;
        if (which == "play") {
            g_play = std::move(on_fire);
            command = "playCommand";
        } else if (which == "next") {
            g_next = std::move(on_fire);
            command = "nextTrackCommand";
        } else if (which == "previous") {
            g_prev = std::move(on_fire);
            command = "previousTrackCommand";
        }
        Class target_cls = remote_target_class();
        id target = nullptr;
        if (target_cls != nullptr) {
            target = apple_objc::msg<id>(apple_objc::msg<id>(reinterpret_cast<id>(target_cls), apple_objc::sel("alloc")),
                                        apple_objc::sel("init"));
        }
        id center = apple_objc::msg<id>(apple_objc::cls("MPRemoteCommandCenter"), apple_objc::sel("sharedCommandCenter"));
        id remote = apple_objc::msg<id>(center, apple_objc::sel(command));
        apple_objc::msg<void>(remote, apple_objc::sel("setEnabled:"), true);
        const char* action = which == "play" ? "onPlay" : which == "next" ? "onNext" : "onPrev";
        apple_objc::msg<void>(remote, apple_objc::sel("addTarget:action:"), target, apple_objc::sel(action));
        result.title.assign(which.begin(), which.end());
        result.ok = center != nullptr && remote != nullptr && target != nullptr;
        if (!result.ok) {
            result.body = "MPRemoteCommandCenter is not available";
        }
        return result;
    }
};

#endif

}  // namespace

void register_ios_media_key(ExtensionRegistry& registry) {
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
    registry.add_media_key(std::make_unique<IosMedia>());
#else
    (void)registry;
#endif
}

}  // namespace zui
