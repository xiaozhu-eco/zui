#ifndef ZUI_EXAMPLE_TESTS_TRANSITION_ACTIONS_H
#define ZUI_EXAMPLE_TESTS_TRANSITION_ACTIONS_H

// The program's handlers, extracted so `main()` and the runtime tests drive the same code. The six
// flags live in one shared record because the lambdas outlive this function.

#include <zui/app.h>

#include <memory>
#include <string>

namespace transition_example {

struct Flags {
    bool cascade = false;
    bool hero = false;
    bool toast = false;
    bool drawer = false;
    bool sheet = false;
    bool day = false;
};

inline void bind_actions(zui::App& app) {
    auto state = std::make_shared<Flags>();
    auto set_flag = [&app](bool& flag, const char* id, const char* cls) {
        flag = !flag;
        if (flag) {
            app.document().add_class(id, cls);
        } else {
            app.document().remove_class(id, cls);
        }
    };
    auto status = [&app, state]() {
        std::string s = state->cascade ? "in" : "out";
        s += " · hero ";
        s += state->hero ? "cyan" : "red";
        s += " · toast ";
        s += state->toast ? "on" : "off";
        s += " · drawer ";
        s += state->drawer ? "open" : "shut";
        s += " · sheet ";
        s += state->sheet ? "up" : "down";
        s += " · ";
        s += state->day ? "day" : "night";
        app.document().set("status", "text", s);
    };

    app.document().bind("on_cascade", [&app, state, status](zui::Event&) {
        state->cascade = !state->cascade;
        if (state->cascade) {
            app.document().add_class("tiles", "in");
            app.document().add_class("feed", "in");
            app.document().add_class("eq", "on");
        } else {
            app.document().remove_class("tiles", "in");
            app.document().remove_class("feed", "in");
            app.document().remove_class("eq", "on");
        }
        status();
    });
    app.document().bind("on_hero", [&app, state, set_flag, status](zui::Event&) {
        set_flag(state->hero, "hero", "alt");
        status();
    });
    app.document().bind("on_toast", [&app, state, set_flag, status](zui::Event&) {
        set_flag(state->toast, "toast", "show");
        status();
    });
    app.document().bind("on_drawer", [&app, state, set_flag, status](zui::Event&) {
        set_flag(state->drawer, "drawer", "open");
        status();
    });
    app.document().bind("on_sheet", [&app, state, set_flag, status](zui::Event&) {
        set_flag(state->sheet, "mask", "show");
        status();
    });
    app.document().bind("on_stage", [&app, state, set_flag, status](zui::Event&) {
        set_flag(state->day, "stage", "day");
        status();
    });
    app.document().bind("on_play", [&app, state, status](zui::Event&) {
        state->cascade = !state->cascade;
        state->hero = !state->hero;
        state->toast = !state->toast;
        state->drawer = !state->drawer;
        state->sheet = !state->sheet;
        state->day = !state->day;
        const auto bump = [&app](const char* id, const char* cls, bool on) {
            if (on) {
                app.document().add_class(id, cls);
            } else {
                app.document().remove_class(id, cls);
            }
        };
        bump("tiles", "in", state->cascade);
        bump("feed", "in", state->cascade);
        bump("eq", "on", state->cascade);
        bump("hero", "alt", state->hero);
        bump("toast", "show", state->toast);
        bump("drawer", "open", state->drawer);
        bump("mask", "show", state->sheet);
        bump("stage", "day", state->day);
        status();
    });
}

}  // namespace transition_example

#endif
