/*
  宿主缺件：原型里的 <i data-lucide="…"> 由预览沙箱注入，zui 与裸页面都没有。
  这里补一个同名的 lucide.createIcons：原型脚本一字未改，仍然按原样调用它。
*/
(function () {
  const NS = "http://www.w3.org/2000/svg";
  const S = 'fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"';
  function glyph(text, size, weight) {
    return '<text x="12" y="17" text-anchor="middle" font-size="' + size + '" font-weight="' +
      (weight || 600) + '" fill="currentColor" stroke="none" font-family="ui-monospace, Consolas, monospace">' +
      text + "</text>";
  }
  const ICONS = {
    "heading-1": glyph("H", 15) + '<path d="M17.5 8v9" ' + S + "/>",
    "heading-2": glyph("H", 15) + '<path d="M17 9h2.5v8h-2" ' + S + "/>",
    quote: '<path d="M9 8c-2 0-3 1.6-3 3.6S7 15 8.6 15c1.2 0 2-.7 2-1.8 0-1-.7-1.7-1.7-1.7-.4 0-.7.1-.9.2" ' + S + "/>" +
      '<path d="M17 8c-2 0-3 1.6-3 3.6S15 15 16.6 15c1.2 0 2-.7 2-1.8 0-1-.7-1.7-1.7-1.7-.4 0-.7.1-.9.2" ' + S + "/>",
    list: '<path d="M9 7h11M9 12h11M9 17h11" ' + S + "/>" +
      '<circle cx="4.5" cy="7" r="1.2" fill="currentColor" stroke="none"/>' +
      '<circle cx="4.5" cy="12" r="1.2" fill="currentColor" stroke="none"/>' +
      '<circle cx="4.5" cy="17" r="1.2" fill="currentColor" stroke="none"/>',
    "list-ordered": '<path d="M10 7h10M10 12h10M10 17h10" ' + S + "/>" + glyph("1", 7) + glyph("2", 7) + glyph("3", 7),
    table: '<rect x="3.5" y="5" width="17" height="14" rx="2" ' + S + "/>" + '<path d="M3.5 10h17M9.5 10v9" ' + S + "/>",
    image: '<rect x="3.5" y="5" width="17" height="14" rx="2" ' + S + "/>" + '<circle cx="9" cy="10" r="1.4" ' + S + "/>" +
      '<path d="M4.5 17.5 10 12.5l4 3.5 2.5-2 3 2.5" ' + S + "/>",
    minus: '<path d="M5 12h14" ' + S + "/>",
    bold: glyph("B", 15, 700),
    italic: glyph("I", 15),
    code: glyph("&lt;/&gt;", 11),
    link: '<path d="M10 14a3.5 3.5 0 0 0 5 0l3-3a3.5 3.5 0 0 0-5-5l-1 1" ' + S + "/>" +
      '<path d="M14 10a3.5 3.5 0 0 0-5 0l-3 3a3.5 3.5 0 0 0 5 5l1-1" ' + S + "/>",
    search: '<circle cx="10.5" cy="10.5" r="5.5" ' + S + "/>" + '<path d="M15 15l4 4" ' + S + "/>",
    settings: '<circle cx="12" cy="12" r="3" ' + S + "/>" +
      '<path d="M12 3.5v2.2M12 18.3v2.2M20.5 12h-2.2M5.7 12H3.5M18 6l-1.6 1.6M7.6 16.4 6 18M18 18l-1.6-1.6M7.6 7.6 6 6" ' + S + "/>",
    "align-left": '<path d="M4 6h16M4 11h10M4 16h13" ' + S + "/>",
    "arrow-up": '<path d="M12 19V5M6.5 10.5 12 5l5.5 5.5" ' + S + "/>",
    "file-plus": '<path d="M13.5 4H7.5A1.5 1.5 0 0 0 6 5.5v13A1.5 1.5 0 0 0 7.5 20h9a1.5 1.5 0 0 0 1.5-1.5V8.5z" ' + S + "/>" +
      '<path d="M13.5 4v4.5H18M12 12v5M9.5 14.5h5" ' + S + "/>",
    "folder-plus": '<path d="M4 7.5A1.5 1.5 0 0 1 5.5 6h3.2l1.8 2h8A1.5 1.5 0 0 1 20 9.5v8A1.5 1.5 0 0 1 18.5 19h-13A1.5 1.5 0 0 1 4 17.5z" ' + S + "/>" +
      '<path d="M12 11.5v5M9.5 14h5" ' + S + "/>",
    pin: '<path d="M9 4h6l-1 5 3.5 3H6.5L10 9z" ' + S + "/>" + '<path d="M12 12v8" ' + S + "/>",
    folder: '<path d="M4 7.5A1.5 1.5 0 0 1 5.5 6h3.2l1.8 2h8A1.5 1.5 0 0 1 20 9.5v8A1.5 1.5 0 0 1 18.5 19h-13A1.5 1.5 0 0 1 4 17.5z" ' + S + "/>",
    "folder-open": '<path d="M4 7.5A1.5 1.5 0 0 1 5.5 6h3.2l1.8 2h8A1.5 1.5 0 0 1 20 9.5V11H4z" ' + S + "/>" +
      '<path d="M4 11h16l-1.6 6.2A1.5 1.5 0 0 1 17 18.5H6.5A1.5 1.5 0 0 1 5 17.2z" ' + S + "/>",
    "file-text": '<path d="M13.5 4H7.5A1.5 1.5 0 0 0 6 5.5v13A1.5 1.5 0 0 0 7.5 20h9a1.5 1.5 0 0 0 1.5-1.5V8.5z" ' + S + "/>" +
      '<path d="M13.5 4v4.5H18M9 13h6M9 16h6" ' + S + "/>",
    highlighter: '<path d="M5 19h6M9 15.5 5.5 19M14.5 5.5 9 11l2.5 2.5 5.5-5.5a1.8 1.8 0 0 0-2.5-2.5z" ' + S + "/>",
    sun: '<circle cx="12" cy="12" r="3.6" ' + S + "/>" +
      '<path d="M12 3.5v2M12 18.5v2M20.5 12h-2M5.5 12h-2M18 6l-1.5 1.5M7.5 16.5 6 18M18 18l-1.5-1.5M7.5 7.5 6 6" ' + S + "/>",
    moon: '<path d="M19 14.5A7.5 7.5 0 0 1 9.5 5a7.5 7.5 0 1 0 9.5 9.5z" ' + S + "/>",
    monitor: '<rect x="3.5" y="5" width="17" height="11" rx="1.5" ' + S + "/>" + '<path d="M9 19h6M12 16v3" ' + S + "/>",
    download: '<path d="M12 4.5v10M8 11l4 4 4-4M5 19h14" ' + S + "/>",
    "columns-2": '<rect x="3.5" y="5" width="17" height="14" rx="2" ' + S + "/>" + '<path d="M12 5v14" ' + S + "/>",
    bookmark: '<path d="M7 4h10v16l-5-4-5 4z" ' + S + "/>",
    clock: '<circle cx="12" cy="12" r="8" ' + S + "/>" + '<path d="M12 8v4.5l3 2" ' + S + "/>",
    plus: '<path d="M12 6v12M6 12h12" ' + S + "/>",
    "grip-vertical": '<circle cx="9" cy="7" r="1.3" fill="currentColor" stroke="none"/>' +
      '<circle cx="15" cy="7" r="1.3" fill="currentColor" stroke="none"/>' +
      '<circle cx="9" cy="12" r="1.3" fill="currentColor" stroke="none"/>' +
      '<circle cx="15" cy="12" r="1.3" fill="currentColor" stroke="none"/>' +
      '<circle cx="9" cy="17" r="1.3" fill="currentColor" stroke="none"/>' +
      '<circle cx="15" cy="17" r="1.3" fill="currentColor" stroke="none"/>'
  };
  function createIcons() {
    const attrs = arguments[0] && arguments[0].attrs ? arguments[0].attrs : {};
    const width = attrs.width || 16;
    document.querySelectorAll("i[data-lucide]").forEach(function (node) {
      const body = ICONS[node.getAttribute("data-lucide")];
      if (!body) {
        return;
      }
      const svg = document.createElementNS(NS, "svg");
      svg.setAttribute("class", "lucide");
      svg.setAttribute("viewBox", "0 0 24 24");
      svg.setAttribute("aria-hidden", node.getAttribute("aria-hidden") || "true");
      svg.setAttribute("width", width);
      svg.setAttribute("height", width);
      svg.innerHTML = body;
      node.replaceWith(svg);
    });
  }
  window.lucide = { createIcons: createIcons };
  createIcons({ attrs: { width: 16, height: 16 } });
})();

