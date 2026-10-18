#include "session.h"

#include <zui/app.h>

#include <filesystem>

int main(int argc, char** argv) {
    zui::App app(argc, argv);
    app.host().set_app_id("zui-designer");
    std::filesystem::path html = std::filesystem::absolute(argv[0]).parent_path() / "index.html";
    if (argc > 1 && argv[1] != nullptr && argv[1][0] != '\0') {
        html = argv[1];
    }
    if (!app.document().load(html.string())) {
        return 1;
    }
    designer::Session session;
    session.app = &app;
    designer::bind_actions(session);
    designer::boot(session);
    return app.run();
}
