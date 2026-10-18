/* Load sql.js and open or export a database kept entirely in wasm memory. */

const HEADER = [
    0x53, 0x51, 0x4c, 0x69, 0x74, 0x65, 0x20, 0x66,
    0x6f, 0x72, 0x6d, 0x61, 0x74, 0x20, 0x33, 0x00,
];

export function loadEngine() {
    const init = globalThis.initSqlJs;
    if (typeof init !== "function") return Promise.reject(new Error("未找到 sql.js。"));
    const wasm = new URL("../../vendor/sql-wasm.wasm", import.meta.url);
    return init({ locateFile: () => wasm.href });
}

export function explainLoadError(error) {
    const text = error && error.message ? error.message : String(error);
    if (/fetch|wasm|network|aborted|failed to/i.test(text)) {
        return "没能加载 sql-wasm.wasm。请在 example/applications/zdb 目录启动静态服务后，用 http:// 打开 index.html。直接双击文件时，浏览器会拦截 WebAssembly。";
    }
    return text;
}

export function explainSqlError(error) {
    const text = typeof error === "string" ? error : (error && error.message) || "未知错误";
    if (/not a database/i.test(text)) return "这个文件不是 SQLite 数据库。";
    if (text === "Nothing to prepare") return "没有可执行的语句。";
    if (text === "Database closed") return "数据库已经关闭。";
    return text;
}

export function looksLikeSqlite(bytes) {
    if (!bytes || bytes.length < HEADER.length) return false;
    for (let i = 0; i < HEADER.length; i++) {
        if (bytes[i] !== HEADER[i]) return false;
    }
    return true;
}

export function openDatabase(SQL, bytes) {
    const db = bytes ? new SQL.Database(bytes) : new SQL.Database();
    db.run("PRAGMA foreign_keys = ON");
    return db;
}

export function closeDatabase(db) {
    if (!db) return;
    try {
        db.close();
    } catch {
        /* The handle is already closed. */
    }
}

export function exportDatabase(db) {
    return db.export();
}
