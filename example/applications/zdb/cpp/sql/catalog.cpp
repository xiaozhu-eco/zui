#include "sql/catalog.h"

#include "sql/db.h"

#include "util.h"

namespace zdb {
namespace {

constexpr int kCountLimit = 40;

std::string origin_label(const std::string& origin) {
    if (origin == "pk") {
        return "主键";
    }
    if (origin == "u") {
        return "唯一约束";
    }
    if (origin == "c") {
        return "CREATE INDEX";
    }
    return origin.empty() ? "—" : origin;
}

std::string dash(std::string text) { return text.empty() ? "—" : text; }

bool one_table(sqlite3* db, const std::string& sql, const std::vector<SqlParam>& params, SqlTable& out) {
    return query_sql(db, sql, params, 10000, out) && out.error.empty();
}

SqlParam text_param(std::string text) {
    SqlParam param;
    param.value.kind = SqlKind::Text;
    param.value.text = std::move(text);
    return param;
}

std::vector<std::string> index_columns(sqlite3* db, const std::string& name) {
    SqlTable info;
    std::vector<std::string> names;
    if (!one_table(db, "PRAGMA index_info(" + quote_ident(name) + ")", {}, info)) {
        return names;
    }
    for (std::size_t row = 0; row < info.rows.size(); ++row) {
        const std::string column = column_text(info, row, "name");
        if (!column.empty()) {
            names.push_back(column);
        }
    }
    return names;
}

}  // namespace

void read_catalog(sqlite3* db, Session& session) {
    session.objects.clear();
    session.version.clear();
    session.bytes = 0;
    session.catalog_error.clear();
    if (db == nullptr) {
        return;
    }
    SqlTable listed;
    const char* list_sql = R"SQL(
        SELECT type, name, tbl_name AS tblName, sql
        FROM sqlite_master
        WHERE substr(name, 1, 7) <> 'sqlite_'
        ORDER BY CASE type
            WHEN 'table' THEN 0
            WHEN 'view' THEN 1
            WHEN 'index' THEN 2
            WHEN 'trigger' THEN 3
            ELSE 4
        END, rowid
    )SQL";
    if (!query_sql(db, list_sql, {}, 10000, listed)) {
        session.catalog_error = listed.error.empty() ? "读不到目录。" : listed.error;
        return;
    }
    int relations = 0;
    for (const auto& row : listed.rows) {
        if (!row.empty() && (column_text(listed, &row - listed.rows.data(), "type") == "table" ||
                             column_text(listed, &row - listed.rows.data(), "type") == "view")) {
            ++relations;
        }
    }
    const bool countable = relations <= kCountLimit;
    for (std::size_t i = 0; i < listed.rows.size(); ++i) {
        DbObject object;
        object.type = column_text(listed, i, "type");
        object.name = column_text(listed, i, "name");
        object.table = column_text(listed, i, "tblName");
        const SqlValue* sql = column(listed, i, "sql");
        if (sql != nullptr && sql->kind == SqlKind::Text) {
            object.sql = sql->text;
        }
        if (countable && (object.type == "table" || object.type == "view")) {
            SqlTable count;
            if (query_sql(db, "SELECT COUNT(*) AS n FROM " + quote_ident(object.name), {}, 1, count) &&
                !count.rows.empty()) {
                const SqlValue* n = column(count, 0, "n");
                if (n != nullptr && n->kind == SqlKind::Integer) {
                    object.counted = true;
                    object.count = n->integer;
                }
            }
        }
        session.objects.push_back(std::move(object));
    }
    SqlTable version;
    if (query_sql(db, "SELECT sqlite_version() AS version", {}, 1, version) && !version.rows.empty()) {
        session.version = column_text(version, 0, "version");
    }
    SqlTable page_size;
    SqlTable page_count;
    if (query_sql(db, "PRAGMA page_size", {}, 1, page_size) &&
        query_sql(db, "PRAGMA page_count", {}, 1, page_count) && !page_size.rows.empty() &&
        !page_count.rows.empty()) {
        const SqlValue* size = column(page_size, 0, "page_size");
        const SqlValue* count = column(page_count, 0, "page_count");
        if (size != nullptr && count != nullptr && size->kind == SqlKind::Integer &&
            count->kind == SqlKind::Integer) {
            session.bytes = size->integer * count->integer;
        }
    }
}

