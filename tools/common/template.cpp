#include "template.h"

#include <string>
#include <vector>

namespace zui_tool {
namespace {

std::string lowercase_copy(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

// The extension of the last path segment, lowercase, or empty when the
// segment carries none.
std::string extension_of(const std::string& rel) {
    const std::size_t slash = rel.find_last_of('/');
    const std::string segment = slash == std::string::npos ? rel : rel.substr(slash + 1);
    const std::size_t dot = segment.find_last_of('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 == segment.size()) {
        return {};
    }
    return lowercase_copy(segment.substr(dot));
}

std::string xml_text(const std::string& text) {
    std::string out;
    for (char c : text) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '"':
            out += "&quot;";
            break;
        case '<':
            out += "&lt;";
            break;
        default:
            out.push_back(c);
            break;
        }
    }
    return out;
}

}  // namespace

std::string Templates::placeholder(const std::string& rel) {
    const std::string extension = extension_of(rel);
    if (extension == ".html" || extension == ".htm") {
        return "<!DOCTYPE html>\n<html>\n<head><meta charset=\"utf-8\"><title></title></head>\n"
               "<body></body>\n</html>\n";
    }
    if (extension == ".json") {
        return "{}\n";
    }
    if (extension == ".svg") {
        return "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 64 64\"></svg>\n";
    }
    return {};
}

bool Templates::is_directory_entry(const std::string& rel) { return extension_of(rel).empty(); }

std::string Templates::app_css_rel(const Project& project) {
    const std::string& page = project.page;
    const std::size_t slash = page.find_last_of('/');
    if (slash == std::string::npos) {
        return "app.css";
    }
    return page.substr(0, slash + 1) + "app.css";
}

std::string Templates::readme(const Project& project) {
    std::string files;
    for (const std::string& rel : project.files) {
        files += "  * `" + rel + "`\n";
    }
    return "# " + project.name + "\n"
           "\n"
           "A zui application: HTML for structure, CSS for layout and skin, C++ for\n"
           "interaction and data.\n"
           "\n"
           "## Build and run\n"
           "\n"
           "    cmake -S . -B .zui/build -DZUI_ROOT=<zui> -DZUI_BUILD=<zui build>\n"
           "    cmake --build .zui/build\n"
           "    .zui/build/" + project.name + "\n"
           "\n"
           "The build copies the page, the icon, and the declared runtime files next to\n"
           "the executable, so the program runs from its own build directory.\n"
           "\n"
           "## Pack\n"
           "\n"
           "    zui-pack . --root <zui> --build <zui build>          # desktop packages\n"
           "    zui-pack android . --root <zui> --build <zui build>  # an APK\n"
           "\n"
           "Packages land in `dist/`, named `<name>-<version>-portable.zip`,\n"
           "`<name>-<version>-setup.exe`, and `<name>-<version>.apk`.\n"
           "\n"
           "## zui.json is the only declaration\n"
           "\n"
           "  * `version`  a dotted number; android version codes need every part under 100\n"
           "  * `identifier`  the application id (`com.example." + lowercase_copy(project.name) + "` until you change it)\n"
           "  * `page`  the document the app opens\n"
           "  * `entry`  the one C++ file that binds data\n"
           "  * `icon`  the launcher icon\n"
           "  * `targets`  what to build: `portable`, `installer`, `android`\n"
           "  * `files`  every runtime file that ships besides the page:\n" + files +
           "  * `permissions`  what the app may ask for on mobile, each with a reason\n"
           "  * `android`  `minSdk`, `targetSdk`, `orientation`, `keystore`/`keyAlias`, `target`\n"
           "  * `windows`  `description`, `company`, `copyright` (the executable's version\n"
           "    resource) and `certificate` for signing\n"
           "\n"
           "The packer ships what is declared, it does not read your markup: if the page\n"
           "links a stylesheet, an image, or a fragment, that file belongs in `files`.\n"
           "\n"
           "## Icons\n"
           "\n"
           "`icon.png` is drawn exactly as given. `icons/icon.svg` is understood only in\n"
           "the shape this project starts with (a rounded tile with a text glyph); a real\n"
           "drawing belongs in a PNG.\n"
           "\n"
           "## Mobile permissions\n"
           "\n"
           "Only a name from the permission table can be requested, and only the names in\n"
           "`permissions` reach the Android manifest and the iOS usage descriptions.\n"
           "User-selected files go through `pick_open` / `pick_save` / `pick_folder` and\n"
           "do not need a storage permission. Broad file access is `storage` (read, old\n"
           "APIs), `storage-write`, `media-images` / `media-video` / `media-audio`, and\n"
           "`manage-storage` (all files; opens system settings).\n"
           "\n"
           "Query first and ask at runtime, then handle a refusal:\n"
           "\n"
           "```cpp\n"
           "const auto now = app.host().permission_state(zui::Permission::Camera);\n"
           "if (now == zui::PermissionState::Granted) {\n"
           "    // use the camera\n"
           "} else if (now == zui::PermissionState::NotRequested ||\n"
           "           now == zui::PermissionState::Denied) {\n"
           "    app.host().request_permission(zui::Permission::Camera, [](zui::PermissionState state) {\n"
           "        // Granted, Denied (can ask again), or Blocked (opens Settings if asked again).\n"
           "    });\n"
           "}\n"
           "```\n"
           "\n"
           "## Layout\n"
           "\n"
           "    zui.json        the project declaration\n"
           "    src/main.cpp    the entry: load, bind, run\n"
           "    ui/index.html   the page\n"
           "    ui/app.css      the skin\n"
           "    icons/icon.svg  the launcher icon\n";
}

