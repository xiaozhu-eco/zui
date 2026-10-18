#include "backends/posix_platform_util.h"
#include "ext/slots.h"
#include "plugins/pick/command.h"

#include <cstdint>
#include <string>
#include <vector>

namespace zui {
namespace backends {
namespace {

using posix_util::run_command_capture;
using posix_util::shell_quote;

#if defined(__linux__) && !defined(__ANDROID__)

std::vector<std::string> split_paths(const std::string& out, char sep) {
    std::vector<std::string> paths;
    std::size_t start = 0;
    while (start <= out.size()) {
        const std::size_t found = out.find(sep, start);
        std::string path = out.substr(start, found == std::string::npos ? std::string::npos : found - start);
        while (!path.empty() && (path.back() == '\r' || path.back() == ' ' || path.back() == '\n')) {
            path.pop_back();
        }
        if (!path.empty()) {
            paths.push_back(std::move(path));
        }
        if (found == std::string::npos) {
            break;
        }
        start = found + 1;
    }
    return paths;
}

PickPaths parse_zenity_open_output(const std::string& out, bool multi) {
    if (out.empty()) {
        return {PickStatus::Cancelled, {}};
    }
    std::vector<std::string> paths;
    if (multi) {
        paths = split_paths(out, '|');
        if (paths.size() <= 1 && out.find('\n') != std::string::npos) {
            paths = split_paths(out, '\n');
        }
    } else {
        std::string path = out;
        const std::size_t nl = path.find('\n');
        if (nl != std::string::npos) {
            path.resize(nl);
        }
        while (!path.empty() && (path.back() == '\r' || path.back() == ' ')) {
            path.pop_back();
        }
        if (!path.empty()) {
            paths.push_back(std::move(path));
        }
    }
    if (paths.empty()) {
        return {PickStatus::Cancelled, {}};
    }
    return {PickStatus::Ok, std::move(paths)};
}

PickStatus dialog_failure(int code) {
    if (code == 0 || code == 1) {
        return PickStatus::Cancelled;
    }
    return PickStatus::Unsupported;
}

bool command_exists(const char* name) {
    const std::string cmd = std::string("command -v ") + shell_quote(name) + " >/dev/null 2>&1";
    int code = -1;
    std::string out;
    return run_command_capture(cmd, out, code);
}

unsigned long long window_xid(NativeWindowId owner) {
    if (!owner) {
        return 0;
    }
    return static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(owner.get()));
}

PickPaths run_open(const std::string& cmd, bool multi, bool zenity_parse) {
    std::string out;
    int code = -1;
    run_command_capture(cmd, out, code);
    if (code != 0 || out.empty()) {
        return {dialog_failure(code), {}};
    }
    if (zenity_parse) {
        return parse_zenity_open_output(out, multi);
    }
    std::vector<std::string> paths = multi ? split_paths(out, '\n') : std::vector<std::string>{out};
    if (paths.empty()) {
        return {PickStatus::Cancelled, {}};
    }
    return {PickStatus::Ok, std::move(paths)};
}

PickPath run_one(const std::string& cmd) {
    std::string out;
    int code = -1;
    run_command_capture(cmd, out, code);
    if (code != 0 || out.empty()) {
        return {dialog_failure(code), {}};
    }
    return {PickStatus::Ok, out};
}

enum class LinuxPicker { None, Zenity, Kdialog, Qarma };

LinuxPicker select_linux_picker() {
    if (command_exists("zenity")) {
        return LinuxPicker::Zenity;
    }
    if (command_exists("kdialog")) {
        return LinuxPicker::Kdialog;
    }
    if (command_exists("qarma")) {
        return LinuxPicker::Qarma;
    }
    return LinuxPicker::None;
}

class LinuxDialogs final : public FileDialogService {
public:
    PickPaths open(NativeWindowId owner, const FileDialogOptions& opt) override {
        const unsigned long long xid = window_xid(owner);
        switch (select_linux_picker()) {
            case LinuxPicker::Zenity:
                return run_open(pick_detail::DialogCommand::zenity_open("zenity", opt), opt.multi, true);
            case LinuxPicker::Kdialog:
                return run_open(pick_detail::DialogCommand::kdialog_open(opt, xid), opt.multi, false);
            case LinuxPicker::Qarma:
                return run_open(pick_detail::DialogCommand::zenity_open("qarma", opt), opt.multi, true);
            case LinuxPicker::None:
                break;
        }
        return {PickStatus::Unsupported, {}};
    }

    PickPath save(NativeWindowId owner, const FileDialogOptions& opt) override {
        const unsigned long long xid = window_xid(owner);
        switch (select_linux_picker()) {
            case LinuxPicker::Zenity:
                return run_one(pick_detail::DialogCommand::zenity_save("zenity", opt));
            case LinuxPicker::Kdialog:
                return run_one(pick_detail::DialogCommand::kdialog_save(opt, xid));
            case LinuxPicker::Qarma:
                return run_one(pick_detail::DialogCommand::zenity_save("qarma", opt));
            case LinuxPicker::None:
                break;
        }
        return {PickStatus::Unsupported, {}};
    }

    PickPath folder(NativeWindowId owner, const FileDialogOptions& opt) override {
        const unsigned long long xid = window_xid(owner);
        switch (select_linux_picker()) {
            case LinuxPicker::Zenity:
                return run_one(pick_detail::DialogCommand::zenity_folder("zenity", opt));
            case LinuxPicker::Kdialog:
                return run_one(pick_detail::DialogCommand::kdialog_folder(opt, xid));
            case LinuxPicker::Qarma:
                return run_one(pick_detail::DialogCommand::zenity_folder("qarma", opt));
            case LinuxPicker::None:
                break;
        }
        return {PickStatus::Unsupported, {}};
    }
};

#endif

}  // namespace

void register_linux_dialogs(ExtensionRegistry& registry) {
#if defined(__linux__) && !defined(__ANDROID__)
    registry.add_dialogs(PlatformKind::Linux, std::make_unique<LinuxDialogs>());
#else
    (void)registry;
#endif
}

}  // namespace backends
}  // namespace zui
