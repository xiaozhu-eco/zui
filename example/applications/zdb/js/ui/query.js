import { formatCount, formatMs } from "../format/display.js";
import { watch } from "../app/watch.js";
import { clear, el } from "./dom.js";
import { buildSheet } from "./table.js";

export function mountQuery(store) {
    const results = document.querySelector("[data-results]");
    watch(store, (state) => ({
        db: state.db,
        queryResults: state.queryResults,
    }), (state) => {
        clear(results);
        if (!state.queryResults.length) {
            results.append(el("p", { class: "empty-note", text: "执行后，结果会显示在这里。" }));
            return;
        }
        state.queryResults.forEach((result, index) => results.append(renderResult(result, index)));
        results.scrollTop = 0;
    });
}

function renderResult(result, index) {
    const card = el("article", { class: result.ok ? "result" : "result is-error" });
    if (!result.ok) {
        if (result.sql) card.append(el("p", { class: "result-sql", text: result.sql }));
        card.append(el("p", { class: "result-error", text: result.error || "执行失败。" }));
        return card;
    }
    const head = el("div", { class: "result-head" });
    head.append(el("span", { text: `语句 ${index + 1}` }));
    head.append(el("span", { text: resultMeta(result) }));
    card.append(head);
    if (result.sql) card.append(el("p", { class: "result-sql", text: result.sql }));
    if (result.columns.length) {
        const sheet = buildSheet(result.columns, result.rows, {
            caption: `语句 ${index + 1} 的结果`,
            rowOffset: 0,
        });
        if (sheet) card.append(el("div", { class: "sheet-wrap scroll-thin" }, [sheet]));
        if (!result.rows.length) card.append(el("p", { class: "result-note", text: "没有返回行。" }));
        if (result.truncated) card.append(el("p", { class: "result-note", text: `只显示前 ${formatCount(result.rows.length)} 行。` }));
    }
    return card;
}

function resultMeta(result) {
    const time = formatMs(result.ms);
    if (!result.columns.length) return `修改 ${formatCount(result.rowsModified)} 行 · ${time}`;
    return `${formatCount(result.rows.length)} 行 · ${time}`;
}
