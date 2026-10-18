#include "backends/platform.h"
#include "ext/slots.h"

#include <memory>
#include <string_view>
#include <utility>

namespace zui {
namespace backends {
namespace {

PickPaths g_next_pick_open{};
bool g_has_next_pick_open = false;
PickPath g_next_pick_save{};
bool g_has_next_pick_save = false;

class NoopPlatform final : public IPlatform {
public:
    PickPaths pick_open(NativeWindowId, const FileDialogOptions&) override {
        if (g_has_next_pick_open) {
            g_has_next_pick_open = false;
            return std::move(g_next_pick_open);
        }
        return {PickStatus::Unsupported, {}};
    }

    PickPath pick_save(NativeWindowId, const FileDialogOptions&) override {
        if (g_has_next_pick_save) {
            g_has_next_pick_save = false;
            return std::move(g_next_pick_save);
        }
        return {PickStatus::Unsupported, {}};
    }

    PickPath pick_folder(NativeWindowId, const FileDialogOptions&) override {
        return {PickStatus::Unsupported, {}};
    }

    bool open_uri(std::string_view uri) override { return shell_open_uri(PlatformKind::Noop, uri); }

    std::string user_data_dir(std::string_view app_id) const override {
        return shell_user_data_dir(PlatformKind::Noop, app_id);
    }

    std::string cache_dir(std::string_view app_id) const override {
        return shell_cache_dir(PlatformKind::Noop, app_id);
    }
};

}  // namespace

void noop_set_next_pick_open(PickPaths next) {
    g_next_pick_open = std::move(next);
    g_has_next_pick_open = true;
}

void noop_set_next_pick_save(PickPath next) {
    g_next_pick_save = std::move(next);
    g_has_next_pick_save = true;
}

std::unique_ptr<IPlatform> create_noop_platform() {
    return std::make_unique<NoopPlatform>();
}

}  // namespace backends
}  // namespace zui
