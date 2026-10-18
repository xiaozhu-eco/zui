// zui_create writes a product project. In a terminal it asks for each answer
// the command line left out; a script that passes them does not read the
// terminal. The command line is Cli, the questions are Wizard, the tree is
// ProjectWriter, and the file bodies are Templates.

#include "cli.h"
#include "project_files.h"
#include "style.h"
#include "template.h"
#include "wizard.h"

#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
    const zui_tool::Cli::Answers answers = zui_tool::Cli::parse(argc, argv);
    if (answers.help || !answers.error.empty()) {
        zui_tool::Cli::usage();
        if (!answers.error.empty() && !answers.help) {
            std::cerr << zui_tool::style_red(zui_tool::mark_no()) << " zui_create: " << answers.error
                      << "\n";
        }
        return answers.help && answers.error.empty() ? 0 : 1;
    }
    zui_tool::Project project;
    std::filesystem::path out;
    std::string error;
    zui_tool::Wizard wizard(answers, project, out);
    if (!wizard.run(error) || !zui_tool::valid_project(project, error)) {
        std::cerr << zui_tool::style_red(zui_tool::mark_no()) << " zui_create: " << error << "\n";
        return 1;
    }
    std::error_code exists_ec;
    if (std::filesystem::exists(out, exists_ec)) {
        std::cerr << zui_tool::style_red(zui_tool::mark_no()) << " zui_create: " << out.string()
                  << " already exists\n";
        return 1;
    }
    if (!zui_tool::ProjectWriter::write(out, project, error)) {
        std::cerr << zui_tool::style_red(zui_tool::mark_no()) << " zui_create: " << error << "\n";
        std::filesystem::remove_all(out, exists_ec);
        return 1;
    }
    zui_tool::style_setup();
    std::cout << zui_tool::style_green(zui_tool::mark_ok()) << " wrote " << out.string() << "\n";
    for (const std::string& rel : project.files) {
        if (zui_tool::Templates::is_directory_entry(rel) ||
            rel == zui_tool::Templates::app_css_rel(project)) {
            continue;
        }
        std::cout << "  " << zui_tool::style_yellow(zui_tool::mark_dot()) << " placeholder " << rel
                  << zui_tool::style_dim(", fill it in before packing") << "\n";
    }
    std::cout << "  " << zui_tool::style_dim("next: zui-pack " + out.string() +
                                            " --root <zui> --build <zui build>")
              << "\n";
    return 0;
}
