import { readPage } from "../sql/browse.js";
import { loadStructure, readCatalog } from "../sql/catalog.js";
import { explainSqlError } from "../sql/engine.js";
import { emptyGrid } from "./defaults.js";

export function showDatabase(store, db, fileName, preferName) {
    const listed = readCatalog(db);
    const selected = pick(listed.objects, preferName);
    store.update((state) => {
        state.db = db;
        state.fileName = fileName;
        state.version = listed.version;
        state.bytes = listed.bytes;
        state.objects = listed.objects;
        state.catalogError = listed.error;
        state.selected = selected;
        state.structure = structureOf(db, selected);
        state.grid = pageOf(db, selected, emptyGrid());
        state.mode = selected && !browsable(selected) ? "structure" : "data";
        state.queryResults = [];
        state.notice = `已打开 ${fileName}`;
        state.noticeError = false;
    });
}

export function refreshDatabase(store) {
    const current = store.get();
    if (!current.db) return;
    const listed = readCatalog(current.db);
    const selected = current.selected
        ? listed.objects.find((item) => item.type === current.selected.type && item.name === current.selected.name) || null
        : null;
    store.update((state) => {
        state.version = listed.version;
        state.bytes = listed.bytes;
        state.objects = listed.objects;
        state.catalogError = listed.error;
        state.selected = selected;
        state.structure = structureOf(current.db, selected);
        state.grid = pageOf(current.db, selected, current.grid);
    });
}

export function selectObject(store, object) {
    const current = store.get();
    if (!current.db || !object) return;
    store.update((state) => {
        state.selected = object;
        state.structure = structureOf(current.db, object);
        state.grid = pageOf(current.db, object, { ...emptyGrid(), limit: current.grid.limit });
        if (!browsable(object) && state.mode === "data") state.mode = "structure";
    });
}

export function selectByName(store, type, name) {
    const object = store.get().objects.find((item) => item.type === type && item.name === name);
    if (object) selectObject(store, object);
}

export function replaceGrid(store, grid) {
    store.update((state) => {
        state.grid = grid;
    });
}

function pick(objects, preferName) {
    if (preferName) {
        const preferred = objects.find((item) => item.type === "table" && item.name === preferName);
        if (preferred) return preferred;
    }
    return objects.find((item) => item.type === "table")
        || objects.find((item) => item.type === "view")
        || objects[0]
        || null;
}

function browsable(object) {
    return object && (object.type === "table" || object.type === "view");
}

function structureOf(db, object) {
    if (!db || !object) return null;
    try {
        return loadStructure(db, object);
    } catch (error) {
        return { error: explainSqlError(error), name: object.name, kind: object.type };
    }
}

function pageOf(db, object, grid) {
    const limit = grid.limit || 25;
    if (!browsable(object)) return { ...emptyGrid(), limit, browsable: false };
    try {
        return readPage(db, {
            name: object.name,
            limit,
            offset: grid.offset || 0,
            order: grid.order,
            dir: grid.dir,
        });
    } catch (error) {
        return {
            ...emptyGrid(),
            limit,
            offset: grid.offset || 0,
            order: grid.order,
            dir: grid.dir || "asc",
            browsable: true,
            error: explainSqlError(error),
        };
    }
}
