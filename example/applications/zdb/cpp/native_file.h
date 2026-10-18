#ifndef ZDB_NATIVE_FILE_H
#define ZDB_NATIVE_FILE_H

#include <zui/app.h>

#include <filesystem>
#include <string>

namespace zdb {

bool pick_open_sqlite(zui::App& app, std::filesystem::path& out);
bool pick_save_sqlite(zui::App& app, std::filesystem::path& out, const std::string& suggested);

}  // namespace zdb

#endif
