import { formatCell } from "../format/display.js";
import { el } from "./dom.js";

export function buildSheet(columns, rows, hooks = {}) {
    if (!columns.length) return null;
    const table = el("table", { class: "sheet" });
    if (hooks.caption) table.append(el("caption", { class: "sr", text: hooks.caption }));
    table.append(buildHead(columns, hooks));
    const body = el("tbody");
    rows.forEach((row, index) => {
        const tr = el("tr");
        if (hooks.rowOffset != null) {
            tr.append(el("th", {
                class: "rownum",
                scope: "row",
                text: String(hooks.rowOffset + index + 1),
            }));
        }
        row.forEach((value, columnIndex) => {
            const formatted = formatCell(value);
            const td = el("td", {
                class: `cell cell-${formatted.kind}`,
                title: formatted.title || null,
                text: formatted.text,
            });
            hooks.onCell?.(td, value, columns[columnIndex]);
            tr.append(td);
        });
        body.append(tr);
    });
    table.append(body);
    return table;
}

function buildHead(columns, hooks) {
    const head = el("thead");
    const row = el("tr");
    if (hooks.rowOffset != null) row.append(el("th", { class: "rownum", scope: "col", text: "#" }));
    columns.forEach((column) => {
        const cell = el("th", { scope: "col" });
        if (hooks.sort) {
            const active = hooks.sort.order === column;
            cell.setAttribute("aria-sort", active ? (hooks.sort.dir === "desc" ? "descending" : "ascending") : "none");
        }
        cell.append(hooks.header ? hooks.header(column) : document.createTextNode(column));
        row.append(cell);
    });
    head.append(row);
    return head;
}
