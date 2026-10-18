/* Prepared-statement reads. Identifiers are quoted; values stay bound. */

const MIN_SAFE = BigInt(Number.MIN_SAFE_INTEGER);
const MAX_SAFE = BigInt(Number.MAX_SAFE_INTEGER);

export function quoteIdent(name) {
    return `"${String(name).replaceAll('"', '""')}"`;
}

export function normalizeValue(value) {
    if (typeof value === "bigint" && value >= MIN_SAFE && value <= MAX_SAFE) return Number(value);
    return value;
}

export function queryTable(db, sql, params) {
    const stmt = db.prepare(sql);
    try {
        if (params && params.length) stmt.bind(params);
        const columns = stmt.getColumnNames();
        const rows = [];
        while (stmt.step()) {
            rows.push(stmt.get(null, { useBigInt: true }).map(normalizeValue));
        }
        return { columns, rows };
    } finally {
        stmt.free();
    }
}

export function queryObjects(db, sql, params) {
    const { columns, rows } = queryTable(db, sql, params);
    return rows.map((values) => {
        const object = {};
        for (let i = 0; i < columns.length; i++) object[columns[i]] = values[i];
        return object;
    });
}
