/* One page of a table or view. Sort columns have to exist on that relation. */

import { queryObjects, queryTable, quoteIdent } from "./rows.js";

const LIMITS = [25, 50, 100, 200];

export function readPage(db, spec) {
    const limit = LIMITS.includes(spec.limit) ? spec.limit : 25;
    const ident = quoteIdent(spec.name);
    const counted = queryObjects(db, `SELECT COUNT(*) AS n FROM ${ident}`);
    const total = counted.length ? Number(counted[0].n) : 0;
    let offset = Number(spec.offset);
    if (!Number.isFinite(offset) || offset < 0) offset = 0;
    offset = Math.floor(offset);
    if (total === 0) offset = 0;
    else if (offset >= total) offset = Math.floor((total - 1) / limit) * limit;

    const order = resolveOrder(db, ident, spec.order, spec.dir);
    const data = queryTable(
        db,
        `SELECT * FROM ${ident}${order.sql} LIMIT ? OFFSET ?`,
        [limit, offset],
    );
    return {
        columns: data.columns,
        rows: data.rows,
        total,
        limit,
        offset,
        order: order.name,
        dir: order.dir,
        error: "",
        browsable: true,
    };
}

function resolveOrder(db, ident, requested, direction) {
    if (typeof requested !== "string" || requested === "") return { name: null, dir: "asc", sql: "" };
    const columns = queryObjects(db, `PRAGMA table_info(${ident})`);
    if (!columns.some((column) => column.name === requested)) return { name: null, dir: "asc", sql: "" };
    const dir = direction === "desc" ? "desc" : "asc";
    return {
        name: requested,
        dir,
        sql: ` ORDER BY ${quoteIdent(requested)} ${dir === "desc" ? "DESC" : "ASC"}`,
    };
}
