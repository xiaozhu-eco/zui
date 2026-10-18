#include "plugins/association/macos_launch.h"

#include "plugins/apple_objc.h"

#include <string>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(__APPLE__) && !(defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
#include <spawn.h>
extern char** environ;
#endif

namespace zui {
namespace association_macos {
namespace {

bool ends_with_app(std::string_view path) {
    return path.size() >= 4 && (path.compare(path.size() - 4, 4, ".app") == 0 ||
                                path.compare(path.size() - 4, 4, ".APP") == 0);
}

}  // namespace

plugin::OpenerReport MacAssociationLaunch::launch(std::string_view executable, std::string_view file_path) {
    plugin::OpenerReport report;
    if (executable.empty() || file_path.empty()) {
        report.ran = true;
        return report;
    }
    report.executable = std::string(executable);
#if defined(__APPLE__) && !(defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    report.ran = true;
    if (ends_with_app(executable)) {
        const std::string exe(executable);
        const std::string file(file_path);
        id workspace = apple_objc::msg<id>(apple_objc::cls("NSWorkspace"), apple_objc::sel("sharedWorkspace"));
        id app_url = apple_objc::msg<id>(apple_objc::cls("NSURL"), apple_objc::sel("fileURLWithPath:"),
                                         apple_objc::ns_string_kept(exe));
        id file_url = apple_objc::msg<id>(apple_objc::cls("NSURL"), apple_objc::sel("fileURLWithPath:"),
                                          apple_objc::ns_string_kept(file));
        id config = apple_objc::msg<id>(apple_objc::msg<id>(apple_objc::cls("NSWorkspaceOpenConfiguration"),
                                                           apple_objc::sel("alloc")),
                                        apple_objc::sel("init"));
        if (workspace != nullptr && app_url != nullptr) {
            apple_objc::msg<void>(workspace,
                                  apple_objc::sel("openURLs:withApplicationAtURL:configuration:completionHandler:"),
                                  file_url, app_url, config, nullptr);
            report.ok = true;
            return report;
        }
    }
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

}  // namespace association_macos
}  // namespace zui
