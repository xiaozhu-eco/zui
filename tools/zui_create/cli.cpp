#include "cli.h"

#include "project.h"
#include "utils.h"

#include <iostream>

namespace zui_tool {
namespace {

bool take_value(int& i, int argc, char** argv, const char* flag, std::string& out,
                std::string& error) {
    if (i + 1 >= argc) {
        error = std::string(flag) + " needs a value";
        return false;
    }
    out = argv[++i];
    if (out.empty()) {
        error = std::string(flag) + " needs a value";
        return false;
    }
    return true;
}

bool push_unique(std::vector<std::string>& items, const std::string& item, std::string& error,
                 const char* what) {
    for (const std::string& have : items) {
        if (have == item) {
            error = std::string(what) + " " + item + " is listed twice";
            return false;
        }
    }
    items.push_back(item);
    return true;
}

}  // namespace

void Cli::usage() {
    std::cerr
        << "zui_create [name] [directory] [options]\n"
        << "\n"
        << "Run zui_create in a terminal and it asks for the project name, directory,\n"
        << "version, application identifier, page, entry, icon, and package targets.\n"
        << "When Android is selected, it prints the mobile-permission catalog and\n"
        << "asks for each one: a stable name, the Android permission it becomes,\n"
        << "and a short reason. An empty name ends that list. Nothing is granted\n"
        << "unless it is listed. The tree is zui.json, src/main.cpp (includes only\n"
        << "<zui/app.h>), the page, the stylesheet, icons/icon.svg, CMakeLists.txt,\n"
        << "README.md, and .gitignore.\n"
        << "zui.json starts with \"schema\": 2. A newer schema fails instead of\n"
        << "being read wrong.\n"
        << "\n"
        << "An answer on the command line skips that question. With no terminal\n"
        << "and no name and directory, this prints the usage and stops.\n"
        << "--interactive asks the same questions when stdin is a pipe.\n"
        << "\n"
        << "  --version VER       default " << kDefaultVersion << "\n"
        << "  --identifier ID     default com.example.<name>\n"
        << "  --page REL          default ui/index.html\n"
        << "  --entry REL         default src/main.cpp\n"
        << "  --icon REL          default icons/icon.svg\n"
        << "  --target NAME       build an existing zui target instead of this tree\n"
        << "  --portable          desktop portable archive\n"
        << "  --installer         desktop installer (.deb on linux, setup.exe on windows)\n"
        << "  --android           Android APK\n"
        << "  --permission NAME   mobile permission (repeatable)\n"
        << "  --reason TEXT       reason for the preceding --permission\n"
        << "  --file REL          extra runtime file or directory (repeatable)\n"
        << "                      a path whose last segment has no extension is a\n"
        << "                      directory; a file starts as a placeholder the\n"
        << "                      author fills in\n"
        << "  --interactive       ask even when stdin is not a terminal\n"
        << "\n"
        << "Permissions:\n"
        << catalog_permission_names() << "\n"
        << "With no package flags and no terminal, the project selects a desktop\n"
        << "portable archive.\n";
}

Cli::Answers Cli::parse(int argc, char** argv) {
    Answers answers;
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            answers.help = true;
        } else if (arg == "--interactive") {
            interactive_forced() = true;
        } else if (arg == "--version") {
            if (!take_value(i, argc, argv, "--version", answers.version, answers.error)) {
                return answers;
            }
        } else if (arg == "--identifier") {
            if (!take_value(i, argc, argv, "--identifier", answers.identifier, answers.error)) {
                return answers;
            }
        } else if (arg == "--page") {
            if (!take_value(i, argc, argv, "--page", answers.page, answers.error)) {
                return answers;
            }
        } else if (arg == "--entry") {
            if (!take_value(i, argc, argv, "--entry", answers.entry, answers.error)) {
                return answers;
            }
        } else if (arg == "--icon") {
            if (!take_value(i, argc, argv, "--icon", answers.icon, answers.error)) {
                return answers;
            }
        } else if (arg == "--target") {
            if (!take_value(i, argc, argv, "--target", answers.target, answers.error)) {
                return answers;
            }
        } else if (arg == "--portable" || arg == "--installer" || arg == "--android") {
            answers.targets_set = true;
            const char* id =
                arg == "--portable" ? "portable" : arg == "--installer" ? "installer" : "android";
            if (!push_unique(answers.targets, id, answers.error, "target")) {
                return answers;
            }
        } else if (arg == "--permission") {
            std::string name;
            if (!take_value(i, argc, argv, "--permission", name, answers.error)) {
                return answers;
            }
            answers.permissions_set = true;
            for (const auto& have : answers.permissions) {
                if (have.first == name) {
                    answers.error = "permission " + name + " is listed twice";
                    return answers;
                }
            }
            answers.permissions.emplace_back(name, std::string());
        } else if (arg == "--reason") {
            std::string reason;
            if (!take_value(i, argc, argv, "--reason", reason, answers.error)) {
                return answers;
            }
            if (answers.permissions.empty() || !answers.permissions.back().second.empty()) {
                answers.error = "--reason needs a permission";
                return answers;
            }
            answers.permissions.back().second = reason;
        } else if (arg == "--file") {
            std::string rel;
            if (!take_value(i, argc, argv, "--file", rel, answers.error)) {
                return answers;
            }
            if (!push_unique(answers.files, rel, answers.error, "file")) {
                return answers;
            }
            answers.files_set = true;
        } else if (!arg.empty() && arg[0] == '-') {
            answers.error = "unknown option " + arg;
            return answers;
        } else {
            positional.push_back(arg);
        }
    }
    if (answers.help) {
        return answers;
    }
    if (positional.size() > 2) {
        answers.error = "expected a name and an output directory";
        return answers;
    }
    if (positional.size() >= 1) {
        answers.name = positional[0];
    }
    if (positional.size() >= 2) {
        answers.out_dir = positional[1];
    }
    if (!is_interactive() && (answers.name.empty() || answers.out_dir.empty())) {
        answers.error = "expected a name and an output directory";
    }
    return answers;
}

}  // namespace zui_tool
