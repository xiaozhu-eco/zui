#include "native_file.h"

namespace zdb {
namespace {

bool take_single(const zui::PickPaths& picked, std::filesystem::path& out) {
    if (picked.status != zui::PickStatus::Ok || picked.paths.empty()) {
        out.clear();
        return false;
    }
    out = picked.paths.front();
    return true;
}

bool take_path(const zui::PickPath& picked, std::filesystem::path& out) {
    if (picked.status != zui::PickStatus::Ok || picked.path.empty()) {
        out.clear();
        return false;
    }
    out = picked.path;
    return true;
}

}  // namespace

bool pick_open_sqlite(zui::App& app, std::filesystem::path& out) {
    zui::FileDialogOptions opt;
    opt.title = "Open SQLite";
    opt.filters.push_back({"SQLite", "db;sqlite;sqlite3;db3"});
    opt.filters.push_back({"All", "*"});
    return take_single(app.host().pick_open(opt), out);
}

bool pick_save_sqlite(zui::App& app, std::filesystem::path& out, const std::string& suggested) {
    zui::FileDialogOptions opt;
    opt.title = "Export SQLite";
    opt.default_name = suggested;
    opt.filters.push_back({"SQLite", "db;sqlite;sqlite3;db3"});
    opt.filters.push_back({"All", "*"});
    return take_path(app.host().pick_save(opt), out);
}

}  // namespace zdb
