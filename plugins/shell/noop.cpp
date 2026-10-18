#include "backends/posix_platform_util.h"
#include "ext/slots.h"

#include <filesystem>
#include <string>

namespace zui {
namespace backends {
namespace {

namespace fs = std::filesystem;
using posix_util::effective_app_id;
using posix_util::ensure_dir;
using posix_util::env_or_empty;

class NoopShell final : public ShellService {
public:
    bool open_uri(std::string_view) override { return false; }

    std::string user_data_dir(std::string_view app_id) const override {
        return ensure_dir(root_for("zui-test-data") / effective_app_id(app_id));
    }

    std::string cache_dir(std::string_view app_id) const override {
        return ensure_dir(root_for("zui-test-cache") / effective_app_id(app_id));
    }

private:
    /// `ZUI_TEST_DATA_DIR` moves both roots under one directory, so a test can keep a profile of
    /// its own: without it every case of every test binary shares `%TEMP%/zui-test-data`, and a
    /// case that leaves `settings.txt` (or `notes/`) behind decides the next case's defaults.
    static fs::path root_for(const char* fallback) {
        const std::string override = env_or_empty("ZUI_TEST_DATA_DIR");
        return override.empty() ? fs::temp_directory_path() / fallback
                                : fs::path(override) / fallback;
    }
};

}  // namespace

void register_noop_shell(ExtensionRegistry& registry) {
    registry.add_shell(PlatformKind::Noop, std::make_unique<NoopShell>());
}

}  // namespace backends
}  // namespace zui
