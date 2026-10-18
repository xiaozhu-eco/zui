#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace zui_tool {

// Builds a command line. Mixed string and const char* arguments are allowed.
template <typename... Ts>
std::vector<std::string> command(Ts&&... parts) {
    std::vector<std::string> out;
    out.reserve(sizeof...(parts));
    (out.emplace_back(std::forward<Ts>(parts)), ...);
    return out;
}

std::string quote_arg(const std::string& text);

// Runs one command. Prints the command line. Returns the process exit code.
int run_process(const std::vector<std::string>& args);

// Like run_process. On Windows, when LIB is unset, sources vcvars64.bat first
// so a child cmake can link kernel32.lib. script_dir receives a temporary .cmd.
int run_msvc_process(const std::vector<std::string>& args,
                     const std::filesystem::path& script_dir);

// The cmake that wrote this build's CMakeCache, when that file names one that
// still exists. A later `cmake` on PATH can be a different, older copy.
std::string cmake_program(const std::filesystem::path& build_dir);

}  // namespace zui_tool
