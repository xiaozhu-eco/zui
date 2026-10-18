#include "ui/view.h"

#include "sql/browse.h"
#include "sql/catalog.h"
#include "sql/db.h"
#include "sql/sample.h"
#include "sql/script.h"
#include "util.h"

namespace zdb {
namespace {

int find_object(const Session& session, const std::string& type, const std::string& name) {
    for (std::size_t i = 0; i < session.objects.size(); ++i) {
        const DbObject& object = session.objects[i];
        if (object.type == type && object.name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int pick_object(const Session& session, const std::string& prefer) {
    if (!prefer.empty()) {
        const int preferred = find_object(session, "table", prefer);
        if (preferred >= 0) {
            return preferred;
        }
    }
    const int table = [&] {
        for (std::size_t i = 0; i < session.objects.size(); ++i) {
            if (session.objects[i].type == "table") {
                return static_cast<int>(i);
            }
        }
        return -1;
    }();
    if (table >= 0) {
        return table;
    }
    for (std::size_t i = 0; i < session.objects.size(); ++i) {
        if (session.objects[i].type == "view") {
            return static_cast<int>(i);
        }
    }
    return session.objects.empty() ? -1 : 0;
}

void load_selected(Session& session, bool keep_page) {
    const int limit = session.page.limit > 0 ? session.page.limit : 25;
    const int offset = keep_page ? session.page.offset : 0;
    const std::string order = keep_page ? session.page.order : std::string{};
    const std::string dir = keep_page && !session.page.dir.empty() ? session.page.dir : "asc";
    const DbObject* object = selected_object(session);
    if (object == nullptr || session.db == nullptr) {
        session.page = {};
        session.page.limit = limit;
        session.structure = {};
        return;
    }
    session.structure = load_structure(session.db, *object);
    if (object->type == "table" || object->type == "view") {
        session.page = read_page(session.db, object->name, limit, offset, order, dir);
    } else {
        session.page = {};
        session.page.limit = limit;
    }
}

void remember_selection(Session& session, std::string& type, std::string& name) {
    const DbObject* object = selected_object(session);
    if (object == nullptr) {
        type.clear();
        name.clear();
        return;
    }
    type = object->type;
    name = object->name;
}

}  // namespace

void adopt_database(Session& session, sqlite3* db, std::string name, const std::string& prefer) {
    close_sqlite(session.db);
    session.db = db;
    session.file_name = std::move(name);
    session.filter.clear();
    session.app->document().set("object-filter", "value", "");
    session.results.clear();
    session.result_index = 0;
    session.detail_open = false;
    session.query_detail_open = false;
    session.grid_pick_ref.clear();
    session.query_pick_ref.clear();
    session.page = {};
    read_catalog(session.db, session);
    session.selected = pick_object(session, prefer);
    const DbObject* object = selected_object(session);
    const bool browsable = object != nullptr && (object->type == "table" || object->type == "view");
    session.mode = browsable ? "data" : "structure";
    load_selected(session, false);
    (void)session.app->window().set_title(session.file_name);
    session.notice = "已打开 " + session.file_name;
    session.notice_error = false;
    session.reset_grid_scroll = true;
    session.reset_structure_scroll = true;
    show_all(session);
}

void select_named(Session& session, const std::string& type, const std::string& name) {
    if (session.db == nullptr) {
        return;
    }
    const int index = find_object(session, type, name);
    if (index < 0) {
        session.notice = "找不到 " + name + "。";
        session.notice_error = true;
        show_shell(session);
        return;
    }
    const int limit = session.page.limit > 0 ? session.page.limit : 25;
    session.selected = index;
    session.page = {};
    session.page.limit = limit;
    session.detail_open = false;
    load_selected(session, false);
    const DbObject* object = selected_object(session);
    if (object != nullptr && object->type != "table" && object->type != "view" && session.mode == "data") {
        session.mode = "structure";
    }
    session.reset_grid_scroll = true;
    session.reset_structure_scroll = true;
    show_all(session);
}

void reload_page(Session& session) {
    const DbObject* object = selected_object(session);
    if (session.db == nullptr || object == nullptr) {
        return;
    }
    const int limit = session.page.limit;
    const int offset = session.page.offset;
    const std::string order = session.page.order;
    const std::string dir = session.page.dir;
    if (object->type == "table" || object->type == "view") {
        session.page = read_page(session.db, object->name, limit, offset, order, dir);
    }
    session.detail_open = false;
    session.reset_grid_scroll = true;
    show_grid(session);
}

void sort_column(Session& session, const std::string& column) {
    if (session.db == nullptr || !session.page.browsable || column.empty()) {
        return;
    }
    if (session.page.order == column && session.page.dir == "asc") {
        session.page.dir = "desc";
    } else if (session.page.order == column && session.page.dir == "desc") {
        session.page.order.clear();
        session.page.dir = "asc";
    } else {
        session.page.order = column;
        session.page.dir = "asc";
    }
    session.page.offset = 0;
    reload_page(session);
}

void move_page(Session& session, int direction) {
    if (!session.page.browsable || direction == 0) {
        return;
    }
    const int limit = session.page.limit > 0 ? session.page.limit : 25;
    if (direction < 0 && session.page.offset <= 0) {
        return;
    }
    if (direction > 0 &&
        static_cast<std::int64_t>(session.page.offset) + limit >= session.page.total) {
        return;
    }
    int offset = session.page.offset + direction * limit;
    if (offset < 0) {
        offset = 0;
    }
    session.page.offset = offset;
    reload_page(session);
}

void set_limit(Session& session, int limit) {
    if (!session.page.browsable) {
        return;
    }
    session.page.limit = limit;
    session.page.offset = 0;
    reload_page(session);
}

void refresh_after_write(Session& session) {
    std::string type;
    std::string name;
    remember_selection(session, type, name);
    const int limit = session.page.limit;
    const int offset = session.page.offset;
    const std::string order = session.page.order;
    const std::string dir = session.page.dir;
    read_catalog(session.db, session);
    session.selected = find_object(session, type, name);
    session.page.limit = limit;
    session.page.offset = offset;
    session.page.order = order;
    session.page.dir = dir;
    load_selected(session, true);
}

void run_pending_sql(Session& session) {
    if (session.db == nullptr) {
        return;
    }
    session.results = run_script(session.db, session.pending_sql);
    session.result_index = 0;
    session.mode = "query";
    session.query_detail_open = false;
    session.notice = summarize_script(session.results);
    session.notice_error = false;
    bool changed = false;
    for (const StatementResult& result : session.results) {
        if (!result.ok) {
            session.notice_error = true;
        }
        if (result.ok && result.columns.empty()) {
            changed = true;
        }
    }
    session.reset_query_scroll = true;
    if (changed) {
        refresh_after_write(session);
    }
    show_all(session);
}

namespace {

bool database_path(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    for (char& c : ext) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return ext == ".db" || ext == ".sqlite" || ext == ".sqlite3";
}

}  // namespace

bool start(Session& session, int argc, char** argv) {
    if (session.app == nullptr || argv == nullptr || argc < 1 || argv[0] == nullptr) {
        return false;
    }
    const std::filesystem::path exe_dir = std::filesystem::absolute(argv[0]).parent_path();
    std::filesystem::path page = exe_dir / "index.html";
    std::filesystem::path content;
    if (argc > 1 && argv[1] != nullptr && argv[1][0] != '\0') {
        const std::filesystem::path argument(argv[1]);
        if (database_path(argument)) {
            content = argument;
        } else {
            page = argument;
        }
    }
    if (!session.app->document().load(page.string())) {
        return false;
    }
    session.dir = page.parent_path();
    if (session.dir.empty()) {
        session.dir = exe_dir;
    }
    session.app->window().set_title("小竹数据库");
    session.app->window().set_window_size(1280, 800);
    bind_actions(session);
    show_all(session);
    if (content.empty()) {
        return true;
    }
    open_path(session, content);
    return session.db != nullptr && !session.notice_error;
}

void open_path(Session& session, const std::filesystem::path& path) {
    sqlite3* db = nullptr;
    std::string error;
    if (!open_sqlite_file(path, db, error)) {
        session.notice = error.empty() ? "打不开这个文件。" : error;
        session.notice_error = true;
        show_shell(session);
        return;
    }
    adopt_database(session, db, utf8_path(path.filename()), "");
}

void open_sample(Session& session) {
    sqlite3* db = nullptr;
    std::string error;
    if (!open_memory(db, error) || !seed_bookstore(db, error)) {
        close_sqlite(db);
        session.notice = error.empty() ? "示例库没有生成。" : error;
        session.notice_error = true;
        show_shell(session);
        return;
    }
    adopt_database(session, db, kSampleName, kSampleTable);
}

}  // namespace zdb