Structure load_structure(sqlite3* db, const DbObject& object) {
    Structure structure;
    structure.sql = object.sql;
    structure.automatic = object.sql.empty() && object.type == "index";
    structure.parent = object.table;
    if (object.type == "index") {
        SqlTable list;
        if (object.table.empty() ||
            !one_table(db, "PRAGMA index_list(" + quote_ident(object.table) + ")", {}, list)) {
            structure.error = list.error;
        }
        for (std::size_t row = 0; row < list.rows.size(); ++row) {
            if (column_text(list, row, "name") != object.name) {
                continue;
            }
            const std::string origin = column_text(list, row, "origin");
            const bool unique = column_text(list, row, "unique") == "1";
            const bool partial = column_text(list, row, "partial") == "1";
            std::string facts = origin_label(origin);
            facts += unique ? " · 唯一" : " · 非唯一";
            if (partial) {
                facts += " · 部分索引";
            }
            if (structure.automatic) {
                facts += " · 自动创建";
            }
            structure.facts = facts;
            break;
        }
        structure.indexed = true;
        SqlTable info;
        if (one_table(db, "PRAGMA index_info(" + quote_ident(object.name) + ")", {}, info)) {
            for (std::size_t row = 0; row < info.rows.size(); ++row) {
                IndexColumn column;
                column.seq = dash(column_text(info, row, "seqno"));
                column.name = dash(column_text(info, row, "name"));
                column.cid = dash(column_text(info, row, "cid"));
                structure.index_columns.push_back(std::move(column));
            }
        }
        return structure;
    }
    if (object.type != "table" && object.type != "view") {
        return structure;
    }
    SqlTable columns;
    if (!one_table(db, "PRAGMA table_info(" + quote_ident(object.name) + ")", {}, columns)) {
        structure.error = columns.error.empty() ? "读不到列。" : columns.error;
        return structure;
    }
    for (std::size_t row = 0; row < columns.rows.size(); ++row) {
        RelationColumn field;
        field.cid = dash(column_text(columns, row, "cid"));
        field.name = dash(column_text(columns, row, "name"));
        field.type = dash(column_text(columns, row, "type"));
        field.notnull = column_text(columns, row, "notnull") == "1" ? "是" : "否";
        const SqlValue* dflt = column(columns, row, "dflt_value");
        field.dflt = dflt == nullptr || dflt->kind == SqlKind::Null ? "—" : column_text(columns, row, "dflt_value");
        const std::string pk = column_text(columns, row, "pk");
        field.pk = pk.empty() || pk == "0" ? "否" : pk;
        structure.columns.push_back(std::move(field));
    }
    if (object.type != "table") {
        return structure;
    }
    SqlTable keys;
    if (one_table(db, "PRAGMA foreign_key_list(" + quote_ident(object.name) + ")", {}, keys)) {
        for (std::size_t row = 0; row < keys.rows.size(); ++row) {
            ForeignKey key;
            key.from = dash(column_text(keys, row, "from"));
            key.table = column_text(keys, row, "table");
            key.to = dash(column_text(keys, row, "to"));
            key.on_update = dash(column_text(keys, row, "on_update"));
            key.on_delete = dash(column_text(keys, row, "on_delete"));
            if (!key.table.empty()) {
                bool seen = false;
                for (const std::string& name : structure.jump_tables) {
                    if (name == key.table) {
                        seen = true;
                        break;
                    }
                }
                if (!seen) {
                    structure.jump_tables.push_back(key.table);
                }
            }
            structure.foreign_keys.push_back(std::move(key));
        }
    }
    SqlTable indexes;
    if (one_table(db, "PRAGMA index_list(" + quote_ident(object.name) + ")", {}, indexes)) {
        for (std::size_t row = 0; row < indexes.rows.size(); ++row) {
            IndexRef index;
            index.name = dash(column_text(indexes, row, "name"));
            const std::vector<std::string> names = index_columns(db, column_text(indexes, row, "name"));
            std::string joined;
            for (const std::string& name : names) {
                if (!joined.empty()) {
                    joined += ", ";
                }
                joined += name;
            }
            index.columns = dash(joined);
            index.origin = origin_label(column_text(indexes, row, "origin"));
            index.unique = column_text(indexes, row, "unique") == "1" ? "是" : "否";
            index.partial = column_text(indexes, row, "partial") == "1" ? "是" : "否";
            structure.indexes.push_back(std::move(index));
        }
    }
    SqlTable triggers;
    if (one_table(db, "SELECT name, sql FROM sqlite_master WHERE type = 'trigger' AND tbl_name = ? ORDER BY rowid",
                  {text_param(object.name)}, triggers)) {
        for (std::size_t row = 0; row < triggers.rows.size(); ++row) {
            TriggerRef trigger;
            trigger.name = column_text(triggers, row, "name");
            const SqlValue* sql = column(triggers, row, "sql");
            if (sql != nullptr && sql->kind == SqlKind::Text) {
                trigger.sql = sql->text;
            }
            structure.triggers.push_back(std::move(trigger));
        }
    }
    return structure;
}

}  // namespace zdb
