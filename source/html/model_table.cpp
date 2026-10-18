#include "html/document.h"
#include "html/document_internal.h"
#include "html/model_internal.h"

#include <zui/model.h>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace zui {
namespace html {

/// The column stamp of a `TableModel` (RFC_MODEL.md KD6): one cell per column inside each row
/// host. A template with a single cell repeats it for every column; a template that already has
/// its own cells must have exactly `column_count()` of them, and anything else fails the
/// `set_model` rather than silently stamping the wrong number.
bool ModelBinder::apply_table_columns(Node* host, const TableModel& table, std::string& error,
                                      const std::function<void(Node*)>& retiring,
                                      bool* unchanged) {
    size_t row = 0;
    for (auto& entry : host->children) {
        if (entry == nullptr || entry->tag == "template") {
            continue;
        }
        Node* row_host = entry.get();
        Node* tmpl = find_template(row_host);
        if (tmpl == nullptr) {
            continue;
        }
        const size_t row_index = row++;
        const std::vector<Node*> protos = model_protos(tmpl);
        const size_t columns = table.column_count();
        if (protos.empty()) {
            error = "column template has no cell";
            return false;
        }
        if (protos.size() != 1 && protos.size() != columns) {
            error = "column template has " + std::to_string(protos.size()) + " cells for " +
                    std::to_string(columns) + " columns";
            return false;
        }
        const FieldFn field = [&table, row_index](size_t column, std::string_view key) {
            std::string v = table.cell_value(row_index, column, key);
            if (v.empty()) {
                if (key == "index" || key == "col") {
                    return std::to_string(column);
                }
                if (key == "row") {
                    return std::to_string(row_index);
                }
            }
            return v;
        };
        ProtoFn proto_of_row;
        if (protos.size() != 1) {
            proto_of_row = [](size_t column) { return column; };
        }
        bool same = false;
        if (!apply_on_node(row_host, columns, field, error, RowKeyFn{}, retiring, &same,
                           proto_of_row)) {
            return false;
        }
        if (!same) {
            // The row element keeps its box, so the top host's `sync_children` sees the same rows
            // and stops above the cells this stamp replaced. The row host is queued on its own.
            touched_hosts.push_back(row_host);
        }
        if (!same && unchanged != nullptr) {
            *unchanged = false;
        }
    }
    return true;
}

}  // namespace html
}  // namespace zui
