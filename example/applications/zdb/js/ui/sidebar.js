import { formatCount } from "../format/display.js";
import { watch } from "../app/watch.js";
import { clear, el } from "./dom.js";

const GROUPS = [
    { type: "table", label: "表", badge: "表" },
    { type: "view", label: "视图", badge: "视" },
    { type: "index", label: "索引", badge: "索" },
    { type: "trigger", label: "触发器", badge: "触" },
];

export function mountSidebar(store, handlers) {
    const search = document.querySelector("[data-search]");
    const list = document.querySelector("[data-list]");
    let filter = "";
    let seen = null;

    search.addEventListener("input", () => {
        filter = search.value;
        paint(store.get());
    });
    list.addEventListener("click", (event) => {
        const button = event.target.closest(".obj");
        if (!button || !list.contains(button)) return;
        const type = button.getAttribute("data-type");
        const name = button.getAttribute("data-name");
        const object = store.get().objects.find((item) => item.type === type && item.name === name);
        if (object) handlers.select(object);
    });

    watch(store, (state) => ({
        db: state.db,
        objects: state.objects,
        selected: state.selected,
        catalogError: state.catalogError,
    }), (state) => {
        if (state.db !== seen) {
            seen = state.db;
            filter = "";
            search.value = "";
        }
        paint(state);
    });

    function paint(state) {
        clear(list);
        if (!state.db) {
            list.append(el("p", { class: "empty-note side-note", text: "打开数据库后，对象会列在这里。" }));
            return;
        }
        if (state.catalogError) list.append(el("p", { class: "empty-note side-note", text: state.catalogError }));
        const needle = filter.trim().toLocaleLowerCase("zh-CN");
        const matched = state.objects.filter((object) => {
            if (!needle) return true;
            const name = object.name.toLocaleLowerCase("zh-CN");
            const parent = (object.tblName || "").toLocaleLowerCase("zh-CN");
            return name.includes(needle) || parent.includes(needle);
        });
        const known = new Set(GROUPS.map((group) => group.type));
        let shown = 0;
        for (const group of GROUPS) {
            const items = matched.filter((object) => object.type === group.type);
            if (!items.length) continue;
            shown += items.length;
            list.append(renderGroup(group, items, state.selected));
        }
        const other = matched.filter((object) => !known.has(object.type));
        if (other.length) {
            shown += other.length;
            list.append(renderGroup({ type: "other", label: "其他", badge: "其" }, other, state.selected));
        }
        if (!shown) {
            const text = needle ? "没有匹配的对象。" : "这个数据库里没有可显示的对象。";
            list.append(el("p", { class: "empty-note side-note", text }));
        }
    }
}

function renderGroup(group, items, selected) {
    const wrap = el("section", { class: "group" }, [
        el("h2", { class: "group-label", text: `${group.label} ${items.length}` }),
    ]);
    for (const item of items) {
        const on = selected && selected.type === item.type && selected.name === item.name;
        const button = el("button", {
            type: "button",
            class: on ? "obj is-selected" : "obj",
            "data-type": item.type,
            "data-name": item.name,
            title: item.name,
            "aria-current": on ? "true" : null,
        });
        button.append(el("span", { class: "kind", "aria-hidden": "true", text: group.badge }));
        button.append(el("span", { class: "obj-name", text: item.name }));
        if (item.type !== "table" && item.type !== "view" && item.tblName) {
            button.append(el("span", { class: "obj-meta", text: item.tblName }));
        }
        if (typeof item.count === "number") {
            button.append(el("span", { class: "obj-count", text: formatCount(item.count) }));
        }
        wrap.append(button);
    }
    return wrap;
}
