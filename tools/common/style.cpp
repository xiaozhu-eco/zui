#include "style.h"

#include "utils.h"

#include <cstdlib>
#include <iostream>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace zui_tool {
namespace {

bool style_on() {
    if (std::getenv("NO_COLOR") != nullptr) {
        return false;
    }
    const char* term = std::getenv("TERM");
    if (term != nullptr && std::string(term) == "dumb") {
        return false;
    }
    return is_interactive();
}

std::string wrap(const char* code, const std::string& text) {
    if (!style_on() || text.empty()) {
        return text;
    }
    return std::string(code) + text + "\x1b[0m";
}

}  // namespace

void style_setup() {
#if defined(_WIN32)
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out != nullptr && out != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(out, &mode) != 0) {
            SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
    }
    if (style_on()) {
        // The symbols below are UTF-8; a console that is not told so shows them
        // as mojibake.
        SetConsoleOutputCP(CP_UTF8);
    }
#endif
}

bool style_enabled() { return style_on(); }

const char* mark_ok() { return style_on() ? "\u2713" : "ok:"; }
const char* mark_no() { return style_on() ? "\u2717" : "error:"; }
const char* mark_ask() { return style_on() ? "?" : "?"; }
const char* mark_dot() { return style_on() ? "\u2022" : "-"; }
const char* mark_arrow() { return style_on() ? "\u203a" : ">"; }

std::string style_bold(const std::string& text) { return wrap("\x1b[1m", text); }
std::string style_dim(const std::string& text) { return wrap("\x1b[2m", text); }
std::string style_green(const std::string& text) { return wrap("\x1b[32m", text); }
std::string style_yellow(const std::string& text) { return wrap("\x1b[33m", text); }
std::string style_red(const std::string& text) { return wrap("\x1b[31m", text); }
std::string style_cyan(const std::string& text) { return wrap("\x1b[36m", text); }

void print_banner(const std::string& tool, const std::string& version, const std::string& blurb) {
    std::cerr << "\n  " << style_bold(tool) << " " << style_dim(version) << "\n";
    if (!blurb.empty()) {
        std::cerr << "  " << style_dim(blurb) << "\n";
    }
    std::cerr << "\n";
}

}  // namespace zui_tool