std::string Templates::gitignore() {
    return "# zui-pack output: the build tree it configures and the packages it writes.\n"
           ".zui/\n"
           "dist/\n";
}

std::string Templates::entry(const Project& project) {
    return "#include <zui/app.h>\n"
           "\n"
           "int main(int argc, char** argv) {\n"
           "    zui::App app(argc, argv);\n"
           "\n"
           "    // The page ships beside the executable. A launch from another\n"
           "    // directory still finds it there, then in the working directory.\n"
           "    const char* page = \"" +
           project.page +
           "\";\n"
           "    std::string beside;\n"
           "    if (argc > 0 && argv != nullptr && argv[0] != nullptr && argv[0][0] != '\\0') {\n"
           "        beside = argv[0];\n"
           "        const std::size_t slash = beside.find_last_of(\"\\\\/\");\n"
           "        if (slash != std::string::npos) {\n"
           "            beside.resize(slash + 1);\n"
           "            beside += page;\n"
           "        }\n"
           "    }\n"
           "    if ((beside.empty() || !app.document().load(beside)) && !app.document().load(page)) {\n"
           "        return 1;\n"
           "    }\n"
           "\n"
           "    // One handler and one data write. Structure and skin stay in HTML and CSS.\n"
           "    app.document().bind(\"on_greet\", [&app](zui::Event&) {\n"
           "        app.document().set(\"greeting\", \"text\", \"Hello from C++\");\n"
           "        app.document().add_class(\"greeting\", \"greeted\");\n"
           "    });\n"
           "    return app.run();\n"
           "}\n";
}

std::string Templates::page(const Project& project) {
    return "<!DOCTYPE html>\n"
           "<html>\n"
           "<head>\n"
           "  <meta charset=\"utf-8\">\n"
           "  <meta name=\"zui-app-id\" content=\"" +
           xml_text(project.identifier) +
           "\">\n"
           "  <meta name=\"zui-window\" content=\"width=960,height=640\">\n"
           "  <title>" +
           xml_text(project.name) +
           "</title>\n"
           "  <link rel=\"stylesheet\" href=\"app.css\">\n"
           "</head>\n"
           "<body>\n"
           "  <main class=\"page\">\n"
           "    <h1>" +
           xml_text(project.name) +
           "</h1>\n"
           "    <p id=\"greeting\">Hello from zui</p>\n"
           "    <button id=\"greet\" class=\"primary\" onclick=\"on_greet\">Greet</button>\n"
           "  </main>\n"
           "</body>\n"
           "</html>\n";
}

