#include "backends/posix_platform_util.h"
#include "ext/slots.h"
#include "plugins/apple_objc.h"
#include "plugins/shell/layout.h"

#include <filesystem>
#include <string>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
#include <dlfcn.h>
#endif

namespace zui {
namespace backends {
namespace {

namespace fs = std::filesystem;
using posix_util::effective_app_id;
using posix_util::ensure_dir;
using posix_util::uri_scheme_allowed;

#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE

std::string container_or_home() {
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

bool open_with_uikit(std::string_view uri) {
    const std::string owned(uri);
    id application = apple_objc::msg<id>(apple_objc::cls("UIApplication"), apple_objc::sel("sharedApplication"));
    if (application == nullptr) {
        return false;
    }
    id url = apple_objc::msg<id>(apple_objc::cls("NSURL"), apple_objc::sel("URLWithString:"),
                                 apple_objc::ns_string_kept(owned));
    if (url == nullptr) {
        return false;
    }
    return apple_objc::msg<bool>(application, apple_objc::sel("openURL:"), url);
}

#endif

class IosShell final : public ShellService {
public:
    bool open_uri(std::string_view uri) override {
        if (!uri_scheme_allowed(uri)) {
            return false;
        }
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
        return open_with_uikit(uri);
#else
        return false;
#endif
    }

    std::string user_data_dir(std::string_view app_id) const override {
        const std::string id = effective_app_id(app_id);
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
        shell_layout::Roots roots;
        roots.ios_container = container_or_home();
        return ensure_dir(fs::path(shell_layout::ios_user_data(roots, id)));
#else
        return ensure_dir(fs::temp_directory_path() / shell_layout::ios_desktop_data_leaf() / id);
#endif
    }

    std::string cache_dir(std::string_view app_id) const override {
        const std::string id = effective_app_id(app_id);
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
        shell_layout::Roots roots;
        roots.ios_container = container_or_home();
        return ensure_dir(fs::path(shell_layout::ios_cache(roots, id)));
#else
        return ensure_dir(fs::temp_directory_path() / shell_layout::ios_desktop_cache_leaf() / id);
#endif
    }
};

}  // namespace

void register_ios_shell(ExtensionRegistry& registry) {
    registry.add_shell(PlatformKind::Ios, std::make_unique<IosShell>());
}

}  // namespace backends
}  // namespace zui
