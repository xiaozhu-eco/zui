#include "sql/sample.h"

#include "sql/db.h"

#include "sqlite3.h"

namespace zdb {
namespace {

const char* kSchema = R"SQL(
CREATE TABLE authors (
  id INTEGER PRIMARY KEY,
  name TEXT NOT NULL,
  region TEXT NOT NULL
);
CREATE TABLE shelves (
  id INTEGER PRIMARY KEY,
  code TEXT NOT NULL UNIQUE,
  title TEXT NOT NULL
);
CREATE TABLE books (
  id INTEGER PRIMARY KEY,
  title TEXT NOT NULL,
  author_id INTEGER NOT NULL REFERENCES authors(id),
  published INTEGER,
  pages INTEGER,
  price_cents INTEGER NOT NULL CHECK (price_cents >= 0)
);
CREATE TABLE copies (
  shelf_id INTEGER NOT NULL REFERENCES shelves(id),
  book_id INTEGER NOT NULL REFERENCES books(id),
  qty INTEGER NOT NULL CHECK (qty > 0),
  PRIMARY KEY (shelf_id, book_id)
);
CREATE TABLE annotations (
  id INTEGER PRIMARY KEY,
  book_id INTEGER NOT NULL REFERENCES books(id),
  body TEXT,
  created TEXT NOT NULL
);
CREATE TABLE stamps (
  id INTEGER PRIMARY KEY,
  book_id INTEGER NOT NULL REFERENCES books(id),
  image BLOB NOT NULL
);
CREATE VIEW book_cards AS
SELECT
  b.id,
  b.title,
  a.name AS author,
  a.region,
  b.published,
  b.pages,
  printf('%.2f', b.price_cents / 100.0) AS price_yuan
FROM books b
JOIN authors a ON a.id = b.author_id;
CREATE INDEX idx_books_author ON books(author_id);
CREATE INDEX idx_books_published ON books(published);
CREATE INDEX idx_copies_book ON copies(book_id);
CREATE TRIGGER books_price_guard
BEFORE UPDATE OF price_cents ON books
WHEN NEW.price_cents < 0
BEGIN
  SELECT RAISE(ABORT, '价格不能为负');
END;
)SQL";

struct BookSeed {
    const char* title;
    int author;
    int published;
    int pages;
    int price;
};

constexpr int kNull = -1;

const BookSeed kBooks[] = {
    {"渡口手记", 1, 2018, 240, 4200}, {"灯塔摘录", 2, 2016, 128, 2800},
    {"海岸潮汐", 3, 2021, 312, 5600}, {"竹间菜谱", 4, 2014, 196, 3900},
    {"雨夜索引", 5, 2019, 88, 2200},  {"南风信札", 6, 2011, 264, 4500},
    {"旧城地图", 1, kNull, 144, 3600}, {"茶烟录", 2, 2008, 176, 3100},
    {"小岛文法", 3, 2020, 208, 4800}, {"冬至来信", 4, 2017, 96, 1900},
    {"桥洞几何", 5, 2013, kNull, 2700}, {"米缸账本", 6, 2006, 352, 5200},
    {"晚钟谱", 1, 2022, 120, 2600},   {"山脊步行", 2, 2015, 288, 6100},
    {"纸上船", 3, 2009, 160, 2400},   {"北窗杂抄", 4, 2012, 224, 4300},
    {"渔灯问答", 5, 2004, 304, 5800}, {"槐花与盐", 6, 2023, 136, 3300},
    {"短途星图", 1, 2010, 192, 4700}, {"厨房纬度", 2, 2018, 168, 3500},
    {"慢河", 3, 2007, 256, 4900},     {"匣中目录", 4, 2016, 80, 1800},
    {"第三个月台", 5, 2024, 272, 6400}, {"雾中邮局", 6, 2011, 184, 3700},
    {"青石算术", 1, 2005, 336, 5500}, {"夜里的厨房", 2, 2019, 148, 3000},
    {"未寄出的票根", 3, 2014, 112, 2100}, {"浅湾", 4, 2020, 400, 7200},
};

const char* kNote =
    "雨下得太久之后，索引里的页码会粘在一起。周晚宁把灯塔那一章折了角，又在空白处写：潮水并不负责把人送回去。"
    "书页边缘沾着盐，摸上去像细砂。店主把这一条标成批注，是因为正文里没有写到厨房停电的那个晚上，而停电才是他们决定把书从岛上运走的原因。"
    "后来的版本删掉了这段，只留了一个脚注编号，编号指向的那一页是空白的。空白页上仍能看出曾经贴过一张潮汐表，表的角落写着「不要按页码找，按风向找」。"
    "再往后是一串到港时间，每条都不超过四个字，连起来却比正文更长：寅初、卯正、雾、停、再开、未时、西风、空仓、有人来接、没人来接。";

SqlValue integer_or_null(int value) {
    SqlValue out;
    if (value == kNull) {
        out.kind = SqlKind::Null;
        return out;
    }
    out.kind = SqlKind::Integer;
    out.integer = value;
    return out;
}

SqlValue text_value(const char* text) {
    SqlValue out;
    if (text == nullptr) {
        out.kind = SqlKind::Null;
        return out;
    }
    out.kind = SqlKind::Text;
    out.text = text;
    return out;
}

SqlValue int_value(int value) {
    SqlValue out;
    out.kind = SqlKind::Integer;
    out.integer = value;
    return out;
}

bool run_row(sqlite3* db, const char* sql, const std::vector<SqlValue>& values, std::string& error) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(db);
        return false;
    }
    for (std::size_t i = 0; i < values.size(); ++i) {
        const SqlValue& value = values[i];
        int rc = SQLITE_OK;
        const int index = static_cast<int>(i + 1);
        if (value.kind == SqlKind::Null) {
            rc = sqlite3_bind_null(stmt, index);
        } else if (value.kind == SqlKind::Integer) {
            rc = sqlite3_bind_int64(stmt, index, value.integer);
        } else if (value.kind == SqlKind::Text) {
            rc = sqlite3_bind_text(stmt, index, value.text.data(), static_cast<int>(value.text.size()),
                                   SQLITE_TRANSIENT);
        } else if (value.kind == SqlKind::Blob) {
            rc = sqlite3_bind_blob(stmt, index, value.blob.data(), static_cast<int>(value.blob.size()),
                                   SQLITE_TRANSIENT);
        }
        if (rc != SQLITE_OK) {
            error = sqlite3_errmsg(db);
            sqlite3_finalize(stmt);
            return false;
        }
    }
    const int stepped = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (stepped != SQLITE_DONE) {
        error = sqlite3_errmsg(db);
        return false;
    }
    return true;
}

}  // namespace

