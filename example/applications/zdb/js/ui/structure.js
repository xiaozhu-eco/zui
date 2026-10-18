import { watch } from "../app/watch.js";
import { clear, el } from "./dom.js";
import { buildSheet } from "./table.js";

const KIND = { table: "表", view: "视图", index: "索引", trigger: "触发器" };
const ORIGIN = { pk: "主键", u: "唯一约束", c: "CREATE INDEX" };

export function mountStructure(root, store, handlers) {
    root.addEventListener("click", (event) => {
        const button = event.target.closest("[data-jump]");
        if (!button || !root.contains(button)) return;
        handlers.openTable(button.getAttribute("data-jump"));
    });
    watch(store, (state) => ({
        db: state.db,
        selected: state.selected,
        structure: state.structure,
    }), (state) => {
        clear(root);
        if (!state.db || !state.selected) {
            root.append(el("p", { class: "empty-note pad", text: "从左侧选择一个对象。" }));
            return;
        }
        const structure = state.structure;
        if (!structure) return;
        root.append(heading(state.selected));
        if (structure.error) {
            root.append(el("p", { class: "empty-note", text: structure.error }));
            return;
        }
        if (structure.table && structure.kind !== "table" && structure.kind !== "view") {
            root.append(el("div", { class: "jump-row" }, [
                el("button", {
                    type: "button",
                    class: "btn btn-tiny",
                    "data-jump": structure.table,
                    text: `打开 ${structure.table}`,
                }),
            ]));
        }
        if (structure.kind === "index") root.append(indexFacts(structure));
        appendColumns(root, structure);
        appendForeignKeys(root, structure);
        appendIndexes(root, structure);
        appendTriggers(root, structure);
        appendSql(root, structure);
    });
}

function heading(selected) {
    return el("div", { class: "structure-title" }, [
        el("h2", { text: selected.name }),
        el("span", { class: "pill", text: KIND[selected.type] || selected.type }),
    ]);
}

function indexFacts(structure) {
    const bits = [];
    if (structure.origin) bits.push(ORIGIN[structure.origin] || structure.origin);
    bits.push(structure.unique ? "唯一" : "非唯一");
    if (structure.partial) bits.push("部分索引");
    if (structure.automatic) bits.push("自动创建");
    return el("p", { class: "empty-note", text: bits.join(" · ") });
}

function appendColumns(root, structure) {
    const columns = structure.columns || [];
    if (!columns.length) return;
    const rows = structure.kind === "index"
        ? columns.map((column) => [column.seqno, column.name || "—", column.cid])
        : columns.map((column) => [
            column.cid,
            column.name,
            column.type || "—",
            column.notnull ? "是" : "否",
            column.dflt_value == null ? "—" : String(column.dflt_value),
            column.pk ? String(column.pk) : "否",
        ]);
    const headers = structure.kind === "index"
        ? ["序号", "列", "表列序号"]
        : ["#", "名称", "类型", "非空", "默认值", "主键"];
    root.append(block("列", buildSheet(headers, rows, { caption: `${structure.name} 的列` })));
}

function appendForeignKeys(root, structure) {
    const keys = structure.foreignKeys || [];
    if (!keys.length) return;
    const rows = keys.map((key) => [
        key.from,
        key.table,
        key.to,
        key.on_update || "—",
        key.on_delete || "—",
    ]);
    const sheet = buildSheet(["列", "引用表", "引用列", "更新", "删除"], rows, { caption: `${structure.name} 的外键` });
    const jumps = [...new Set(keys.map((key) => key.table).filter(Boolean))];
    const links = el("div", { class: "jump-row" });
    for (const name of jumps) {
        links.append(el("button", { type: "button", class: "btn btn-tiny", "data-jump": name, text: `打开 ${name}` }));
    }
    root.append(block("外键", el("div", {}, [sheet, links])));
}

function appendIndexes(root, structure) {
    const indexes = structure.indexes || [];
    if (!indexes.length) return;
    const rows = indexes.map((index) => [
        index.name,
        (index.columns || []).join(", ") || "—",
        ORIGIN[index.origin] || index.origin || "—",
        index.unique ? "是" : "否",
        index.partial ? "是" : "否",
    ]);
    root.append(block("索引", buildSheet(["名称", "列", "来源", "唯一", "部分"], rows, { caption: `${structure.name} 的索引` })));
}

function appendTriggers(root, structure) {
    const triggers = structure.triggers || [];
    if (!triggers.length) return;
    const wrap = el("div");
    for (const trigger of triggers) {
        wrap.append(el("p", { class: "empty-note", text: trigger.name }));
        wrap.append(el("pre", { class: "sql-block scroll-thin", text: trigger.sql || "" }));
    }
    root.append(block("触发器", wrap));
}

function appendSql(root, structure) {
    if (structure.sql) {
        root.append(block("SQL", el("pre", { class: "sql-block scroll-thin", text: structure.sql })));
        return;
    }
    if (structure.automatic) {
        root.append(el("p", { class: "empty-note", text: "这是 SQLite 自动创建的索引，没有单独的 CREATE INDEX 语句。" }));
    }
}

function block(title, node) {
    return el("section", { class: "block" }, [el("h3", { text: title }), node]);
}
