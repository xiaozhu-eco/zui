#include "actions.h"

#include <zui/app.h>

#include <filesystem>

int main(int argc, char** argv) {
    zui::App app(argc, argv);
    std::filesystem::path html;
    if (argc > 1) {
        html = argv[1];
    } else {
        html = std::filesystem::absolute(argv[0]).parent_path() / "hello.html";
    }
    if (!app.document().load(html.string())) {
        return 1;
    }
    hello_example::bind_actions(app);
    return app.run();
}
