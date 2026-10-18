#include "plugins/association/ios_query.h"

#include "plugins/apple_objc.h"
#include "plugins/association/record.h"

#include <string>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace zui {
namespace association_ios {

plugin::OpenerReport IosAssociationQuery::query(std::string_view type) {
    plugin::OpenerReport report;
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
    report.ran = true;
    const association_detail::Classified kind = association_detail::classify(type);
    std::string url_text = "file:///tmp/zui-probe";
    if (kind.kind == association_detail::TypeKind::Scheme) {
        url_text = kind.value + "://open";
        report.scheme = kind.value;
    } else if (kind.kind == association_detail::TypeKind::Extension) {
        url_text += kind.value;
        report.extension = kind.value;
    } else if (kind.kind == association_detail::TypeKind::Mime) {
        report.mime = kind.value;
    }
    id workspace = apple_objc::msg<id>(apple_objc::cls("LSApplicationWorkspace"), apple_objc::sel("defaultWorkspace"));
    id url = apple_objc::msg<id>(apple_objc::cls("NSURL"), apple_objc::sel("URLWithString:"),
                                 apple_objc::ns_string_kept(url_text));
    id apps = apple_objc::msg<id>(workspace, apple_objc::sel("applicationsAvailableForHandlingURLScheme:"),
                                  apple_objc::ns_string_kept(kind.value));
    id app = apple_objc::msg<id>(workspace, apple_objc::sel("applicationForOpeningResource:"), url);
    (void)apps;
    id bundle = apple_objc::msg<id>(app, apple_objc::sel("boundApplicationIdentifier"));
    const char* utf8 =
        bundle != nullptr ? apple_objc::msg<const char*>(bundle, apple_objc::sel("UTF8String")) : nullptr;
    if (utf8 != nullptr && utf8[0] != '\0') {
        report.executable = utf8;
        report.ok = true;
    }
#else
    (void)type;
#endif
    return report;
}

}  // namespace association_ios
}  // namespace zui
