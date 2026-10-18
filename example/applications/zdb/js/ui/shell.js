import { formatCount, formatSize } from "../format/display.js";
import { watch } from "../app/watch.js";

const KIND_LABELS = [
    ["table", "表"],
    ["view", "视图"],
    ["index", "索引"],
    ["trigger", "触发器"],
];

export function mountShell(store) {
    const fileName = document.getElementById("file-name");
    const status = document.getElementById("status");
    const statusText = document.getElementById("status-text");
    const statusMeta = document.getElementById("status-meta");
    const engineError = document.querySelector("[data-engine-error]");
    const panes = [...document.querySelectorAll("[data-pane]")];
    const modes = [...document.querySelectorAll("[data-mode]")];
    const fileInput = document.getElementById("file-open");

    for (const button of modes) {
        button.addEventListener("click", () => {
            const mode = button.dataset.mode;
            store.update((state) => {
                if (state.db) state.mode = mode;
            });
        });
    }

    watch(store, (state) => ({
        engine: state.engine,
        engineError: state.engineError,
        db: state.db,
        fileName: state.fileName,
        mode: state.mode,
        notice: state.notice,
        noticeError: state.noticeError,
        version: state.version,
        bytes: state.bytes,
        objects: state.objects,
    }), (state) => {
        const engineReady = Boolean(state.engine);
        const dbReady = Boolean(state.db);
        fileInput.disabled = !engineReady;
        for (const node of document.querySelectorAll("[data-needs-engine]")) setEnabled(node, engineReady);
        for (const node of document.querySelectorAll("[data-needs-db]")) setEnabled(node, dbReady);

        const visible = dbReady ? state.mode : "welcome";
        document.querySelector(".modebar").hidden = !dbReady;
        for (const pane of panes) pane.hidden = pane.dataset.pane !== visible;
        for (const button of modes) button.setAttribute("aria-selected", button.dataset.mode === state.mode ? "true" : "false");

        fileName.textContent = state.fileName || "未打开数据库";
        document.title = state.fileName ? `${state.fileName} · 小竹数据库` : "小竹数据库";
        status.classList.toggle("is-error", state.noticeError);
        statusText.textContent = state.notice;
        statusMeta.textContent = dbReady ? meta(state) : "";

        if (state.engineError) {
            engineError.hidden = false;
            engineError.textContent = state.engineError;
        } else {
            engineError.hidden = true;
            engineError.textContent = "";
        }
    });
}

function setEnabled(node, on) {
    if ("disabled" in node) node.disabled = !on;
    node.classList.toggle("is-disabled", !on);
    if (on) node.removeAttribute("aria-disabled");
    else node.setAttribute("aria-disabled", "true");
}

function meta(state) {
    const parts = [];
    if (state.version) parts.push(`SQLite ${state.version}`);
    for (const [type, label] of KIND_LABELS) {
        const count = state.objects.filter((item) => item.type === type).length;
        if (count) parts.push(`${formatCount(count)} ${label}`);
    }
    const size = formatSize(state.bytes);
    if (size) parts.push(size);
    parts.push("外键已启用");
    parts.push("改动只在内存，导出才会保存");
    return parts.join(" · ");
}
