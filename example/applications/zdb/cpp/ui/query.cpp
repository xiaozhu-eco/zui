#include "ui/view.h"

#include "ui/models.h"

#include "util.h"

namespace zdb {

void paint_query_detail(Session& session) {
    zui::App& app = *session.app;
    show(app, "query-detail", session.query_detail_open);
    if (!session.query_detail_open) {
        return;
    }
    write(app, "query-detail-label", session.query_detail_label);
    write(app, "query-detail-value", session.query_detail_text);
}

void show_query(Session& session) {
    if (!session.query_detail_open) {
        session.query_pick_ref.clear();
    }
    zui::App& app = *session.app;
    const bool any = !session.results.empty();
    show(app, "query-note", !any);
    show(app, "query-list", false);   // the cards show every statement's result at once
    show(app, "query-error", false);
    show(app, "query-meta", false);
    if (!any) {
        write(app, "query-note", "执行后，结果会显示在这里。");
        show(app, "query-tables", false);
        show(app, "query-detail", false);
        return;
    }
    // Every result set gets a card, and each card's table is its own `TableModel` handed out by
    // `with_children` (docs/RFC_MODEL.md KD2). Row 0 of that table is the column header, so a
    // card is one child model instead of a head host plus a body host.
    ResultList list;
    for (std::size_t i = 0; i < session.results.size(); ++i) {
        const StatementResult& result = session.results[i];
        ResultList::Card card;
        card.label = "语句 " + std::to_string(i + 1) + " · ";
        if (!result.ok) {
            card.label += "失败";
            // `is-plain` is the native-only token that hides the table a template always emits;
            // the browser simply never builds one for these two cases.
            card.cls = "is-error is-plain";
            card.error = result.error.empty() ? "执行失败。" : result.error;
        } else if (result.columns.empty()) {
            card.label += "修改 " + std::to_string(result.rows_modified) + " 行";
            card.cls = "is-plain";
        } else {
            card.label += std::to_string(result.rows.size()) + " 行";
            if (result.truncated) {
                card.label += "，已截断";
            }
        }
        if (result.ms > 0) {
            const std::string ms = format_ms(result.ms);
            if (!ms.empty()) {
                card.meta = ms;
            }
        }
        card.has_table = result.ok && !result.columns.empty();
        if (card.has_table) {
            ResultTable& table = card.table;
            table.result_index = i;
            table.keys = result.columns;
            // Row 0: the column names. `ResultTable::cell_value` marks that row `zhead-btn`.
            table.cells.push_back(result.columns);
            for (const auto& row : result.rows) {
                std::vector<std::string> cells;
                cells.reserve(row.size());
                for (const SqlValue& value : row) {
                    cells.push_back(format_cell(value));
                }
                table.cells.push_back(std::move(cells));
            }
            stamp_query_classes(table, result.rows, session.query_pick_ref);
        }
        list.cards.push_back(std::move(card));
    }
    show(app, "query-tables", true);
    app.document().set_model("query-tables", list);
    if (session.reset_query_scroll) {
        app.document().set("query-results", "scroll_top", "0");
        session.reset_query_scroll = false;
    }
    paint_query_detail(session);
}

}  // namespace zdb
