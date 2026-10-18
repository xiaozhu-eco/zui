#ifndef ZUI_EXAMPLE_TESTS_STUDIO_ACTIONS_H
#define ZUI_EXAMPLE_TESTS_STUDIO_ACTIONS_H

// The program's models and handlers, extracted so `main()` and the runtime tests drive the same
// code. `reshuffle()` is shared with the debug tour in `main()`.

#include <zui/app.h>
#include <zui/model.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace studio_example {

class LogLines : public zui::ListModel {
public:
    std::vector<std::string> lines;
    size_t row_count() const override { return lines.size(); }
    std::string value(size_t row, std::string_view key) const override {
        return (row < lines.size() && key == "text") ? lines[row] : std::string{};
    }
};

class RandTable : public zui::TableModel {
public:
    std::vector<std::string> keys;
    std::vector<std::vector<std::string>> cells;

    void randomize() {
        int cols = 50 + std::rand() % 51;
        int rows = 50 + std::rand() % 51;
        // A fixed size when the harness asks for one (`ZUI_STUDIO_GRID=80x80`), so two builds can be
        // compared cell for cell - the random size makes every run a different amount of work.
        if (const char* fixed = std::getenv("ZUI_STUDIO_GRID")) {
            int want_cols = 0;
            int want_rows = 0;
            if (std::sscanf(fixed, "%dx%d", &want_cols, &want_rows) == 2 && want_cols > 0 &&
                want_rows > 0) {
                cols = want_cols;
                rows = want_rows;
            }
        }
        keys.clear();
        keys.reserve(static_cast<size_t>(cols));
        for (int c = 0; c < cols; ++c) {
            keys.push_back("C" + std::to_string(c + 1));
        }
        cells.assign(static_cast<size_t>(rows), std::vector<std::string>(static_cast<size_t>(cols)));
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                cells[static_cast<size_t>(r)][static_cast<size_t>(c)] =
                    std::to_string(10 * (r + 1) + c + (std::rand() % 9));
            }
        }
    }

    size_t row_count() const override { return cells.size(); }
    size_t column_count() const override { return keys.size(); }
    std::string column_key(size_t column) const override {
        return column < keys.size() ? keys[column] : std::string{};
    }
    std::string cell(size_t row, size_t column) const override {
        if (row >= cells.size() || column >= cells[row].size()) {
            return {};
        }
        return cells[row][column];
    }
};

class ColumnHeads : public zui::ListModel {
public:
    const zui::TableModel* table = nullptr;
    explicit ColumnHeads(const zui::TableModel& t) : table(&t) {}
    size_t row_count() const override { return table != nullptr ? table->column_count() : 0; }
    std::string value(size_t row, std::string_view key) const override {
        if (table == nullptr || (key != "text" && key != "key")) {
            return {};
        }
        return table->column_key(row);
    }
};

/// Owns the models the handlers read and write, so tests can drive the same object `main()` uses.
class Actions {
public:
    explicit Actions(zui::App& app) : app_(&app) {}

    void bind() {
        log_.lines = {
            "00  Nested overflow:auto inside the split pane.",
            "01  Wheel and the thumb should move only this list.",
            "02  The page shell stays put while this pane scrolls.",
            "03  Checkbox state is a class-free data field on the node.",
            "04  Open dialog loads export.html as a fragment.",
            "05  Mask flex-centers the rounded card.",
            "06  Label clicks toggle the checkbox they wrap.",
            "07  Nav uses radio :checked, not add_class.",
            "08  Keep adding lines so the log is taller than 220px.",
            "09  Line nine of the nested scroller.",
            "10  Line ten of the nested scroller.",
            "11  Line eleven of the nested scroller.",
            "12  Line twelve of the nested scroller.",
            "13  Line thirteen of the nested scroller.",
            "14  Line fourteen of the nested scroller.",
            "15  Last line - scroll to reach me.",
        };
        app_->document().set_model("log", log_);
        reshuffle();

        app_->document().bind("on_page", [this](zui::Event& event) {
            if (event.source() == "nav_inspect") {
                app_->document().set("heading", "text", "Inspect");
                app_->document().set("status", "text", "Inspect pane.");
            } else {
                app_->document().set("heading", "text", "Log");
                app_->document().set("status", "text", "Scroll the nested log list.");
            }
        });
        app_->document().bind("on_open", [this](zui::Event&) {
            app_->document().load_fragment("export.html");
            app_->document().set("status", "text", "Dialog open.");
        });
        app_->document().bind("on_close", [this](zui::Event&) {
            app_->document().unload_fragment("export");
            app_->document().set("status", "text", "Dialog closed.");
        });
        app_->document().bind("on_confirm", [this](zui::Event&) {
            app_->document().unload_fragment("export");
            const bool notify = app_->document().get("notify", "checked") == "true";
            app_->document().set("status", "text", notify ? "Exported (notify on)." : "Exported.");
        });
        app_->document().bind("on_theme", [this](zui::Event& event) {
            if (event.source() == "theme_dawn") {
                app_->document().set_theme("dawn");
                app_->document().set("status", "text", "Theme dawn.");
            } else {
                app_->document().set_theme("night");
                app_->document().set("status", "text", "Theme night.");
            }
        });
        app_->document().bind("on_reshuffle", [this](zui::Event&) {
            reshuffle();
            app_->document().set("status", "text", "Shuffled " + app_->document().get("grid_caption", "text") + ".");
        });
    }

    /// The grid rebuild `on_reshuffle` and the debug tour both run.
    void reshuffle() {
        table_.randomize();
        ColumnHeads heads(table_);
        app_->document().set_model("grid_cols", heads);
        app_->document().set_model("grid_body", table_);
        app_->document().set("grid_caption", "text",
                  std::to_string(table_.column_count()) + " x " +
                      std::to_string(table_.row_count()) + " grid");
    }

private:
    zui::App* app_ = nullptr;
    LogLines log_;
    RandTable table_;
};

}  // namespace studio_example

#endif
