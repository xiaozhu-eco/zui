import { runScript } from "../sql/script.js";
import { refreshDatabase } from "./refresh.js";

export function bindQuery(store) {
    const input = document.getElementById("sql-input");
    document.getElementById("run-sql").addEventListener("click", () => run(store, input));
    document.getElementById("clear-sql").addEventListener("click", () => {
        store.update((state) => {
            state.queryResults = [];
            state.notice = "已清空查询结果。";
            state.noticeError = false;
        });
    });
    document.addEventListener("keydown", (event) => {
        if (event.key !== "Enter" || !(event.ctrlKey || event.metaKey)) return;
        const pane = document.querySelector('[data-pane="query"]');
        if (!pane || pane.hidden) return;
        event.preventDefault();
        run(store, input);
    });
}

function run(store, input) {
    const db = store.get().db;
    if (!db) return;
    const results = runScript(db, input.value);
    const changed = results.some((result) => result.ok && result.columns && result.columns.length === 0);
    store.update((state) => {
        state.queryResults = results;
        state.mode = "query";
        state.notice = summarize(results);
        state.noticeError = results.some((result) => !result.ok);
    });
    if (changed) refreshDatabase(store);
}

function summarize(results) {
    const failed = results.filter((result) => !result.ok).length;
    const ok = results.length - failed;
    if (failed && ok) return `执行了 ${ok} 条语句，${failed} 条失败。`;
    if (failed) return "查询没有完成。";
    if (results.length === 1 && results[0].columns && results[0].columns.length) {
        const extra = results[0].truncated ? "，结果已截断" : "";
        return `返回 ${results[0].rows.length} 行${extra}。`;
    }
    return `执行了 ${results.length} 条语句。`;
}
