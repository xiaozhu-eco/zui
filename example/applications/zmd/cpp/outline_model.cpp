#include "outline_model.h"

namespace zmd {

void OutlineModel::set_rows(std::vector<Heading> rows) {
    rows_ = std::move(rows);
    if (active_ >= rows_.size()) {
        active_ = rows_.empty() ? 0 : rows_.size() - 1;
    }
}

void OutlineModel::set_active(std::size_t row) {
    if (row < rows_.size()) {
        active_ = row;
    }
}

std::size_t OutlineModel::row_count() const {
    return rows_.size();
}

std::string OutlineModel::value(std::size_t row, std::string_view key) const {
    if (row >= rows_.size()) {
        return {};
    }
    const Heading& heading = rows_[row];
    if (key == "text") {
        return heading.text;
    }
    if (key == "depth") {
        return std::to_string(heading.depth);
    }
    // `state` only ever carries the current-row class; the page appends it.
    if (key == "state") {
        return row == active_ ? "is-active" : "";
    }
    return {};
}

}  // namespace zmd

