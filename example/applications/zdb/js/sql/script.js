/* Run a user script statement by statement and keep earlier results if a later one fails. */

import { explainSqlError } from "./engine.js";
import { normalizeValue } from "./rows.js";

const STATEMENT_CAP = 40;
const ROW_CAP = 500;

export function runScript(db, sql) {
    const text = String(sql ?? "");
    if (!text.trim()) return [{ ok: false, error: "请输入 SQL。" }];

    let iter;
    try {
        iter = db.iterateStatements(text);
    } catch (error) {
        return [{ ok: false, error: explainSqlError(error) }];
    }

    const results = [];
    let executed = 0;
    let capped = false;
    for (;;) {
        let step;
        try {
            step = iter.next();
        } catch (error) {
            results.push({ ok: false, error: explainSqlError(error) });
            break;
        }
        if (step.done) break;
        const stmt = step.value;
        if (executed >= STATEMENT_CAP) {
            // The iterator finalizes this statement when the next one is prepared.
            capped = true;
            continue;
        }
        results.push(consume(db, stmt));
        executed += 1;
    }
    if (capped) results.push({ ok: false, error: `语句超过 ${STATEMENT_CAP} 条，多出来的没有执行。` });
    if (!results.length) results.push({ ok: false, error: "没有可执行的语句。" });
    return results;
}

function consume(db, stmt) {
    const started = performance.now();
    try {
        const columns = stmt.getColumnNames();
        const rows = [];
        let truncated = false;
        if (columns.length === 0) {
            stmt.step();
        } else {
            while (stmt.step()) {
                if (rows.length >= ROW_CAP) {
                    truncated = true;
                    break;
                }
                rows.push(stmt.get(null, { useBigInt: true }).map(normalizeValue));
            }
        }
        return {
            ok: true,
            sql: safeSql(stmt),
            columns,
            rows,
            truncated,
            rowsModified: columns.length === 0 ? db.getRowsModified() : 0,
            ms: performance.now() - started,
        };
    } catch (error) {
        return { ok: false, error: explainSqlError(error), sql: safeSql(stmt) };
    }
}

function safeSql(stmt) {
    try {
        return stmt.getSQL() || "";
    } catch {
        return "";
    }
}

