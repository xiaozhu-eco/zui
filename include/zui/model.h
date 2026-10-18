#ifndef ZUI_MODEL_H
#define ZUI_MODEL_H

#include <zui/export.h>

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace zui {

/// A sequence of records. Each row is a set of named fields used as {{key}}
/// in an HTML <template>. Subclass this for app-specific rows. TableModel covers
/// columns. Models never describe layout or style. Call Document::set_model again
/// after the data changes.
class ZUI_EXPORT ListModel {
public:
    virtual ~ListModel() = default;
    virtual size_t row_count() const = 0;
    /// Field @p key on @p row. Empty when the key is absent.
    /// The engine also supplies {{index}} (0-based) when this returns empty.
    virtual std::string value(size_t row, std::string_view key) const = 0;
    /// Stable identity of @p row. Empty (the default) matches rows by position.
    virtual std::string row_key(size_t row) const {
        (void)row;
        return {};
    }
    /// Nested rows of @p row: the model the row's inner `<template>` renders. The engine calls
    /// `visit` at most once per row, inside the `set_model` that asked for it, and never keeps the
    /// model - so an implementation may hand out a member or a temporary. Not calling `visit`
    /// (and the default implementation) means the row has no nested content, which retires any
    /// rows already stamped there. A kept parent row still reconciles its children this way.
    virtual void with_children(size_t row,
                               const std::function<void(const ListModel&)>& visit) const {
        (void)row;
        (void)visit;
    }
};

/// Rows by column. column_key(i) is the {{name}} in the row template.
/// value() maps those keys onto cell() so set_model can bind a table as a list.
class ZUI_EXPORT TableModel : public ListModel {
public:
    virtual size_t column_count() const = 0;
    virtual std::string column_key(size_t column) const = 0;
    virtual std::string cell(size_t row, size_t column) const = 0;

    /// Per-cell field used by a column template. The default maps "text", "key" and column_key()
    /// onto cell(); any other key is empty unless a subclass overrides this - that is how a cell
    /// gets a class or a `data-*` of its own. Empty still means absent.
    virtual std::string cell_value(size_t row, size_t column, std::string_view key) const {
        if (key == "text" || key == "key" || key == column_key(column)) {
            return cell(row, column);
        }
        return {};
    }

    std::string value(size_t row, std::string_view key) const override {
        const size_t n = column_count();
        for (size_t c = 0; c < n; ++c) {
            if (column_key(c) == key) {
                return cell(row, c);
            }
        }
        return {};
    }
};

}  // namespace zui

#endif  // ZUI_MODEL_H
