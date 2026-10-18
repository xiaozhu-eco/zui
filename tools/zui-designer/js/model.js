/* Design document. Geometry is an 8px cell grid shared with the native session. */
(function () {
    const CELL = 8;
    const DEVICES = {
        desktop: { label: "桌面", w: 704, h: 480, cols: 88, rows: 60, mode: "free" },
        phone: { label: "手机", w: 392, h: 640, cols: 49, rows: 80, mode: "stack" },
    };
    const KINDS = {
        heading: { label: "标题", text: "标题", w: 28, h: 6, fs: "fs-28", fg: "fg-ink" },
        text: { label: "文本", text: "正文", w: 40, h: 8, fs: "fs-14", fg: "fg-soft" },
        button: { label: "按钮", text: "按钮", w: 14, h: 5, fs: "fs-14", bg: "bg-accent", fg: "fg-paper", radius: "radius-8" },
        input: { label: "输入框", text: "请输入", w: 32, h: 5, fs: "fs-14", bg: "bg-paper", radius: "radius-8" },
        area: { label: "多行文本", text: "多行文本", w: 36, h: 12, fs: "fs-14", bg: "bg-paper", radius: "radius-8" },
        check: { label: "勾选", text: "选项", w: 16, h: 4, fs: "fs-14", fg: "fg-ink" },
        image: { label: "图片", text: "图片", w: 22, h: 16, bg: "bg-tint", fg: "fg-soft", radius: "radius-12" },
        rule: { label: "分割线", text: "", w: 40, h: 2 },
        row: { label: "横向", text: "", w: 40, h: 12, radius: "radius-8" },
        col: { label: "纵向", text: "", w: 24, h: 28, radius: "radius-8" },
    };

    function createDoc() {
        return {
            version: 1,
            device: "desktop",
            title: "未命名界面",
            nextId: 1,
            selected: "",
            roots: [],
            undo: [],
            redo: [],
        };
    }

    function isContainer(node) {
        return node != null && (node.kind === "row" || node.kind === "col");
    }

    function deviceOf(doc) {
        return DEVICES[doc.device] || DEVICES.desktop;
    }

    function walk(list, fn, parent) {
        for (let i = 0; i < list.length; i++) {
            const node = list[i];
            if (fn(node, parent || null, list, i)) {
                return node;
            }
            const found = walk(node.children, fn, node);
            if (found) {
                return found;
            }
        }
        return null;
    }

    function locate(doc, id) {
        if (!id) {
            return null;
        }
        let hit = null;
        walk(doc.roots, function (node, parent, list, index) {
            if (node.id === id) {
                hit = { node: node, parent: parent, list: list, index: index };
                return true;
            }
            return false;
        });
        return hit;
    }

    function find(doc, id) {
        const at = locate(doc, id);
        return at ? at.node : null;
    }

    function depth(doc, node) {
        let d = 0;
        let cursor = node;
        while (cursor) {
            d += 1;
            const at = locate(doc, cursor.id);
            cursor = at ? at.parent : null;
        }
        return d;
    }

    function subtreeHeight(node) {
        let height = 1;
        for (let i = 0; i < node.children.length; i++) {
            height = Math.max(height, 1 + subtreeHeight(node.children[i]));
        }
        return height;
    }

    function minSize(kind) {
        if (kind === "rule") {
            return { w: 4, h: 1 };
        }
        if (kind === "check") {
            return { w: 8, h: 3 };
        }
        return { w: 4, h: 3 };
    }

    function clampBox(doc, node) {
        const dev = deviceOf(doc);
        const min = minSize(node.kind);
        node.w = Math.max(min.w, node.w);
        node.h = Math.max(min.h, node.h);
        if (node.w > dev.cols) {
            node.w = dev.cols;
        }
        if (node.h > dev.rows) {
            node.h = dev.rows;
        }
        if (node.x < 0) {
            node.x = 0;
        }
        if (node.y < 0) {
            node.y = 0;
        }
        if (node.x + node.w > dev.cols) {
            node.x = Math.max(0, dev.cols - node.w);
        }
        if (node.y + node.h > dev.rows) {
            node.y = Math.max(0, dev.rows - node.h);
        }
    }

    function makeNode(doc, kind) {
        const spec = KINDS[kind];
        if (!spec) {
            return null;
        }
        return {
            id: "n" + doc.nextId++,
            kind: kind,
            text: spec.text,
            x: 0,
            y: 0,
            w: spec.w,
            h: spec.h,
            bg: spec.bg || "",
            fg: spec.fg || "",
            fs: spec.fs || "",
            radius: spec.radius || "",
            align: spec.align || "",
            children: [],
        };
    }

    function lookClass(node) {
        return [node.bg, node.fg, node.fs, node.radius, node.align].filter(Boolean).join(" ");
    }

    function geomClass(node) {
        return "x-" + node.x + " y-" + node.y + " w-" + node.w + " h-" + node.h;
    }

    function flagsClass(node) {
        return isContainer(node) && node.children.length === 0 ? "is-empty" : "";
    }

    function toPlain(doc) {
        const nodes = [];
        function rec(list, parent) {
            for (let i = 0; i < list.length; i++) {
                const n = list[i];
                nodes.push({
                    id: n.id,
                    kind: n.kind,
                    text: n.text,
                    parent: parent,
                    x: n.x,
                    y: n.y,
                    w: n.w,
                    h: n.h,
                    bg: n.bg,
                    fg: n.fg,
                    fs: n.fs,
                    radius: n.radius,
                    align: n.align,
                });
                rec(n.children, n.id);
            }
        }
        rec(doc.roots, "");
        return {
            version: 1,
            device: doc.device,
            title: doc.title,
            nextId: doc.nextId,
            selected: doc.selected,
            nodes: nodes,
        };
    }

    function fromPlain(plain) {
        const doc = createDoc();
        if (!plain || plain.version !== 1 || !Array.isArray(plain.nodes)) {
            return doc;
        }
        doc.device = plain.device === "phone" ? "phone" : "desktop";
        doc.title = typeof plain.title === "string" && plain.title ? plain.title : doc.title;
        doc.nextId = plain.nextId > 0 ? plain.nextId : 1;
        doc.selected = plain.selected || "";
        const byId = {};
        for (let i = 0; i < plain.nodes.length; i++) {
            const row = plain.nodes[i];
            if (!row || !row.id || !KINDS[row.kind]) {
                continue;
            }
            const node = {
                id: String(row.id),
                kind: row.kind,
                text: row.text == null ? "" : String(row.text),
                x: row.x | 0,
                y: row.y | 0,
                w: row.w | 0,
                h: row.h | 0,
                bg: row.bg || "",
                fg: row.fg || "",
                fs: row.fs || "",
                radius: row.radius || "",
                align: row.align || "",
                children: [],
            };
            clampBox(doc, node);
            byId[node.id] = node;
            const parent = row.parent ? byId[row.parent] : null;
            if (parent) {
                parent.children.push(node);
            } else {
                doc.roots.push(node);
            }
        }
        if (doc.selected && !byId[doc.selected]) {
            doc.selected = "";
        }
        let maxId = 0;
        Object.keys(byId).forEach(function (id) {
            const match = /^n(\d+)$/.exec(id);
            if (match) {
                maxId = Math.max(maxId, Number(match[1]));
            }
        });
        if (doc.nextId <= maxId) {
            doc.nextId = maxId + 1;
        }
        return doc;
    }

    function pushUndo(doc) {
        doc.undo.push(JSON.stringify(toPlain(doc)));
        if (doc.undo.length > 40) {
            doc.undo.shift();
        }
        doc.redo = [];
    }

    function undo(doc) {
        if (!doc.undo.length) {
            return false;
        }
        doc.redo.push(JSON.stringify(toPlain(doc)));
        const plain = JSON.parse(doc.undo.pop());
        const next = fromPlain(plain);
        doc.device = next.device;
        doc.title = next.title;
        doc.nextId = next.nextId;
        doc.selected = next.selected;
        doc.roots = next.roots;
        return true;
    }

    function redo(doc) {
        if (!doc.redo.length) {
            return false;
        }
        doc.undo.push(JSON.stringify(toPlain(doc)));
        const plain = JSON.parse(doc.redo.pop());
        const next = fromPlain(plain);
        doc.device = next.device;
        doc.title = next.title;
        doc.nextId = next.nextId;
        doc.selected = next.selected;
        doc.roots = next.roots;
        return true;
    }

    function canPlace(doc, kind, parent) {
        const parentDepth = parent ? depth(doc, parent) : 0;
        const nextDepth = parentDepth + 1;
        if (nextDepth > 4) {
            return false;
        }
        if (isContainer({ kind: kind }) && nextDepth > 3) {
            return false;
        }
        return true;
    }

    function canReparent(doc, node, parent) {
        if (!node) {
            return false;
        }
        if (parent) {
            let cursor = parent;
            while (cursor) {
                if (cursor.id === node.id) {
                    return false;
                }
                const at = locate(doc, cursor.id);
                cursor = at ? at.parent : null;
            }
        }
        const parentDepth = parent ? depth(doc, parent) : 0;
        if (parentDepth + subtreeHeight(node) > 4) {
            return false;
        }
        if (isContainer(node) && parentDepth + 1 > 3) {
            return false;
        }
        return true;
    }

    function detach(doc, id) {
        const at = locate(doc, id);
        if (!at) {
            return null;
        }
        return at.list.splice(at.index, 1)[0];
    }

    function insert(doc, node, parent, index) {
        const list = parent ? parent.children : doc.roots;
        let at = index;
        if (at == null || at < 0 || at > list.length) {
            at = list.length;
        }
        list.splice(at, 0, node);
    }

    function remove(doc, id) {
        if (!detach(doc, id)) {
            return false;
        }
        if (!find(doc, doc.selected)) {
            doc.selected = "";
        }
        return true;
    }

    function moveTo(doc, id, parent, index) {
        const at = locate(doc, id);
        if (!at) {
            return false;
        }
        const list = parent ? parent.children : doc.roots;
        let dest = index == null || index < 0 ? list.length : index;
        if (at.list === list && at.index < dest) {
            dest -= 1;
        }
        const node = at.list.splice(at.index, 1)[0];
        if (dest > list.length) {
            dest = list.length;
        }
        if (dest < 0) {
            dest = 0;
        }
        list.splice(dest, 0, node);
        return true;
    }

    function indexBeside(doc, node, x, y, rectOf) {
        const at = locate(doc, node.id);
        if (!at) {
            return 0;
        }
        const rect = rectOf(node.id);
        const before = at.parent && at.parent.kind === "row" ? x < rect.cx : y < rect.cy;
        return before ? at.index : at.index + 1;
    }

    function loadSample(doc) {
        doc.roots = [];
        doc.selected = "";
        doc.nextId = 1;
        const heading = makeNode(doc, "heading");
        heading.text = "小竹问候";
        heading.x = 4;
        heading.y = 3;
        const text = makeNode(doc, "text");
        text.text = "把组件拖进画布，选中后在右侧改属性。";
        text.x = 4;
        text.y = 10;
        text.w = 48;
        const input = makeNode(doc, "input");
        input.text = "你的名字";
        input.x = 4;
        input.y = 20;
        const row = makeNode(doc, "row");
        row.x = 4;
        row.y = 28;
        row.w = 36;
        row.h = 8;
        const ok = makeNode(doc, "button");
        ok.text = "确定";
        const cancel = makeNode(doc, "button");
        cancel.text = "取消";
        cancel.bg = "bg-paper";
        cancel.fg = "fg-ink";
        row.children.push(ok, cancel);
        doc.roots.push(heading, text, input, row);
        doc.selected = heading.id;
    }

    window.DesignerModel = {
        CELL: CELL,
        DEVICES: DEVICES,
        KINDS: KINDS,
        createDoc: createDoc,
        isContainer: isContainer,
        deviceOf: deviceOf,
        walk: walk,
        locate: locate,
        find: find,
        depth: depth,
        clampBox: clampBox,
        makeNode: makeNode,
        lookClass: lookClass,
        geomClass: geomClass,
        flagsClass: flagsClass,
        toPlain: toPlain,
        fromPlain: fromPlain,
        pushUndo: pushUndo,
        undo: undo,
        redo: redo,
        canPlace: canPlace,
        canReparent: canReparent,
        detach: detach,
        insert: insert,
        remove: remove,
        moveTo: moveTo,
        indexBeside: indexBeside,
        loadSample: loadSample,
    };
})();
