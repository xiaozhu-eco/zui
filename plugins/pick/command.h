#ifndef ZUI_PLUGIN_PICK_COMMAND_H
#define ZUI_PLUGIN_PICK_COMMAND_H

// Dialog command and filter text. No OS headers: the dialog service runs it.

#include <zui/host_types.h>

#include <string>
#include <string_view>
#include <vector>

namespace zui {
namespace pick_detail {

class DialogCommand {
public:
    static std::string zenity_filters(const std::vector<FileFilter>& filters);
    static std::string kdialog_filter(const std::vector<FileFilter>& filters);
    static std::string zenity_open(const char* tool, const FileDialogOptions& opt);
    static std::string zenity_save(const char* tool, const FileDialogOptions& opt);
    static std::string zenity_folder(const char* tool, const FileDialogOptions& opt);
    static std::string kdialog_open(const FileDialogOptions& opt, unsigned long long xid);
    static std::string kdialog_save(const FileDialogOptions& opt, unsigned long long xid);
    static std::string kdialog_folder(const FileDialogOptions& opt, unsigned long long xid);
    static std::string applescript_open(const FileDialogOptions& opt);
    static std::string applescript_save(const FileDialogOptions& opt);
    static std::string applescript_folder(const FileDialogOptions& opt);
    static std::string win32_patterns(std::string_view patterns);
    static std::string uti_for(std::string_view extension);
    static std::string mime_for(std::string_view extension);
    static std::vector<std::string> ios_types(const std::vector<FileFilter>& filters);
    static std::vector<std::string> android_mimes(const std::vector<FileFilter>& filters);
};

}  // namespace pick_detail
}  // namespace zui

#endif
