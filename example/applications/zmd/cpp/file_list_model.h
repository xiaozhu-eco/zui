#ifndef ZMD_FILE_LIST_MODEL_H
#define ZMD_FILE_LIST_MODEL_H

#include <string>
#include <string_view>
#include <vector>

#include <zui/model.h>

namespace zmd {

/// One row of the file panel: a document or a folder under the working directory.
struct FileEntry {
    std::string name;
    bool folder = false;
    bool active = false;
};

/// Rows of the file panel. Same contract as OutlineModel: data here, look there.
class FileListModel : public zui::ListModel {
public:
    void set_rows(std::vector<FileEntry> rows);
    void set_active_name(std::string_view name);
    const std::vector<FileEntry>& rows() const { return rows_; }

    std::size_t row_count() const override;
    std::string value(std::size_t row, std::string_view key) const override;

private:
    std::vector<FileEntry> rows_;
};

}  // namespace zmd

#endif
