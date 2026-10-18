#ifndef ZDB_UI_VIEW_H
#define ZDB_UI_VIEW_H

#include "session.h"

#include <zui/app.h>

#include <string_view>
#include <vector>

namespace zdb {

void show_all(Session& session);
void show_shell(Session& session);
void show_sidebar(Session& session);
void show_grid(Session& session);
void show_structure(Session& session);
void show_query(Session& session);
void paint_detail(Session& session);
void paint_query_detail(Session& session);
void match_clicked_cell(const std::vector<std::string>& columns, const std::vector<std::vector<SqlValue>>& rows,
                        int offset, std::string_view shown, std::string& label, std::string& body);

void adopt_database(Session& session, sqlite3* db, std::string name, const std::string& prefer);
void select_named(Session& session, const std::string& type, const std::string& name);
void sort_column(Session& session, const std::string& column);
void move_page(Session& session, int direction);
void set_limit(Session& session, int limit);
void run_pending_sql(Session& session);
void open_path(Session& session, const std::filesystem::path& path);
void open_sample(Session& session);
void bind_actions(Session& session);
/// Loads index.html beside the executable. A database argv is that document, not the page.
bool start(Session& session, int argc, char** argv);

inline void show(zui::App& app, const char* id, bool visible) {
    app.document().set(id, "visible", visible ? "true" : "false");
}

inline void write(zui::App& app, const char* id, std::string_view text) {
    app.document().set(id, "text", text);
}

inline void flag(zui::App& app, const char* id, const char* name, bool on) {
    if (on) {
        app.document().add_class(id, name);
    } else {
        app.document().remove_class(id, name);
    }
}

}  // namespace zdb

#endif
