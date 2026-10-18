#include "plugins/association/linux_store.h"

#include "plugins/association/linux_paths.h"

#include <cerrno>
#include <vector>

namespace zui {
namespace association_linux {

plugin::OpenerReport LinuxAssociationStore::write(const association_detail::Record& record,
                                                   plugin::OpenerReport report) {
#if defined(__linux__) && !defined(__ANDROID__)
    std::filesystem::path applications;
    std::filesystem::path mimeapps;
    if (!dirs(applications, mimeapps)) {
        return report;
    }
    std::error_code error;
    std::filesystem::create_directories(applications, error);
    std::filesystem::create_directories(mimeapps.parent_path(), error);
    if (error || !std::filesystem::is_directory(applications)) {
        return report;
    }
    report.ran = true;
    if (!write_text(applications / record.desktop_name, record.desktop_entry)) {
        report.os_error = static_cast<unsigned long>(errno);
        return report;
    }
    std::string list = read_text(mimeapps);
    std::vector<std::string> lines;
    if (!record.mime.empty()) {
        lines.push_back(record.mime + "=" + record.desktop_name);
    }
    if (!record.scheme.empty()) {
        lines.push_back("x-scheme-handler/" + record.scheme + "=" + record.desktop_name);
    }
    ensure_lines(list, lines);
    if (!write_text(mimeapps, list)) {
        report.os_error = static_cast<unsigned long>(errno);
        return report;
    }
    report.ok = true;
#else
    (void)record;
#endif
    return report;
}

bool LinuxAssociationStore::remove(const association_detail::Record& record) {
#if defined(__linux__) && !defined(__ANDROID__)
    std::filesystem::path applications;
    std::filesystem::path mimeapps;
    if (!dirs(applications, mimeapps) || record.desktop_name.empty()) {
        return false;
    }
    std::error_code error;
    std::filesystem::remove(applications / record.desktop_name, error);
    if (std::filesystem::is_regular_file(mimeapps)) {
        write_text(mimeapps, without_desktop(read_text(mimeapps), record.desktop_name));
    }
    return true;
#else
    (void)record;
    return false;
#endif
}

}  // namespace association_linux
}  // namespace zui
