/* Property panel. Writes the document, then asks the view to restyle by class. */
(function () {
    const M = () => window.DesignerModel;
    const V = () => window.DesignerView;

    function doc() {
        return window.Designer.doc;
    }

    function status(text) {
        window.Designer.status(text);
    }

    function save() {
        window.Designer.save();
    }

    function setValue(id, value) {
        const el = document.getElementById(id);
        if (!el) {
            return;
        }
        if (el.value !== value) {
            el.value = value;
        }
    }

    function setText(id, value) {
        const el = document.getElementById(id);
        if (el) {
            el.textContent = value;
        }
    }

    function selected() {
        return M().find(doc(), doc().selected);
    }

    function positionEnabled(node) {
        if (!node) {
            return false;
        }
        const at = M().locate(doc(), node.id);
        return M().deviceOf(doc()).mode === "free" && at && !at.parent;
    }

    function refresh(current) {
        const node = current.selected ? M().find(current, current.selected) : null;
        document.getElementById("comp-block").hidden = !node;
        document.getElementById("page-hint").hidden = !!node;
        setValue("prop-title", current.title);
        document.getElementById("btn-undo").disabled = current.undo.length === 0;
        document.getElementById("btn-redo").disabled = current.redo.length === 0;
        document.getElementById("menu-undo").disabled = current.undo.length === 0;
        document.getElementById("menu-redo").disabled = current.redo.length === 0;
        setText("doc-name", current.title || "未命名界面");
        setText("status-sel", "未选中");
        ["nx-dec", "nx-inc", "ny-dec", "ny-inc"].forEach(function (id) {
            document.getElementById(id).disabled = !positionEnabled(node);
        });
        if (node) {
            const spec = M().KINDS[node.kind];
            const kind = spec ? spec.label : node.kind;
            setText("prop-kind", kind + "  ·  " + node.id);
            setText("status-sel", kind + " · " + node.id);
            setValue("prop-text", node.text);
            setText("lab-x", String(node.x * M().CELL));
            setText("lab-y", String(node.y * M().CELL));
            setText("lab-w", String(node.w * M().CELL));
            setText("lab-h", String(node.h * M().CELL));
            document.querySelectorAll("[data-act='swatch']").forEach(function (button) {
                button.classList.toggle("is-on", button.dataset.token === (node[button.dataset.group] || ""));
            });
        }
        setValue("code-view", window.DesignerCode.generate(current));
    }

    function select(id) {
        const current = doc();
        const prev = current.selected;
        current.selected = id || "";
        if (prev !== current.selected) {
            V().setFlag(prev, "is-selected", false);
            V().setFlag("layer-" + prev, "is-on", false);
            V().setFlag(current.selected, "is-selected", true);
            V().setFlag("layer-" + current.selected, "is-on", true);
            window.Designer.textUndo = false;
        }
        refresh(current);
    }

    function onTitle() {
        const current = doc();
        if (!window.Designer.titleUndo) {
            M().pushUndo(current);
            window.Designer.titleUndo = true;
        }
        current.title = document.getElementById("prop-title").value;
        V().applyDevice(current);
        refresh(current);
        save();
    }

    function onText() {
        const node = selected();
        if (!node) {
            return;
        }
        if (!window.Designer.textUndo) {
            M().pushUndo(doc());
            window.Designer.textUndo = true;
        }
        node.text = document.getElementById("prop-text").value;
        const cap = document.getElementById("cap-" + node.id);
        if (cap) {
            cap.textContent = node.text;
        }
        const layer = document.getElementById("layer-" + node.id);
        if (layer) {
            layer.textContent = V().layerText(doc(), node);
        }
        refresh(doc());
        save();
    }

    function onNudge(key, step) {
        const node = selected();
        if (!node) {
            return;
        }
        if ((key === "x" || key === "y") && !positionEnabled(node)) {
            return;
        }
        M().pushUndo(doc());
        node[key] = (node[key] | 0) + step;
        M().clampBox(doc(), node);
        V().syncGeom(node);
        refresh(doc());
        save();
        status("已调整");
    }

    function onSwatch(group, token) {
        const node = selected();
        if (!node || !group) {
            return;
        }
        M().pushUndo(doc());
        node[group] = token || "";
        V().syncLook(node);
        refresh(doc());
        save();
    }

    function onDevice(name) {
        const current = doc();
        if (name !== "desktop" && name !== "phone") {
            return;
        }
        if (current.device === name) {
            return;
        }
        M().pushUndo(current);
        current.device = name;
        V().render(current);
        refresh(current);
        save();
        status(name === "phone" ? "手机画布：纵向排列" : "桌面画布：自由摆放");
    }

    function onUndo() {
        if (!M().undo(doc())) {
            return;
        }
        window.Designer.textUndo = false;
        window.Designer.titleUndo = false;
        V().render(doc());
        refresh(doc());
        save();
        status("已撤销");
    }

    function onRedo() {
        if (!M().redo(doc())) {
            return;
        }
        window.Designer.textUndo = false;
        window.Designer.titleUndo = false;
        V().render(doc());
        refresh(doc());
        save();
        status("已重做");
    }

    function onSample() {
        M().pushUndo(doc());
        M().loadSample(doc());
        V().render(doc());
        refresh(doc());
        save();
        status("已放入示例");
    }

    function onClear() {
        const current = doc();
        if (!current.roots.length) {
            return;
        }
        M().pushUndo(current);
        current.roots = [];
        current.selected = "";
        V().render(current);
        refresh(current);
        save();
        status("已清空画布");
    }

    function onTab(name) {
        const props = name !== "code";
        document.getElementById("panel-props").hidden = !props;
        document.getElementById("panel-code").hidden = props;
        document.getElementById("tab-props").classList.toggle("is-on", props);
        document.getElementById("tab-code").classList.toggle("is-on", !props);
        document.getElementById("menu-props").classList.toggle("is-on", props);
        document.getElementById("menu-code").classList.toggle("is-on", !props);
        if (!props) {
            setValue("code-view", window.DesignerCode.generate(doc()));
        }
    }

    function onLayer(id) {
        select(id);
        status("已选中");
    }

    function onCopy() {
        const text = window.DesignerCode.generate(doc());
        if (navigator.clipboard && navigator.clipboard.writeText) {
            navigator.clipboard.writeText(text).then(function () {
                status("已复制 HTML");
            }, function () {
                status("复制失败");
            });
            return;
        }
        status("这个浏览器不能写入剪贴板");
    }

    function onExport() {
        const text = window.DesignerCode.generate(doc());
        const blob = new Blob([text], { type: "text/html;charset=utf-8" });
        const link = document.createElement("a");
        link.href = URL.createObjectURL(blob);
        link.download = (doc().title || "界面") + ".html";
        link.click();
        URL.revokeObjectURL(link.href);
        status("已导出 HTML");
    }

    window.DesignerInspector = {
        refresh: refresh,
        select: select,
        onTitle: onTitle,
        onText: onText,
        onNudge: onNudge,
        onSwatch: onSwatch,
        onDevice: onDevice,
        onUndo: onUndo,
        onRedo: onRedo,
        onSample: onSample,
        onClear: onClear,
        onTab: onTab,
        onLayer: onLayer,
        onCopy: onCopy,
        onExport: onExport,
    };
})();
