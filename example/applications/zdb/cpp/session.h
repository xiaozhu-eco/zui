#ifndef ZDB_SESSION_H
#define ZDB_SESSION_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct sqlite3;

namespace zui {
class App;
}

namespace zdb {

enum class SqlKind { Null, Integer, Real, Text, Blob };

struct SqlValue {
    SqlKind kind = SqlKind::Null;
    std::int64_t integer = 0;
    double real = 0;
    std::string text;
    std::vector<unsigned char> blob;
};

struct DbObject {
    std::string type;
    std::string name;
    std::string table;
    std::string sql;
    bool counted = false;
    std::int64_t count = 0;
};

struct RelationColumn {
    std::string cid;
    std::string name;
    std::string type;
    std::string notnull;
    std::string dflt;
    std::string pk;
};

struct IndexColumn {
    std::string seq;
    std::string name;
    std::string cid;
};

struct ForeignKey {
    std::string from;
    std::string table;
    std::string to;
    std::string on_update;
    std::string on_delete;
};

struct IndexRef {
    std::string name;
    std::string columns;
    std::string origin;
    std::string unique;
    std::string partial;
};

struct TriggerRef {
    std::string name;
    std::string sql;
};

struct Structure {
    std::string error;
    std::string facts;
    std::string parent;
    std::string sql;
    bool automatic = false;
    bool indexed = false;
    std::vector<RelationColumn> columns;
    std::vector<IndexColumn> index_columns;
    std::vector<ForeignKey> foreign_keys;
    std::vector<std::string> jump_tables;
    std::vector<IndexRef> indexes;
    std::vector<TriggerRef> triggers;
};

struct Page {
    bool browsable = false;
    std::string error;
    std::vector<std::string> columns;
    std::vector<std::vector<SqlValue>> rows;
    std::int64_t total = 0;
    int limit = 25;
    int offset = 0;
    std::string order;
    std::string dir = "asc";
};

struct StatementResult {
    bool ok = false;
    std::string sql;
    std::string error;
    std::vector<std::string> columns;
    std::vector<std::vector<SqlValue>> rows;
    bool truncated = false;
    int rows_modified = 0;
    double ms = 0;
};

struct Session {
    Session();
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    zui::App* app = nullptr;
    std::filesystem::path dir;
    sqlite3* db = nullptr;
    std::string file_name;
    std::string version;
    std::int64_t bytes = 0;
    std::string catalog_error;
    std::vector<DbObject> objects;
    std::string filter;
    int selected = -1;
    std::string mode = "data";
    Page page;
    Structure structure;
    std::vector<StatementResult> results;
    int result_index = 0;
    std::string notice = "打开一个数据库，或载入示例库。";
    bool notice_error = false;
    bool detail_open = false;
    std::string detail_label;
    std::string detail_text;
    bool query_detail_open = false;
    std::string query_detail_label;
    std::string query_detail_text;
    /// `data-cell` of the open grid detail (`row,column` on the current page). Empty when closed.
    std::string grid_pick_ref;
    /// `data-cell` of the open query detail (`result,row,column`). Empty when closed.
    std::string query_pick_ref;
    int generation = 0;
    std::filesystem::path pending_path;
    std::string pending_sql;
    bool pending_sample = false;
    bool reset_grid_scroll = false;
    bool reset_structure_scroll = false;
    bool reset_query_scroll = false;
};

const DbObject* selected_object(const Session& session);

}  // namespace zdb

#endif
