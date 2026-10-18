import {
    closeDatabase,
    explainSqlError,
    exportDatabase,
    looksLikeSqlite,
    openDatabase,
} from "../sql/engine.js";
import { SAMPLE_NAME, SAMPLE_TABLE, seedBookstore } from "../sql/sample.js";
import { showDatabase } from "./refresh.js";

export function bindOpen(store) {
    const input = document.getElementById("file-open");
    const app = document.getElementById("app");
    let ticket = 0;
    const take = () => ++ticket;
    const current = () => ticket;

    input.addEventListener("change", () => {
        const file = input.files && input.files[0];
        input.value = "";
        if (file) void openFile(store, file, take, current);
    });
    window.on_open = () => input.click();
    window.on_sample = () => openSample(store, take, current);
    window.on_export = () => exportCurrent(store);
    // The shared markup uses onclick="on_open" so zui can bind that name.
    // A browser treats that attribute as an expression, not a call, so the
    // same actions are also listened for here.
    listen("on_open", window.on_open);
    listen("on_sample", window.on_sample);
    listen("on_export", window.on_export);

    window.addEventListener("dragenter", (event) => {
        if (!carriesFile(event)) return;
        event.preventDefault();
        app.classList.add("is-dragover");
    });
    window.addEventListener("dragover", (event) => {
        if (!carriesFile(event)) return;
        event.preventDefault();
    });
    window.addEventListener("dragleave", (event) => {
        const next = event.relatedTarget;
        if (next && app.contains(next)) return;
        app.classList.remove("is-dragover");
    });
    window.addEventListener("drop", (event) => {
        if (!carriesFile(event)) return;
        event.preventDefault();
        app.classList.remove("is-dragover");
        const file = event.dataTransfer.files && event.dataTransfer.files[0];
        if (file) void openFile(store, file, take, current);
    });
}

async function openFile(store, file, take, current) {
    const mine = take();
    const engine = store.get().engine;
    if (!engine) {
        fail(store, "SQLite 还没加载完。");
        return;
    }
    let bytes;
    try {
        bytes = new Uint8Array(await file.arrayBuffer());
    } catch {
        if (mine === current()) fail(store, "读取文件失败。");
        return;
    }
    if (mine !== current()) return;
    if (!looksLikeSqlite(bytes)) {
        fail(store, "这个文件不是 SQLite 数据库。");
        return;
    }
    let db;
    try {
        db = openDatabase(engine, bytes);
    } catch (error) {
        fail(store, explainSqlError(error));
        return;
    }
    if (mine !== current()) {
        closeDatabase(db);
        return;
    }
    swap(store, db, file.name);
}

function openSample(store, take, current) {
    const mine = take();
    const engine = store.get().engine;
    if (!engine) {
        fail(store, "SQLite 还没加载完。");
        return;
    }
    let db;
    try {
        db = openDatabase(engine);
        seedBookstore(db);
    } catch (error) {
        closeDatabase(db);
        fail(store, explainSqlError(error));
        return;
    }
    if (mine !== current()) {
        closeDatabase(db);
        return;
    }
    swap(store, db, SAMPLE_NAME, SAMPLE_TABLE);
}

function swap(store, db, fileName, preferName) {
    const previous = store.get().db;
    try {
        showDatabase(store, db, fileName, preferName);
    } catch (error) {
        closeDatabase(db);
        fail(store, explainSqlError(error));
        return;
    }
    if (previous && previous !== db) closeDatabase(previous);
}

function exportCurrent(store) {
    const { db, fileName } = store.get();
    if (!db) return;
    let bytes;
    try {
        bytes = exportDatabase(db);
    } catch (error) {
        fail(store, explainSqlError(error));
        return;
    }
    const blob = new Blob([bytes], { type: "application/x-sqlite3" });
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    link.href = url;
    link.download = downloadName(fileName);
    document.body.append(link);
    link.click();
    link.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
}

function downloadName(fileName) {
    const name = fileName || "database.sqlite";
    if (/\.(sqlite3?|db3?)$/i.test(name)) return name;
    return `${name}.sqlite`;
}

function listen(name, handler) {
    for (const node of document.querySelectorAll(`[onclick="${name}"]`)) {
        node.addEventListener("click", handler);
    }
}

function carriesFile(event) {
    const types = event.dataTransfer && event.dataTransfer.types;
    return Boolean(types && [...types].includes("Files"));
}

function fail(store, message) {
    store.update((state) => {
        state.notice = message;
        state.noticeError = true;
    });
}
