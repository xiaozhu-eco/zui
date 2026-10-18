#include "ui/view.h"

#include "ui/models.h"

namespace zdb {
namespace {

std::string kind_label(const std::string& type) {
    if (type == "table") {
        return "表";
    }
    if (type == "view") {
        return "视图";
    }
    if (type == "index") {
        return "索引";
    }
    if (type == "trigger") {
        return "触发器";
    }
    return type;
}

void fill_span_rows(zui::App& app, const char* id, const std::vector<std::vector<std::pair<const char*, std::string>>>& rows) {
    RecordList list;
    for (const auto& fields : rows) {
        RecordList::Row row;
        for (const auto& field : fields) {
            put(row, field.first, field.second);
        }
        list.rows.push_back(std::move(row));
    }
    app.document().set_model(id, list);
}

}  // namespace

void show_structure(Session& session) {
    zui::App& app = *session.app;
    const DbObject* object = selected_object(session);
    const bool ready = object != nullptr && session.structure.error.empty();
    show(app, "struct-empty", !ready);
    show(app, "struct-title", object != nullptr);
    if (object == nullptr) {
        write(app, "struct-empty", "从左侧选择一个对象。");
    } else if (!session.structure.error.empty()) {
        write(app, "struct-name", object->name);
        write(app, "struct-kind", kind_label(object->type));
        write(app, "struct-empty", session.structure.error);
    } else {
        write(app, "struct-name", object->name);
        write(app, "struct-kind", kind_label(object->type));
    }
    const Structure& structure = session.structure;
    const bool facts = ready && !structure.facts.empty();
    show(app, "struct-facts", facts);
    if (facts) {
        write(app, "struct-facts", structure.facts);
    }
    const bool parent = ready && !structure.parent.empty() && object != nullptr &&
                        (object->type == "index" || object->type == "trigger");
    show(app, "struct-parent-wrap", parent);
    if (parent) {
        write(app, "struct-parent", "打开 " + structure.parent);
        app.document().set("struct-parent", "value", structure.parent);
    }
    const bool columns = ready && !structure.indexed && !structure.columns.empty();
    show(app, "struct-col-h", columns);
    show(app, "struct-columns-wrap", columns);
    if (columns) {
        std::vector<std::vector<std::pair<const char*, std::string>>> rows;
        for (const RelationColumn& column : structure.columns) {
            rows.push_back({{"cid", column.cid},
                            {"name", column.name},
                            {"type", column.type},
                            {"notnull", column.notnull},
                            {"dflt", column.dflt},
                            {"pk", column.pk}});
        }
        fill_span_rows(app, "struct-columns", rows);
    }
    const bool index_columns = ready && structure.indexed && !structure.index_columns.empty();
    show(app, "struct-idxcol-h", index_columns);
    show(app, "struct-idxcols-wrap", index_columns);
    if (index_columns) {
        std::vector<std::vector<std::pair<const char*, std::string>>> rows;
        for (const IndexColumn& column : structure.index_columns) {
            rows.push_back({{"seq", column.seq}, {"name", column.name}, {"cid", column.cid}});
        }
        fill_span_rows(app, "struct-idxcols", rows);
    }
    const bool keys = ready && !structure.foreign_keys.empty();
    show(app, "struct-fk-h", keys);
    show(app, "struct-fks-wrap", keys);
    show(app, "struct-fk-jumps", keys);
    if (keys) {
        std::vector<std::vector<std::pair<const char*, std::string>>> rows;
        for (const ForeignKey& key : structure.foreign_keys) {
            rows.push_back({{"from", key.from},
                            {"table", key.table},
                            {"to", key.to},
                            {"on_update", key.on_update},
                            {"on_delete", key.on_delete}});
        }
        fill_span_rows(app, "struct-fks", rows);
        RecordList jumps;
        for (const std::string& name : structure.jump_tables) {
            RecordList::Row row;
            put(row, "table", name);
            jumps.rows.push_back(std::move(row));
        }
        app.document().set_model("struct-fk-jumps", jumps);
    }
    const bool indexes = ready && !structure.indexes.empty();
    show(app, "struct-index-h", indexes);
    show(app, "struct-indexes-wrap", indexes);
    if (indexes) {
        std::vector<std::vector<std::pair<const char*, std::string>>> rows;
        for (const IndexRef& index : structure.indexes) {
            rows.push_back({{"name", index.name},
                            {"columns", index.columns},
                            {"origin", index.origin},
                            {"unique", index.unique},
                            {"partial", index.partial}});
        }
        fill_span_rows(app, "struct-indexes", rows);
    }
    const bool triggers = ready && !structure.triggers.empty();
    show(app, "struct-trigger-h", triggers);
    show(app, "struct-triggers", triggers);
    if (triggers) {
        RecordList list;
        for (const TriggerRef& trigger : structure.triggers) {
            RecordList::Row row;
            put(row, "name", trigger.name);
            put(row, "sql", trigger.sql);
            list.rows.push_back(std::move(row));
        }
        app.document().set_model("struct-triggers", list);
    }
    const bool sql = ready && !structure.sql.empty();
    const bool automatic = ready && structure.automatic && structure.sql.empty();
    show(app, "struct-sql-h", sql);
    show(app, "struct-sql", sql);
    show(app, "struct-auto", automatic);
    if (sql) {
        write(app, "struct-sql", structure.sql);
    }
    if (session.reset_structure_scroll && ready) {
        app.document().set("structure", "scroll_top", "0");
        session.reset_structure_scroll = false;
    }
}

}  // namespace zdb
