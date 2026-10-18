#pragma once

#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace zui_tool {

inline std::string trim_copy(std::string text) {
    while (!text.empty() && (text.back() == ' ' || text.back() == '\n' || text.back() == '\r' ||
                             text.back() == '\t')) {
        text.pop_back();
    }
    std::size_t start = 0;
    while (start < text.size() && (text[start] == ' ' || text[start] == '\n' || text[start] == '\r' ||
                                   text[start] == '\t')) {
        ++start;
    }
    return text.substr(start);
}

inline std::string lower_copy(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

// Set when the command line asks for the wizard on a pipe (--interactive).
inline bool& interactive_forced() {
    static bool forced = false;
    return forced;
}

// A live terminal, or --interactive, is asked for answers the command line
// left out. Pack and create tests are not a terminal, so they take the defaults
// unless they pass --interactive and feed the answers on stdin.
inline bool is_interactive() {
    if (interactive_forced()) {
        return true;
    }
#if defined(_WIN32)
    // GetConsoleMode is true only for a console. NUL and pipes are character
    // devices that _isatty accepts, and a read there is an immediate end.
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    if (input == nullptr || input == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD mode = 0;
    return GetConsoleMode(input, &mode) != 0;
#else
    return isatty(fileno(stdin)) != 0;
#endif
}

}  // namespace zui_tool
