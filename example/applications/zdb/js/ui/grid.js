import { formatCount, inspect } from "../format/display.js";
import { watch } from "../app/watch.js";
import { clear, el } from "./dom.js";
import { buildSheet } from "./table.js";

const KIND = { table: "表", view: "视图", index: "索引", trigger: "触发器" };

export function mountGrid(root, store, handlers) {
    const head = root.querySelector("[data-head]");
    const scroll = root.querySelector("[data-scroll]");
    const pager = root.querySelector("[data-pager]");
    const detail = root.querySelector("[data-detail]");
    const detailLabel = root.querySelector("[data-detail-label]");
    const detailValue = root.querySelector("[data-detail-value]");
    const cells = new WeakMap();
    let picked = null;

    root.querySelector("[data-detail-close]").addEventListener("click", hideDetail);
    scroll.addEventListener("click", (event) => {
        const sort = event.target.closest("[data-sort]");
        if (sort && scroll.contains(sort)) {
            handlers.sort(sort.getAttribute("data-sort"));
            return;
        }
        const cell = event.target.closest("td");
        if (!cell || !scroll.contains(cell)) return;
        const entry = cells.get(cell);
        if (!entry) return;
        if (picked) picked.classList.remove("is-picked");
        picked = cell;
        cell.classList.add("is-picked");
        detailLabel.textContent = entry.column;
        detailValue.textContent = inspect(entry.value);
        detail.hidden = false;
    });
    root.addEventListener("click", (event) => {
        if (event.target.closest("[data-goto='structure']")) handlers.showStructure();
    });
    // The page keeps #page-prev / #page-next / #limit-* (the same ids the native session toggles).
    // Inline onclick="on_limit" is a name the engine calls; a browser does not invoke it, so the
    // click listener is what actually changes the page there.
    pager.addEventListener("click", (event) => {
        const button = event.target.closest("button");
        if (!button || button.disabled || !pager.contains(button)) return;
        if (button.id === "page-prev") handlers.page("prev");
        else if (button.id === "page-next") handlers.page("next");
        else if (button.id.startsWith("limit-")) handlers.limit(Number(button.value));
    });

    watch(store, (state) => ({
        db: state.db,
        selected: state.selected,
        grid: state.grid,
    }), (state) => {
        hideDetail();
        clear(head);
        clear(scroll);
        if (!state.db || !state.selected) {
            paintPager(null);
            scroll.append(el("p", { class: "empty-note pad", text: "从左侧选择一张表或视图。" }));
            return;
        }
        head.append(title(state.selected, state.grid));
        if (!state.grid.browsable) {
            paintPager(null);
            scroll.append(el("p", { class: "empty-note pad", text: "这个对象没有数据行。" }));
            scroll.append(el("p", { class: "pad" }, [
                el("button", { type: "button", class: "btn", "data-goto": "structure", text: "查看结构" }),
            ]));
            return;
        }
        if (state.grid.error) {
            paintPager(null);
            scroll.append(el("p", { class: "empty-note pad", text: state.grid.error }));
            return;
        }
        const sheet = buildSheet(state.grid.columns, state.grid.rows, {
            caption: state.selected.name,
            rowOffset: state.grid.offset,
            sort: { order: state.grid.order, dir: state.grid.dir },
            header: (column) => sortButton(column, state.grid),
            onCell: (cell, value, column) => cells.set(cell, { value, column }),
        });
        if (sheet) scroll.append(sheet);
        else scroll.append(el("p", { class: "empty-note pad", text: "没有列。" }));
        paintPager(state.grid);
    });

    function paintPager(grid) {
        const show = !!grid;
        pager.hidden = !show;
        const range = document.getElementById("page-range");
        if (range) range.textContent = show ? rangeText(grid) : "";
        const prev = document.getElementById("page-prev");
        const next = document.getElementById("page-next");
        if (prev) prev.disabled = !show || grid.offset <= 0;
        if (next) next.disabled = !show || grid.offset + grid.limit >= grid.total;
        for (const size of [25, 50, 100, 200]) {
            const button = document.getElementById("limit-" + size);
            if (!button) continue;
            button.classList.toggle("is-on", show && grid.limit === size);
            button.disabled = !show;
        }
    }

    function hideDetail() {
        detail.hidden = true;
        detailLabel.textContent = "";
        detailValue.textContent = "";
        if (picked) picked.classList.remove("is-picked");
        picked = null;
    }
}

function title(selected, grid) {
    const row = el("div", { class: "grid-title" }, [
        el("h2", { text: selected.name }),
        el("span", { class: "pill", text: KIND[selected.type] || selected.type }),
    ]);
    if (grid.browsable && !grid.error) row.append(el("span", { class: "empty-note", text: `${formatCount(grid.total)} 行` }));
    return row;
}

function sortButton(column, grid) {
    const active = grid.order === column;
    const button = el("button", {
        type: "button",
        class: active ? "sort is-active" : "sort",
        "data-sort": column,
        title: "按此列排序",
    }, [el("span", { text: column })]);
    if (active) {
        button.append(el("span", {
            class: "sort-mark",
            "aria-hidden": "true",
            text: grid.dir === "desc" ? "↓" : "↑",
        }));
    }
    return button;
}

function rangeText(grid) {
    if (!grid.total) return "0 行";
    const start = grid.offset + 1;
    const end = grid.offset + grid.rows.length;
    return `${formatCount(start)}–${formatCount(end)} / ${formatCount(grid.total)} 行`;
}


