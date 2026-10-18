// Nested `with_children` hosts after the parent row stamp (RFC_MODEL.md Q1 / section 6).

#include "html/document.h"
#include "html/document_internal.h"
#include "html/model_internal.h"
#include "log/log.h"

#include <zui/model.h>

#include <functional>
#include <string>
#include <vector>

namespace zui {
namespace html {

namespace {

/// The row nesting the engine expands before it stops. This is the one depth inside the model
/// layer that grows without an application-visible bound, so the walk stops here and says so once
/// (RFC_MODEL.md section 6).
constexpr std::size_t kMaxNestingDepth = 8;

void note_depth_limit() {
    static bool warned = false;
    if (!warned) {
        warned = true;
        ZUI_WARN("zui::html::apply_model: nesting is deeper than {} levels; the deeper rows keep "
                 "what is stamped",
                 kMaxNestingDepth);
    }
}

}  // namespace

bool ModelBinder::apply_nested(Node* host, size_t row_count, int stride,
                               const std::vector<size_t>& row_head, const ListModel* nested,
                               size_t depth, const std::function<void(Node*)>& retiring,
                               std::string& error, bool* nested_same) {
    if (nested_same != nullptr) {
        *nested_same = true;
    }
    if (nested == nullptr || row_count == 0) {
        return true;
    }
    bool same = true;
        for (size_t row = 0; row < row_count; ++row) {
            std::vector<Node*> group;
            for (int s = 0; s < stride; ++s) {
                const size_t index = row_head[row] + static_cast<size_t>(s);
                if (index < host->children.size()) {
                    group.push_back(host->children[index].get());
                }
            }
            Node* sub = nested_host_of(group);
            if (sub == nullptr) {
                continue;
            }
            // A `TableModel`'s row template holds the *column* template: its parent - the row
            // element itself - is exactly where `apply_table_columns` stamps the cells, not a
            // nested child-model host. Treating it as one would retire the cells the column stamp
            // writes into that row, on every `set_model` (RFC_MODEL.md KD6).
            if (dynamic_cast<const TableModel*>(nested) != nullptr && group.size() == 1 &&
                sub == group[0]) {
                continue;
            }
            if (depth + 1 > kMaxNestingDepth) {
                note_depth_limit();
                continue;
            }
            bool visited = false;
            bool ok = true;
            nested->with_children(row, [&](const ListModel& child) {
                if (visited) {
                    return;  // one child model per row (RFC_MODEL.md Q1)
                }
                visited = true;
                bool child_same = false;
                ok = apply_list(sub, child, error, retiring, &child_same, depth + 1);
                same = same && child_same;
                if (!child_same) {
                    touched_hosts.push_back(sub);
                }
            });
            if (!ok) {
                return false;
            }
            if (!visited) {
                bool child_same = false;
                if (!apply_on_node(sub, 0, FieldFn{}, error, RowKeyFn{}, retiring, &child_same)) {
                    return false;
                }
                same = same && child_same;
                if (!child_same) {
                    touched_hosts.push_back(sub);
                }
            }
        }
    if (nested_same != nullptr) {
        *nested_same = same;
    }
    return true;
}

}  // namespace html
}  // namespace zui
