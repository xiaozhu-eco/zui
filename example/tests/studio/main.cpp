#include "actions.h"

#include <zui/app.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <functional>
#include <memory>

int main(int argc, char** argv) {
    zui::App app(argc, argv);

    std::filesystem::path html;
    if (argc > 1) {
        html = argv[1];
    } else {
        html = std::filesystem::absolute(argv[0]).parent_path() / "studio.html";
    }
    if (!app.document().load(html.string())) {
        return 1;
    }

    std::srand(static_cast<unsigned>(std::time(nullptr)));
    studio_example::Actions actions(app);
    actions.bind();

    // Debug harness: `ZUI_STUDIO_AUTOTOUR=<frames>` walks the interactions this page's grid is
    // measured through - shuffle (rebuild the ~10k-cell grid), scroll it, switch the theme, switch
    // the page - through the same state changes the handlers make, so a script can record what a
    // click costs on Release. Each step prints `[studio] tour <step>` to stderr first, which is what
    // lets the frame counters be sorted into steps. ZUI_STUDIO_AUTOTOUR_EXIT=1 quits at the end
    // (that is what flushes the trace). Inert unless the variable is set.
    if (const char* tour = std::getenv("ZUI_STUDIO_AUTOTOUR")) {
        const int settle = std::max(1, std::atoi(tour));
        const bool exit_at_end = std::getenv("ZUI_STUDIO_AUTOTOUR_EXIT") != nullptr;
        auto step = std::make_shared<int>(0);
        auto wait = std::make_shared<int>(settle);
        auto tick = std::make_shared<std::function<void()>>();
        *tick = [&, step, wait, tick, settle, exit_at_end]() {
            if (*wait > 0) {
                --*wait;
                app.document().defer(*tick);
                return;
            }
            switch (*step) {
                case 0:
                    std::fprintf(stderr, "[studio] tour shuffle\n");
                    actions.reshuffle();
                    break;
                case 1:
                    std::fprintf(stderr, "[studio] tour scroll_mid\n");
                    app.document().set("grid_scroll", "scroll_top", "600");
                    break;
                case 2:
                    std::fprintf(stderr, "[studio] tour scroll_top\n");
                    app.document().set("grid_scroll", "scroll_top", "0");
                    break;
                case 3:
                    std::fprintf(stderr, "[studio] tour theme_dawn\n");
                    app.document().set_theme("dawn");
                    break;
                case 4:
                    std::fprintf(stderr, "[studio] tour theme_night\n");
                    app.document().set_theme("night");
                    break;
                case 5:
                    std::fprintf(stderr, "[studio] tour page_log\n");
                    app.document().set("nav_log", "checked", "true");
                    app.document().set("nav_inspect", "checked", "false");
                    break;
                case 6:
                    std::fprintf(stderr, "[studio] tour page_inspect\n");
                    app.document().set("nav_inspect", "checked", "true");
                    app.document().set("nav_log", "checked", "false");
                    break;
                default:
                    std::fprintf(stderr, "[studio] tour done\n");
                    if (exit_at_end) {
                        app.quit();
                    }
                    return;
            }
            *wait = settle;
            ++*step;
            app.document().defer(*tick);
        };
        app.document().defer(*tick);
    }
    app.document().set_theme("night");
    return app.run();
}
