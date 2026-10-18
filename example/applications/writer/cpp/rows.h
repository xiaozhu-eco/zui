#ifndef WRITER_ROWS_H
#define WRITER_ROWS_H

#include <zui/app.h>

#include <string>
#include <utility>
#include <vector>

namespace writer {

class FieldRows : public zui::ListModel {
public:
    using Field = std::pair<std::string, std::string>;
    struct Row {
        std::vector<Field> fields;
    };

    std::vector<Row> rows;

    void put(Row& row, std::string key, std::string value);
    void add(Row row);

    std::size_t row_count() const override;
    std::string value(std::size_t row, std::string_view key) const override;
    std::string row_key(std::size_t row) const override;
};

}  // namespace writer

#endif
