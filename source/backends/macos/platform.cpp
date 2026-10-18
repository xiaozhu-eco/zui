#include "backends/platform.h"
#include "ext/slots.h"

#include <memory>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace zui {
namespace backends {
namespace {

#if defined(__APPLE__) && !(defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)

class MacOsPlatform final : public IPlatform {
public:
    PickPaths pick_open(NativeWindowId owner, const FileDialogOptions& opt) override {
        return dialog_open(PlatformKind::Macos, owner, opt);
    }

    PickPath pick_save(NativeWindowId owner, const FileDialogOptions& opt) override {
        return dialog_save(PlatformKind::Macos, owner, opt);
    }

    PickPath pick_folder(NativeWindowId owner, const FileDialogOptions& opt) override {
        return dialog_folder(PlatformKind::Macos, owner, opt);
    }

    bool open_uri(std::string_view uri) override { return shell_open_uri(PlatformKind::Macos, uri); }

    std::string user_data_dir(std::string_view app_id) const override {
        return shell_user_data_dir(PlatformKind::Macos, app_id);
    }

    std::string cache_dir(std::string_view app_id) const override {
        return shell_cache_dir(PlatformKind::Macos, app_id);
    }
};

#endif

}  // namespace

std::unique_ptr<IPlatform> create_macos_platform() {
#if defined(__APPLE__) && !(defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    return std::make_unique<MacOsPlatform>();
#else
    return create_noop_platform();
#endif
}

}  // namespace backends
}  // namespace zui
