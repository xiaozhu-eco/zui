/* Pointer drag. Structural edits wait until pointerup so the pressed node stays put. */
(function () {
    const M = () => window.DesignerModel;
    const V = () => window.DesignerView;
    const I = () => window.DesignerInspector;

    function doc() {
        return window.Designer.doc;
    }

    function status(text) {
        window.Designer.status(text);
    }

    function save() {
        window.Designer.save();
    }

    function rectOf(id) {
        const el = document.getElementById(id);
        if (!el) {
            return null;
        }
        const rect = el.getBoundingClientRect();
        return {
            left: rect.left,
            top: rect.top,
            right: rect.right,
            bottom: rect.bottom,
            cx: (rect.left + rect.right) / 2,
            cy: (rect.top + rect.bottom) / 2,
        };
    }

    function insideBoard(x, y) {
        const rect = rectOf("board");
        return !!rect && x >= rect.left && y >= rect.top && x < rect.right && y < rect.bottom;
    }

    function under(current, node, ancestorId) {
        let cursor = node;
        while (cursor) {
            if (cursor.id === ancestorId) {
                return true;
            }
            const at = M().locate(current, cursor.id);
            cursor = at ? at.parent : null;
        }
        return false;
    }

    function nodeAt(current, x, y, skipId) {
        let best = null;
        let bestArea = Infinity;
        M().walk(current.roots, function (node) {
            if (skipId && under(current, node, skipId)) {
                return false;
            }
            const rect = rectOf(node.id);
            if (!rect || x < rect.left || y < rect.top || x >= rect.right || y >= rect.bottom) {
                return false;
            }
            const area = Math.max(1, (rect.right - rect.left) * (rect.bottom - rect.top));
            if (area < bestArea) {
                best = node;
                bestArea = area;
            }
            return false;
        });
        return best;
    }

    function beside(current, node, x, y) {
        return M().indexBeside(current, node, x, y, function (id) {
            return rectOf(id) || { cx: 0, cy: 0 };
        });
    }

    function dropTarget(current, x, y, movingId, createKind) {
        if (!insideBoard(x, y)) {
            return null;
        }
        const moving = movingId ? M().find(current, movingId) : null;
        const hit = nodeAt(current, x, y, movingId);
        const dev = M().deviceOf(current);

        function accept(parent) {
            if (moving) {
                return M().canReparent(current, moving, parent);
            }
            return M().canPlace(current, createKind, parent);
        }

        if (hit && M().isContainer(hit) && accept(hit)) {
            return { parent: hit, index: hit.children.length, free: false };
        }
        if (hit) {
            const at = M().locate(current, hit.id);
            const parent = at ? at.parent : null;
            if (parent && accept(parent)) {
                return { parent: parent, index: beside(current, hit, x, y), free: false };
            }
            if (!parent && dev.mode === "stack" && accept(null)) {
                return { parent: null, index: beside(current, hit, x, y), free: false };
            }
        }
        if (moving && dev.mode === "free") {
            const at = M().locate(current, moving.id);
            if (at && !at.parent) {
                return { keep: true };
            }
            if (accept(null)) {
                return { parent: null, index: current.roots.length, free: true };
            }
            return { keep: true };
        }
        if (accept(null)) {
            return { parent: null, index: current.roots.length, free: dev.mode === "free" };
        }
        return null;
    }

    function placeFree(node, x, y) {
        const board = rectOf("board");
        const cell = M().CELL;
        node.x = Math.floor((x - board.left) / cell) - Math.floor(node.w / 2);
        node.y = Math.floor((y - board.top) / cell) - Math.floor(node.h / 2);
        M().clampBox(doc(), node);
    }

    function highlight(id) {
        if (window.DesignerView.dropId === id) {
            return;
        }
        V().setFlag(window.DesignerView.dropId, "is-drop", false);
        window.DesignerView.dropId = id || "";
        V().setFlag(window.DesignerView.dropId, "is-drop", true);
    }

    function closeMenus(except) {
        document.querySelectorAll(".menu-toggle").forEach(function (box) {
            if (box !== except) {
                box.checked = false;
            }
        });
    }

    function onPointerDown(event) {
        if (event.button !== 0) {
            return;
        }
        const wrap = event.target.closest(".menu-wrap");
        closeMenus(wrap ? wrap.querySelector(".menu-toggle") : null);
        const roleEl = event.target.closest("[data-role]");
        if (!roleEl) {
            return;
        }
        const role = roleEl.dataset.role;
        const current = doc();
        if (role === "pal") {
            window.Designer.drag = {
                mode: "create",
                kind: roleEl.dataset.kind,
                startX: event.clientX,
                startY: event.clientY,
                moved: false,
                undo: false,
            };
            event.preventDefault();
            return;
        }
        if (role === "resize" || role === "node") {
            const id = roleEl.dataset.id;
            const node = M().find(current, id);
            if (!node) {
                return;
            }
            I().select(id);
            window.Designer.drag = {
                mode: role === "resize" ? "resize" : "move",
                id: id,
                startX: event.clientX,
                startY: event.clientY,
                ox: node.x,
                oy: node.y,
                ow: node.w,
                oh: node.h,
                moved: false,
                undo: false,
            };
            V().setFlag(id, "is-dragging", true);
            event.preventDefault();
            return;
        }
        if (role === "board") {
            window.Designer.drag = { mode: "board", moved: false };
        }
    }

    function onPointerMove(event) {
        const drag = window.Designer.drag;
        if (!drag || drag.mode === "board") {
            return;
        }
        const current = doc();
        const dx = Math.round((event.clientX - drag.startX) / M().CELL);
        const dy = Math.round((event.clientY - drag.startY) / M().CELL);
        if (dx !== 0 || dy !== 0) {
            drag.moved = true;
        }
        if (drag.mode === "create") {
            V().showGhost(current, drag.kind, event.clientX, event.clientY);
            const target = dropTarget(current, event.clientX, event.clientY, "", drag.kind);
            highlight(target && target.parent ? target.parent.id : "");
            status(target ? "松手放下" : "拖到画布上");
            return;
        }
        const node = M().find(current, drag.id);
        if (!node) {
            return;
        }
        if (!drag.undo && drag.moved) {
            M().pushUndo(current);
            drag.undo = true;
        }
        if (drag.mode === "resize") {
            node.w = drag.ow + dx;
            node.h = drag.oh + dy;
            M().clampBox(current, node);
            V().syncGeom(node);
            I().refresh(current);
            return;
        }
        const at = M().locate(current, node.id);
        if (M().deviceOf(current).mode === "free" && at && !at.parent) {
            node.x = drag.ox + dx;
            node.y = drag.oy + dy;
            M().clampBox(current, node);
            V().syncGeom(node);
            I().refresh(current);
        }
        const target = dropTarget(current, event.clientX, event.clientY, node.id, node.kind);
        highlight(target && target.parent ? target.parent.id : "");
    }

    function onPointerUp(event) {
        const drag = window.Designer.drag;
        window.Designer.drag = null;
        V().hideGhost();
        highlight("");
        if (!drag) {
            return;
        }
        const current = doc();
        if (drag.id) {
            V().setFlag(drag.id, "is-dragging", false);
        }
        if (drag.mode === "board") {
            I().select("");
            status("未选中");
            return;
        }
        if (drag.mode === "create") {
            const target = dropTarget(current, event.clientX, event.clientY, "", drag.kind);
            if (!target) {
                status("没有放到画布上");
                return;
            }
            M().pushUndo(current);
            const node = M().makeNode(current, drag.kind);
            if (!node) {
                return;
            }
            if (target.free) {
                placeFree(node, event.clientX, event.clientY);
            }
            M().insert(current, node, target.parent, target.index);
            current.selected = node.id;
            V().render(current);
            I().refresh(current);
            save();
            status("已添加「" + M().KINDS[drag.kind].label + "」");
            return;
        }
        if (drag.mode === "resize") {
            I().refresh(current);
            save();
            status(drag.moved ? "已调整大小" : "已选中");
            return;
        }
        const target = dropTarget(current, event.clientX, event.clientY, drag.id, "");
        if (target && target.keep) {
            I().refresh(current);
            save();
            status(drag.moved ? "已移动" : "已选中");
            return;
        }
        if (target && !target.keep) {
            const at = M().locate(current, drag.id);
            const same = at && at.parent === target.parent && at.index === target.index;
            if (!same) {
                if (!drag.undo) {
                    M().pushUndo(current);
                }
                if (target.free) {
                    const node = M().find(current, drag.id);
                    placeFree(node, event.clientX, event.clientY);
                }
                M().moveTo(current, drag.id, target.parent, target.index);
                V().render(current);
                I().refresh(current);
                save();
                status(target.parent ? "已放入容器" : "已移动");
                return;
            }
        }
        I().refresh(current);
        save();
        status(drag.moved ? "已移动" : "已选中");
    }

    function typing(event) {
        const tag = event.target && event.target.tagName;
        return tag === "INPUT" || tag === "TEXTAREA";
    }

    function onKey(event) {
        if (typing(event)) {
            return;
        }
        const current = doc();
        const key = event.key;
        if (key === "Delete" || key === "Backspace") {
            if (!current.selected) {
                return;
            }
            event.preventDefault();
            M().pushUndo(current);
            M().remove(current, current.selected);
            V().render(current);
            I().refresh(current);
            save();
            status("已删除");
            return;
        }
        if ((event.ctrlKey || event.metaKey) && (key === "z" || key === "Z")) {
            event.preventDefault();
            if (event.shiftKey) {
                I().onRedo();
            } else {
                I().onUndo();
            }
            return;
        }
        if ((event.ctrlKey || event.metaKey) && (key === "y" || key === "Y")) {
            event.preventDefault();
            I().onRedo();
            return;
        }
        if (key === "Escape") {
            I().select("");
            status("未选中");
            return;
        }
        const arrows = { ArrowLeft: ["x", -1], ArrowRight: ["x", 1], ArrowUp: ["y", -1], ArrowDown: ["y", 1] };
        if (!arrows[key] || !current.selected) {
            return;
        }
        const node = M().find(current, current.selected);
        const at = node && M().locate(current, node.id);
        if (!node || !at || at.parent || M().deviceOf(current).mode !== "free") {
            return;
        }
        event.preventDefault();
        M().pushUndo(current);
        node[arrows[key][0]] += arrows[key][1];
        M().clampBox(current, node);
        V().syncGeom(node);
        I().refresh(current);
        save();
    }

    function onClick(event) {
        const el = event.target.closest("[data-act]");
        if (!el) {
            return;
        }
        const act = el.dataset.act;
        if (act === "undo") I().onUndo();
        else if (act === "redo") I().onRedo();
        else if (act === "sample") I().onSample();
        else if (act === "clear") I().onClear();
        else if (act === "export") I().onExport();
        else if (act === "copy") I().onCopy();
        else if (act === "device") I().onDevice(el.dataset.device);
        else if (act === "tab") I().onTab(el.dataset.tab);
        else if (act === "layer") I().onLayer(el.dataset.id);
        else if (act === "nudge") I().onNudge(el.dataset.key, Number(el.dataset.step));
        else if (act === "swatch") I().onSwatch(el.dataset.group, el.dataset.token || "");
        if (el.closest(".menu")) {
            closeMenus(null);
        }
    }

    function bind() {
        document.addEventListener("pointerdown", onPointerDown);
        document.addEventListener("pointermove", onPointerMove);
        document.addEventListener("pointerup", onPointerUp);
        document.addEventListener("keydown", onKey);
        document.addEventListener("click", onClick);
        document.getElementById("prop-title").addEventListener("input", function () {
            I().onTitle();
        });
        document.getElementById("prop-text").addEventListener("input", function () {
            I().onText();
        });
        document.getElementById("prop-title").addEventListener("blur", function () {
            window.Designer.titleUndo = false;
        });
        document.getElementById("prop-text").addEventListener("blur", function () {
            window.Designer.textUndo = false;
        });
    }

    window.DesignerInteract = { bind: bind, dropTarget: dropTarget };
})();
