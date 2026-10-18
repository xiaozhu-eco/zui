#include "backends/posix_platform_util.h"
#include "ext/slots.h"
#include "plugins/pick/command.h"

#include <string>
#include <vector>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace zui {
namespace backends {
namespace {

using posix_util::classify_osascript_pick;
using posix_util::OsascriptPickKind;
using posix_util::run_command_capture;
using posix_util::shell_quote;

#if defined(__APPLE__) && !(defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)

struct OsascriptRun {
    OsascriptPickKind kind = OsascriptPickKind::Unsupported;
    std::string text;
};

OsascriptRun run_osascript(const std::string& script) {
    const std::string cmd = "osascript -e " + shell_quote(script) + " 2>&1";
    std::string out;
    int code = -1;
    run_command_capture(cmd, out, code);
    OsascriptRun result;
    result.kind = classify_osascript_pick(code, out);
    if (result.kind == OsascriptPickKind::Ok) {
        std::string paths;
        std::size_t start = 0;
        while (start <= out.size()) {
            const std::size_t nl = out.find('\n', start);
            std::string line = out.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
                line.pop_back();
            }
            if (!line.empty() && line.front() == '/') {
                if (!paths.empty()) {
                    paths.push_back('\n');
                }
                paths.append(line);
            }
            if (nl == std::string::npos) {
                break;
            }
            start = nl + 1;
        }
        result.text = std::move(paths);
        if (result.text.empty()) {
            result.kind = OsascriptPickKind::Cancelled;
        }
    }
    return result;
}

PickPaths paths_from(const OsascriptRun& ran) {
    if (ran.kind == OsascriptPickKind::Unsupported) {
        return {PickStatus::Unsupported, {}};
    }
    if (ran.kind != OsascriptPickKind::Ok) {
        return {PickStatus::Cancelled, {}};
    }
    std::vector<std::string> paths;
    std::size_t start = 0;
    while (start <= ran.text.size()) {
        const std::size_t nl = ran.text.find('\n', start);
        std::string path = ran.text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        while (!path.empty() && path.back() == '\r') {
            path.pop_back();
        }
        if (!path.empty()) {
            paths.push_back(std::move(path));
        }
        if (nl == std::string::npos) {
            break;
        }
        start = nl + 1;
    }
    if (paths.empty()) {
        return {PickStatus::Cancelled, {}};
    }
    return {PickStatus::Ok, std::move(paths)};
}

PickPath one_from(const OsascriptRun& ran) {
    if (ran.kind == OsascriptPickKind::Unsupported) {
        return {PickStatus::Unsupported, {}};
    }
    if (ran.kind != OsascriptPickKind::Ok || ran.text.empty()) {
        return {PickStatus::Cancelled, {}};
    }
    return {PickStatus::Ok, ran.text};
}

class MacDialogs final : public FileDialogService {
public:
    PickPaths open(NativeWindowId, const FileDialogOptions& opt) override {
        return paths_from(run_osascript(pick_detail::DialogCommand::applescript_open(opt)));
    }
    PickPath save(NativeWindowId, const FileDialogOptions& opt) override {
        return one_from(run_osascript(pick_detail::DialogCommand::applescript_save(opt)));
    }
    PickPath folder(NativeWindowId, const FileDialogOptions& opt) override {
        return one_from(run_osascript(pick_detail::DialogCommand::applescript_folder(opt)));
    }
};

#endif

}  // namespace

void register_macos_dialogs(ExtensionRegistry& registry) {
#if defined(__APPLE__) && !(defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    registry.add_dialogs(PlatformKind::Macos, std::make_unique<MacDialogs>());
#else
    (void)registry;
#endif
}

}  // namespace backends
}  // namespace zui
