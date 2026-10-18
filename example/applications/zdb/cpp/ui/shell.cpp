#include "ui/view.h"

#include "util.h"

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

std::string status_meta(const Session& session) {
    std::string meta = "SQLite " + session.version;
    const char* types[] = {"table", "view", "index", "trigger"};
    for (const char* type : types) {
        int count = 0;
        for (const DbObject& object : session.objects) {
            if (object.type == type) {
                ++count;
            }
        }
        if (count > 0) {
            meta += " · ";
            meta += format_count(count);
            meta += " ";
            meta += kind_label(type);
        }
    }
    const std::string size = format_size(session.bytes);
    if (!size.empty()) {
        meta += " · ";
        meta += size;
    }
    meta += " · 外键已启用 · 改动只在内存，导出才会保存";
    return meta;
}

void enable(zui::App& app, const char* id, bool on) {
    app.document().set(id, "disabled", on ? "false" : "true");
}

}  // namespace

void show_shell(Session& session) {
    zui::App& app = *session.app;
    const bool open = session.db != nullptr;
    write(app, "file-name", open ? session.file_name : "未打开数据库");
    write(app, "status-text", session.notice);
    write(app, "status-meta", open ? status_meta(session) : "");
    flag(app, "status", "is-error", session.notice_error);
    // Native sqlite is already linked, which is the browser's `engineReady` (sql.js loaded).
    enable(app, "open-file", true);
    enable(app, "open-sample", true);
    enable(app, "export-db", open);
    enable(app, "mode-data", open);
    enable(app, "mode-structure", open);
    enable(app, "mode-query", open);
    enable(app, "object-filter", open);
    enable(app, "run-sql", open);
    enable(app, "clear-sql", open);
    show(app, "modebar", open);
    const std::string pane = open ? session.mode : "welcome";
    show(app, "pane-welcome", pane == "welcome");
    show(app, "pane-data", pane == "data");
    show(app, "pane-structure", pane == "structure");
    show(app, "pane-query", pane == "query");
    flag(app, "mode-data", "is-on", session.mode == "data");
    flag(app, "mode-structure", "is-on", session.mode == "structure");
    flag(app, "mode-query", "is-on", session.mode == "query");
    // The browser marks the current mode with aria-selected (shell.js). Both tokens paint.
    app.document().set("mode-data", "aria-selected", session.mode == "data" ? "true" : "false");
    app.document().set("mode-structure", "aria-selected", session.mode == "structure" ? "true" : "false");
    app.document().set("mode-query", "aria-selected", session.mode == "query" ? "true" : "false");
}

void show_all(Session& session) {
    show_shell(session);
    show_sidebar(session);
    show_grid(session);
    show_structure(session);
    show_query(session);
}

}  // namespace zdb
