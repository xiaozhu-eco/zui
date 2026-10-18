// The `<template>` instantiator: clone once per row, substitute `{{key}}`, swap the host's
// children. `ModelBinder` owns the walk; `apply_model` / `apply_model_on_node` are the API.

#include "html/document.h"
#include "html/document_internal.h"
#include "html/model_internal.h"

#include <zui/model.h>

#include <algorithm>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zui {
namespace html {

std::vector<Node*> model_protos(Node* tmpl) {
    std::vector<Node*> protos;
    if (tmpl == nullptr) {
        return protos;
    }
    for (const auto& proto : tmpl->children) {
        if (proto == nullptr || (proto->tag == "#text" && whitespace_only(proto->text))) {
            continue;
        }
        protos.push_back(proto.get());
    }
    return protos;
}

/// The parent of the first `<template>` under @p row_nodes: the row's nested host
/// (RFC_MODEL.md Q1). nullptr when the row has no nested template.
Node* nested_host_of(const std::vector<Node*>& row_nodes) {
    for (Node* node : row_nodes) {
        if (Node* tmpl = find_template(node)) {
            return tmpl->parent;
        }
    }
    return nullptr;
}

Node* find_template(Node* host) {
    if (host == nullptr) {
        return nullptr;
    }
    for (auto& ch : host->children) {
        if (ch->tag == "template") {
            return ch.get();
        }
    }
    return find_tag(host, "template");
}

void erase_list_window(Document& doc, std::string_view host_id) {
    auto& book = doc.list_windows;
    book.erase(std::remove_if(book.begin(), book.end(),
                              [&](const ListWindow& window) { return window.host_id == host_id; }),
               book.end());
}

bool ModelBinder::apply(std::string_view id, size_t row_count, const FieldFn& field,
                        std::string& error, const RowKeyFn& row_key,
                        const std::function<void(Node*)>& retiring, bool* unchanged) {
    Document& doc = doc_;
    Node* host = find(doc, id);
    if (host == nullptr) {
        error = "model host id not found";
        return false;
    }
    return apply_on_node(host, row_count, field, error, row_key, retiring, unchanged);
}

bool ModelBinder::apply_list(Node* host, const ListModel& model, std::string& error,
                             const std::function<void(Node*)>& retiring, bool* unchanged,
                             size_t depth) {
    const bool table = dynamic_cast<const TableModel*>(&model) != nullptr;
    Node* tmpl = find_template(host);
    const bool nested_template =
        tmpl != nullptr && nested_host_of(model_protos(tmpl)) != nullptr;
    // Under 1000 rows the stamp stays whole. PerfStage2 lays out 500 rows and expects one box
    // per row; windowing starts above that. Tables and nested templates stay whole too: a column
    // stamp and `with_children` both need every row present.
    if (depth == 0 && host != nullptr && !host->id.empty() && !table && !nested_template &&
        model.row_count() >= kListWindowMin) {
        return apply_windowed_list(host, model, error, retiring, unchanged);
    }
    if (host != nullptr && !host->id.empty()) {
        erase_list_window(doc_, host->id);
    }
    const FieldFn field = [&model](size_t row, std::string_view key) {
        return model.value(row, key);
    };
    const RowKeyFn row_key = [&model](size_t row) { return model.row_key(row); };
    bool same = true;
    if (!apply_on_node(host, model.row_count(), field, error, row_key, retiring, &same, {}, &model,
                       depth)) {
        return false;
    }
    if (const auto* table = dynamic_cast<const TableModel*>(&model)) {
        if (!apply_table_columns(host, *table, error, retiring, &same)) {
            return false;
        }
    }
    if (unchanged != nullptr) {
        *unchanged = same;
    }
    return true;
}

bool apply_list_model_on_node(Document& doc, Node* host, const ListModel& model,
                              std::string& error,
                              const std::function<void(Node*)>& retiring, bool* unchanged,
                              std::vector<Node*>* sync_hosts) {
    ModelBinder binder(doc);
    const bool ok = binder.apply_list(host, model, error, retiring, unchanged, 0);
    if (ok && sync_hosts != nullptr) {
        sync_hosts->insert(sync_hosts->end(), binder.touched_hosts.begin(),
                           binder.touched_hosts.end());
    }
    return ok;
}

bool apply_list_model(Document& doc, std::string_view id, const ListModel& model,
                      std::string& error, const std::function<void(Node*)>& retiring,
                      bool* unchanged, std::vector<Node*>* sync_hosts) {
    Node* host = find(doc, id);
    if (host == nullptr) {
        error = "model host id not found";
        return false;
    }
    return apply_list_model_on_node(doc, host, model, error, retiring, unchanged, sync_hosts);
}

bool apply_model_on_node(Document& doc, Node* host, size_t row_count,
                         const std::function<std::string(size_t, std::string_view)>& field,
                         std::string& error, const std::function<std::string(size_t)>& row_key,
                         const std::function<void(Node*)>& retiring, bool* unchanged) {
    return ModelBinder(doc).apply_on_node(host, row_count, field, error, row_key, retiring,
                                         unchanged);
}

bool apply_model(Document& doc, std::string_view id, size_t row_count,
                 const std::function<std::string(size_t, std::string_view)>& field,
                 std::string& error, const std::function<std::string(size_t)>& row_key,
                 const std::function<void(Node*)>& retiring, bool* unchanged) {
    return ModelBinder(doc).apply(id, row_count, field, error, row_key, retiring, unchanged);
}

}  // namespace html
}  // namespace zui
