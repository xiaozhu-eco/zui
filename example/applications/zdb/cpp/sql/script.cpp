#include "sql/script.h"

#include "util.h"

#include "sqlite3.h"

#include <chrono>
#include <utility>

namespace zdb {
namespace {

constexpr int kStatementCap = 40;
constexpr int kRowCap = 500;

SqlValue read_column(sqlite3_stmt* stmt, int column) {
    SqlValue value;
    switch (sqlite3_column_type(stmt, column)) {
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
    case SQLITE_NULL:
        value.kind = SqlKind::Null;
        break;
    default:
        value.kind = SqlKind::Text;
        if (const auto* text = sqlite3_column_text(stmt, column)) {
            value.text.assign(reinterpret_cast<const char*>(text));
        }
        break;
    }
    return value;
}

StatementResult consume(sqlite3* db, sqlite3_stmt* stmt) {
    StatementResult result;
    result.ok = true;
    const char* sql = sqlite3_sql(stmt);
    if (sql != nullptr) {
        result.sql = sql;
    }
    const auto started = std::chrono::steady_clock::now();
    const int columns = sqlite3_column_count(stmt);
    result.columns.reserve(static_cast<std::size_t>(columns));
    for (int i = 0; i < columns; ++i) {
        const char* name = sqlite3_column_name(stmt, i);
        result.columns.emplace_back(name != nullptr ? name : "");
    }
    if (columns == 0) {
        const int stepped = sqlite3_step(stmt);
        if (stepped != SQLITE_DONE) {
            result.ok = false;
            result.error = sqlite3_errmsg(db);
        } else {
            result.rows_modified = sqlite3_changes(db);
        }
    } else {
        while (true) {
            const int stepped = sqlite3_step(stmt);
            if (stepped == SQLITE_DONE) {
                break;
            }
            if (stepped != SQLITE_ROW) {
                result.ok = false;
                result.error = sqlite3_errmsg(db);
                result.rows.clear();
                break;
            }
            if (static_cast<int>(result.rows.size()) >= kRowCap) {
                result.truncated = true;
                break;
            }
            std::vector<SqlValue> row;
            row.reserve(static_cast<std::size_t>(columns));
            for (int i = 0; i < columns; ++i) {
                row.push_back(read_column(stmt, i));
            }
            result.rows.push_back(std::move(row));
        }
    }
    const auto elapsed = std::chrono::steady_clock::now() - started;
    result.ms = std::chrono::duration<double, std::milli>(elapsed).count();
    return result;
}

}  // namespace

std::vector<StatementResult> run_script(sqlite3* db, const std::string& sql) {
    std::vector<StatementResult> results;
    if (trim_copy(sql).empty()) {
        StatementResult blank;
        blank.error = "请输入 SQL。";
        results.push_back(std::move(blank));
        return results;
    }
    const char* cursor = sql.c_str();
    const char* end = cursor + sql.size();
    int executed = 0;
    bool capped = false;
    while (cursor < end) {
        sqlite3_stmt* stmt = nullptr;
        const char* tail = nullptr;
        const int prepared = sqlite3_prepare_v2(db, cursor, static_cast<int>(end - cursor), &stmt, &tail);
        if (prepared != SQLITE_OK) {
            StatementResult failed;
            failed.error = sqlite3_errmsg(db);
            results.push_back(std::move(failed));
            break;
        }
        if (stmt == nullptr) {
            if (tail == nullptr || tail == cursor) {
                break;
            }
            cursor = tail;
            continue;
        }
        if (executed >= kStatementCap) {
            sqlite3_finalize(stmt);
            capped = true;
            break;
        }
        results.push_back(consume(db, stmt));
        sqlite3_finalize(stmt);
        ++executed;
        if (!results.back().ok) {
            break;
        }
        cursor = tail != nullptr ? tail : end;
    }
    if (capped) {
        StatementResult extra;
        extra.error = "语句超过 40 条，多出来的没有执行。";
        results.push_back(std::move(extra));
    }
    if (results.empty()) {
        StatementResult none;
        none.error = "没有可执行的语句。";
        results.push_back(std::move(none));
    }
    return results;
}

std::string summarize_script(const std::vector<StatementResult>& results) {
    int failed = 0;
    for (const StatementResult& result : results) {
        if (!result.ok) {
            ++failed;
        }
    }
    const int ok = static_cast<int>(results.size()) - failed;
    if (failed && ok) {
        return "执行了 " + std::to_string(ok) + " 条语句，" + std::to_string(failed) + " 条失败。";
    }
    if (failed) {
        return "查询没有完成。";
    }
    if (results.size() == 1 && !results[0].columns.empty()) {
        std::string text = "返回 " + std::to_string(results[0].rows.size()) + " 行";
        if (results[0].truncated) {
            text += "，结果已截断";
        }
        text += "。";
        return text;
    }
    return "执行了 " + std::to_string(results.size()) + " 条语句。";
}

}  // namespace zdb
