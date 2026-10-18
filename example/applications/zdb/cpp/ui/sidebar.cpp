#include "ui/view.h"

#include "ui/models.h"

#include "util.h"

namespace zdb {
namespace {

struct Group {
    const char* type;
    const char* label;
    const char* badge;
};

const Group kGroups[] = {
    {"table", "表", "表"},
    {"view", "视图", "视"},
    {"index", "索引", "索"},
    {"trigger", "触发器", "触"},
};

bool matches(const DbObject& object, const std::string& needle) {
    if (needle.empty()) {
        return true;
    }
    if (lower_ascii(object.name).find(needle) != std::string::npos) {
        return true;
    }
    return lower_ascii(object.table).find(needle) != std::string::npos;
}

}  // namespace

void show_sidebar(Session& session) {
    zui::App& app = *session.app;
    RecordList list;
    const std::string needle = lower_ascii(trim_copy(session.filter));
    const DbObject* current = selected_object(session);
    std::string selected_row_id;
    auto add_group = [&](const Group& group, const std::vector<const DbObject*>& items) {
        if (items.empty()) {
            return;
        }
        RecordList::Row header;
        put(header, "rowclass", "group-label");
        put(header, "handler", "");
        put(header, "key", "");
        put(header, "badge", "");
        put(header, "name", std::string(group.label) + " " + std::to_string(items.size()));
        put(header, "meta", "");
        put(header, "count", "");
        put(header, "tip", "");
        list.rows.push_back(std::move(header));
        for (const DbObject* object : items) {
            RecordList::Row row;
            const bool on = current != nullptr && current->type == object->type && current->name == object->name;
            put(row, "rowclass", on ? "obj is-selected" : "obj");
            put(row, "handler", "on_object");
            put(row, "key", object->type + std::string("\x1f") + object->name);
            put(row, "badge", group.badge);
            put(row, "name", object->name);
            const bool relation = object->type == "table" || object->type == "view";
            put(row, "meta", !relation ? object->table : "");
            put(row, "count", object->counted ? format_count(object->count) : "");
            put(row, "tip", object->name);
            list.rows.push_back(std::move(row));
            if (on) {
                selected_row_id = "obj-" + std::to_string(list.rows.size() - 1);
            }
        }
    };
    int shown = 0;
    for (const Group& group : kGroups) {
        std::vector<const DbObject*> items;
        for (const DbObject& object : session.objects) {
            if (object.type == group.type && matches(object, needle)) {
                items.push_back(&object);
            }
        }
        shown += static_cast<int>(items.size());
        add_group(group, items);
    }
    std::vector<const DbObject*> other;
    for (const DbObject& object : session.objects) {
        bool known = false;
        for (const Group& group : kGroups) {
            if (object.type == group.type) {
                known = true;
                break;
            }
        }
        if (!known && matches(object, needle)) {
            other.push_back(&object);
        }
    }
    shown += static_cast<int>(other.size());
    add_group(Group{"other", "其他", "其"}, other);
    app.document().set_model("object-list", list);
    std::string empty = "打开数据库后，对象会列在这里。";
    if (session.db != nullptr && !session.catalog_error.empty()) {
        empty = session.catalog_error;
    } else if (session.db != nullptr && shown == 0) {
        empty = needle.empty() ? "这个数据库里没有可显示的对象。" : "没有匹配的对象。";
    }
    write(app, "object-empty", empty);
    show(app, "object-empty", shown == 0);
    if (!selected_row_id.empty()) {
        zui::App* app_ptr = session.app;
        const std::string id = selected_row_id;
        app.document().defer([app_ptr, id]() {
            if (app_ptr != nullptr) {
                (void)app_ptr->document().scroll_into_view(id, zui::ScrollAlign::Nearest);
            }
        });
    }
}

}  // namespace zdb
