#ifndef ZUI_EXAMPLE_TESTS_HELLO_ACTIONS_H
#define ZUI_EXAMPLE_TESTS_HELLO_ACTIONS_H

// The program's handlers, extracted so `main()` and the runtime tests drive the same code. The
// counter is shared because the lambdas outlive this function.

#include <zui/app.h>

#include <memory>
#include <string>
#include <string_view>

namespace hello_example {

inline void bind_actions(zui::App& app) {
    auto projects = std::make_shared<int>(12);
    auto show_page = [&app](std::string_view title, std::string_view blurb, std::string_view nav) {
        app.document().set("heading", "text", title);
        app.document().set("blurb", "text", blurb);
        app.document().remove_class("nav_home", "active");
        app.document().remove_class("nav_projects", "active");
        app.document().remove_class("nav_settings", "active");
        app.document().add_class(nav, "active");
        app.document().set("status", "text", std::string("Opened ") + std::string(title));
    };

    app.document().bind("on_home", [&app, show_page](zui::Event&) {
        show_page("Home", "Structure in HTML, layout in CSS, behavior in C++.", "nav_home");
    });
    app.document().bind("on_projects", [&app, show_page](zui::Event&) {
        show_page("Projects", "Twelve workspaces on this machine.", "nav_projects");
    });
    app.document().bind("on_settings", [&app, show_page](zui::Event&) {
        show_page("Settings", "Window and renderer: ZUI_WINDOW / ZUI_RENDERER.", "nav_settings");
    });
    app.document().bind("on_name", [&app](zui::Event& event) {
        app.document().set("status", "text", std::string("Editing ") + std::string(event.value()));
    });
    app.document().bind("on_save", [&app](zui::Event&) {
        app.document().set("status", "text", "Saved profile for " + app.document().get("name", "value") + ".");
    });
    app.document().bind("on_inc", [&app, projects](zui::Event&) {
        ++*projects;
        app.document().set("stat_projects", "text", std::to_string(*projects));
        app.document().set("status", "text", "Projects is now " + std::to_string(*projects) + ".");
    });
}

}  // namespace hello_example

#endif
