#pragma once

// Interactive wizard. A non-interactive run keeps the defaults and does not
// read the terminal. Questions are asked only for answers the command line
// left out. Invalid answers are asked again.

#include "project.h"
#include "template.h"
#include "style.h"
#include "cli.h"
#include "utils.h"
#include "version.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace zui_tool {

/// The interactive wizard: every question the command line left out, asked once,
/// with invalid answers asked again. It owns the terminal, not the filesystem.
class Wizard {
public:
    Wizard(const Cli::Answers& answers, Project& project, std::filesystem::path& out)
        : answers_(answers), project_(project), out_(out) {}

    /// Fills the project with the answers, then writes nothing itself.
    bool run(std::string& error);

private:

struct Line {
    bool eof = false;
    std::string value;
};

Line ask_line(const std::string& prompt, const std::string& fallback) {
    if (!is_interactive()) {
        return {false, fallback};
    }
    prompted_ = true;
    std::cerr << style_cyan(mark_ask()) << " " << prompt << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) {
        return {true, {}};
    }
    line = trim_copy(line);
    return {false, line.empty() ? fallback : line};
}

template <typename Accept>
bool ask_until(const std::string& prompt, const std::string& fallback, Accept accept, std::string& value,
               std::string& error) {
    for (;;) {
        const Line line = ask_line(prompt, fallback);
        if (line.eof) {
            error = "cancelled";
            return false;
        }
        std::string why;
        if (accept(line.value, why)) {
            value = line.value;
            return true;
        }
        if (!is_interactive()) {
            error = why;
            return false;
        }
        std::cerr << "  " << style_red(mark_no() + std::string(" ")) << style_red(why) << "\n";
    }
}

bool ask_yes(const std::string& prompt, bool default_yes, bool& yes, std::string& error) {
    const std::string fallback = default_yes ? "y" : "n";
    const std::string shown = prompt + (default_yes ? " [Y/n]: " : " [y/N]: ");
    for (;;) {
        const Line line = ask_line(shown, fallback);
        if (line.eof) {
            error = "cancelled";
            return false;
        }
        const std::string value = lower_copy(line.value);
        if (value == "y" || value == "yes") {
            yes = true;
            return true;
        }
        if (value == "n" || value == "no") {
            yes = false;
            return true;
        }
        if (!is_interactive()) {
            error = "answer y or n";
            return false;
        }
        std::cerr << "  " << style_red(mark_no() + std::string(" answer y or n")) << "\n";
    }
}

bool accept_rel(const std::string& label, const std::string& text, std::string& why) {
    if (!valid_rel_path(text)) {
        why = label + " " + text + " is outside the project";
        return false;
    }
    return true;
}

std::vector<std::string> split_commas(const std::string& line) {
    std::vector<std::string> out;
    std::string token;
    for (std::size_t i = 0; i <= line.size(); ++i) {
        if (i == line.size() || line[i] == ',') {
            token = trim_copy(token);
            if (!token.empty()) {
                out.push_back(token);
            }
            token.clear();
        } else {
            token.push_back(line[i]);
        }
    }
    return out;
}

bool ask_targets(Project& project, std::string& error) {
    for (;;) {
        bool portable = false;
        bool installer = false;
        bool android = false;
        if (!ask_yes("Portable archive", true, portable, error) ||
            !ask_yes("Installer", false, installer, error) ||
            !ask_yes("Android APK", false, android, error)) {
            return false;
        }
        project.targets.clear();
        if (portable) {
            project.targets.emplace_back("portable");
        }
        if (installer) {
            project.targets.emplace_back("installer");
        }
        if (android) {
            project.targets.emplace_back("android");
        }
        if (!project.targets.empty()) {
            return true;
        }
        if (!is_interactive()) {
            error = "missing targets";
            return false;
        }
        std::cerr << "  " << style_red(mark_no() + std::string(" select at least one package target")) << "\n";
    }
}

