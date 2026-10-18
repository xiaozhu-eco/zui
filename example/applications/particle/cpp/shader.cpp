#include "shader.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace particle {

std::string trim(std::string_view in) {
    std::size_t a = 0;
    while (a < in.size() && (in[a] == ' ' || in[a] == '\t' || in[a] == '\n' || in[a] == '\r')) {
        ++a;
    }
    std::size_t b = in.size();
    while (b > a && (in[b - 1] == ' ' || in[b - 1] == '\t' || in[b - 1] == '\n' || in[b - 1] == '\r')) {
        --b;
    }
    return std::string(in.substr(a, b - a));
}

std::string num1(float value) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", value);
    return buf;
}

std::string default_shader() {
    return "#version 300 es\n"
           "precision highp float;\n"
           "#define U_COUNT 12.0\n"
           "#define U_SPEED 1.0\n"
           "#define U_SPREAD 0.6\n"
           "uniform float u_count;\n"
           "uniform float u_speed;\n"
           "uniform float u_spread;\n"
           "// keep-panel-ignored\n"
           "uniform float u_custom;\n"
           "void main() {\n"
           "  gl_FragColor = vec4(u_custom, u_speed / U_COUNT, U_SPREAD, 1.0);\n"
           "}\n";
}

bool read_define(const std::string& src, const std::string& name, float& out) {
    const std::string key = "#define " + name;
    std::size_t pos = 0;
    while ((pos = src.find(key, pos)) != std::string::npos) {
        std::size_t i = pos + key.size();
        if (i >= src.size() || (src[i] != ' ' && src[i] != '\t')) {
            pos += key.size();
            continue;
        }
        while (i < src.size() && (src[i] == ' ' || src[i] == '\t')) {
            ++i;
        }
        char* end = nullptr;
        const float value = std::strtof(src.c_str() + i, &end);
        if (end != src.c_str() + i) {
            out = value;
            return true;
        }
        pos += key.size();
    }
    return false;
}

void write_define(std::string& src, const std::string& name, float value) {
    const std::string key = "#define " + name;
    const std::string text = num1(value);
    const std::size_t pos = src.find(key);
    if (pos == std::string::npos) {
        if (!src.empty() && src.back() != '\n') {
            src.push_back('\n');
        }
        src += key + " " + text + "\n";
        return;
    }
    std::size_t i = pos + key.size();
    while (i < src.size() && (src[i] == ' ' || src[i] == '\t')) {
        ++i;
    }
    std::size_t end = i;
    while (end < src.size() && src[end] != '\n' && src[end] != '\r' && src[end] != ' ' && src[end] != '\t') {
        ++end;
    }
    src.replace(i, end - i, text);
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool write_file(const std::filesystem::path& path, const std::string& text) {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(out);
}

}  // namespace particle
