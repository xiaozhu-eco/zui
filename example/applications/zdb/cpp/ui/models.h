#ifndef ZDB_UI_MODELS_H
#define ZDB_UI_MODELS_H

#include <zui/model.h>

#include "../session.h"

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zdb {

class RecordList : public zui::ListModel {
public:
    struct Row {
        std::vector<std::pair<std::string, std::string>> fields;
    };

    std::vector<Row> rows;

    std::size_t row_count() const override { return rows.size(); }

    std::string value(std::size_t row, std::string_view key) const override {
        if (row >= rows.size()) {
            return {};
        }
        for (const auto& field : rows[row].fields) {
            if (field.first == key) {
                return field.second;
            }
        }
        return {};
    }
};

inline void put(RecordList::Row& row, std::string key, std::string value) {
    row.fields.emplace_back(std::move(key), std::move(value));
}

class TextTable : public zui::TableModel {
public:
    std::vector<std::string> keys;
    std::vector<std::vector<std::string>> cells;
    /// Per-cell class, parallel to `cells`. NULL, BLOB, an empty string, and a number all format
    /// to text a reader cannot tell apart from a label, so the kind travels beside the text.
    std::vector<std::vector<std::string>> tags;

    std::size_t row_count() const override { return cells.size(); }
    std::size_t column_count() const override { return keys.size(); }

    std::string column_key(std::size_t column) const override {
        return column < keys.size() ? keys[column] : std::string{};
    }

    std::string cell(std::size_t row, std::size_t column) const override {
        if (row >= cells.size() || column >= cells[row].size()) {
            return {};
        }
        return cells[row][column];
    }

    /// The per-cell keys a column template can use (RFC_MODEL.md KD6). `class` carries the SQL
    /// kind; `cell` is the exact `row,column` a click landed on - the displayed text is not an
    /// identity (two NULLs, or a value that happens to equal a row number, look the same).
    std::string cell_value(std::size_t row, std::size_t column, std::string_view key) const override {
        if (key == "class") {
            return row < tags.size() && column < tags[row].size() ? tags[row][column]
                                                                   : std::string{};
        }
        if (key == "cell") {
            if (row >= cells.size() || column >= cells[row].size()) {
                return {};
            }
            return std::to_string(row) + "," + std::to_string(column);
        }
        return TableModel::cell_value(row, column, key);
    }
};

/// One result set as a card's child model. Row 0 is the column header - KD4's "a group header is
/// just another row in the list" - and the data rows follow, so a card needs one child model, not
/// two hosts. `cell` carries the result index as well, because the click has to name the card it
/// landed in, not only the row and column.
class ResultTable : public TextTable {
public:
    std::size_t result_index = 0;

    std::string cell_value(std::size_t row, std::size_t column, std::string_view key) const override {
        if (key == "cell") {
            return std::to_string(result_index) + "," + std::to_string(row) + "," +
                   std::to_string(column);
        }
        if (key == "class" && row == 0) {
            // The shared stylesheet already paints a column head (`grid.css`'s `.zhead-btn`).
            return "zhead-btn";
        }
        return TextTable::cell_value(row, column, key);
    }
};

/// The query pane's outer list: one row per statement's result, and each row's own table handed
/// out through `with_children` (RFC_MODEL.md Q2 / KD2). This is what replaces "list the
/// statements, then look at one".
class ResultList : public zui::ListModel {
public:
    struct Card {
        std::string label;
        std::string meta;
        std::string error;
        std::string cls;
        bool has_table = false;
        ResultTable table;
    };

    std::vector<Card> cards;

    std::size_t row_count() const override { return cards.size(); }

    std::string value(std::size_t row, std::string_view key) const override {
        if (row >= cards.size()) {
            return {};
        }
        const Card& card = cards[row];
        if (key == "label") {
            return card.label;
        }
        if (key == "meta") {
            return card.meta;
        }
        if (key == "error") {
            return card.error;
        }
        if (key == "class") {
            return card.cls;
        }
        return {};
    }

    void with_children(std::size_t row,
                       const std::function<void(const zui::ListModel&)>& visit) const override {
        if (row < cards.size() && cards[row].has_table) {
            visit(cards[row].table);
        }
    }
};

/// Browser `display.js` names a number `cell-number`. Integer and Real share that token.
/// `cell_ref` is this button's `data-cell`; `pick_ref` is the ref the click stored.
inline std::string cell_classes(const SqlValue& value, std::string_view cell_ref,
                                std::string_view pick_ref) {
    std::string tag;
    if (value.kind == SqlKind::Null) {
        tag = "cell-null";
    } else if (value.kind == SqlKind::Blob) {
        tag = "cell-blob";
    } else if (value.kind == SqlKind::Text && value.text.empty()) {
        tag = "cell-empty";
    } else if (value.kind == SqlKind::Integer || value.kind == SqlKind::Real) {
        tag = "cell-number";
    }
    if (!pick_ref.empty() && cell_ref == pick_ref) {
        if (!tag.empty()) {
            tag.push_back(' ');
        }
        tag += "is-picked";
    }
    return tag;
}

inline bool parse_size_token(std::string_view text, std::size_t& i, std::size_t& value) {
    if (i >= text.size() || text[i] < '0' || text[i] > '9') {
        return false;
    }
    std::size_t parsed = 0;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        const std::size_t digit = static_cast<std::size_t>(text[i] - '0');
        if (parsed > (static_cast<std::size_t>(-1) - digit) / 10) {
            return false;
        }
        parsed = parsed * 10 + digit;
        ++i;
    }
    value = parsed;
    return true;
}

