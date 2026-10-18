export function clear(node) {
    while (node.firstChild) node.removeChild(node.firstChild);
}

export function el(tag, attrs, children) {
    const node = document.createElement(tag);
    if (attrs) {
        for (const [key, value] of Object.entries(attrs)) {
            if (key === "text") {
                node.textContent = value == null ? "" : String(value);
                continue;
            }
            if (key === "class") {
                node.className = value == null ? "" : String(value);
                continue;
            }
            if (key === "hidden" || key === "disabled") {
                node[key] = Boolean(value);
                continue;
            }
            if (value == null || value === false) continue;
            node.setAttribute(key, value === true ? "" : String(value));
        }
    }
    if (children) {
        for (const child of children) {
            if (child == null) continue;
            node.append(child);
        }
    }
    return node;
}
