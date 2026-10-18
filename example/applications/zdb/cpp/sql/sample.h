#ifndef ZDB_SQL_SAMPLE_H
#define ZDB_SQL_SAMPLE_H

#include <string>

struct sqlite3;

namespace zdb {

inline constexpr const char* kSampleName = "竹间书肆.sqlite";
inline constexpr const char* kSampleTable = "books";

bool seed_bookstore(sqlite3* db, std::string& error);

}  // namespace zdb

#endif
