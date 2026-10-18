"""Build the zdb capability fixture database.

`zdb_capability.db` is the file a zdb test opens when it would otherwise raise a host file picker.
It is designed from zdb's own capabilities (see `example/applications/zdb/cpp/sql/catalog.cpp` and
`ui/structure.cpp`) so the tests exercise each pane on real data:

  * `events_log`   - one very wide table (40 columns) with many rows: grid paging and wide-table
                     layout are the two heavy paths the app has to keep inside their bounds.
  * `notes`        - an ordinary table (few columns, few rows) with a foreign key, indexes and a
                     trigger, so the structure pane has columns, indexes, triggers and FK jumps.
  * `wide_view` /  - two views.
    `notes_by_category`
  * `dim_00..dim_59` - sixty extra tables so the object panel has to lay out a long list and so the
                     catalog's `kCountLimit=40` skips per-table COUNT(*) work (the panel perf path).
  * `events_big`   - 80-column table, used by the structure pane's own long column list.

Run:  python tests/fixtures/zdb/make_fixture.py
Then commit the regenerated `zdb_capability.db`. The file is deterministic (fixed RNG seed) so a
rerun does not churn the fixture.
"""

from __future__ import annotations

import random
import sqlite3
from pathlib import Path

HERE = Path(__file__).resolve().parent
DB_PATH = HERE / "zdb_capability.db"

# Row/column counts are the fixture's contract: a test that measures the grid needs "many rows", a
# test that measures the structure pane needs "many columns". Keep them named, not inlined.
EVENTS_ROWS = 20000
EVENTS_COLS = 40
EVENTS_BIG_ROWS = 400
EVENTS_BIG_COLS = 80
DIM_TABLES = 60
DIM_ROWS = 5


def q(name: str) -> str:
    return '"' + name.replace('"', '""') + '"'


def build() -> None:
    if DB_PATH.exists():
        DB_PATH.unlink()
    rng = random.Random(20261003)
    db = sqlite3.connect(DB_PATH)
    db.execute("PRAGMA foreign_keys = ON")

    # --- an ordinary table with a FK, indexes and a trigger -------------------------------
    db.execute("CREATE TABLE categories(id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE, color TEXT)")
    db.execute(
        "CREATE TABLE notes("
        " id INTEGER PRIMARY KEY,"
        " category_id INTEGER REFERENCES categories(id),"
        " title TEXT NOT NULL,"
        " body TEXT,"
        " pinned INTEGER NOT NULL DEFAULT 0,"
        " created_at TEXT,"
        " updated_at TEXT)"
    )
    db.execute("CREATE INDEX idx_notes_category ON notes(category_id)")
    db.execute("CREATE INDEX idx_notes_pinned_title ON notes(pinned, title)")
    db.execute("CREATE UNIQUE INDEX idx_notes_title_unique ON notes(title)")
    db.execute(
        "CREATE TRIGGER notes_touch AFTER UPDATE OF title ON notes "
        "BEGIN UPDATE notes SET updated_at = datetime('now') WHERE id = NEW.id; END"
    )
    db.execute("CREATE VIEW notes_recent AS SELECT id, title, created_at FROM notes ORDER BY created_at DESC")
    db.execute(
        "CREATE VIEW notes_by_category AS "
        "SELECT c.name AS category, COUNT(n.id) AS n FROM categories c "
        "LEFT JOIN notes n ON n.category_id = c.id GROUP BY c.id"
    )
    colors = ["clay", "dusk", "ink", "paper", "sand"]
    for i, name in enumerate(["工作", "阅读", "旅行", "账目", "杂记"]):
        db.execute("INSERT INTO categories VALUES(?,?,?)", (i + 1, name, colors[i]))
    for i in range(20):
        db.execute(
            "INSERT INTO notes VALUES(?,?,?,?,?,?,?)",
            (
                i + 1,
                (i % 5) + 1,
                f"笔记 {i + 1:02d}",
                "正文 " + "字" * (i % 7),
                1 if i % 3 == 0 else 0,
                f"2026-09-{(i % 28) + 1:02d}T08:00",
                f"2026-09-{(i % 28) + 1:02d}T09:30",
            ),
        )

    # --- a very wide table with many rows (grid paging + wide layout) ---------------------
    cols = ["id INTEGER PRIMARY KEY", "event_time TEXT", "level TEXT", "source TEXT"]
    for c in range(5, EVENTS_COLS + 1):
        if c % 5 == 0:
            cols.append(f"{q('col_%02d' % c)} REAL")
        elif c % 5 == 2:
            cols.append(f"{q('col_%02d' % c)} TEXT")
        else:
            cols.append(f"{q('col_%02d' % c)} INTEGER")
    db.execute("CREATE TABLE events_log(" + ",".join(cols) + ")")
    placeholders = ",".join("?" * EVENTS_COLS)
    levels = ["debug", "info", "warn", "error"]
    sources = ["paint", "layout", "css", "input", "text"]
    rows = []
    for i in range(1, EVENTS_ROWS + 1):
        row = [i, f"2026-10-{(i % 28) + 1:02d}T{(i % 24):02d}:{i % 60:02d}", levels[i % 4], sources[i % 5]]
        for c in range(5, EVENTS_COLS + 1):
            if c % 5 == 0:
                row.append(round(rng.uniform(0.0, 1000.0), 2))
            elif c % 5 == 2:
                row.append(f"值{i % 97}-{c}")
            else:
                row.append((i * c) % 100000)
        rows.append(row)
    db.executemany(f"INSERT INTO events_log VALUES({placeholders})", rows)
    db.execute("CREATE INDEX idx_events_level ON events_log(level)")
    db.execute("CREATE INDEX idx_events_time ON events_log(event_time)")
    db.execute("CREATE VIEW events_errors AS SELECT id, event_time, source FROM events_log WHERE level = 'error'")

    # --- an 80-column table for the structure pane's own long list ------------------------
    big_cols = ["id INTEGER PRIMARY KEY"] + [f"{q('field_%02d' % c)} TEXT" for c in range(1, EVENTS_BIG_COLS)]
    db.execute("CREATE TABLE events_big(" + ",".join(big_cols) + ")")
    db.executemany(
        f"INSERT INTO events_big VALUES({','.join('?' * EVENTS_BIG_COLS)})",
        [[i] + [f"v{i}-{c}" for c in range(1, EVENTS_BIG_COLS)] for i in range(1, EVENTS_BIG_ROWS + 1)],
    )

    # --- sixty small tables so the object panel is long and the count path is skipped ------
    for t in range(DIM_TABLES):
        name = f"dim_{t:02d}"
        db.execute(f"CREATE TABLE {name}(id INTEGER PRIMARY KEY, code TEXT NOT NULL, label TEXT)")
        db.executemany(
            f"INSERT INTO {name} VALUES(?,?,?)",
            [(i + 1, f"code-{t:02d}-{i}", f"维度 {t:02d} 项 {i}") for i in range(DIM_ROWS)],
        )
        db.execute(f"CREATE INDEX idx_{name}_code ON {name}(code)")
    for t in range(0, DIM_TABLES, 10):
        name = f"dim_{t:02d}"
        db.execute(
            f"CREATE TRIGGER trg_{name} AFTER INSERT ON {name} "
            f"BEGIN UPDATE {name} SET label = label WHERE id = NEW.id; END"
        )

    db.commit()
    db.execute("VACUUM")
    db.close()


if __name__ == "__main__":
    build()
    print(f"{DB_PATH} ({DB_PATH.stat().st_size} bytes)")
