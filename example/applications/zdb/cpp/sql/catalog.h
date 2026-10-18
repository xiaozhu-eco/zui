#ifndef ZDB_SQL_CATALOG_H
#define ZDB_SQL_CATALOG_H

#include "session.h"

struct sqlite3;

namespace zdb {

void read_catalog(sqlite3* db, Session& session);
Structure load_structure(sqlite3* db, const DbObject& object);

}  // namespace zdb

#endif
