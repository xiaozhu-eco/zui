#ifndef ZDB_SQL_DB_H
#define ZDB_SQL_DB_H

#include "session.h"

#include <string>
#include <string_view>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace zdb {

struct SqlParam {
    SqlValue value;
};

struct SqlTable {
    std::vector<std::string> columns;
    std::vector<std::vector<SqlValue>> rows;
    bool truncated = false;
    std::string error;
};

bool exec_sql(sqlite3* db, const char* sql, std::string& error);
bool query_sql(sqlite3* db, const std::string& sql, const std::vector<SqlParam>& params, int row_cap,
               SqlTable& out);
const SqlValue* column(const SqlTable& table, std::size_t row, std::string_view name);
std::string column_text(const SqlTable& table, std::size_t row, std::string_view name);

// Copies a file into a private in-memory database. The original is not written.
bool open_sqlite_file(const std::filesystem::path& path, sqlite3*& out, std::string& error);
bool open_memory(sqlite3*& out, std::string& error);
bool export_sqlite(sqlite3* db, const std::filesystem::path& path, std::string& error);
void close_sqlite(sqlite3*& db);

}  // namespace zdb

#endif
