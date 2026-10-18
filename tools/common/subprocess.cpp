#include "subprocess.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace zui_tool {

std::string quote_arg(const std::string& text) {
    std::string out = "\"";
    out.reserve(text.size() + 2);
    for (char c : text) {
        if (c == '"') {
            out += "\\\"";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
    return out;
}

std::string join_command(const std::vector<std::string>& args) {
    std::string cmd;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i != 0) {
            cmd.push_back(' ');
        }
        cmd += quote_arg(args[i]);
    }
    return cmd;
}

int run_process(const std::vector<std::string>& args) {
    // std::system runs `cmd /c`. When the line has more than one quoted
    // argument, cmd strips the first and last quote. An extra pair keeps
    // paths that contain spaces intact.
    const std::string cmd = "\"" + join_command(args) + "\"";
    std::cout << join_command(args) << std::endl;
    return std::system(cmd.c_str());
}

std::filesystem::path find_vcvars() {
    const char* candidates[] = {
        "C:/Program Files/Microsoft Visual Studio/18/Enterprise/VC/Auxiliary/Build/vcvars64.bat",
        "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/Auxiliary/Build/vcvars64.bat",
        "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat",
        "C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/Auxiliary/Build/vcvars64.bat",
    };
    for (const char* candidate : candidates) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(candidate, ec)) {
            return candidate;
        }
    }
    return {};
}

int run_msvc_process(const std::vector<std::string>& args, const std::filesystem::path& script_dir) {
#if !defined(_WIN32)
    (void)script_dir;
    return run_process(args);
#else
    if (std::getenv("LIB") != nullptr) {
        return run_process(args);
    }
    const std::filesystem::path vcvars = find_vcvars();
    if (vcvars.empty()) {
        return run_process(args);
    }
    std::error_code ec;
    std::filesystem::create_directories(script_dir, ec);
    const std::filesystem::path script = script_dir / "zui-msvc.cmd";
    std::ofstream out(script, std::ios::binary);
    if (!out) {
        return run_process(args);
    }
    out << "@echo off\r\ncall " << quote_arg(vcvars.string()) << " >nul\r\n";
    out << join_command(args) << "\r\nexit /b %ERRORLEVEL%\r\n";
    out.close();
    const std::string cmd = "\"" + quote_arg(script.string()) + "\"";
    std::cout << script.string() << std::endl;
    return std::system(cmd.c_str());
#endif
}

std::string cmake_program(const std::filesystem::path& build_dir) {
    std::ifstream in(build_dir / "CMakeCache.txt");
    std::string found;
    if (in) {
        const std::string prefix = "CMAKE_COMMAND:";
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.compare(0, prefix.size(), prefix) != 0) {
                continue;
            }
            const auto eq = line.find('=');
            if (eq == std::string::npos) {
                continue;
            }
            const std::string value = line.substr(eq + 1);
            if (!value.empty()) {
                found = value;
            }
        }
    }
    std::error_code ec;
    if (!found.empty() && std::filesystem::is_regular_file(std::filesystem::path(found), ec)) {
        return found;
    }
    return "cmake";
}

}  // namespace zui_tool
