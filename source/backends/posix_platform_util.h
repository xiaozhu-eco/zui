#ifndef ZUI_BACKENDS_POSIX_PLATFORM_UTIL_H
#define ZUI_BACKENDS_POSIX_PLATFORM_UTIL_H

#include <zui/host_types.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#define ZUI_POPEN _popen
#define ZUI_PCLOSE _pclose
#else
#define ZUI_POPEN popen
#define ZUI_PCLOSE pclose
#include <sys/wait.h>
#endif

namespace zui {
namespace backends {
namespace posix_util {

inline std::string effective_app_id(std::string_view app_id) {
    return app_id.empty() ? std::string("app") : std::string(app_id);
}

inline std::string env_or_empty(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr ? std::string(v) : std::string();
}

inline std::string ensure_dir(const std::filesystem::path& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir.string();
}

inline bool ascii_ieq_prefix(std::string_view text, std::string_view prefix) {
    if (text.size() < prefix.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        char a = text[i];
        const char b = prefix[i];
        if (a >= 'A' && a <= 'Z') {
            a = static_cast<char>(a - 'A' + 'a');
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

inline bool uri_scheme_allowed(std::string_view uri) {
    return ascii_ieq_prefix(uri, "https:") || ascii_ieq_prefix(uri, "http:") ||
           ascii_ieq_prefix(uri, "mailto:") || ascii_ieq_prefix(uri, "tel:") ||
           ascii_ieq_prefix(uri, "file:");
}

/// Shell-escape a single argument for /bin/sh (single-quote style).
inline std::string shell_quote(std::string_view s) {
    std::string out;
    out.push_back('\'');
    for (char c : s) {
        if (c == '\'') {
            out.append("'\\''");
        } else {
            out.push_back(c);
        }
    }
    out.push_back('\'');
    return out;
}

inline std::string trim_trailing_newlines(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) {
        s.pop_back();
    }
    return s;
}

/// pclose / _pclose status as a process exit code. -1 when the pipe failed or the
/// child did not exit normally.
inline int normalized_exit_code(int raw) {
#ifdef _WIN32
    return raw;
#else
    if (raw == -1) {
        return -1;
    }
    if (WIFEXITED(raw)) {
        return WEXITSTATUS(raw);
    }
    return -1;
#endif
}

/// Run a command. exit_code is the process exit code (0 on success), not a raw wait status.
inline bool run_command_capture(const std::string& cmd, std::string& stdout_out, int& exit_code) {
    stdout_out.clear();
    FILE* pipe = ZUI_POPEN(cmd.c_str(), "r");
    if (pipe == nullptr) {
        exit_code = -1;
        return false;
    }
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe) != nullptr) {
        stdout_out.append(buf);
    }
    exit_code = normalized_exit_code(ZUI_PCLOSE(pipe));
    stdout_out = trim_trailing_newlines(std::move(stdout_out));
    return exit_code == 0;
}

/// osascript `choose file` / `choose folder`: exit 0 is success, -128 on stderr is the
/// user dismissing the panel, anything else is a missing tool or a script error.
enum class OsascriptPickKind { Ok, Cancelled, Unsupported };

inline OsascriptPickKind classify_osascript_pick(int exit_code, std::string_view output) {
    if (exit_code == 0) {
        return output.empty() ? OsascriptPickKind::Cancelled : OsascriptPickKind::Ok;
    }
    if (output.find("-128") != std::string_view::npos ||
        output.find("User canceled") != std::string_view::npos ||
        output.find("User cancelled") != std::string_view::npos) {
        return OsascriptPickKind::Cancelled;
    }
    return OsascriptPickKind::Unsupported;
}

inline std::string applescript_string(std::string_view s) {
    std::string out;
    out.push_back('"');
    for (char c : s) {
        if (c == '\\' || c == '"') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

}  // namespace posix_util
}  // namespace backends
}  // namespace zui

#endif