/// The grid click's `data-cell` (`row,column` on the current page).
inline bool grid_cell_at(std::string_view data_cell, const std::vector<std::vector<SqlValue>>& rows,
                         std::size_t& row, std::size_t& column) {
    std::size_t i = 0;
    std::size_t parsed_row = 0;
    std::size_t parsed_column = 0;
    if (!parse_size_token(data_cell, i, parsed_row) || i >= data_cell.size() || data_cell[i] != ',') {
        return false;
    }
    ++i;
    if (!parse_size_token(data_cell, i, parsed_column) || i != data_cell.size()) {
        return false;
    }
    if (parsed_row >= rows.size() || parsed_column >= rows[parsed_row].size()) {
        return false;
    }
    row = parsed_row;
    column = parsed_column;
    return true;
}

struct QueryCellRef {
    std::size_t result = 0;
    std::size_t row = 0;
    std::size_t column = 0;
};

/// The query click's `data-cell` (`result,row,column`). Row 0 is the card header.
inline bool query_cell_at(std::string_view data_cell, QueryCellRef& out) {
    std::size_t i = 0;
    QueryCellRef parsed;
    if (!parse_size_token(data_cell, i, parsed.result) || i >= data_cell.size() || data_cell[i] != ',') {
        return false;
    }
    ++i;
    if (!parse_size_token(data_cell, i, parsed.row) || i >= data_cell.size() || data_cell[i] != ',') {
        return false;
    }
    ++i;
    if (!parse_size_token(data_cell, i, parsed.column) || i != data_cell.size()) {
        return false;
    }
    out = parsed;
    return true;
}

/// Writes `table.tags` for a grid page. `pick_ref` is empty when the detail is closed.
inline void stamp_grid_classes(TextTable& table, const std::vector<std::vector<SqlValue>>& rows,
                               std::string_view pick_ref) {
    table.tags.clear();
    table.tags.reserve(rows.size());
    for (std::size_t r = 0; r < rows.size(); ++r) {
        const std::vector<SqlValue>& row = rows[r];
        std::vector<std::string> tags;
        tags.reserve(row.size());
        for (std::size_t c = 0; c < row.size(); ++c) {
            const std::string ref = std::to_string(r) + "," + std::to_string(c);
            tags.push_back(cell_classes(row[c], ref, pick_ref));
        }
        table.tags.push_back(std::move(tags));
    }
}

/// Writes `table.tags` for one query card. Row 0 is the header (`ResultTable` forces `zhead-btn`);
/// `rows` are the SQL data rows, so model row `r + 1` is data row `r`.
inline void stamp_query_classes(ResultTable& table, const std::vector<std::vector<SqlValue>>& rows,
                                std::string_view pick_ref) {
    table.tags.clear();
    table.tags.reserve(rows.size() + 1);
    table.tags.push_back(std::vector<std::string>(table.keys.size()));
    for (std::size_t r = 0; r < rows.size(); ++r) {
        const std::vector<SqlValue>& row = rows[r];
        std::vector<std::string> tags;
        tags.reserve(row.size());
        const std::size_t model_row = r + 1;
        for (std::size_t c = 0; c < row.size(); ++c) {
            const std::string ref = std::to_string(table.result_index) + "," + std::to_string(model_row) +
                                    "," + std::to_string(c);
            tags.push_back(cell_classes(row[c], ref, pick_ref));
        }
        table.tags.push_back(std::move(tags));
    }
}

}  // namespace zdb

#endif
