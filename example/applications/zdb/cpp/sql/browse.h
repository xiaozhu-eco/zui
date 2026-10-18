#ifndef ZDB_SQL_BROWSE_H
#define ZDB_SQL_BROWSE_H

#include "session.h"

struct sqlite3;

namespace zdb {

Page read_page(sqlite3* db, const std::string& name, int limit, int offset, const std::string& order,
               const std::string& dir);

}  // namespace zdb

#endif
