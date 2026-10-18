#include "cpp/bench.h"
#include "cpp/session.h"
#include "cpp/util.h"

#include <zui/app.h>

#include <filesystem>
#include <string_view>

int main(int argc, char** argv) {
    // `zui_share bench ...` runs the headless transfer harness instead of the window.
    if (argc > 1 && std::string_view(argv[1]) == "bench") {
        return share::bench_main(argc - 1, argv + 1);
    }

    zui::App app(argc, argv);
    const std::filesystem::path exe_dir = std::filesystem::absolute(argv[0]).parent_path();
    std::filesystem::path html = exe_dir / "index.html";
    if (argc > 1) {
        html = argv[1];
    }
    if (!app.document().load(html.string())) {
        return 1;
    }
    // Identity + receive directory live with the rest of this app's user data.
    share::set_config_dir(app.host().user_data_dir());

    share::Session session(app);
    session.bind();
    session.show();
    const int rc = app.run();
    session.stop();
    return rc;
}
