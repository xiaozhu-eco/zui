#include "plugins/pick/command.h"

#include "backends/posix_platform_util.h"

#include <functional>
#include <string>

namespace zui {
namespace pick_detail {
namespace {

using backends::posix_util::applescript_string;
using backends::posix_util::shell_quote;

void each_ext(std::string_view patterns, const std::function<void(std::string_view)>& visit) {
    std::size_t start = 0;
    while (start <= patterns.size()) {
        const std::size_t semi = patterns.find(';', start);
        const std::string_view part =
            patterns.substr(start, semi == std::string_view::npos ? std::string_view::npos : semi - start);
        visit(part);
        if (semi == std::string_view::npos) {
            break;
        }
        start = semi + 1;
    }
}

std::string bare_ext(std::string_view extension) {
    if (!extension.empty() && extension.front() == '.') {
        extension.remove_prefix(1);
    }
    return std::string(extension);
}

std::string glob_spec(const FileFilter& filter, bool kdialog) {
    std::string globs;
    const std::string patterns = filter.patterns.empty() ? std::string("*") : filter.patterns;
    each_ext(patterns, [&](std::string_view part) {
        if (!globs.empty()) {
            globs.push_back(' ');
        }
        if (part.empty() || part == "*") {
            globs.append(kdialog ? "*" : "*.*");
        } else {
            globs.append("*.");
            globs.append(part);
        }
    });
    return globs.empty() ? std::string(kdialog ? "*" : "*.*") : globs;
}

void append_title_eq(std::string& cmd, std::string_view title) {
    if (title.empty()) {
        return;
    }
    cmd.append(" --title=");
    cmd.append(shell_quote(title));
}

void append_title_sp(std::string& cmd, std::string_view title) {
    if (title.empty()) {
        return;
    }
    cmd.append(" --title ");
    cmd.append(shell_quote(title));
}

void append_attach(std::string& cmd, unsigned long long xid) {
    if (xid == 0) {
        return;
    }
    cmd.append(" --attach ");
    cmd.append(std::to_string(xid));
}

std::string applescript_types(const std::vector<FileFilter>& filters) {
    std::string list = "{";
    bool any = false;
    for (const FileFilter& filter : filters) {
        each_ext(filter.patterns, [&](std::string_view part) {
            if (part.empty() || part == "*") {
                return;
            }
            if (any) {
                list.append(", ");
            }
            any = true;
            list.append(applescript_string(part));
        });
    }
    if (!any) {
        return {};
    }
    list.push_back('}');
    return list;
}

}  // namespace

std::string DialogCommand::zenity_filters(const std::vector<FileFilter>& filters) {
    std::string args;
    if (filters.empty()) {
        args.append(" --file-filter=");
        args.append(shell_quote("All | *.*"));
        return args;
    }
    for (const FileFilter& filter : filters) {
        std::string spec = filter.label.empty() ? filter.patterns : filter.label;
        spec.append(" | ");
        const std::string globs = glob_spec(filter, false);
        if (!globs.empty() && globs.front() == ' ') {
            spec.append(globs.substr(1));
        } else {
            spec.append(globs);
        }
        args.append(" --file-filter=");
        args.append(shell_quote(spec));
    }
    return args;
}

std::string DialogCommand::kdialog_filter(const std::vector<FileFilter>& filters) {
    if (filters.empty()) {
        return "*";
    }
    std::string out;
    for (std::size_t i = 0; i < filters.size(); ++i) {
        const FileFilter& filter = filters[i];
        if (i > 0) {
            out.push_back('|');
        }
        if (!filter.label.empty()) {
            out.append(filter.label);
            out.push_back('|');
        }
        out.append(glob_spec(filter, true));
    }
    return out;
}

std::string DialogCommand::zenity_open(const char* tool, const FileDialogOptions& opt) {
    std::string cmd = std::string(tool != nullptr ? tool : "zenity") + " --file-selection";
    if (opt.multi) {
        cmd.append(" --multiple --separator='|'");
    }
    append_title_eq(cmd, opt.title);
    if (!opt.default_name.empty()) {
        cmd.append(" --filename=");
        cmd.append(shell_quote(opt.default_name));
    }
    cmd.append(zenity_filters(opt.filters));
    cmd.append(" 2>/dev/null");
    return cmd;
}

std::string DialogCommand::zenity_save(const char* tool, const FileDialogOptions& opt) {
    std::string cmd = std::string(tool != nullptr ? tool : "zenity") + " --file-selection --save --confirm-overwrite";
    append_title_eq(cmd, opt.title);
    if (!opt.default_name.empty()) {
        cmd.append(" --filename=");
        cmd.append(shell_quote(opt.default_name));
    }
    cmd.append(zenity_filters(opt.filters));
    cmd.append(" 2>/dev/null");
    return cmd;
}

std::string DialogCommand::zenity_folder(const char* tool, const FileDialogOptions& opt) {
    std::string cmd = std::string(tool != nullptr ? tool : "zenity") + " --file-selection --directory";
    append_title_eq(cmd, opt.title);
    cmd.append(" 2>/dev/null");
    return cmd;
}

std::string DialogCommand::kdialog_open(const FileDialogOptions& opt, unsigned long long xid) {
    std::string cmd = "kdialog --getopenfilename";
    if (opt.multi) {
        cmd.append(" --multiple --separate-output");
    }
    append_title_sp(cmd, opt.title);
    const std::string start = opt.default_name.empty() ? std::string(".") : opt.default_name;
    cmd.push_back(' ');
    cmd.append(shell_quote(start));
    cmd.push_back(' ');
    cmd.append(shell_quote(kdialog_filter(opt.filters)));
    append_attach(cmd, xid);
    cmd.append(" 2>/dev/null");
    return cmd;
}

std::string DialogCommand::kdialog_save(const FileDialogOptions& opt, unsigned long long xid) {
    std::string cmd = "kdialog --getsavefilename";
    append_title_sp(cmd, opt.title);
    const std::string start = opt.default_name.empty() ? std::string(".") : opt.default_name;
    cmd.push_back(' ');
    cmd.append(shell_quote(start));
    cmd.push_back(' ');
    cmd.append(shell_quote(kdialog_filter(opt.filters)));
    append_attach(cmd, xid);
    cmd.append(" 2>/dev/null");
    return cmd;
}

std::string DialogCommand::kdialog_folder(const FileDialogOptions& opt, unsigned long long xid) {
    std::string cmd = "kdialog --getexistingdirectory";
    append_title_sp(cmd, opt.title);
    const std::string start = opt.default_name.empty() ? std::string(".") : opt.default_name;
    cmd.push_back(' ');
    cmd.append(shell_quote(start));
    append_attach(cmd, xid);
    cmd.append(" 2>/dev/null");
    return cmd;
}

std::string DialogCommand::applescript_open(const FileDialogOptions& opt) {
    std::string choose = "choose file";
    if (!opt.title.empty()) {
        choose.append(" with prompt ");
        choose.append(applescript_string(opt.title));
    }
    const std::string types = applescript_types(opt.filters);
    if (!types.empty()) {
        choose.append(" of type ");
        choose.append(types);
    }
    if (opt.multi) {
        choose.append(" with multiple selections allowed");
        return "set out to \"\"\nset picks to " + choose +
               "\nrepeat with p in picks\nset out to out & (POSIX path of p) & linefeed\nend repeat\nreturn out";
    }
    return "POSIX path of (" + choose + ")";
}

std::string DialogCommand::applescript_save(const FileDialogOptions& opt) {
    std::string choose = "choose file name";
    if (!opt.title.empty()) {
        choose.append(" with prompt ");
        choose.append(applescript_string(opt.title));
    }
    if (!opt.default_name.empty()) {
        choose.append(" default name ");
        choose.append(applescript_string(opt.default_name));
    }
    return "POSIX path of (" + choose + ")";
}

std::string DialogCommand::applescript_folder(const FileDialogOptions& opt) {
    std::string choose = "choose folder";
    if (!opt.title.empty()) {
        choose.append(" with prompt ");
        choose.append(applescript_string(opt.title));
    }
    return "POSIX path of (" + choose + ")";
}

std::string DialogCommand::win32_patterns(std::string_view patterns) {
    std::string out;
    each_ext(patterns, [&](std::string_view part) {
        if (!out.empty()) {
            out.push_back(';');
        }
        out.append("*.");
        out.append(part);
    });
    return out;
}

std::string DialogCommand::uti_for(std::string_view extension) {
    const std::string ext = bare_ext(extension);
    if (ext == "png") {
        return "public.png";
    }
    if (ext == "jpg" || ext == "jpeg") {
        return "public.jpeg";
    }
    if (ext == "gif") {
        return "com.compuserve.gif";
    }
    if (ext == "pdf") {
        return "com.adobe.pdf";
    }
    if (ext == "html" || ext == "htm") {
        return "public.html";
    }
    if (ext == "json") {
        return "public.json";
    }
    if (ext == "txt" || ext == "md" || ext == "text") {
        return "public.plain-text";
    }
    return "public.data";
}

std::string DialogCommand::mime_for(std::string_view extension) {
    const std::string ext = bare_ext(extension);
    if (ext == "png") {
        return "image/png";
    }
    if (ext == "jpg" || ext == "jpeg") {
        return "image/jpeg";
    }
    if (ext == "gif") {
        return "image/gif";
    }
    if (ext == "pdf") {
        return "application/pdf";
    }
    if (ext == "html" || ext == "htm") {
        return "text/html";
    }
    if (ext == "json") {
        return "application/json";
    }
    if (ext == "txt" || ext == "md" || ext == "text") {
        return "text/plain";
    }
    return "application/octet-stream";
}

std::vector<std::string> DialogCommand::ios_types(const std::vector<FileFilter>& filters) {
    std::vector<std::string> types;
    for (const FileFilter& filter : filters) {
        each_ext(filter.patterns, [&](std::string_view part) {
            if (!part.empty() && part != "*") {
                types.push_back(uti_for(part));
            }
        });
    }
    if (types.empty()) {
        types.emplace_back("public.data");
    }
    return types;
}

std::vector<std::string> DialogCommand::android_mimes(const std::vector<FileFilter>& filters) {
    std::vector<std::string> mimes;
    for (const FileFilter& filter : filters) {
        each_ext(filter.patterns, [&](std::string_view part) {
            if (!part.empty() && part != "*") {
                mimes.push_back(mime_for(part));
            }
        });
    }
    if (mimes.empty()) {
        mimes.emplace_back("*/*");
    }
    return mimes;
}

}  // namespace pick_detail
}  // namespace zui
