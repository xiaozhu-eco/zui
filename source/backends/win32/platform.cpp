#include "backends/platform.h"
#include "ext/slots.h"

#include <memory>

namespace zui {
namespace backends {
namespace {

class Win32Platform final : public IPlatform {
public:
    PickPaths pick_open(NativeWindowId owner, const FileDialogOptions& opt) override {
        return dialog_open(PlatformKind::Win32, owner, opt);
    }

    PickPath pick_save(NativeWindowId owner, const FileDialogOptions& opt) override {
        return dialog_save(PlatformKind::Win32, owner, opt);
    }

    PickPath pick_folder(NativeWindowId owner, const FileDialogOptions& opt) override {
        return dialog_folder(PlatformKind::Win32, owner, opt);
    }

    bool open_uri(std::string_view uri) override { return shell_open_uri(PlatformKind::Win32, uri); }

    std::string user_data_dir(std::string_view app_id) const override {
        return shell_user_data_dir(PlatformKind::Win32, app_id);
    }

    std::string cache_dir(std::string_view app_id) const override {
        return shell_cache_dir(PlatformKind::Win32, app_id);
    }
};

}  // namespace

std::unique_ptr<IPlatform> create_win32_platform() {
    return std::make_unique<Win32Platform>();
}

}  // namespace backends
}  // namespace zui
