#ifndef ZUI_SOURCE_HTML_MODEL_INTERNAL_H
#define ZUI_SOURCE_HTML_MODEL_INTERNAL_H

// Internals shared by the model translation units. `ModelBinder` lives in
// document_internal.h; this header holds the helpers two or more of those
// units need.

#include "html/document_internal.h"

#include <string>
#include <string_view>
#include <vector>

namespace zui {
namespace html {

/// Under this many rows the stamp stays whole (`apply_list`).
constexpr std::size_t kListWindowMin = 1000;

/// Non-whitespace children of a `<template>`: the protos a row (or column) clones.
std::vector<Node*> model_protos(Node* tmpl);

/// The parent of the first `<template>` under @p row_nodes: the row's nested host
/// (RFC_MODEL.md Q1). nullptr when the row has no nested template.
Node* nested_host_of(const std::vector<Node*>& row_nodes);

void erase_list_window(Document& doc, std::string_view host_id);

}  // namespace html
}  // namespace zui

#endif
