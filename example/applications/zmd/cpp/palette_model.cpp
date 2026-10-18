#include "palette_model.h"

#include "util.h"

namespace zmd {

void PaletteModel::set_items(std::vector<Command> items) {
    all_ = std::move(items);
    visible_ = all_;
}

void PaletteModel::set_active(std::size_t row) {
    active_ = row;
}

void PaletteModel::filter(std::string_view query) {
    std::string needle = util::lower(util::trim(query));
    visible_.clear();
    for (const Command& item : all_) {
        if (needle.empty() || util::lower(item.label).find(needle) != std::string::npos) {
            visible_.push_back(item);
        }
    }
}

const Command* PaletteModel::at(std::size_t row) const {
    return row < visible_.size() ? &visible_[row] : nullptr;
}

std::size_t PaletteModel::row_count() const {
    return visible_.size();
}

std::string PaletteModel::value(std::size_t row, std::string_view key) const {
    const Command* item = at(row);
    if (item == nullptr) {
        return {};
    }
    if (key == "label") {
        return item->label;
    }
    if (key == "hint") {
        return item->hint;
    }
    if (key == "icon") {
        return item->icon;
    }
    if (key == "state") {
        return row == active_ ? "is-active" : "";
    }
    return {};
}

}  // namespace zmd
