#include "backends/platform.h"
#include "ext/slots.h"

#include <memory>

namespace zui {
namespace backends {
namespace {

#if defined(__linux__)

class LinuxPlatform final : public IPlatform {
public:
    PickPaths pick_open(NativeWindowId owner, const FileDialogOptions& opt) override {
        return dialog_open(PlatformKind::Linux, owner, opt);
    }

    PickPath pick_save(NativeWindowId owner, const FileDialogOptions& opt) override {
        return dialog_save(PlatformKind::Linux, owner, opt);
    }

    PickPath pick_folder(NativeWindowId owner, const FileDialogOptions& opt) override {
        return dialog_folder(PlatformKind::Linux, owner, opt);
    }

    bool open_uri(std::string_view uri) override { return shell_open_uri(PlatformKind::Linux, uri); }

    std::string user_data_dir(std::string_view app_id) const override {
        return shell_user_data_dir(PlatformKind::Linux, app_id);
    }

    std::string cache_dir(std::string_view app_id) const override {
        return shell_cache_dir(PlatformKind::Linux, app_id);
    }
};

#endif

}  // namespace

std::unique_ptr<IPlatform> create_linux_platform() {
#if defined(__linux__)
    return std::make_unique<LinuxPlatform>();
#else
    return create_noop_platform();
#endif
}

}  // namespace backends
}  // namespace zui
