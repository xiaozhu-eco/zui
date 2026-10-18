#include "plugins/shell/layout.h"

namespace zui {
namespace shell_layout {
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

std::string under_zui(std::string_view root, std::string_view app_id) {
    return join(join(root, "zui"), effective_id(app_id));
}

}  // namespace

std::string effective_id(std::string_view app_id) {
    return app_id.empty() ? std::string("app") : std::string(app_id);
}

std::string linux_user_data(const Roots& roots, std::string_view app_id) {
    const std::string root = roots.xdg_data_home.empty() ? join(join(roots.home, ".local"), "share")
                                                         : roots.xdg_data_home;
    return under_zui(root, app_id);
}

std::string linux_cache(const Roots& roots, std::string_view app_id) {
    const std::string root = roots.xdg_cache_home.empty() ? join(roots.home, ".cache") : roots.xdg_cache_home;
    return under_zui(root, app_id);
}

std::string macos_user_data(const Roots& roots, std::string_view app_id) {
    return under_zui(join(join(roots.home, "Library"), "Application Support"), app_id);
}

std::string macos_cache(const Roots& roots, std::string_view app_id) {
    return under_zui(join(join(roots.home, "Library"), "Caches"), app_id);
}

std::string ios_user_data(const Roots& roots, std::string_view app_id) {
    return under_zui(join(join(roots.ios_container, "Library"), "Application Support"), app_id);
}

std::string ios_cache(const Roots& roots, std::string_view app_id) {
    return under_zui(join(join(roots.ios_container, "Library"), "Caches"), app_id);
}

std::string android_user_data(const Roots& roots, std::string_view app_id) {
    return under_zui(roots.android_files, app_id);
}

std::string android_cache(const Roots& roots, std::string_view app_id) {
    return under_zui(roots.android_cache, app_id);
}

const char* ios_desktop_data_leaf() { return "zui-ios-data"; }
const char* ios_desktop_cache_leaf() { return "zui-ios-cache"; }
const char* android_desktop_data_leaf() { return "zui-android-data"; }
const char* android_desktop_cache_leaf() { return "zui-android-cache"; }

}  // namespace shell_layout
}  // namespace zui
