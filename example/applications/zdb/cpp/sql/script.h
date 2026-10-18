#ifndef ZDB_SQL_SCRIPT_H
#define ZDB_SQL_SCRIPT_H

#include "session.h"

#include <string>
#include <vector>

struct sqlite3;

namespace zdb {

std::vector<StatementResult> run_script(sqlite3* db, const std::string& sql);
std::string summarize_script(const std::vector<StatementResult>& results);

}  // namespace zdb

#endif
