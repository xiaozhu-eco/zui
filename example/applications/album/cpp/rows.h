#ifndef ALBUM_ROWS_H
#define ALBUM_ROWS_H

#include <zui/app.h>

#include <string>
#include <utility>
#include <vector>

namespace album {

/// One list model for the album buttons, the photo grid, and the lightbox `<select>`.
class FieldRows : public zui::ListModel {
public:
    using Field = std::pair<std::string, std::string>;
    using Row = std::vector<Field>;

    std::vector<Row> rows;

    void put(Row& row, std::string key, std::string value);
    void add(Row row);

    std::size_t row_count() const override;
    std::string value(std::size_t row, std::string_view key) const override;
};

}  // namespace album

#endif
