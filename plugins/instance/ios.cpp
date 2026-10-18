#include "ext/slots.h"
#include "plugins/apple_objc.h"
#include "plugins/instance/endpoint.h"
#include "plugins/instance/unix_claim.h"

#include <string>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
#include <dlfcn.h>
#endif

namespace zui {
namespace {

#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE

extern "C" void zui_ios_instance_note(void*, void*, void*, void*, void*) {}

std::string container_path() {
    using HomeFn = id (*)();
    auto home_fn = reinterpret_cast<HomeFn>(dlsym(RTLD_DEFAULT, "NSHomeDirectory"));
    if (home_fn == nullptr) {
        return {};
    }
    id home = home_fn();
    if (home == nullptr) {
        return {};
    }
    const char* utf8 = apple_objc::msg<const char*>(home, apple_objc::sel("UTF8String"));
    return utf8 != nullptr ? std::string(utf8) : std::string();
}

void* darwin_center() {
    using GetCenter = void* (*)();
    auto get_center = reinterpret_cast<GetCenter>(dlsym(RTLD_DEFAULT, "CFNotificationCenterGetDarwinNotifyCenter"));
    return get_center != nullptr ? get_center() : nullptr;
}

void* cf_string(const std::string& text) {
    using MakeString = void* (*)(void*, const char*, unsigned);
    auto make = reinterpret_cast<MakeString>(dlsym(RTLD_DEFAULT, "CFStringCreateWithCString"));
    if (make == nullptr) {
        return nullptr;
    }
    return make(nullptr, text.c_str(), 0x08000100u);
}

void cf_release(void* value) {
    using Release = void (*)(void*);
    auto release = reinterpret_cast<Release>(dlsym(RTLD_DEFAULT, "CFRelease"));
    if (release != nullptr && value != nullptr) {
        release(value);
    }
}

void observe_note(const std::string& name) {
    using AddObserver = void (*)(void*, const void*, void (*)(void*, void*, void*, void*, void*), void*, const void*,
                                 int);
    auto add = reinterpret_cast<AddObserver>(dlsym(RTLD_DEFAULT, "CFNotificationCenterAddObserver"));
    void* center = darwin_center();
    void* cfname = cf_string(name);
    if (add == nullptr || center == nullptr || cfname == nullptr) {
        cf_release(cfname);
        return;
    }
    static int observer = 0;
    add(center, &observer, zui_ios_instance_note, cfname, nullptr, 4);
    cf_release(cfname);
}

void post_note(const std::string& name) {
    using Post = void (*)(void*, void*, const void*, void*, unsigned char);
    auto post = reinterpret_cast<Post>(dlsym(RTLD_DEFAULT, "CFNotificationCenterPostNotification"));
    void* center = darwin_center();
    void* cfname = cf_string(name);
    if (post == nullptr || center == nullptr || cfname == nullptr) {
        cf_release(cfname);
        return;
    }
    post(center, cfname, nullptr, nullptr, 1);
    cf_release(cfname);
}

class IosInstance final : public InstanceService {
public:
    bool claim(std::string_view identity) override {
        path_ = instance_detail::ios_socket_path(container_path(), identity);
        note_ = instance_detail::ios_notification_name(identity);
        const bool held = claim_.claim(path_);
        if (!held) {
            return false;
        }
        identity_.assign(identity.begin(), identity.end());
        observe_note(note_);
        return true;
    }

    bool signal_primary(std::string_view identity, std::string_view args) override {
        const std::string path = instance_detail::ios_socket_path(container_path(), identity);
        if (!claim_.signal(path, args)) {
            return false;
        }
        post_note(instance_detail::ios_notification_name(identity));
        return true;
    }

    std::string take_args(std::string_view identity) override {
        if (identity_.empty() || identity != identity_) {
            return {};
        }
        return claim_.take();
    }

private:
    instance_detail::UnixClaim claim_;
    std::string path_;
    std::string note_;
    std::string identity_;
};

#endif

}  // namespace

void register_ios_instance(ExtensionRegistry& registry) {
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
    registry.add_instance(std::make_unique<IosInstance>());
#else
    (void)registry;
#endif
}

}  // namespace zui
