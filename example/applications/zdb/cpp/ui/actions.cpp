#include "ui/view.h"

#include "ui/models.h"
#include "native_file.h"
#include "sql/db.h"
#include "util.h"

#include <cstdlib>
#include <string>

namespace zdb {
namespace {

int number_value(std::string_view text) {
    if (text.empty()) {
        return 0;
    }
    return std::atoi(std::string(text).c_str());
}

}  // namespace

void bind_actions(Session& session) {
    zui::App& app = *session.app;

    app.document().bind("on_open", [&session](zui::Event&) {
        std::filesystem::path path;
        if (!pick_open_sqlite(*session.app, path)) {
#ifndef _WIN32
            session.notice = "当前系统没有文件对话框。";
            session.notice_error = true;
            show_shell(session);
#endif
            return;
        }
        session.pending_path = std::move(path);
        const int ticket = ++session.generation;
        session.app->document().set("status-text", "text", "正在打开…");
        session.app->document().remove_class("status", "is-error");
        session.app->document().defer([&session, ticket] {
            if (ticket != session.generation) {
                return;
            }
            open_path(session, session.pending_path);
        });
    });

    app.document().bind("on_drop", [&session](zui::Event& event) {
        const zui::Drop* drop = event.is_drop();
        if (drop == nullptr || drop->count == 0) {
            return;
        }
        session.pending_path = std::filesystem::path(std::string(drop->path(0)));
        const int ticket = ++session.generation;
        session.app->document().set("status-text", "text", "正在打开…");
        session.app->document().remove_class("status", "is-error");
        session.app->document().defer([&session, ticket] {
            if (ticket != session.generation) {
                return;
            }
            open_path(session, session.pending_path);
        });
    });

    app.document().bind("on_sample", [&session](zui::Event&) {
        const int ticket = ++session.generation;
        session.app->document().set("status-text", "text", "正在生成示例库…");
        session.app->document().remove_class("status", "is-error");
        session.app->document().defer([&session, ticket] {
            if (ticket != session.generation) {
                return;
            }
            open_sample(session);
        });
    });

    app.document().bind("on_export", [&session](zui::Event&) {
        if (session.db == nullptr) {
            return;
        }
        std::filesystem::path path;
        if (!pick_save_sqlite(*session.app, path, session.file_name)) {
#ifndef _WIN32
            session.notice = "当前系统没有文件对话框。";
            session.notice_error = true;
            show_shell(session);
#endif
            return;
        }
        std::string error;
        if (!export_sqlite(session.db, path, error)) {
            session.notice = error.empty() ? "导出失败。" : error;
            session.notice_error = true;
        } else {
            session.notice = "已导出 " + utf8_path(path.filename());
            session.notice_error = false;
        }
        show_shell(session);
    });

    app.document().bind("on_object", [&session](zui::Event& event) {
        const std::string key(event.data("key"));
        const std::size_t cut = key.find('\x1f');
        if (cut == std::string::npos) {
            return;
        }
        select_named(session, key.substr(0, cut), key.substr(cut + 1));
    });

    app.document().bind("on_mode", [&session](zui::Event& event) {
        if (session.db == nullptr) {
            return;
        }
        const std::string mode(event.data("mode"));
        if (mode != "data" && mode != "structure" && mode != "query") {
            return;
        }
        session.mode = mode;
        show_shell(session);
    });

    app.document().bind("on_filter", [&session](zui::Event& event) {
        session.filter = event.value();
        show_sidebar(session);
    });

    app.document().bind("on_sort", [&session](zui::Event& event) { sort_column(session, std::string(event.data("name"))); });
    app.document().bind("on_page_prev", [&session](zui::Event&) { move_page(session, -1); });
    app.document().bind("on_page_next", [&session](zui::Event&) { move_page(session, 1); });
    app.document().bind("on_limit", [&session](zui::Event& event) { set_limit(session, number_value(event.value())); });

    app.document().bind("on_cell", [&session](zui::Event& event) {
        // The button has no per-cell id, so the picked class rides on `{{class}}` at the next stamp.
        // `data-cell` names the cell; the displayed text does not (two cells can show the same string).
        const std::string ref(event.data("cell"));
        std::size_t row = 0;
        std::size_t column = 0;
        if (grid_cell_at(ref, session.page.rows, row, column)) {
            const std::string name =
                column < session.page.columns.size() ? session.page.columns[column] : "";
            session.detail_label = name + " · 第 " +
                                   format_count(static_cast<std::int64_t>(session.page.offset) +
                                                static_cast<std::int64_t>(row) + 1) +
                                   " 行";
            session.detail_text = inspect_value(session.page.rows[row][column]);
            session.grid_pick_ref = ref;
        } else {
            match_clicked_cell(session.page.columns, session.page.rows, session.page.offset, event.value(),
                               session.detail_label, session.detail_text);
            session.grid_pick_ref.clear();
        }
        session.detail_open = true;
        show_grid(session);
    });
    app.document().bind("on_detail_close", [&session](zui::Event&) {
        session.detail_open = false;
        show_grid(session);
    });
    app.document().bind("on_show_structure", [&session](zui::Event&) {
        if (session.db == nullptr) {
            return;
        }
        session.mode = "structure";
        show_shell(session);
    });
    app.document().bind("on_open_table", [&session](zui::Event& event) {
        // The FK-jump buttons carry `data-table` from their template. The parent jump's markup
        // (`#struct-parent`) has no data attribute, so show_structure writes the table onto the
        // control's `value`, which the click payload carries. js/ui/structure.js uses the same
        // handler for both, driven by `data-jump`.
        std::string table(event.data("table"));
        if (table.empty()) {
            table = std::string(event.value());
        }
        if (table.empty()) {
            return;
        }
        select_named(session, "table", table);
    });

    auto run_sql = [&session]() {
        if (session.db == nullptr) {
            return;
        }
        session.pending_sql = session.app->document().get("sql-input", "value");
        const int ticket = ++session.generation;
        session.app->document().set("status-text", "text", "正在执行…");
        session.app->document().remove_class("status", "is-error");
        session.app->document().defer([&session, ticket] {
            if (ticket != session.generation) {
                return;
            }
            run_pending_sql(session);
        });
    };

    app.document().bind("on_run", [run_sql](zui::Event&) { run_sql(); });
    app.document().bind("on_sql_key", [run_sql](zui::Event& event) {
        const zui::Key* key = event.is_key();
        if (key == nullptr || key->key != "Enter" || key->composing) {
            return;
        }
        if (!key->ctrl && !key->meta) {
            return;
        }
        event.prevent_default();
        run_sql();
    });
    app.document().bind("on_clear", [&session](zui::Event&) {
        session.results.clear();
        session.result_index = 0;
        session.query_detail_open = false;
        session.notice = "已清空查询结果。";
        session.notice_error = false;
        show_shell(session);
        show_query(session);
    });
    app.document().bind("on_query_pick", [&session](zui::Event& event) {
        if (session.results.empty()) {
            return;
        }
        const int index = number_value(event.data("index"));
        if (index < 0 || static_cast<std::size_t>(index) >= session.results.size()) {
            return;
        }
        session.result_index = index;
        session.query_detail_open = false;
        show_query(session);
    });
    app.document().bind("on_query_detail_close", [&session](zui::Event&) {
        session.query_detail_open = false;
        show_query(session);
    });
    app.document().bind("on_query_cell", [&session](zui::Event& event) {
        // `data-cell` is the exact `result,row,column` this button stands for (RFC_MODEL.md KD6):
        // two cells that display the same text are still two cells. Row 0 is the card's header.
        // The picked class is stamped into `{{class}}` on the next paint; the button has no id.
        const std::string ref(event.data("cell"));
        QueryCellRef cell;
        if (!query_cell_at(ref, cell) || cell.row < 1 || cell.result >= session.results.size()) {
            return;
        }
        const StatementResult& result = session.results[cell.result];
        const std::size_t data_row = cell.row - 1;
        if (data_row >= result.rows.size() || cell.column >= result.columns.size() ||
            cell.column >= result.rows[data_row].size()) {
            return;
        }
        session.query_detail_label = result.columns[cell.column] + " · 第 " +
                                     std::to_string(data_row + 1) + " 行";
        session.query_detail_text = inspect_value(result.rows[data_row][cell.column]);
        session.query_pick_ref = ref;
        session.query_detail_open = true;
        show_query(session);
        // The browser grid opens `[data-detail]`, which is `#cell-detail`. The query pane also
        // keeps `#query-detail` for the native handler tests.
        session.detail_open = true;
        session.detail_label = session.query_detail_label;
        session.detail_text = session.query_detail_text;
        show_grid(session);
    });
}

}  // namespace zdb
