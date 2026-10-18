#include "ui/view.h"

#include "ui/models.h"

#include "util.h"

namespace zdb {
namespace {

std::string kind_label(const std::string& type) {
    if (type == "table") {
        return "表";
    }
    if (type == "view") {
        return "视图";
    }
    if (type == "index") {
        return "索引";
    }
    if (type == "trigger") {
        return "触发器";
    }
    return type;
}

std::string range_text(const Page& page) {
    if (page.total <= 0) {
        return "0 行";
    }
    const std::int64_t from = static_cast<std::int64_t>(page.offset) + 1;
    std::int64_t to = static_cast<std::int64_t>(page.offset) + page.limit;
    if (to > page.total) {
        to = page.total;
    }
    return format_count(from) + "–" + format_count(to) + " / " + format_count(page.total) + " 行";
}

}  // namespace

void match_clicked_cell(const std::vector<std::string>& columns, const std::vector<std::vector<SqlValue>>& rows,
                        int offset, std::string_view shown, std::string& label, std::string& body) {
    struct Hit {
        std::size_t row = 0;
        std::size_t column = 0;
    };
    std::vector<Hit> hits;
    for (std::size_t row = 0; row < rows.size(); ++row) {
        for (std::size_t column = 0; column < rows[row].size(); ++column) {
            if (format_cell(rows[row][column]) == shown) {
                hits.push_back(Hit{row, column});
            }
        }
    }
    if (hits.size() == 1) {
        const Hit& hit = hits[0];
        const std::string name = hit.column < columns.size() ? columns[hit.column] : "";
        label = name + " · 第 " + format_count(static_cast<std::int64_t>(offset) + static_cast<std::int64_t>(hit.row) + 1) +
                " 行";
        body = inspect_value(rows[hit.row][hit.column]);
        return;
    }
    label = "单元格";
    if (hits.empty()) {
        body = shown.empty() ? "空字符串" : std::string(shown);
        return;
    }
    body = inspect_value(rows[hits[0].row][hits[0].column]);
    body += "\n\n有多格显示成同一段文字，点下去分不出是哪一格。";
}

void paint_detail(Session& session) {
    zui::App& app = *session.app;
    show(app, "cell-detail", session.detail_open);
    if (!session.detail_open) {
        return;
    }
    write(app, "detail-label", session.detail_label);
    write(app, "detail-value", session.detail_text);
}

void show_grid(Session& session) {
    if (!session.detail_open) {
        session.grid_pick_ref.clear();
    }
    zui::App& app = *session.app;
    const DbObject* object = selected_object(session);
    const bool ready = object != nullptr && session.page.browsable && session.page.error.empty();
    write(app, "grid-title", object != nullptr ? object->name : "");
    write(app, "grid-kind", object != nullptr ? kind_label(object->type) : "");
    write(app, "grid-total", ready ? format_count(session.page.total) + " 行" : "");
    show(app, "grid-title-row", object != nullptr);
    std::string empty;
    bool structure = false;
    if (object == nullptr) {
        empty = "从左侧选择一张表或视图。";
    } else if (!session.page.error.empty()) {
        empty = session.page.error;
    } else if (!session.page.browsable) {
        empty = "这个对象没有数据行。";
        structure = true;
    }
    write(app, "grid-empty", empty);
    show(app, "grid-empty", !ready);
    show(app, "grid-structure-wrap", structure);
    show(app, "grid-table", ready);
    show(app, "pager", ready);
    if (ready) {
        RecordList heads;
        for (const std::string& column : session.page.columns) {
            RecordList::Row row;
            put(row, "name", column);
            std::string label = column;
            std::string mark;
            if (session.page.order == column) {
                label += session.page.dir == "desc" ? " ↓" : " ↑";
                mark = "is-active";
            }
            put(row, "label", label);
            put(row, "mark", mark);
            heads.rows.push_back(std::move(row));
        }
        TextTable body;
        body.keys = session.page.columns;
        body.cells.reserve(session.page.rows.size());
        for (const auto& row : session.page.rows) {
            std::vector<std::string> cells;
            cells.reserve(row.size());
            for (const SqlValue& value : row) {
                cells.push_back(format_cell(value));
            }
            body.cells.push_back(std::move(cells));
        }
        stamp_grid_classes(body, session.page.rows, session.grid_pick_ref);
        app.document().set_model("grid-head", heads);
        app.document().set_model("grid-body", body);
        write(app, "page-range", range_text(session.page));
        const bool prev = session.page.offset > 0;
        const bool next = static_cast<std::int64_t>(session.page.offset) + session.page.limit < session.page.total;
        app.document().set("page-prev", "disabled", prev ? "false" : "true");
        app.document().set("page-next", "disabled", next ? "false" : "true");
        const int limits[] = {25, 50, 100, 200};
        for (int limit : limits) {
            const std::string id = "limit-" + std::to_string(limit);
            flag(app, id.c_str(), "is-on", session.page.limit == limit);
        }
        if (session.reset_grid_scroll) {
            app.document().set("grid-scroll", "scroll_top", "0");
            session.reset_grid_scroll = false;
        }
    }
    paint_detail(session);
}

}  // namespace zdb
