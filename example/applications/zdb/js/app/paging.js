import { readPage } from "../sql/browse.js";
import { explainSqlError } from "../sql/engine.js";
import { emptyGrid } from "./defaults.js";
import { replaceGrid } from "./refresh.js";

export function bindPaging(store) {
    return {
        sort(column) {
            const grid = store.get().grid;
            if (grid.order === column && grid.dir === "asc") load(store, { order: column, dir: "desc", offset: 0 });
            else if (grid.order === column && grid.dir === "desc") load(store, { order: null, dir: "asc", offset: 0 });
            else load(store, { order: column, dir: "asc", offset: 0 });
        },
        page(which) {
            const grid = store.get().grid;
            const offset = which === "next" ? grid.offset + grid.limit : Math.max(0, grid.offset - grid.limit);
            load(store, { offset });
        },
        limit(limit) {
            load(store, { limit, offset: 0 });
        },
    };
}

function load(store, changes) {
    const state = store.get();
    if (!state.db || !state.selected) return;
    const spec = {
        name: state.selected.name,
        limit: changes.limit ?? state.grid.limit,
        offset: changes.offset ?? state.grid.offset,
        order: Object.prototype.hasOwnProperty.call(changes, "order") ? changes.order : state.grid.order,
        dir: changes.dir ?? state.grid.dir,
    };
    try {
        replaceGrid(store, readPage(state.db, spec));
    } catch (error) {
        replaceGrid(store, {
            ...emptyGrid(),
            limit: spec.limit,
            offset: spec.offset,
            order: spec.order,
            dir: spec.dir,
            browsable: true,
            error: explainSqlError(error),
        });
    }
}
