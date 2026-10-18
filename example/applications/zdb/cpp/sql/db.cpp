#include "sql/db.h"

#include "util.h"

#include "sqlite3.h"

#include <cstring>
#include <fstream>
#include <utility>

namespace zdb {
namespace {

constexpr char kMagic[] = "SQLite format 3";

SqlValue read_column(sqlite3_stmt* stmt, int column) {
    SqlValue value;
    switch (sqlite3_column_type(stmt, column)) {
    case SQLITE_NULL:
        value.kind = SqlKind::Null;
        break;
    case SQLITE_INTEGER:
        value.kind = SqlKind::Integer;
        value.integer = sqlite3_column_int64(stmt, column);
        break;
    case SQLITE_FLOAT:
        value.kind = SqlKind::Real;
        value.real = sqlite3_column_double(stmt, column);
        break;
    case SQLITE_BLOB: {
        value.kind = SqlKind::Blob;
        const auto* bytes = static_cast<const unsigned char*>(sqlite3_column_blob(stmt, column));
        const int size = sqlite3_column_bytes(stmt, column);
        if (bytes != nullptr && size > 0) {
            value.blob.assign(bytes, bytes + size);
        }
        break;
    }
    default: {
        value.kind = SqlKind::Text;
        const auto* text = sqlite3_column_text(stmt, column);
        if (text != nullptr) {
            value.text.assign(reinterpret_cast<const char*>(text));
        }
        break;
    }
    }
    return value;
}

bool bind_param(sqlite3_stmt* stmt, int index, const SqlValue& value, std::string& error) {
    int rc = SQLITE_OK;
    switch (value.kind) {
    case SqlKind::Null:
        rc = sqlite3_bind_null(stmt, index);
        break;
    case SqlKind::Integer:
        rc = sqlite3_bind_int64(stmt, index, value.integer);
        break;
    case SqlKind::Real:
        rc = sqlite3_bind_double(stmt, index, value.real);
        break;
    case SqlKind::Text:
        rc = sqlite3_bind_text(stmt, index, value.text.data(), static_cast<int>(value.text.size()),
                               SQLITE_TRANSIENT);
        break;
    case SqlKind::Blob:
        rc = sqlite3_bind_blob(stmt, index, value.blob.empty() ? nullptr : value.blob.data(),
                               static_cast<int>(value.blob.size()), SQLITE_TRANSIENT);
        break;
    }
    if (rc != SQLITE_OK) {
        error = sqlite3_errmsg(sqlite3_db_handle(stmt));
        return false;
    }
    return true;
}

std::string explain_open(const std::string& message) {
    if (message.find("not a database") != std::string::npos) {
        return "这个文件不是 SQLite 数据库。";
    }
    return message;
}

bool backup_into(sqlite3* destination, sqlite3* source, std::string& error) {
    sqlite3_backup* backup = sqlite3_backup_init(destination, "main", source, "main");
    if (backup == nullptr) {
        error = explain_open(sqlite3_errmsg(destination));
        return false;
    }
    const int stepped = sqlite3_backup_step(backup, -1);
    const int finished = sqlite3_backup_finish(backup);
    if (stepped != SQLITE_DONE || finished != SQLITE_OK) {
        error = explain_open(sqlite3_errmsg(destination));
        return false;
    }
    return true;
}

bool looks_like_sqlite(const std::filesystem::path& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "打不开这个文件。";
        return false;
    }
    char magic[16];
    in.read(magic, 16);
    if (in.gcount() != 16 || std::memcmp(magic, kMagic, 15) != 0 || magic[15] != '\0') {
        error = "这个文件不是 SQLite 数据库。";
        return false;
    }
    return true;
}

}  // namespace

bool exec_sql(sqlite3* db, const char* sql, std::string& error) {
    char* message = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &message);
    if (rc != SQLITE_OK) {
        error = message != nullptr ? message : sqlite3_errmsg(db);
        sqlite3_free(message);
        return false;
    }
    return true;
}

