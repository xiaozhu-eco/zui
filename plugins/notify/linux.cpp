#include "backends/posix_platform_util.h"
#include "ext/slots.h"

#include <string>

namespace zui {
namespace {

#if defined(__linux__) && !defined(__ANDROID__)

class LinuxNotify final : public NotifyService {
public:
    plugin::OsResult notify(std::string_view title, std::string_view body) override {
        plugin::OsResult result;
        const std::string t(title);
        const std::string b(body);
        std::string out;
        int code = -1;
        std::string cmd = "notify-send " + backends::posix_util::shell_quote(t) + " " +
                          backends::posix_util::shell_quote(b) + " >/dev/null 2>&1";
        bool ok = backends::posix_util::run_command_capture(cmd, out, code);
        if (!ok) {
            cmd = "gdbus call --session --dest org.freedesktop.Notifications "
                  "--object-path /org/freedesktop/Notifications "
                  "--method org.freedesktop.Notifications.Notify zui 0 \"\" " +
                  backends::posix_util::shell_quote(t) + " " + backends::posix_util::shell_quote(b) +
                  " \"[]\" \"{}\" 5000";
            ok = backends::posix_util::run_command_capture(cmd, out, code);
        }
        result.ok = ok;
        if (ok) {
            result.title = t;
            result.body = b;
        } else {
            result.os_error = static_cast<unsigned long>(code < 0 ? 0 : code);
        }
        return result;
    }
};

#endif

}  // namespace

void register_linux_notify(ExtensionRegistry& registry) {
#if defined(__linux__) && !defined(__ANDROID__)
    registry.add_notify(std::make_unique<LinuxNotify>());
#else
    (void)registry;
#endif
}

}  // namespace zui
