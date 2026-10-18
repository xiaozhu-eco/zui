#include "backends/posix_platform_util.h"
#include "ext/slots.h"
#include "plugins/shell/layout.h"

#include <filesystem>
#include <string>

namespace zui {
namespace backends {
namespace {

#if defined(__linux__) && !defined(__ANDROID__)

namespace fs = std::filesystem;
using posix_util::ensure_dir;
using posix_util::env_or_empty;
using posix_util::run_command_capture;
using posix_util::shell_quote;
using posix_util::uri_scheme_allowed;

class LinuxShell final : public ShellService {
public:
    bool open_uri(std::string_view uri) override {
        if (!uri_scheme_allowed(uri)) {
            return false;
        }
        const std::string cmd = std::string("xdg-open ") + shell_quote(uri) + " >/dev/null 2>&1";
        int code = -1;
        std::string out;
        return run_command_capture(cmd, out, code);
    }

    std::string user_data_dir(std::string_view app_id) const override {
        shell_layout::Roots roots;
        roots.home = env_or_empty("HOME");
        roots.xdg_data_home = env_or_empty("XDG_DATA_HOME");
        return ensure_dir(fs::path(shell_layout::linux_user_data(roots, app_id)));
    }

    std::string cache_dir(std::string_view app_id) const override {
        shell_layout::Roots roots;
        roots.home = env_or_empty("HOME");
        roots.xdg_cache_home = env_or_empty("XDG_CACHE_HOME");
        return ensure_dir(fs::path(shell_layout::linux_cache(roots, app_id)));
    }
};

#endif

}  // namespace

void register_linux_shell(ExtensionRegistry& registry) {
#if defined(__linux__) && !defined(__ANDROID__)
    registry.add_shell(PlatformKind::Linux, std::make_unique<LinuxShell>());
#else
    (void)registry;
#endif
}

}  // namespace backends
}  // namespace zui