bool query_sql(sqlite3* db, const std::string& sql, const std::vector<SqlParam>& params, int row_cap,
               SqlTable& out) {
    out = {};
    sqlite3_stmt* stmt = nullptr;
    const int prepared =
        sqlite3_prepare_v2(db, sql.c_str(), static_cast<int>(sql.size()), &stmt, nullptr);
    if (prepared != SQLITE_OK) {
        out.error = sqlite3_errmsg(db);
        return false;
    }
    for (std::size_t i = 0; i < params.size(); ++i) {
        if (!bind_param(stmt, static_cast<int>(i + 1), params[i].value, out.error)) {
            sqlite3_finalize(stmt);
            return false;
        }
    }
    const int columns = sqlite3_column_count(stmt);
    out.columns.reserve(static_cast<std::size_t>(columns));
    for (int i = 0; i < columns; ++i) {
        const char* name = sqlite3_column_name(stmt, i);
        out.columns.emplace_back(name != nullptr ? name : "");
    }
    while (true) {
        const int stepped = sqlite3_step(stmt);
        if (stepped == SQLITE_DONE) {
            break;
        }
        if (stepped != SQLITE_ROW) {
            out.error = sqlite3_errmsg(db);
            sqlite3_finalize(stmt);
            return false;
        }
        if (row_cap >= 0 && static_cast<int>(out.rows.size()) >= row_cap) {
            out.truncated = true;
            break;
        }
        std::vector<SqlValue> row;
        row.reserve(static_cast<std::size_t>(columns));
        for (int i = 0; i < columns; ++i) {
            row.push_back(read_column(stmt, i));
        }
        out.rows.push_back(std::move(row));
    }
    sqlite3_finalize(stmt);
    return true;
}

const SqlValue* column(const SqlTable& table, std::size_t row, std::string_view name) {
    if (row >= table.rows.size()) {
        return nullptr;
    }
    for (std::size_t i = 0; i < table.columns.size() && i < table.rows[row].size(); ++i) {
        if (table.columns[i] == name) {
            return &table.rows[row][i];
        }
    }
    return nullptr;
}

std::string column_text(const SqlTable& table, std::size_t row, std::string_view name) {
    const SqlValue* value = column(table, row, name);
    if (value == nullptr || value->kind == SqlKind::Null) {
        return {};
    }
    if (value->kind == SqlKind::Integer) {
        return std::to_string(value->integer);
    }
    if (value->kind == SqlKind::Real) {
        return std::to_string(value->real);
    }
    if (value->kind == SqlKind::Blob) {
        return format_cell(*value);
    }
    return value->text;
}

bool open_memory(sqlite3*& out, std::string& error) {
    out = nullptr;
    sqlite3* db = nullptr;
    if (sqlite3_open(":memory:", &db) != SQLITE_OK) {
        error = db != nullptr ? sqlite3_errmsg(db) : "打不开内存数据库。";
        if (db != nullptr) {
            sqlite3_close(db);
        }
        return false;
    }
    if (!exec_sql(db, "PRAGMA foreign_keys = ON", error)) {
        sqlite3_close(db);
        return false;
    }
    out = db;
    return true;
}

bool open_sqlite_file(const std::filesystem::path& path, sqlite3*& out, std::string& error) {
    out = nullptr;
    if (!looks_like_sqlite(path, error)) {
        return false;
    }
    sqlite3* file = nullptr;
    const std::string utf8 = utf8_path(path);
    if (sqlite3_open_v2(utf8.c_str(), &file, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        error = file != nullptr ? explain_open(sqlite3_errmsg(file)) : "打不开这个文件。";
        if (file != nullptr) {
            sqlite3_close(file);
        }
        return false;
    }
    sqlite3* memory = nullptr;
    if (!open_memory(memory, error)) {
        sqlite3_close(file);
        return false;
    }
    if (!backup_into(memory, file, error)) {
        sqlite3_close(file);
        sqlite3_close(memory);
        return false;
    }
    sqlite3_close(file);
    out = memory;
    return true;
}

bool export_sqlite(sqlite3* db, const std::filesystem::path& path, std::string& error) {
    sqlite3* file = nullptr;
    const std::string utf8 = utf8_path(path);
    if (sqlite3_open_v2(utf8.c_str(), &file, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) !=
        SQLITE_OK) {
        error = file != nullptr ? sqlite3_errmsg(file) : "写不出这个文件。";
        if (file != nullptr) {
            sqlite3_close(file);
        }
        return false;
    }
    const bool copied = backup_into(file, db, error);
    sqlite3_close(file);
    return copied;
}

void close_sqlite(sqlite3*& db) {
    if (db != nullptr) {
        sqlite3_close(db);
        db = nullptr;
    }
}

}  // namespace zdb
