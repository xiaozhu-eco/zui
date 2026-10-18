#include "cpp/session.h"

#include <zui/app.h>

#include <filesystem>

int main(int argc, char** argv) {
    zui::App app(argc, argv);
    const std::filesystem::path exe_dir = std::filesystem::absolute(argv[0]).parent_path();
    std::filesystem::path html = exe_dir / "index.html";
    if (argc > 1) {
        html = argv[1];
    }
    if (!app.document().load(html.string())) {
        return 1;
    }
    calendar::Session session;
    session.app = &app;
    session.dir = app.host().user_data_dir();
    calendar::bind_actions(session);
    calendar::show(session);
    return app.run();
}
