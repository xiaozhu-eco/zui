#ifndef CHAT_ROWS_H
#define CHAT_ROWS_H

#include <zui/app.h>

#include <string>
#include <utility>
#include <vector>

namespace chat {

/// One list model for the conversation list and the message log.
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

}  // namespace chat

#endif