std::string Templates::style(const Project& project) {
    (void)project;
    return "/* Standard CSS. The engine reads HTML and CSS; C++ binds data. */\n"
           "body {\n"
           "  margin: 0;\n"
           "  background: #f4f7f2;\n"
           "  color: #1f2a24;\n"
           "  font-family: \"Segoe UI\", \"Microsoft YaHei UI\", sans-serif;\n"
           "  font-size: 15px;\n"
           "  line-height: 1.5;\n"
           "}\n"
           "\n"
           ".page {\n"
           "  max-width: 40rem;\n"
           "  margin: 0 auto;\n"
           "  padding: 3rem 1.5rem;\n"
           "}\n"
           "\n"
           "h1 {\n"
           "  margin: 0 0 1rem;\n"
           "  font-size: 1.6rem;\n"
           "}\n"
           "\n"
           "#greeting {\n"
           "  margin: 0 0 1.25rem;\n"
           "  color: #55605a;\n"
           "}\n"
           "\n"
           ".greeted {\n"
           "  color: #2c6b4e;\n"
           "  font-weight: 600;\n"
           "}\n"
           "\n"
           "button.primary {\n"
           "  padding: 0.6rem 1.1rem;\n"
           "  border: 0;\n"
           "  border-radius: 8px;\n"
           "  background: #2c6b4e;\n"
           "  color: #f4f7f2;\n"
           "  font-size: 15px;\n"
           "}\n"
           "\n"
           "button.primary:hover {\n"
           "  background: #35795a;\n"
           "}\n"
           "\n"
           "button.primary:active {\n"
           "  background: #23553e;\n"
           "}\n";
}

std::string Templates::icon(const Project& project) {
    std::string glyph = "Z";
    if (!project.name.empty()) {
        const unsigned char c = static_cast<unsigned char>(project.name[0]);
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            glyph.assign(1, static_cast<char>(c));
        }
    }
    return "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 64 64\">\n"
           "  <rect width=\"64\" height=\"64\" rx=\"14\" fill=\"#2c6b4e\"/>\n"
           "  <text x=\"32\" y=\"44\" text-anchor=\"middle\" font-size=\"34\" fill=\"#f4f7f2\">" +
           glyph + "</text>\n</svg>\n";
}