bool ask_permissions(Project& project, std::string& error) {
    std::cerr << "\n  " << style_bold("Mobile permissions") << "\n";
    std::cerr << "  " << style_dim("Each entry is a stable name, the Android permission it becomes,")
              << "\n";
    std::cerr << "  " << style_dim("and a short reason. An empty name ends the list. Nothing is")
              << "\n";
    std::cerr << "  " << style_dim("granted unless it is listed.") << "\n\n";
    for (const CatalogPermission& item : kPermissionCatalog) {
        std::string tag;
        if (item.settings) {
            tag = " " + style_yellow("(opens system settings)");
        } else if (item.runtime) {
            tag = " " + style_yellow("(the system asks at run time)");
        }
        std::cerr << "  " << style_cyan(mark_dot()) << " " << style_bold(item.name) << tag << "\n";
        std::cerr << "      " << style_dim(item.android) << "\n";
        std::cerr << "      " << style_dim(item.reason) << "\n";
    }
    std::cerr << "\n";
    for (;;) {
        std::string name;
        if (!ask_until(
                "Permission [none]: ", "",
                [&](const std::string& text, std::string& why) {
                    if (text.empty()) {
                        return true;
                    }
                    if (find_catalog_permission(text) == nullptr) {
                        why = "unknown permission " + text;
                        return false;
                    }
                    for (const Permission& have : project.permissions) {
                        if (have.name == text) {
                            why = "permission " + text + " is listed twice";
                            return false;
                        }
                    }
                    return true;
                },
                name, error)) {
            return false;
        }
        if (name.empty()) {
            return true;
        }
        const CatalogPermission* known = find_catalog_permission(name);
        std::string reason;
        if (!ask_until(
                "Reason [" + std::string(known->reason) + "]: ", known->reason,
                [&](const std::string& text, std::string& why) {
                    Permission permission;
                    return resolve_permission(name, text, permission, why);
                },
                reason, error)) {
            return false;
        }
        Permission permission;
        if (!resolve_permission(name, reason, permission, error)) {
            return false;
        }
        project.permissions.push_back(permission);
    }
}

bool ask_files(Project& project, std::string& error) {
    std::string line;
    if (!ask_until(
            "Extra files [none]: ", "",
            [&](const std::string& text, std::string& why) {
                for (const std::string& rel : split_commas(text)) {
                    if (!valid_rel_path(rel)) {
                        why = "file " + rel + " is outside the project";
                        return false;
                    }
                }
                return true;
            },
            line, error)) {
        return false;
    }
    project.files = split_commas(line);
    return true;
}

void print_summary(const Project& project, const std::filesystem::path& directory) {
    auto row = [](const char* label, const std::string& value) {
        std::cerr << "  " << style_dim(std::string(label) + std::string(12 - std::strlen(label), ' '))
                  << value << "\n";
    };
    std::cerr << "\n  " << style_bold("Project") << "\n";
    row("name", project.name);
    row("directory", directory.string());
    row("version", project.version);
    row("identifier", project.identifier);
    row("page", project.page);
    row("entry", project.entry);
    row("icon", project.icon);
    std::string targets;
    for (const std::string& target : project.targets) {
        targets += targets.empty() ? target : ", " + target;
    }
    row("targets", targets);
    if (project.permissions.empty()) {
        row("permissions", style_dim("none"));
    } else {
        std::cerr << "  " << style_dim("permissions") << "\n";
        for (const Permission& permission : project.permissions) {
            std::cerr << "    " << style_cyan(mark_dot()) << " " << permission.name << "  "
                      << style_dim(permission.android) << "\n";
            std::cerr << "      " << style_dim(permission.reason) << "\n";
        }
    }
    if (!project.files.empty()) {
        std::string files;
        for (const std::string& file : project.files) {
            files += files.empty() ? file : ", " + file;
        }
        row("files", files);
    }
}

    const Cli::Answers& answers_;
    Project& project_;
    std::filesystem::path& out_;
    bool prompted_ = false;
};

