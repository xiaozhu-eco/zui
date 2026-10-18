/* Read sqlite_master and PRAGMA descriptions. Internal sqlite_* objects stay out of the sidebar. */

import { queryObjects, quoteIdent } from "./rows.js";

const COUNT_LIMIT = 40;

export function readCatalog(db) {
    let objects = [];
    let error = "";
    try {
        objects = listObjects(db);
    } catch (err) {
        error = err && err.message ? err.message : String(err);
    }
    return {
        objects,
        version: quiet(() => versionOf(db), ""),
        bytes: quiet(() => byteSize(db), 0),
        error,
    };
}

export function loadStructure(db, object) {
    if (object.type === "table" || object.type === "view") return loadRelation(db, object);
    if (object.type === "index") return loadIndex(db, object);
    if (object.type === "trigger") return loadTrigger(object);
    return {
        kind: object.type,
        name: object.name,
        sql: object.sql || "",
        columns: [],
        foreignKeys: [],
        indexes: [],
        triggers: [],
    };
}

function listObjects(db) {
    const rows = queryObjects(db, `
        SELECT type, name, tbl_name AS tblName, sql
        FROM sqlite_master
        WHERE substr(name, 1, 7) <> 'sqlite_'
        ORDER BY CASE type
            WHEN 'table' THEN 0
            WHEN 'view' THEN 1
            WHEN 'index' THEN 2
            WHEN 'trigger' THEN 3
            ELSE 4
        END, rowid
    `);
    const countable = rows.filter((row) => row.type === "table" || row.type === "view").length <= COUNT_LIMIT;
    return rows.map((row) => ({
        type: row.type,
        name: row.name,
        tblName: row.tblName,
        sql: row.sql,
        count: countable && (row.type === "table" || row.type === "view") ? rowCount(db, row.name) : null,
    }));
}

function loadRelation(db, object) {
    const name = object.name;
    return {
        kind: object.type,
        name,
        sql: object.sql || "",
        columns: pragma(db, "table_info", name),
        foreignKeys: object.type === "table" ? pragma(db, "foreign_key_list", name) : [],
        indexes: object.type === "table" ? indexesOf(db, name) : [],
        triggers: object.type === "table" ? triggersOf(db, name) : [],
    };
}

function loadIndex(db, object) {
    const siblings = object.tblName ? pragma(db, "index_list", object.tblName) : [];
    const self = siblings.find((item) => item.name === object.name);
    return {
        kind: "index",
        name: object.name,
        table: object.tblName || "",
        sql: object.sql || "",
        automatic: !object.sql,
        unique: self ? Boolean(self.unique) : false,
        origin: self ? self.origin : "",
        partial: self ? Boolean(self.partial) : false,
        columns: pragma(db, "index_info", object.name),
        foreignKeys: [],
        indexes: [],
        triggers: [],
    };
}

function loadTrigger(object) {
    return {
        kind: "trigger",
        name: object.name,
        table: object.tblName || "",
        sql: object.sql || "",
        columns: [],
        foreignKeys: [],
        indexes: [],
        triggers: [],
    };
}

function indexesOf(db, table) {
    return pragma(db, "index_list", table).map((item) => {
        const columns = pragma(db, "index_info", item.name).map((column) => column.name).filter(Boolean);
        const meta = queryObjects(db, "SELECT sql FROM sqlite_master WHERE type = 'index' AND name = ?", [item.name]);
        const sql = meta.length ? meta[0].sql : null;
        return {
            name: item.name,
            unique: Boolean(item.unique),
            origin: item.origin,
            partial: Boolean(item.partial),
            columns,
            sql,
            automatic: !sql,
        };
    });
}

function triggersOf(db, table) {
    return queryObjects(
        db,
        "SELECT name, sql FROM sqlite_master WHERE type = 'trigger' AND tbl_name = ? ORDER BY rowid",
        [table],
    );
}

function pragma(db, name, argument) {
    return queryObjects(db, `PRAGMA ${name}(${quoteIdent(argument)})`);
}

function rowCount(db, name) {
    try {
        const rows = queryObjects(db, `SELECT COUNT(*) AS n FROM ${quoteIdent(name)}`);
        return rows.length ? Number(rows[0].n) : null;
    } catch {
        return null;
    }
}

function versionOf(db) {
    const rows = queryObjects(db, "SELECT sqlite_version() AS version");
    return rows.length ? String(rows[0].version) : "";
}

function byteSize(db) {
    const size = queryObjects(db, "PRAGMA page_size");
    const count = queryObjects(db, "PRAGMA page_count");
    if (!size.length || !count.length) return 0;
    return Number(size[0].page_size) * Number(count[0].page_count);
}

function quiet(read, fallback) {
    try {
        return read();
    } catch {
        return fallback;
    }
}