std::string Templates::cmake(const Project& project) {
    std::vector<std::string> extra;
    auto add = [&extra](const std::string& rel) {
        for (const std::string& have : extra) {
            if (have == rel) {
                return;
            }
        }
        extra.push_back(rel);
    };
    add(project.page);
    add(project.icon);
    for (const std::string& rel : project.files) {
        add(rel);
    }
    std::string copies;
    for (const std::string& rel : extra) {
        const std::string source = "${CMAKE_CURRENT_SOURCE_DIR}/" + rel;
        const std::string destination = "$<TARGET_FILE_DIR:" + project.name + ">/" + rel;
        if (Templates::is_directory_entry(rel)) {
            copies += "  COMMAND ${CMAKE_COMMAND} -E copy_directory\n    \"" + source + "\"\n    \"" +
                      destination + "\"\n";
        } else {
            copies += "  COMMAND ${CMAKE_COMMAND} -E copy_if_different\n    \"" + source +
                      "\"\n    \"" + destination + "\"\n";
        }
    }
    const std::string app = project.name;
    return "cmake_minimum_required(VERSION 3.24)\n"
           "project(" +
           app +
           " LANGUAGES CXX)\n"
           "\n"
           "if(NOT DEFINED ZUI_ROOT OR NOT DEFINED ZUI_BUILD)\n"
           "  message(FATAL_ERROR \"configure with -DZUI_ROOT=<zui> and -DZUI_BUILD=<zui build>\")\n"
           "endif()\n"
           "\n"
           "# The engine as a target: linking it also stages its runtime libraries\n"
           "# (and any plugin that is imported the same way) next to this program.\n"
           "add_library(zui::zui SHARED IMPORTED)\n"
           "target_include_directories(zui::zui INTERFACE\n"
           "  \"${ZUI_ROOT}/include\"\n"
           "  \"${ZUI_BUILD}/generated/include\")\n"
           "if(WIN32)\n"
           "  set_target_properties(zui::zui PROPERTIES\n"
           "    IMPORTED_IMPLIB \"${ZUI_BUILD}/zui.lib\"\n"
           "    IMPORTED_LOCATION \"${ZUI_BUILD}/zui.dll\")\n"
           "else()\n"
           "  set_target_properties(zui::zui PROPERTIES IMPORTED_LOCATION \"${ZUI_BUILD}/libzui.so\")\n"
           "endif()\n"
           "\n"
           "if(ANDROID)\n"
           "  # An android app is a shared library whose main() SDL calls on its native\n"
           "  # thread. zui.json names this target in android.target.\n"
           "  add_library(" +
           app + "_android SHARED " + project.entry +
           ")\n"
           "  target_compile_features(" +
           app +
           "_android PRIVATE cxx_std_17)\n"
           "  target_link_libraries(" +
           app +
           "_android PRIVATE zui::zui)\n"
           "  set_target_properties(" +
           app +
           "_android PROPERTIES OUTPUT_NAME \"main\")\n"
           "else()\n"
           "  add_executable(" +
           app + " " + project.entry +
           ")\n"
           "  target_compile_features(" +
           app +
           " PRIVATE cxx_std_17)\n"
           "  target_link_libraries(" +
           app +
           " PRIVATE zui::zui)\n"
           "  # A plugin is one more imported target, for example:\n"
           "  #   add_library(zui::plugin_notify SHARED IMPORTED)\n"
           "  #   set_target_properties(zui::plugin_notify PROPERTIES\n"
           "  #     IMPORTED_IMPLIB \"${ZUI_BUILD}/zui_plugin_notify.lib\"\n"
           "  #     IMPORTED_LOCATION \"${ZUI_BUILD}/zui_plugin_notify.dll\")\n"
           "  #   target_link_libraries(" +
           app +
           " PRIVATE zui::plugin_notify)\n"
           "\n"
           "  # Page, icon, declared runtime files, and the runtime libraries, next to\n"
           "  # the executable: the project runs from its own build directory.\n"
           "  # The engine's plugin libraries are what zui.dll imports in turn, so\n"
           "  # they are staged with it.\n"
           "  file(GLOB ZUI_RUNTIME_LIBS\n"
           "    \"${ZUI_BUILD}/zui.dll\"\n"
           "    \"${ZUI_BUILD}/zui_plugin_*.dll\"\n"
           "    \"${ZUI_BUILD}/libzui.so\"\n"
           "    \"${ZUI_BUILD}/libzui_plugin_*.so\")\n"
           "  add_custom_command(TARGET " +
           app +
           " POST_BUILD\n"
           "    COMMAND ${CMAKE_COMMAND} -E copy_if_different\n"
           "      \"$<TARGET_RUNTIME_DLLS:" +
           app +
           ">\" ${ZUI_RUNTIME_LIBS}\n"
           "      \"$<TARGET_FILE_DIR:" +
           app +
           ">\"\n"
           "    COMMAND_EXPAND_LISTS\n" + copies + "  )\n"
           "endif()\n"
           "\n"
           "if(MSVC)\n"
           "  target_compile_options(" +
           app +
           " PRIVATE /utf-8)\n"
           "endif()\n";
}

}  // namespace zui_tool
