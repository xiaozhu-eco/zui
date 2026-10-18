/* Standalone HTML the designer exports. Real tags, standard CSS, no script. */
(function () {
    const M = () => window.DesignerModel;

    function esc(text) {
        return String(text)
            .replace(/&/g, "&amp;")
            .replace(/</g, "&lt;")
            .replace(/>/g, "&gt;");
    }

    function escAttr(text) {
        return esc(text).replace(/"/g, "&quot;");
    }

    const KIND_CSS = [
        ".k-heading{margin:0;font-weight:700;display:flex;align-items:center;overflow:hidden;}",
        ".k-text{margin:0;display:flex;align-items:flex-start;overflow:hidden;}",
        ".k-button{border:none;display:flex;align-items:center;justify-content:center;overflow:hidden;background:#2c6b4e;color:#fffdf8;border-radius:8px;}",
        ".k-input,.k-area{display:block;border:1px solid rgba(72,54,32,.22);background:#fffdf8;border-radius:8px;padding:4px 8px;}",
        ".k-check{display:flex;align-items:center;gap:6px;}",
        ".k-image{display:flex;align-items:center;justify-content:center;overflow:hidden;background:rgba(44,107,78,.12);color:#8a7f72;border-radius:12px;}",
        ".k-rule{border:none;border-top:1px solid rgba(72,54,32,.35);height:0;margin:8px 0;}",
        ".k-row{display:flex;flex-direction:row;flex-wrap:wrap;gap:8px;align-items:flex-start;}",
        ".k-col{display:flex;flex-direction:column;gap:8px;}",
    ].join("");

    function paint(node) {
        const parts = [];
        const bg = {
            "bg-paper": "#fffdf8",
            "bg-tint": "rgba(44,107,78,.12)",
            "bg-accent": "#2c6b4e",
            "bg-ink": "#2b261f",
            "bg-danger": "#8c3a32",
            "bg-none": "transparent",
        };
        const fg = {
            "fg-ink": "#2b261f",
            "fg-soft": "#8a7f72",
            "fg-paper": "#fffdf8",
            "fg-accent": "#2c6b4e",
            "fg-danger": "#8c3a32",
        };
        const fs = { "fs-12": "12px", "fs-14": "14px", "fs-16": "16px", "fs-20": "20px", "fs-28": "28px" };
        const radius = { "radius-0": "0", "radius-8": "8px", "radius-12": "12px", "radius-999": "999px" };
        const align = { "align-left": "left", "align-center": "center", "align-right": "right" };
        if (bg[node.bg]) {
            parts.push("background:" + bg[node.bg]);
        }
        if (fg[node.fg]) {
            parts.push("color:" + fg[node.fg]);
        }
        if (fs[node.fs]) {
            parts.push("font-size:" + fs[node.fs]);
        }
        if (radius[node.radius]) {
            parts.push("border-radius:" + radius[node.radius]);
        }
        if (align[node.align]) {
            parts.push("text-align:" + align[node.align]);
        }
        return parts.length ? parts.join(";") + ";" : "";
    }

    function box(doc, node, top) {
        const dev = M().deviceOf(doc);
        const px = M().CELL;
        if (top && dev.mode === "stack") {
            return "position:relative;width:100%;height:" + node.h * px + "px;" + paint(node);
        }
        if (!top) {
            return "position:relative;width:" + node.w * px + "px;height:" + node.h * px + "px;" + paint(node);
        }
        return (
            "position:absolute;left:" +
            node.x * px +
            "px;top:" +
            node.y * px +
            "px;width:" +
            node.w * px +
            "px;height:" +
            node.h * px +
            "px;" +
            paint(node)
        );
    }

    function openTag(doc, node, top) {
        const cls = "k-" + node.kind + " n-" + node.id;
        const style = box(doc, node, top);
        const text = esc(node.text);
        if (node.kind === "heading") {
            return "<h2 class=\"" + cls + "\" style=\"" + style + "\">" + text + "</h2>";
        }
        if (node.kind === "text") {
            return "<p class=\"" + cls + "\" style=\"" + style + "\">" + text + "</p>";
        }
        if (node.kind === "button") {
            return "<button class=\"" + cls + "\" style=\"" + style + "\" type=\"button\">" + text + "</button>";
        }
        if (node.kind === "input") {
            return "<input class=\"" + cls + "\" style=\"" + style + "\" type=\"text\" placeholder=\"" + escAttr(node.text) + "\">";
        }
        if (node.kind === "area") {
            return "<textarea class=\"" + cls + "\" style=\"" + style + "\" placeholder=\"" + escAttr(node.text) + "\"></textarea>";
        }
        if (node.kind === "check") {
            return "<label class=\"" + cls + "\" style=\"" + style + "\"><input type=\"checkbox\"> <span>" + text + "</span></label>";
        }
        if (node.kind === "image") {
            return "<div class=\"" + cls + "\" style=\"" + style + "\" role=\"img\">" + text + "</div>";
        }
        if (node.kind === "rule") {
            return "<hr class=\"" + cls + "\" style=\"" + style + "\">";
        }
        return null;
    }

    function emit(doc, node, top, indent) {
        const pad = "  ".repeat(indent);
        const leaf = openTag(doc, node, top);
        if (leaf != null) {
            return pad + leaf + "\n";
        }
        const cls = "k-" + node.kind + " n-" + node.id;
        let out = pad + "<div class=\"" + cls + "\" style=\"" + box(doc, node, top) + "\">\n";
        for (let i = 0; i < node.children.length; i++) {
            out += emit(doc, node.children[i], false, indent + 1);
        }
        out += pad + "</div>\n";
        return out;
    }

    function pageCss(doc) {
        const dev = M().deviceOf(doc);
        if (dev.mode === "stack") {
            return ".page{display:flex;flex-direction:column;gap:8px;width:" + dev.w + "px;min-height:" + dev.h + "px;margin:0 auto;background:#fffdf8;padding:16px;}";
        }
        return ".page{position:relative;width:" + dev.w + "px;height:" + dev.h + "px;margin:0 auto;background:#fffdf8;overflow:hidden;}";
    }

    function generate(doc) {
        let body = "";
        for (let i = 0; i < doc.roots.length; i++) {
            body += emit(doc, doc.roots[i], true, 2);
        }
        return (
            "<!DOCTYPE html>\n" +
            "<html lang=\"zh-CN\">\n" +
            "<head>\n" +
            "<meta charset=\"utf-8\">\n" +
            "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n" +
            "<title>" + esc(doc.title ? doc.title : "未命名界面") + "</title>\n" +
            "<style>\n" +
            "*{box-sizing:border-box;}\n" +
            "body{margin:0;background:#fbf6ec;color:#2b261f;font-family:\"Microsoft YaHei\",\"PingFang SC\",\"Noto Sans SC\",sans-serif;}\n" +
            pageCss(doc) + "\n" +
            KIND_CSS + "\n" +
            "</style>\n" +
            "</head>\n" +
            "<body>\n" +
            "  <div class=\"page\">\n" +
            body +
            "  </div>\n" +
            "</body>\n" +
            "</html>\n"
        );
    }

    window.DesignerCode = { generate: generate, esc: esc };
})();
