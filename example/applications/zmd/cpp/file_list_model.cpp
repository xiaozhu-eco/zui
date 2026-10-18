#include "file_list_model.h"

namespace zmd {

void FileListModel::set_rows(std::vector<FileEntry> rows) {
    rows_ = std::move(rows);
}

void FileListModel::set_active_name(std::string_view name) {
    for (FileEntry& row : rows_) {
        row.active = row.name == name;
    }
}

std::size_t FileListModel::row_count() const {
    return rows_.size();
}

std::string FileListModel::value(std::size_t row, std::string_view key) const {
    if (row >= rows_.size()) {
        return {};
    }
    const FileEntry& entry = rows_[row];
    if (key == "name") {
        return entry.name;
    }
    // `kind` is the row's class list: the tree styles folders and the current
    // document through it.
    if (key == "kind") {
        std::string kind = entry.folder ? "mdw-node" : "mdw-item";
        if (entry.active && !entry.folder) {
            kind += " is-active";
        }
        return kind;
    }
    return {};
}

}  // namespace zmd

