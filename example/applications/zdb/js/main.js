import { installBridge } from "./bridge.js";
import { initialState } from "./app/defaults.js";
import { bindOpen } from "./app/open.js";
import { bindPaging } from "./app/paging.js";
import { bindQuery } from "./app/query-run.js";
import { selectByName, selectObject } from "./app/refresh.js";
import { createStore } from "./app/store.js";
import { explainLoadError, loadEngine } from "./sql/engine.js";
import { mountGrid } from "./ui/grid.js";
import { mountQuery } from "./ui/query.js";
import { mountShell } from "./ui/shell.js";
import { mountSidebar } from "./ui/sidebar.js";
import { mountStructure } from "./ui/structure.js";

installBridge();
const store = createStore(initialState());
const paging = bindPaging(store);

mountShell(store);
mountSidebar(store, {
    select(object) {
        selectObject(store, object);
    },
});
mountGrid(document.querySelector('[data-pane="data"]'), store, {
    ...paging,
    showStructure() {
        store.update((state) => {
            state.mode = "structure";
        });
    },
});
mountStructure(document.querySelector("[data-structure]"), store, {
    openTable(name) {
        selectByName(store, "table", name);
    },
});
mountQuery(store);
bindQuery(store);
bindOpen(store);

loadEngine()
    .then((engine) => {
        store.update((state) => {
            state.engine = engine;
            state.notice = "打开一个数据库，或载入示例库。";
            state.noticeError = false;
        });
        if (new URLSearchParams(location.search).get("sample") === "1") {
            document.querySelector("[data-sample]").click();
        }
    })
    .catch((error) => {
        store.update((state) => {
            state.engineError = explainLoadError(error);
            state.notice = "SQLite 引擎没有加载成功。";
            state.noticeError = true;
        });
    });
