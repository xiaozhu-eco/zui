#pragma once

// Terminal look for the tools. A live terminal gets color and symbols; a pipe
// or a log gets the same words with no escape sequences, so a test reads what a
// person reads.

#include <string>

namespace zui_tool {

// Call once before the first styled write. Enables VT processing and UTF-8
// output on a Windows console.
void style_setup();

// True when the output goes to a terminal that can show color and symbols.
bool style_enabled();

// Empty when styled output is off, so a caller can drop a marker without
// branching.
const char* mark_ok();
const char* mark_no();
const char* mark_ask();
const char* mark_dot();
const char* mark_arrow();

std::string style_bold(const std::string& text);
std::string style_dim(const std::string& text);
std::string style_green(const std::string& text);
std::string style_yellow(const std::string& text);
std::string style_red(const std::string& text);
std::string style_cyan(const std::string& text);

// One line of the wizard banner.
void print_banner(const std::string& tool, const std::string& version, const std::string& blurb);

}  // namespace zui_tool
