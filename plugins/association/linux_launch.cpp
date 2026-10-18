#include "plugins/association/linux_launch.h"

#include <string>

#if defined(__linux__) && !defined(__ANDROID__)
#include <spawn.h>
extern char** environ;
#endif

namespace zui {
namespace association_linux {

plugin::OpenerReport LinuxAssociationLaunch::launch(std::string_view executable, std::string_view file_path) {
    plugin::OpenerReport report;
    if (executable.empty() || file_path.empty()) {
        report.ran = true;
        return report;
    }
    report.executable = std::string(executable);
#if defined(__linux__) && !defined(__ANDROID__)
    report.ran = true;
    std::string exe(executable);
    std::string path(file_path);
    char* argv[] = {exe.data(), path.data(), nullptr};
    pid_t pid = 0;
    const int rc = posix_spawn(&pid, exe.c_str(), nullptr, nullptr, argv, environ);
    if (rc != 0) {
        report.os_error = static_cast<unsigned long>(rc);
        return report;
    }
    report.ok = true;
#endif
    return report;
}

}  // namespace association_linux
}  // namespace zui
