#include "plugins/association/linux_query.h"

#include "plugins/association/linux_paths.h"
#include "plugins/association/record.h"

namespace zui {
namespace association_linux {

plugin::OpenerReport LinuxAssociationQuery::query(std::string_view type) {
    plugin::OpenerReport report;
#if defined(__linux__) && !defined(__ANDROID__)
    std::filesystem::path applications;
    std::filesystem::path mimeapps;
    if (!dirs(applications, mimeapps)) {
        return report;
    }
    report.ran = true;
    const association_detail::Classified kind = association_detail::classify(type);
    std::error_code error;
    if (!std::filesystem::is_directory(applications, error)) {
        return report;
    }
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(applications, error)) {
        if (entry.path().extension() != ".desktop") {
            continue;
        }
        const std::string text = read_text(entry.path());
        const bool match = (kind.kind == association_detail::TypeKind::Extension &&
                            field_value(text, "X-Zui-Extension") == kind.value) ||
                           (kind.kind == association_detail::TypeKind::Mime &&
                            field_value(text, "X-Zui-Mime") == kind.value) ||
                           (kind.kind == association_detail::TypeKind::Scheme &&
                            field_value(text, "X-Zui-Scheme") == kind.value);
        if (!match) {
            continue;
        }
        report.app_name = field_value(text, "Name");
        report.command = field_value(text, "Exec");
        report.executable = association_detail::executable_from_command(report.command);
        report.extension = field_value(text, "X-Zui-Extension");
        report.mime = field_value(text, "X-Zui-Mime");
        report.scheme = field_value(text, "X-Zui-Scheme");
        report.desktop_name = entry.path().filename().string();
        report.ok = !report.executable.empty();
        return report;
    }
#else
    (void)type;
#endif
    return report;
}

}  // namespace association_linux
}  // namespace zui
