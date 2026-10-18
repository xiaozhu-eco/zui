#include "plugins/association/macos_query.h"

#include "plugins/apple_objc.h"
#include "plugins/association/record.h"

#include <string>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace zui {
namespace association_macos {

plugin::OpenerReport MacAssociationQuery::query(std::string_view type) {
    plugin::OpenerReport report;
#if defined(__APPLE__) && !(defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    report.ran = true;
    const association_detail::Classified kind = association_detail::classify(type);
    std::string url_text;
    if (kind.kind == association_detail::TypeKind::Scheme) {
        url_text = kind.value + "://open";
        report.scheme = kind.value;
    } else if (kind.kind == association_detail::TypeKind::Extension) {
        url_text = "file:///tmp/zui-probe" + kind.value;
        report.extension = kind.value;
    } else if (kind.kind == association_detail::TypeKind::Mime) {
        url_text = "file:///tmp/zui-probe.bin";
        report.mime = kind.value;
    } else {
        return report;
    }
    id workspace = apple_objc::msg<id>(apple_objc::cls("NSWorkspace"), apple_objc::sel("sharedWorkspace"));
    id url = apple_objc::msg<id>(apple_objc::cls("NSURL"), apple_objc::sel("URLWithString:"),
                                 apple_objc::ns_string_kept(url_text));
    id app = apple_objc::msg<id>(workspace, apple_objc::sel("URLForApplicationToOpenURL:"), url);
    id path = apple_objc::msg<id>(app, apple_objc::sel("path"));
    const char* utf8 = path != nullptr ? apple_objc::msg<const char*>(path, apple_objc::sel("UTF8String")) : nullptr;
    if (utf8 == nullptr || utf8[0] == '\0') {
        return report;
    }
    report.executable = utf8;
    report.ok = true;
#else
    (void)type;
#endif
    return report;
}

}  // namespace association_macos
}  // namespace zui
