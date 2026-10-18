#include "sql/browse.h"

#include "sql/db.h"

#include "util.h"

namespace zdb {
namespace {

int normalize_limit(int limit) {
    if (limit == 25 || limit == 50 || limit == 100 || limit == 200) {
        return limit;
    }
    return 25;
}

SqlParam int_param(std::int64_t value) {
    SqlParam param;
    param.value.kind = SqlKind::Integer;
    param.value.integer = value;
    return param;
}

}  // namespace

Page read_page(sqlite3* db, const std::string& name, int limit, int offset, const std::string& order,
               const std::string& dir) {
    Page page;
    page.browsable = true;
    page.limit = normalize_limit(limit);
    page.offset = offset < 0 ? 0 : offset;
    page.dir = dir == "desc" ? "desc" : "asc";
    const std::string ident = quote_ident(name);
    SqlTable counted;
    if (!query_sql(db, "SELECT COUNT(*) AS n FROM " + ident, {}, 1, counted)) {
        page.error = counted.error.empty() ? "读不到行数。" : counted.error;
        return page;
    }
    if (!counted.rows.empty()) {
        const SqlValue* n = column(counted, 0, "n");
        if (n != nullptr && n->kind == SqlKind::Integer) {
            page.total = n->integer;
        }
    }
    if (page.total <= 0) {
        page.offset = 0;
    } else if (page.offset >= page.total) {
        page.offset = static_cast<int>(((page.total - 1) / page.limit) * page.limit);
    }
    std::string order_sql;
    if (!order.empty()) {
        SqlTable info;
        if (query_sql(db, "PRAGMA table_info(" + ident + ")", {}, 1000, info)) {
            for (std::size_t row = 0; row < info.rows.size(); ++row) {
                if (column_text(info, row, "name") == order) {
                    page.order = order;
                    order_sql = " ORDER BY " + quote_ident(order) + (page.dir == "desc" ? " DESC" : " ASC");
                    break;
                }
            }
        }
    }
    SqlTable data;
    const std::string sql = "SELECT * FROM " + ident + order_sql + " LIMIT ? OFFSET ?";
    if (!query_sql(db, sql, {int_param(page.limit), int_param(page.offset)}, page.limit, data)) {
        page.error = data.error.empty() ? "读不到这一页。" : data.error;
        return page;
    }
    page.columns = std::move(data.columns);
    page.rows = std::move(data.rows);
    return page;
}

}  // namespace zdb
