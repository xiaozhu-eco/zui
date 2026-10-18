/* Clones the same <template> the native session fills with set_model. */
(function () {
    const M = () => window.DesignerModel;

    function subst(raw, fields) {
        return String(raw).replace(/\{\{\s*([^}]+?)\s*\}\}/g, function (_, key) {
            const value = fields[key.trim()];
            return value == null ? "" : String(value);
        });
    }

    function interpolate(node, fields) {
        if (node.nodeType === 3) {
            if (node.textContent.indexOf("{{") !== -1) {
                node.textContent = subst(node.textContent, fields);
            }
            return;
        }
        if (node.nodeType !== 1 || node.tagName === "TEMPLATE") {
            return;
        }
        Array.prototype.forEach.call(node.attributes, function (attr) {
            if (attr.value.indexOf("{{") !== -1) {
                node.setAttribute(attr.name, subst(attr.value, fields));
            }
        });
        Array.prototype.forEach.call(node.childNodes, function (child) {
            interpolate(child, fields);
        });
    }

    function fieldsOf(doc, node) {
        return {
            id: node.id,
            kind: "kind-" + node.kind,
            text: node.text,
            sel: node.id === doc.selected ? "is-selected" : "",
            geom: M().geomClass(node),
            look: M().lookClass(node),
            flags: M().flagsClass(node),
        };
    }

    function clearHost(host) {
        Array.prototype.forEach.call(Array.from(host.childNodes), function (child) {
            if (child.nodeType === 1 && child.tagName === "TEMPLATE") {
                return;
            }
            host.removeChild(child);
        });
    }

    function fill(doc, host, nodes, depth) {
        const tmpl = host.querySelector(":scope > template");
        if (!tmpl) {
            return;
        }
        clearHost(host);
        nodes.forEach(function (node) {
            const frag = tmpl.content.cloneNode(true);
            Array.prototype.forEach.call(Array.from(frag.childNodes), function (child) {
                if (child.nodeType === 3 && !child.textContent.trim()) {
                    return;
                }
                interpolate(child, fieldsOf(doc, node));
                host.appendChild(child);
            });
            if (depth < 3) {
                const kids = document.getElementById("kids-" + node.id);
                if (kids) {
                    fill(doc, kids, node.children, depth + 1);
                }
            }
        });
    }

    function layerText(doc, node) {
        const spec = M().KINDS[node.kind];
        const name = spec ? spec.label : node.kind;
        const pad = new Array(M().depth(doc, node) + 1).join("· ");
        return pad + name + (node.text ? " " + node.text : "");
    }

    function renderLayers(doc) {
        const host = document.getElementById("layers");
        const rows = [];
        M().walk(doc.roots, function (node) {
            rows.push(node);
            return false;
        });
        fillLayers(host, rows.map(function (node) {
            return {
                id: node.id,
                text: layerText(doc, node),
                sel: node.id === doc.selected ? "is-on" : "",
            };
        }));
        const empty = document.getElementById("layers-empty");
        if (empty) {
            empty.hidden = rows.length > 0;
        }
        const count = document.getElementById("layer-count");
        if (count) {
            count.textContent = String(rows.length);
        }
    }

    function fillLayers(host, rows) {
        const tmpl = host.querySelector(":scope > template");
        clearHost(host);
        rows.forEach(function (row) {
            const frag = tmpl.content.cloneNode(true);
            Array.prototype.forEach.call(Array.from(frag.childNodes), function (child) {
                if (child.nodeType === 3 && !child.textContent.trim()) {
                    return;
                }
                interpolate(child, row);
                host.appendChild(child);
            });
        });
    }

    function applyDevice(doc) {
        const dev = M().deviceOf(doc);
        const device = document.getElementById("device");
        device.classList.toggle("device-desktop", doc.device === "desktop");
        device.classList.toggle("device-phone", doc.device === "phone");
        const board = document.getElementById("board");
        board.classList.toggle("mode-free", dev.mode === "free");
        board.classList.toggle("mode-stack", dev.mode === "stack");
        document.getElementById("device-title").textContent = dev.label + " " + dev.w + " × " + dev.h;
        document.getElementById("btn-desktop").classList.toggle("is-on", doc.device === "desktop");
        document.getElementById("btn-phone").classList.toggle("is-on", doc.device === "phone");
        document.getElementById("menu-desktop").classList.toggle("is-on", doc.device === "desktop");
        document.getElementById("menu-phone").classList.toggle("is-on", doc.device === "phone");
        const name = document.getElementById("doc-name");
        if (name) {
            name.textContent = doc.title || "未命名界面";
        }
        document.title = doc.title ? doc.title + " · 小竹设计" : "小竹设计";
    }

    function render(doc) {
        applyDevice(doc);
        fill(doc, document.getElementById("board"), doc.roots, 0);
        renderLayers(doc);
        const hint = document.getElementById("empty-hint");
        if (hint) {
            hint.hidden = doc.roots.length > 0;
        }
        hideGhost();
        setFlag(window.DesignerView.dropId, "is-drop", false);
        window.DesignerView.dropId = "";
    }

    function classTokens(el) {
        return el.className.split(/\s+/).filter(Boolean);
    }

    function swapToken(id, prefix, next) {
        const el = document.getElementById(id);
        if (!el) {
            return;
        }
        const tokens = classTokens(el).filter(function (token) {
            return token.indexOf(prefix) !== 0;
        });
        if (next) {
            tokens.push(next);
        }
        el.className = tokens.join(" ");
    }

    function setFlag(id, token, on) {
        if (!id) {
            return;
        }
        const el = document.getElementById(id);
        if (!el) {
            return;
        }
        const tokens = classTokens(el).filter(function (item) {
            return item !== token;
        });
        if (on) {
            tokens.push(token);
        }
        el.className = tokens.join(" ");
    }

    function syncGeom(node) {
        swapToken(node.id, "x-", "x-" + node.x);
        swapToken(node.id, "y-", "y-" + node.y);
        swapToken(node.id, "w-", "w-" + node.w);
        swapToken(node.id, "h-", "h-" + node.h);
    }

    function syncLook(node) {
        ["bg-", "fg-", "fs-", "radius-", "align-"].forEach(function (prefix) {
            const field = prefix === "bg-" ? node.bg : prefix === "fg-" ? node.fg : prefix === "fs-" ? node.fs : prefix === "radius-" ? node.radius : node.align;
            swapToken(node.id, prefix, field);
        });
    }

    function hideGhost() {
        const ghost = document.getElementById("ghost");
        if (ghost) {
            ghost.hidden = true;
        }
    }

    function showGhost(doc, kind, x, y) {
        const spec = M().KINDS[kind];
        const ghost = document.getElementById("ghost");
        if (!spec || !ghost) {
            return;
        }
        const board = document.getElementById("board").getBoundingClientRect();
        if (x < board.left || y < board.top || x >= board.right || y >= board.bottom) {
            ghost.hidden = true;
            return;
        }
        const cell = M().CELL;
        const dev = M().deviceOf(doc);
        let gx = Math.floor((x - board.left) / cell) - Math.floor(spec.w / 2);
        let gy = Math.floor((y - board.top) / cell) - Math.floor(spec.h / 2);
        gx = Math.max(0, Math.min(gx, dev.cols - spec.w));
        gy = Math.max(0, Math.min(gy, dev.rows - spec.h));
        swapToken("ghost", "x-", "x-" + gx);
        swapToken("ghost", "y-", "y-" + gy);
        swapToken("ghost", "w-", "w-" + spec.w);
        swapToken("ghost", "h-", "h-" + spec.h);
        document.getElementById("ghost-cap").textContent = spec.label;
        ghost.hidden = false;
    }

    window.DesignerView = {
        render: render,
        applyDevice: applyDevice,
        renderLayers: renderLayers,
        swapToken: swapToken,
        setFlag: setFlag,
        syncGeom: syncGeom,
        syncLook: syncLook,
        hideGhost: hideGhost,
        showGhost: showGhost,
        layerText: layerText,
        dropId: "",
    };
})();
