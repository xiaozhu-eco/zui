/* Seed the 竹间书肆 sample. Inserts run after the schema so foreign keys can be checked. */

const SCHEMA = `
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
`;

const AUTHORS = [
    [1, "周晚宁", "江南"],
    [2, "宋小满", "闽东"],
    [3, "何泊", "湘西"],
    [4, "阮清和", "岭南"],
    [5, "裴照", "燕北"],
    [6, "唐米", "蜀中"],
];

const SHELVES = [
    [1, "A", "窗边新到"],
    [2, "B", "手记"],
    [3, "C", "图册"],
];

const BOOKS = [
    ["渡口手记", 1, 2018, 240, 4200],
    ["灯塔摘录", 2, 2016, 128, 2800],
    ["海岸潮汐", 3, 2021, 312, 5600],
    ["竹间菜谱", 4, 2014, 196, 3900],
    ["雨夜索引", 5, 2019, 88, 2200],
    ["南风信札", 6, 2011, 264, 4500],
    ["旧城地图", 1, null, 144, 3600],
    ["茶烟录", 2, 2008, 176, 3100],
    ["小岛文法", 3, 2020, 208, 4800],
    ["冬至来信", 4, 2017, 96, 1900],
    ["桥洞几何", 5, 2013, null, 2700],
    ["米缸账本", 6, 2006, 352, 5200],
    ["晚钟谱", 1, 2022, 120, 2600],
    ["山脊步行", 2, 2015, 288, 6100],
    ["纸上船", 3, 2009, 160, 2400],
    ["北窗杂抄", 4, 2012, 224, 4300],
    ["渔灯问答", 5, 2004, 304, 5800],
    ["槐花与盐", 6, 2023, 136, 3300],
    ["短途星图", 1, 2010, 192, 4700],
    ["厨房纬度", 2, 2018, 168, 3500],
    ["慢河", 3, 2007, 256, 4900],
    ["匣中目录", 4, 2016, 80, 1800],
    ["第三个月台", 5, 2024, 272, 6400],
    ["雾中邮局", 6, 2011, 184, 3700],
    ["青石算术", 1, 2005, 336, 5500],
    ["夜里的厨房", 2, 2019, 148, 3000],
    ["未寄出的票根", 3, 2014, 112, 2100],
    ["浅湾", 4, 2020, 400, 7200],
];

const NOTE = "雨下得太久之后，索引里的页码会粘在一起。周晚宁把灯塔那一章折了角，又在空白处写：潮水并不负责把人送回去。书页边缘沾着盐，摸上去像细砂。店主把这一条标成批注，是因为正文里没有写到厨房停电的那个晚上，而停电才是他们决定把书从岛上运走的原因。后来的版本删掉了这段，只留了一个脚注编号，编号指向的那一页是空白的。空白页上仍能看出曾经贴过一张潮汐表，表的角落写着「不要按页码找，按风向找」。再往后是一串到港时间，每条都不超过四个字，连起来却比正文更长：寅初、卯正、雾、停、再开、未时、西风、空仓、有人来接、没人来接。";

export const SAMPLE_NAME = "竹间书肆.sqlite";
export const SAMPLE_TABLE = "books";

export function seedBookstore(db) {
    db.run("BEGIN");
    try {
        db.run(SCHEMA);
        insert(db, "INSERT INTO authors(id, name, region) VALUES (?,?,?)", AUTHORS);
        insert(db, "INSERT INTO shelves(id, code, title) VALUES (?,?,?)", SHELVES);
        insert(db, "INSERT INTO books(id, title, author_id, published, pages, price_cents) VALUES (?,?,?,?,?,?)", bookRows());
        insert(db, "INSERT INTO copies(shelf_id, book_id, qty) VALUES (?,?,?)", copyRows());
        insert(db, "INSERT INTO annotations(id, book_id, body, created) VALUES (?,?,?,?)", noteRows());
        insert(db, "INSERT INTO stamps(id, book_id, image) VALUES (?,?,?)", stampRows());
        db.run("COMMIT");
    } catch (error) {
        try {
            db.run("ROLLBACK");
        } catch {
            /* The transaction is already closed. */
        }
        throw error;
    }
}

function bookRows() {
    return BOOKS.map((book, index) => [index + 1, book[0], book[1], book[2], book[3], book[4]]);
}

function copyRows() {
    const rows = [];
    for (let id = 1; id <= BOOKS.length; id++) {
        const shelf = ((id - 1) % SHELVES.length) + 1;
        rows.push([shelf, id, (id % 5) + 1]);
        if (id <= 8) rows.push([(shelf % SHELVES.length) + 1, id, 1]);
    }
    return rows;
}

function noteRows() {
    return [
        [1, 1, "码头的风比正文里写的更大。", "2024-03-02"],
        [2, 1, null, "2024-06-18"],
        [3, 4, NOTE, "2025-01-09"],
        [4, 12, "", "2023-11-01"],
    ];
}

function stampRows() {
    return [[
        1,
        3,
        new Uint8Array([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d]),
    ]];
}

function insert(db, sql, rows) {
    const stmt = db.prepare(sql);
    try {
        for (const row of rows) stmt.run(row);
    } finally {
        stmt.free();
    }
}
