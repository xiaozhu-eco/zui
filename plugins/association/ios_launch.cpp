#include "plugins/association/ios_launch.h"

#include "plugins/apple_objc.h"

#include <string>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace zui {
namespace association_ios {

plugin::OpenerReport IosAssociationLaunch::launch(std::string_view executable, std::string_view file_path) {
    plugin::OpenerReport report;
    if (executable.empty() || file_path.empty()) {
        report.ran = true;
        return report;
    }
    report.executable = std::string(executable);
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
    report.ran = true;
    const std::string file(file_path);
    id application = apple_objc::msg<id>(apple_objc::cls("UIApplication"), apple_objc::sel("sharedApplication"));
    id url = apple_objc::msg<id>(apple_objc::cls("NSURL"), apple_objc::sel("fileURLWithPath:"),
                                 apple_objc::ns_string_kept(file));
    if (application == nullptr || url == nullptr) {
        return report;
    }
    report.ok = apple_objc::msg<bool>(application, apple_objc::sel("openURL:"), url);
#endif
    return report;
}

}  // namespace association_ios
}  // namespace zui