inline bool Wizard::run(std::string& error) {
    const Cli::Answers& answers = answers_;
    Project& project = project_;
    std::filesystem::path& directory = out_;
    prompted_ = false;
    const bool wizard = is_interactive();
    bool android_selected = false;
    for (const std::string& target : answers.targets) {
        if (target == "android") {
            android_selected = true;
        }
    }
    const bool will_ask = wizard && (answers.name.empty() || answers.out_dir.empty() || answers.version.empty() ||
                                     answers.identifier.empty() || answers.page.empty() || answers.entry.empty() ||
                                     answers.icon.empty() || !answers.targets_set || !answers.files_set ||
                                     (android_selected && !answers.permissions_set));
    if (will_ask) {
        style_setup();
        print_banner("zui_create", kToolVersion,
                     "An HTML page, a skin, and one C++ file that binds data.");
    }

    if (!answers.name.empty()) {
        project.name = answers.name;
    } else if (!ask_until(
                   "Project name: ", "",
                   [](const std::string& text, std::string& why) {
                       if (!valid_name(text)) {
                           why = "name must be a C identifier";
                           return false;
                       }
                       return true;
                   },
                   project.name, error)) {
        return false;
    }

    if (!answers.out_dir.empty()) {
        directory = answers.out_dir;
    } else {
        std::string dir;
        if (!ask_until(
                "Directory [" + project.name + "]: ", project.name,
                [](const std::string& text, std::string& why) {
                    if (text.empty()) {
                        why = "directory is required";
                        return false;
                    }
                    std::error_code ec;
                    if (std::filesystem::exists(text, ec)) {
                        why = text + " already exists";
                        return false;
                    }
                    return true;
                },
                dir, error)) {
            return false;
        }
        directory = dir;
    }

    if (!answers.version.empty()) {
        project.version = answers.version;
    } else if (!ask_until(
                   "Version [" + std::string(kDefaultVersion) + "]: ", kDefaultVersion,
                   [](const std::string& text, std::string& why) {
                       if (!valid_version(text)) {
                           why = "version " + text + " is not a dotted number";
                           return false;
                       }
                       return true;
                   },
                   project.version, error)) {
        return false;
    }

    const std::string default_id = "com.example." + lower_copy(project.name);
    if (!answers.identifier.empty()) {
        project.identifier = answers.identifier;
    } else if (!ask_until(
                   "Identifier [" + default_id + "]: ", default_id,
                   [](const std::string& text, std::string& why) {
                       if (!valid_identifier(text)) {
                           why = "identifier " + text + " is not an application id";
                           return false;
                       }
                       return true;
                   },
                   project.identifier, error)) {
        return false;
    }

    if (!answers.page.empty()) {
        project.page = answers.page;
    } else if (!ask_until(
                   "Page [ui/index.html]: ", "ui/index.html",
                   [this](const std::string& text, std::string& why) { return accept_rel("page", text, why); },
                   project.page, error)) {
        return false;
    }

    if (!answers.entry.empty()) {
        project.entry = answers.entry;
    } else if (!ask_until(
                   "Entry [src/main.cpp]: ", "src/main.cpp",
                   [this](const std::string& text, std::string& why) { return accept_rel("entry", text, why); },
                   project.entry, error)) {
        return false;
    }

    if (!answers.icon.empty()) {
        project.icon = answers.icon;
    } else if (!ask_until(
                   "Icon [icons/icon.svg]: ", "icons/icon.svg",
                   [this](const std::string& text, std::string& why) { return accept_rel("icon", text, why); },
                   project.icon, error)) {
        return false;
    }

    project.target = answers.target;
    if (answers.targets_set) {
        project.targets = answers.targets;
    } else if (wizard) {
        if (!ask_targets(project, error)) {
            return false;
        }
    } else {
        project.targets.emplace_back("portable");
    }

    project.files = answers.files;
    if (answers.permissions_set) {
        for (const auto& item : answers.permissions) {
            Permission permission;
            if (!resolve_permission(item.first, item.second, permission, error)) {
                return false;
            }
            project.permissions.push_back(permission);
        }
    } else if (has_target(project, "android") && wizard) {
        if (!ask_permissions(project, error)) {
            return false;
        }
    }

    if (!answers.files_set && wizard) {
        if (!ask_files(project, error)) {
            return false;
        }
    }

    // The page links "app.css" beside it, so that file has to exist and be
    // declared. It is part of the scaffold, not an extra the author adds.
    const std::string css = Templates::app_css_rel(project);
    if (std::find(project.files.begin(), project.files.end(), css) == project.files.end()) {
        project.files.insert(project.files.begin(), css);
    }
    // The generated CMakeLists defines this android library: a shared library
    // whose main() SDL calls on its native thread.
    if (has_target(project, "android") && project.android.target.empty()) {
        project.android.target = project.name + "_android";
    }

    if (wizard) {
        print_summary(project, directory);
        bool create = false;
        if (!ask_yes("\nCreate this project?", true, create, error)) {
            return false;
        }
        if (!create) {
            error = "cancelled";
            return false;
        }
    }
    return true;
}

}  // namespace zui_tool
