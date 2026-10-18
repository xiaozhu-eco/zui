#ifndef ZUI_SOURCE_HTML_DOCUMENT_INTERNAL_H
#define ZUI_SOURCE_HTML_DOCUMENT_INTERNAL_H

// Internals shared by the document translation units. The public surface stays in document.h;
// this header holds the pieces two or more of those units need:
//
//   loader.cpp     `DocumentLoader` parses a file / fragment into the `Node` tree
//   document.cpp   the id bookkeeping those writes have to keep in step

#include "html/document.h"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace zui {
namespace html {

/// `true` for a string that is only whitespace (used to drop boilerplate text from a template).
bool whitespace_only(std::string_view s);

/// `Document::ids` is maintained by the writers, not derived on demand: these walk a subtree to
/// drop / collect / re-adopt its id entries.
void unregister_ids(Document& doc, Node* node);
void collect_ids(Node* node, std::vector<std::string>& ids);
void adopt_ids(Document& doc, Node* node);
void collect_descendants_with_ids(Node* node, const std::unordered_set<std::string>& want,
                                  std::vector<Node*>& out);

/// Reads an HTML file and turns the lexbor DOM into a `Node` tree, collecting the `<link>` /
/// `<style>` stylesheets in document order. The class owns the workflow; the lexbor conversion
/// helpers stay private to its own translation unit.
class DocumentLoader {
public:
    explicit DocumentLoader(Document& doc) : doc_(doc) {}

    bool load(std::string_view path, std::string& error);
    /// Append the HTML fragment at `path` under `into` (context element `body`).
    bool attach_fragment(Node* into, std::string_view path, std::string& error,
                         const std::function<void(Node*)>& before_remove, bool replace_children);

private:
    Document& doc_;
};

/// The `<select>` control: which `<option>` is current, and the value / commit protocol the input
/// layer drives. The class owns the `Node*`; the free functions in document.h delegate here.
class SelectControl {
public:
    explicit SelectControl(Node* select) : select_(select) {}

    void commit(Node* option);
    void init();
    bool set_value(std::string_view value);

    static std::string option_label(const Node* option);
    static std::string option_value(const Node* option);

private:
    Node* select_;
};

/// The `<template>` instantiation: clone the template once per row, substitute `{{key}}`
/// placeholders and swap the host's children. This is what `apply_model` drives.
class ModelBinder {
public:
    using FieldFn = std::function<std::string(size_t, std::string_view)>;
    using RowKeyFn = std::function<std::string(size_t)>;
    /// `{{key}}` lookup: the row's field, or - for `{{index}}` - the row number.
    using Lookup = std::function<std::string(std::string_view)>;
    /// Which template proto a row clones. Empty means every proto (the row template: one row is a
    /// group of `stride` nodes). Set for a column template: one cell per column, so a column is
    /// one node and the proto it clones is the column's cell (RFC_MODEL.md KD6).
    using ProtoFn = std::function<size_t(size_t)>;

    explicit ModelBinder(Document& doc) : doc_(doc) {}

    bool apply_on_node(Node* host, size_t row_count, const FieldFn& field, std::string& error,
                       const RowKeyFn& row_key, const std::function<void(Node*)>& retiring,
                       bool* unchanged, const ProtoFn& proto_of_row = {},
                       const ListModel* nested = nullptr, size_t depth = 0);
    bool apply(std::string_view id, size_t row_count, const FieldFn& field, std::string& error,
               const RowKeyFn& row_key, const std::function<void(Node*)>& retiring,
               bool* unchanged);
    /// `apply_list_model_on_node`: rows, nested `with_children` hosts and the column stamp.
    bool apply_list(Node* host, const ListModel& model, std::string& error,
                    const std::function<void(Node*)>& retiring, bool* unchanged, size_t depth);
    bool apply_windowed_list(Node* host, const ListModel& model, std::string& error,
                             const std::function<void(Node*)>& retiring, bool* unchanged);
    bool rebind_window(ListWindow& window, Node* host,
                       const std::function<void(Node*)>& retiring, bool* unchanged);
    /// The `TableModel` cells inside each row host (RFC_MODEL.md KD6).
    bool apply_table_columns(Node* host, const TableModel& table, std::string& error,
                             const std::function<void(Node*)>& retiring, bool* unchanged);

    /// Every host whose *own* children this bind rebuilt or retired: the column-stamp row hosts
    /// and the nested `with_children` hosts. `App::set_model` queues them for `sync_children`
    /// beside the top host. A row host keeps its box across a restamp, so the top host's child
    /// list still matches and `sync_children` would stop above the new cells - they would have no
    /// box until the next real layout of that subtree.
    std::vector<Node*> touched_hosts;

private:
    /// Nested `with_children` hosts after the parent row stamp (RFC_MODEL.md Q1 / section 6).
    bool apply_nested(Node* host, size_t row_count, int stride, const std::vector<size_t>& row_head,
                      const ListModel* nested, size_t depth,
                      const std::function<void(Node*)>& retiring, std::string& error,
                      bool* nested_same);
    static std::string subst_placeholders(std::string_view in, const Lookup& lookup);
    static std::unique_ptr<Node> clone_node(const Node* src, Node* parent);
    static void interpolate_node(Node* n, const Lookup& lookup);
    static int row_stride(const Node* tmpl);
    /// @p stop is the row's nested host (RFC_MODEL.md Q1): what Q1's template stamped under it is
    /// not part of the row's own fingerprint, or a nested list would rebuild its parent row every
    /// time. The host's own tag and attributes stay in.
    static std::string node_fingerprint(const Node* n, const Node* stop);
    /// `node_fingerprint` of the row this template proto would produce for @p lookup, without
    /// cloning it. A kept row is compared with this, so the reconcile does not build the very
    /// node it is about to keep (RFC_MODEL.md KD3).
    static std::string node_fingerprint_with(const Node* n, const Lookup& lookup,
                                             const Node* stop);
    static std::string group_fingerprint(const std::vector<Node*>& nodes, const Node* stop);

    Document& doc_;
};

}  // namespace html
}  // namespace zui

#endif