bool seed_bookstore(sqlite3* db, std::string& error) {
    if (!exec_sql(db, "BEGIN", error) || !exec_sql(db, kSchema, error)) {
        std::string ignored;
        exec_sql(db, "ROLLBACK", ignored);
        return false;
    }
    const char* authors[][2] = {{"周晚宁", "江南"}, {"宋小满", "闽东"}, {"何泊", "湘西"},
                                {"阮清和", "岭南"}, {"裴照", "燕北"},   {"唐米", "蜀中"}};
    for (int i = 0; i < 6; ++i) {
        if (!run_row(db, "INSERT INTO authors(id, name, region) VALUES (?,?,?)",
                     {int_value(i + 1), text_value(authors[i][0]), text_value(authors[i][1])}, error)) {
            std::string ignored;
            exec_sql(db, "ROLLBACK", ignored);
            return false;
        }
    }
    const char* shelves[][2] = {{"A", "窗边新到"}, {"B", "手记"}, {"C", "图册"}};
    for (int i = 0; i < 3; ++i) {
        if (!run_row(db, "INSERT INTO shelves(id, code, title) VALUES (?,?,?)",
                     {int_value(i + 1), text_value(shelves[i][0]), text_value(shelves[i][1])}, error)) {
            std::string ignored;
            exec_sql(db, "ROLLBACK", ignored);
            return false;
        }
    }
    constexpr int kBookCount = static_cast<int>(sizeof(kBooks) / sizeof(kBooks[0]));
    for (int i = 0; i < kBookCount; ++i) {
        const BookSeed& book = kBooks[i];
        if (!run_row(db,
                     "INSERT INTO books(id, title, author_id, published, pages, price_cents) VALUES (?,?,?,?,?,?)",
                     {int_value(i + 1), text_value(book.title), int_value(book.author),
                      integer_or_null(book.published), integer_or_null(book.pages), int_value(book.price)},
                     error)) {
            std::string ignored;
            exec_sql(db, "ROLLBACK", ignored);
            return false;
        }
        const int shelf = (i % 3) + 1;
        if (!run_row(db, "INSERT INTO copies(shelf_id, book_id, qty) VALUES (?,?,?)",
                     {int_value(shelf), int_value(i + 1), int_value((i + 1) % 5 + 1)}, error)) {
            std::string ignored;
            exec_sql(db, "ROLLBACK", ignored);
            return false;
        }
        if (i < 8) {
            const int second = (shelf % 3) + 1;
            if (!run_row(db, "INSERT INTO copies(shelf_id, book_id, qty) VALUES (?,?,?)",
                         {int_value(second), int_value(i + 1), int_value(1)}, error)) {
                std::string ignored;
                exec_sql(db, "ROLLBACK", ignored);
                return false;
            }
        }
    }
    const unsigned char stamp[] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d};
    SqlValue blob;
    blob.kind = SqlKind::Blob;
    blob.blob.assign(stamp, stamp + sizeof(stamp));
    const bool notes =
        run_row(db, "INSERT INTO annotations(id, book_id, body, created) VALUES (?,?,?,?)",
                {int_value(1), int_value(1), text_value("码头的风比正文里写的更大。"), text_value("2024-03-02")},
                error) &&
        run_row(db, "INSERT INTO annotations(id, book_id, body, created) VALUES (?,?,?,?)",
                {int_value(2), int_value(1), text_value(nullptr), text_value("2024-06-18")}, error) &&
        run_row(db, "INSERT INTO annotations(id, book_id, body, created) VALUES (?,?,?,?)",
                {int_value(3), int_value(4), text_value(kNote), text_value("2025-01-09")}, error) &&
        run_row(db, "INSERT INTO annotations(id, book_id, body, created) VALUES (?,?,?,?)",
                {int_value(4), int_value(12), text_value(""), text_value("2023-11-01")}, error) &&
        run_row(db, "INSERT INTO stamps(id, book_id, image) VALUES (?,?,?)",
                {int_value(1), int_value(3), blob}, error);
    if (!notes || !exec_sql(db, "COMMIT", error)) {
        std::string ignored;
        exec_sql(db, "ROLLBACK", ignored);
        return false;
    }
    return true;
}

}  // namespace zdb
