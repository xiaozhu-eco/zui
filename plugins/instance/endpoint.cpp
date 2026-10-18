#include "plugins/instance/endpoint.h"

namespace zui {
namespace instance_detail {
namespace {

std::string join(std::string_view left, std::string_view right) {
    if (left.empty()) {
        return std::string(right);
    }
    if (right.empty()) {
        return std::string(left);
    }
    std::string out(left);
    if (out.back() != '/') {
        out.push_back('/');
    }
    if (right.front() == '/') {
        out.append(right.substr(1));
    } else {
        out.append(right);
    }
    return out;
}

std::string socket_file(std::string_view identity) {
    return "zui-instance-" + safe_name(identity) + ".sock";
}

}  // namespace

std::string safe_name(std::string_view identity) {
    if (identity.empty()) {
        return "app";
    }
    std::string out;
    out.reserve(identity.size());
    for (unsigned char c : identity) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                        c == '_' || c == '-';
        out.push_back(ok ? static_cast<char>(c) : '_');
    }
    return out;
}

std::string win32_mutex_name(std::string_view identity) {
    return std::string("Local\\zui-instance-") + std::string(identity);
}

std::string win32_pipe_name(std::string_view identity) {
    return std::string("\\\\.\\pipe\\zui-instance-") + std::string(identity);
}

std::string linux_socket_path(std::string_view runtime_dir, std::string_view identity) {
    const std::string name = socket_file(identity);
    if (runtime_dir.empty()) {
        return "/tmp/" + name;
    }
    return join(runtime_dir, name);
}

std::string macos_socket_path(std::string_view home, std::string_view identity) {
    const std::string name = socket_file(identity);
    if (home.empty()) {
        return "/tmp/" + name;
    }
    return join(join(join(home, "Library"), "Caches"), name);
}

std::string ios_socket_path(std::string_view container, std::string_view identity) {
    const std::string name = socket_file(identity);
    if (container.empty()) {
        return "/tmp/" + name;
    }
    return join(join(container, "tmp"), name);
}

std::string android_socket_path(std::string_view files_dir, std::string_view identity) {
    const std::string name = socket_file(identity);
    if (files_dir.empty()) {
        return "/tmp/" + name;
    }
    return join(files_dir, name);
}

std::string ios_notification_name(std::string_view identity) {
    return "zui.instance." + safe_name(identity);
}

}  // namespace instance_detail
}  // namespace zui
