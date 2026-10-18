#include "rows.h"

namespace particle {

void FieldRows::put(Row& row, std::string key, std::string value) {
    row.fields.emplace_back(std::move(key), std::move(value));
}

void FieldRows::add(Row row) { rows.push_back(std::move(row)); }

std::size_t FieldRows::row_count() const { return rows.size(); }

std::string FieldRows::value(std::size_t row, std::string_view key) const {
    if (row >= rows.size()) {
        return {};
    }
    for (const Field& field : rows[row].fields) {
        if (field.first == key) {
            return field.second;
        }
    }
    return {};
}

std::string FieldRows::row_key(std::size_t row) const { return value(row, "index"); }

}  // namespace particle
