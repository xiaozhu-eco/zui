#ifndef ZUI_EXAMPLE_TESTS_CHECKS_ACTIONS_H
#define ZUI_EXAMPLE_TESTS_CHECKS_ACTIONS_H

// The program's handler, extracted so `main()` and the runtime tests drive the same code.

#include <zui/app.h>

#include <memory>

namespace checks_example {

inline void bind_actions(zui::App& app) {
    auto sheet = std::make_shared<bool>(false);
    app.document().bind("on_sheet", [&app, sheet](zui::Event&) {
        *sheet = !*sheet;
        if (*sheet) {
            app.document().add_class("mask", "show");
        } else {
            app.document().remove_class("mask", "show");
        }
    });
}

}  // namespace checks_example

#endif
