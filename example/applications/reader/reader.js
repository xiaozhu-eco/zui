(function () {
  "use strict";

  var STORE = "xiaozhu-reader";
  var DB_NAME = "xiaozhu-reader";
  var books = [];
  var imports = [];
  var place = {};
  var marks = [];
  var hidden = {};
  var prefs = {
    size: "m",
    lead: "m",
    theme: "paper",
    font: "serif",
    mode: "scroll",
    sort: "recent",
    // The reading view opens on the page itself: the contents tray is a drawer, one tap
    // away, on every width.
    tocHidden: true,
    hintSeen: false
  };
  var cleared = false;
  var current = -1;
  var anchor = { ci: 0, pi: 0, ch: 0, t: 0 };
  var shown = -1;
  var clearArmed = false;
  var removeArmed = "";
  var removeTimer = 0;
  var saveTimer = 0;
  var findTimer = 0;
  var toastTimer = 0;
  var findQuery = "";
  var db = null;
  var useLocal = false;
  var reduceMotion = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  var wakeLock = null;
  var scrollLock = 0;
  var scrollScheduled = false;
  var windowFrom = 0;
  var windowTo = -1;
  var measured = {};
  var measureKey = "";
  var pageChapter = 0;
  var pageIndex = 0;
  var pageDir = 0;
  var pageState = { key: "", map: {} };
  var legacyScroll = {};
  var wheelLock = 0;
  var downX = 0;
  var downY = 0;
  var downT = 0;
  var swiped = false;
  var probe = null;
  var catalog = [];
  var loaded = {};
  var loadToken = 0;

  function $(id) {
    return document.getElementById(id);
  }

  function pageMode() {
    return prefs.mode === "page";
  }

  function textOf(id) {
    var node = $(id);
    return node ? node.textContent.replace(/^\n/, "").replace(/\s+$/, "") : "";
  }

  function clamp(n, min, max) {
    if (n < min) return min;
    if (n > max) return max;
    return n;
  }

  function toast(message) {
    var node = $("toast");
    if (!node) return;
    node.textContent = message;
    node.classList.add("show");
    clearTimeout(toastTimer);
    toastTimer = setTimeout(function () {
      node.classList.remove("show");
    }, 2200);
  }

  function normalizeHead(line) {
    return String(line || "")
      .replace(/^[\s\u3000]+|[\s\u3000]+$/g, "")
      .replace(/^[【\[（(]\s*/, "")
      .replace(/\s*[】\]）)]$/, "");
  }

  function cleanLine(rawLine) {
    return String(rawLine || "")
      .replace(/\u00a0/g, " ")
      .replace(/^[\s\u3000]+|[\s\u3000]+$/g, "")
      .replace(/[ \t\u3000]+/g, " ")
      .trim();
  }

  function isHeading(line) {
    var text = normalizeHead(cleanLine(line));
    if (!text || text.length > 80) return false;
    if (text.indexOf("。") >= 0) return false;
    if (/第\s*\d+\s*\/\s*\d+\s*页/.test(text)) return false;
    if (/https?:|www\.|\.com|\.vip|\.org|最新网址/.test(text)) return false;
    if (/^(序章|楔子|前言|序言|引子|尾声|后记|番外|终章|跋|附录)(\s|$|[：:.\d第（(【])/.test(text)) return true;
    if (text === "序" || text === "跋") return true;
    if (/^chapter(\s|\d)/i.test(text)) return true;
    if (/^第\s*[0-9零〇一二三四五六七八九十百千两]{1,12}\s*[章节回卷部集篇]/.test(text)) return true;
    if (/^[卷部集][0-9零〇一二三四五六七八九十百千]{1,8}(\s|$|[：:.])/.test(text)) return true;
    return false;
  }

  function parseCn(s) {
    if (/^\d+$/.test(s)) return parseInt(s, 10);
    var digit = { "零": 0, "〇": 0, "一": 1, "二": 2, "两": 2, "三": 3, "四": 4, "五": 5, "六": 6, "七": 7, "八": 8, "九": 9 };
    var unit = { "十": 10, "百": 100, "千": 1000, "万": 10000 };
    var acc = 0;
    var cur = 0;
    for (var i = 0; i < s.length; i++) {
      var ch = s.charAt(i);
      if (digit[ch] != null) cur = digit[ch];
      else if (unit[ch]) {
        if (cur === 0) cur = 1;
        if (unit[ch] === 10000) {
          acc = (acc + cur) * 10000;
          cur = 0;
        } else {
          acc += cur * unit[ch];
          cur = 0;
        }
      } else return null;
    }
    return acc + cur;
  }

  function headingInfo(line) {
    var text = normalizeHead(cleanLine(line));
    if (!isHeading(text)) return null;
    var matched = text.match(/^第\s*([0-9]+|[零〇一二三四五六七八九十百千两]+)\s*[章节回卷部集篇]\s*(.*)$/);
    if (!matched) return { title: text, n: null, rest: "" };
    return { title: text, n: parseCn(matched[1]), rest: (matched[2] || "").replace(/\s+/g, "") };
  }

  function sameChapter(info, lastNum, lastRest) {
    if (info.n == null || info.n !== lastNum) return false;
    if (!info.rest || !lastRest) return info.rest === lastRest;
    return info.rest === lastRest || info.rest.indexOf(lastRest) >= 0 || lastRest.indexOf(info.rest) >= 0;
  }

  function parseBook(key, raw) {
    var lines = String(raw || "").replace(/\r/g, "").split("\n");
    var title = String(key || "").replace(/\.(txt|text)$/i, "");
    var author = "";
    var chapters = [];
    var cur = null;
    var seenHead = false;
    var usedTitle = false;
    var chars = 0;
    var lastNum = null;
    var lastRest = "";
    lines.forEach(function (rawLine) {
      var line = cleanLine(rawLine);
      if (!line) return;
      if (!seenHead) {
        var named = line.match(/^(?:书名|标题)[:：]\s*(.*)$/);
        if (named) {
          if (named[1]) title = named[1];
          return;
        }
        var wrote = line.match(/^作者[:：]\s*(.*)$/);
        if (wrote) {
          author = wrote[1];
          return;
        }
        if (/简介/.test(line) && line.length <= 16) return;
        if (/最新网址|https?:|www\./.test(line)) return;
        if (!usedTitle && !isHeading(line) && line.length <= 24 && line.indexOf("，") < 0 && line.indexOf("。") < 0) {
          title = line;
          usedTitle = true;
          return;
        }
        usedTitle = true;
      }
      var info = headingInfo(line);
      if (info) {
        if (sameChapter(info, lastNum, lastRest)) return;
        cur = { title: info.title, paragraphs: [] };
        chapters.push(cur);
        lastNum = info.n;
        lastRest = info.rest;
        seenHead = true;
        return;
      }
      if (/最新网址|https?:\/\/|www\./.test(line)) return;
      if (!cur) cur = { title: "简介", paragraphs: [] };
      if (!chapters.length) chapters.push(cur);
      cur.paragraphs.push(line);
      chars += line.length;
    });
    if (!chapters.length) chapters.push({ title: "正文", paragraphs: [] });
    chapters.forEach(function (ch) {
      if (!ch.paragraphs.length) ch.paragraphs.push("（空）");
    });
    var offsets = [];
    var cursor = 0;
    chapters.forEach(function (ch) {
      offsets.push(cursor);
      ch.paragraphs.forEach(function (para) { cursor += String(para).length; });
    });
    return {
      key: key,
      title: title || key,
      author: author,
      chapters: chapters,
      offsets: offsets,
      chars: chars || cursor,
      chapterCount: chapters.length,
      imported: false
    };
  }

  function validBook(book) {
    if (!book || typeof book.key !== "string" || typeof book.title !== "string") return false;
    if (!Array.isArray(book.chapters) || !book.chapters.length) return false;
    return book.chapters.every(function (ch) {
      return ch && typeof ch.title === "string" && Array.isArray(ch.paragraphs);
    });
  }

  function charsOf(book) {
    if (typeof book.chars === "number") return book.chars;
    var n = 0;
    book.chapters.forEach(function (ch) {
      ch.paragraphs.forEach(function (p) { n += String(p).length; });
    });
    book.chars = n;
    return n;
  }

  function formatChars(n) {
    if (n >= 10000) {
      var wan = n / 10000;
      var text = wan >= 10 ? String(Math.round(wan)) : wan.toFixed(1).replace(/\.0$/, "");
      return text + " 万字";
    }
    return n + " 字";
  }

  function decodeBytes(buf) {
    var bytes = new Uint8Array(buf);
    if (bytes.length >= 2 && bytes[0] === 0xfe && bytes[1] === 0xff) {
      return new TextDecoder("utf-16be").decode(bytes);
    }
    if (bytes.length >= 2 && bytes[0] === 0xff && bytes[1] === 0xfe) {
      return new TextDecoder("utf-16le").decode(bytes);
    }
    var utf8 = bytes;
    if (bytes.length >= 3 && bytes[0] === 0xef && bytes[1] === 0xbb && bytes[2] === 0xbf) {
      utf8 = bytes.subarray(3);
    }
    try {
      return new TextDecoder("utf-8", { fatal: true }).decode(utf8);
    } catch (err) { /* try a Chinese legacy encoding */ }
    try {
      return new TextDecoder("gb18030").decode(bytes);
    } catch (err2) {
      return new TextDecoder("utf-8").decode(bytes);
    }
  }

  function readFile(file) {
    return new Promise(function (resolve) {
      if (file.size > 48 * 1024 * 1024) {
        resolve("");
        return;
      }
      var reader = new FileReader();
      reader.onload = function () { resolve(decodeBytes(reader.result)); };
      reader.onerror = function () { resolve(""); };
      reader.readAsArrayBuffer(file);
    });
  }

  function isBookFile(name) {
  const n = String(name || "").toLowerCase();
  return /\.(txt|text|epub|pdf)$/.test(n);
}
function isTxt(file) {
    var name = file && file.name ? file.name : "";
    return /\.(txt|text)$/i.test(name) || file.type === "text/plain";
  }

  function builtinBooks() {
    return [
      parseBook("lantern.txt", textOf("book-lantern")),
      parseBook("ferry.txt", textOf("book-ferry"))
    ];
  }

  function library() {
    var base = catalog.length ? catalog : (cleared ? [] : builtinBooks());
    var seen = {};
    var all = [];
    base.forEach(function (item) {
      if (!item || hidden[item.key]) return;
      seen[item.key] = true;
      all.push(loaded[item.key] || item);
    });
    imports.forEach(function (item) {
      if (!item || hidden[item.key]) return;
      var book = loaded[item.key] || item;
      if (seen[item.key]) {
        all = all.map(function (entry) { return entry.key === item.key ? book : entry; });
        return;
      }
      seen[item.key] = true;
      all.push(book);
    });
    all.sort(function (a, b) {
      if (prefs.sort === "title") return a.title.localeCompare(b.title, "zh");
      var at = place[a.key] && place[a.key].t ? place[a.key].t : 0;
      var bt = place[b.key] && place[b.key].t ? place[b.key].t : 0;
      if (at !== bt) return bt - at;
      return a.title.localeCompare(b.title, "zh");
    });
    return all;
  }

  function toneOf(title, used) {
    var n = 0;
    var s = title || "";
    for (var i = 0; i < s.length; i++) n = (n * 31 + s.charCodeAt(i)) >>> 0;
    var tone = n % 4;
    var guard = 0;
    while (used[tone] && guard < 4) {
      tone = (tone + 1) % 4;
      guard += 1;
    }
    used[tone] = true;
    return tone;
  }

  function clearHost(id) {
    var host = $(id);
    Array.prototype.slice.call(host.children).forEach(function (node) {
      if (node.tagName !== "TEMPLATE") node.remove();
    });
    return host;
  }

  function chapterCountOf(book) {
    if (book.chapters && book.chapters.length) return book.chapters.length;
    return book.chapterCount || 0;
  }

  function percentOf(book, pos) {
    if (!book || !book.chapters || !book.chapters.length || !pos) return pos && pos.pct ? pos.pct : 0;
    var ci = clamp(pos.ci || 0, 0, book.chapters.length - 1);
    var paras = book.chapters[ci].paragraphs;
    var pi = clamp(pos.pi || 0, 0, Math.max(0, paras.length - 1));
    var within = 0;
    for (var i = 0; i < pi; i++) within += String(paras[i]).length;
    if (paras[pi]) within += Math.min(String(paras[pi]).length, pos.ch || 0);
    var total = book.chars || 1;
    var start = book.offsets && book.offsets[ci] ? book.offsets[ci] : 0;
    var pct = Math.round((start + within) / total * 100);
    if (start + within > 0 && pct === 0) pct = 1;
    var lastC = book.chapters.length - 1;
    var lastP = book.chapters[lastC].paragraphs.length - 1;
    if (ci === lastC && pi === lastP) {
      var lastLen = String(book.chapters[lastC].paragraphs[lastP] || "").length;
      if ((pos.ch || 0) >= Math.max(0, lastLen - 1)) pct = 100;
    }
    return pct;
  }

  function cardWhere(book) {
    var pos = place[book.key];
    if (!pos || typeof pos !== "object") return "未读";
    if (book.chapters && book.chapters.length) {
      var pct = percentOf(book, pos);
      if (pct >= 100) return "已读完";
      var ch = book.chapters[pos.ci] || book.chapters[0];
      return pct + "% · " + (ch ? ch.title : "");
    }
    if ((pos.pct || 0) >= 100) return "已读完";
    if (pos.chapterTitle) return (pos.pct || 0) + "% · " + pos.chapterTitle;
    return "读过";
  }

  function cardPct(book) {
    var pos = place[book.key];
    if (!pos || typeof pos !== "object") return 0;
    if (book.chapters && book.chapters.length) return percentOf(book, pos);
    return pos.pct || 0;
  }

  function renderShelf() {
    var openKey = current >= 0 && books[current] ? books[current].key : "";
    books = library();
    if (openKey) {
      var next = -1;
      for (var i = 0; i < books.length; i++) {
        if (books[i].key === openKey) next = i;
      }
      if (next >= 0) current = next;
    }
    var query = ($("shelf_query").value || "").trim().toLowerCase();
    var host = clearHost("books");
    var tpl = host.querySelector("template");
    var usedTones = {};
    var shownCards = 0;
    books.forEach(function (book, index) {
      var blob = (book.title + " " + (book.author || "")).toLowerCase();
      if (query && blob.indexOf(query) < 0) return;
      var node = tpl.content.firstElementChild.cloneNode(true);
      node.id = "book" + index;
      node.dataset.index = String(index);
      var card = node.querySelector(".card");
      card.className = "card tone-" + toneOf(book.title, usedTones);
      node.querySelector(".cover-title").textContent = book.title;
      node.querySelector(".cover-author").textContent = book.author || "未署名";
      var count = chapterCountOf(book);
      node.querySelector(".card-meta").textContent = count
        ? count + " 章 · " + formatChars(charsOf(book))
        : "尚未打开";
      var pct = cardPct(book);
      node.querySelector(".card-fill").style.width = pct + "%";
      node.querySelector(".card-where").textContent = cardWhere(book);
      var remove = node.querySelector(".card-remove");
      remove.textContent = removeArmed === book.key ? "确认" : "移出";
      host.appendChild(node);
      shownCards += 1;
    });
    var shelf = $("shelf");
    var empty = books.length === 0 || shownCards === 0;
    shelf.classList.toggle("is-empty", empty);
    shelf.classList.remove("is-booting");
    if (!books.length) {
      $("empty_title").textContent = "书架还空着";
      $("empty_hint").textContent = "点右上角加号，导入一本 txt 小说。也可以把文件拖进来。";
    } else if (!shownCards) {
      $("empty_title").textContent = "没有匹配的书";
      $("empty_hint").textContent = "换个词，或者清空搜索。";
    }
    var resume = $("resume");
    var best = null;
    books.forEach(function (book) {
      var pos = place[book.key];
      if (!pos || typeof pos.t !== "number") return;
      if (!best || pos.t > best.pos.t) best = { book: book, pos: pos };
    });
    if (!best || query) {
      resume.hidden = true;
    } else {
      resume.hidden = false;
      resume.dataset.key = best.book.key;
      $("resume_title").textContent = best.book.title;
      $("resume_where").textContent = cardWhere(best.book);
    }
    var sortBtn = $("sort_btn");
    sortBtn.textContent = prefs.sort === "title" ? "书名" : "最近";
    sortBtn.setAttribute("aria-pressed", prefs.sort === "recent" ? "true" : "false");
  }

  function themeColor() {
    if (prefs.theme === "ink") return "#12151a";
    if (prefs.theme === "dusk") return "#2a221c";
    if (prefs.theme === "eye") return "#d5e0d2";
    return "#e6dfd0";
  }

  function applyThemeColor() {
    var meta = document.querySelector('meta[name="theme-color"]');
    if (meta) meta.setAttribute("content", themeColor());
  }

  function updateHint() {
    var hint = $("read_hint");
    if (!hint) return;
    hint.textContent = pageMode()
      ? "点左右翻页，点中间打开工具栏"
      : "轻点屏幕中间，打开工具栏";
  }

  function applyPrefs() {
    var size = $("size_" + (prefs.size || "m"));
    var lead = $("lead_" + (prefs.lead || "m"));
    var theme = $("theme_" + (prefs.theme || "paper"));
    var font = $("font_" + (prefs.font || "serif"));
    var mode = $("mode_" + (prefs.mode === "page" ? "page" : "scroll"));
    if (size) size.checked = true;
    if (lead) lead.checked = true;
    if (theme) theme.checked = true;
    if (font) font.checked = true;
    if (mode) mode.checked = true;
    setTocHidden(!!prefs.tocHidden);
    $("reader").classList.toggle("hint-seen", !!prefs.hintSeen);
    var toggle = document.querySelector(".toc-toggle");
    if (toggle) toggle.setAttribute("aria-expanded", prefs.tocHidden ? "false" : "true");
    syncSettingsButton();
    applyThemeColor();
    updateHint();
  }

  /* Aa reports whether the preferences panel is open, the way the tray toggle reports
     its own state. */
  function syncSettingsButton() {
    var button = document.querySelector(".settings-btn");
    if (button) button.setAttribute("aria-expanded", $("settings_open").checked ? "true" : "false");
  }

  function readPrefsFromRadios() {
    prefs.size = $("size_s").checked ? "s" : ($("size_l").checked ? "l" : "m");
    prefs.lead = $("lead_s").checked ? "s" : ($("lead_l").checked ? "l" : "m");
    if ($("theme_ink").checked) prefs.theme = "ink";
    else if ($("theme_eye").checked) prefs.theme = "eye";
    else if ($("theme_dusk").checked) prefs.theme = "dusk";
    else prefs.theme = "paper";
    prefs.font = $("font_sans").checked ? "sans" : "serif";
    prefs.mode = $("mode_page").checked ? "page" : "scroll";
  }

  function prefsNeedLayout(before) {
    return before.size !== prefs.size || before.lead !== prefs.lead || before.font !== prefs.font
      || before.mode !== prefs.mode;
  }

  function openDb() {
    return new Promise(function (resolve, reject) {
      if (!window.indexedDB) {
        reject(new Error("no idb"));
        return;
      }
      var req = indexedDB.open(DB_NAME, 2);
      req.onupgradeneeded = function () {
        var base = req.result;
        if (!base.objectStoreNames.contains("meta")) base.createObjectStore("meta");
        if (!base.objectStoreNames.contains("books")) base.createObjectStore("books");
        if (!base.objectStoreNames.contains("texts")) base.createObjectStore("texts");
      };
      req.onsuccess = function () { resolve(req.result); };
      req.onerror = function () { reject(req.error); };
    });
  }

  function idbGet(store, key) {
    return new Promise(function (resolve, reject) {
      var tx = db.transaction(store, "readonly");
      var req = tx.objectStore(store).get(key);
      req.onsuccess = function () { resolve(req.result); };
      req.onerror = function () { reject(req.error); };
    });
  }

  function idbPut(store, value, key) {
    return new Promise(function (resolve, reject) {
      var tx = db.transaction(store, "readwrite");
      tx.objectStore(store).put(value, key);
      tx.oncomplete = function () { resolve(); };
      tx.onerror = function () { reject(tx.error); };
    });
  }

  function idbDel(store, key) {
    return new Promise(function (resolve, reject) {
      if (!db) {
        resolve();
        return;
      }
      var tx = db.transaction(store, "readwrite");
      tx.objectStore(store).delete(key);
      tx.oncomplete = function () { resolve(); };
      tx.onerror = function () { reject(tx.error); };
    });
  }

  function idbAll(store) {
    return new Promise(function (resolve, reject) {
      var tx = db.transaction(store, "readonly");
      var req = tx.objectStore(store).getAll();
      req.onsuccess = function () { resolve(req.result || []); };
      req.onerror = function () { reject(req.error); };
    });
  }

  function idbClear(store) {
    return new Promise(function (resolve, reject) {
      if (!db) {
        resolve();
        return;
      }
      var tx = db.transaction(store, "readwrite");
      tx.objectStore(store).clear();
      tx.oncomplete = function () { resolve(); };
      tx.onerror = function () { reject(tx.error); };
    });
  }

  function lightOf(book) {
    return {
      key: book.key,
      file: book.file || book.key,
      title: book.title,
      author: book.author || "",
      chapterCount: chapterCountOf(book),
      chars: typeof book.chars === "number" ? book.chars : 0,
      imported: !!book.imported,
      source: book.source || (book.imported ? "import" : "file")
    };
  }

  function metaSnapshot() {
    return {
      cleared: cleared,
      hidden: hidden,
      prefs: prefs,
      place: place,
      marks: marks,
      imports: imports.map(lightOf)
    };
  }

  function applyRoot(root, storedBooks) {
    if (root && typeof root === "object") {
      cleared = !!root.cleared;
      hidden = root.hidden && typeof root.hidden === "object" ? root.hidden : {};
      if (root.prefs && typeof root.prefs === "object") {
        ["size", "lead", "theme", "font", "mode", "sort"].forEach(function (key) {
          if (root.prefs[key]) prefs[key] = root.prefs[key];
        });
        prefs.tocHidden = !!root.prefs.tocHidden;
        prefs.hintSeen = !!root.prefs.hintSeen;
      }
      place = root.place && typeof root.place === "object" ? root.place : {};
      marks = Array.isArray(root.marks) ? root.marks.filter(function (mark) {
        return mark && typeof mark.key === "string" && typeof mark.id === "string";
      }) : [];
    }
    Object.keys(place).forEach(function (key) {
      if (typeof place[key] === "number") {
        legacyScroll[key] = place[key];
        delete place[key];
      }
    });
    var storedImports = root && Array.isArray(root.imports) ? root.imports : (storedBooks || []);
    imports = storedImports.map(function (item) {
      if (!item || typeof item.key !== "string") return null;
      var book = {
        key: item.key,
        file: item.file || item.key,
        title: item.title || item.key,
        author: item.author || "",
        chapterCount: item.chapterCount || (item.chapters ? item.chapters.length : 0),
        chars: item.chars || 0,
        chapters: item.chapters && item.chapters.length ? item.chapters : null,
        offsets: item.offsets || null,
        imported: true,
        source: "import"
      };
      if (book.chapters) loaded[book.key] = book;
      return book.chapters ? lightOf(book) : book;
    }).filter(function (item) { return !!item; });
    if (!root && window.matchMedia("(prefers-color-scheme: dark)").matches) prefs.theme = "ink";
  }

  function readLegacy() {
    try {
      var raw = localStorage.getItem(STORE);
      if (!raw) return null;
      return JSON.parse(raw);
    } catch (err) {
      return null;
    }
  }

  function saveLocal() {
    try {
      localStorage.setItem(STORE, JSON.stringify({
        cleared: cleared,
        hidden: hidden,
        prefs: prefs,
        place: place,
        marks: marks,
        imports: imports.map(lightOf)
      }));
    } catch (err) {
      toast("这本书太大，浏览器没能存下");
    }
  }

  function saveMeta() {
    if (useLocal || !db) {
      saveLocal();
      return Promise.resolve();
    }
    return idbPut("meta", metaSnapshot(), "root").catch(function () {
      useLocal = true;
      saveLocal();
    });
  }

  function saveText(key, text) {
    if (useLocal || !db || !text) return Promise.resolve();
    return idbPut("texts", text, key).catch(function () {});
  }

  function stubFromCatalog(item) {
    var file = item.file || item;
    return {
      key: file,
      file: file,
      title: item.title || String(file).replace(/\.(txt|text)$/i, ""),
      author: item.author || "",
      chapterCount: item.chapters || item.chapterCount || 0,
      chars: item.chars || 0,
      chapters: null,
      source: "file"
    };
  }

  function loadCatalog() {
    var listed = fetch("books/catalog.json", { cache: "no-cache" }).then(function (res) {
      if (!res.ok) throw new Error("catalog");
      return res.json();
    }).catch(function () { return []; });
    var found = fetch("books/").then(function (res) {
      if (!res.ok) return [];
      return res.text();
    }).then(function (html) {
      var names = [];
      var pattern = /href="([^"]+\.(?:txt|text))"/gi;
      var match;
      while ((match = pattern.exec(html))) {
        var file = decodeURIComponent(match[1].replace(/^\.\//, ""));
        if (file.indexOf("/") >= 0 || file.indexOf("\\") >= 0) continue;
        if (names.indexOf(file) < 0) names.push(file);
      }
      return names;
    }).catch(function () { return []; });
    return Promise.all([listed, found]).then(function (rows) {
      var items = Array.isArray(rows[0]) ? rows[0].slice() : [];
      rows[1].forEach(function (file) {
        var known = false;
        for (var i = 0; i < items.length; i++) {
          if (items[i].file === file) known = true;
        }
        if (!known) items.push({ file: file });
      });
      catalog = items.map(stubFromCatalog).filter(function (item) { return item.key; });
    });
  }

  function loadText(item) {
    var cached = db ? idbGet("texts", item.key).catch(function () { return ""; }) : Promise.resolve("");
    return cached.then(function (text) {
      if (typeof text === "string" && text) return text;
      return fetch("books/" + encodeURIComponent(item.file || item.key)).then(function (res) {
        if (!res.ok) throw new Error("missing");
        return res.arrayBuffer();
      }).then(function (buf) {
        return decodeBytes(buf);
      });
    });
  }

  function loadState() {
    var legacy = readLegacy();
    return openDb().then(function (opened) {
      db = opened;
      return Promise.all([idbGet("meta", "root"), idbAll("books")]);
    }).then(function (rows) {
      var root = rows[0];
      var stored = rows[1];
      if (!root && legacy) {
        applyRoot(legacy, legacy.imports || []);
        return saveMeta().then(function () {
          try { localStorage.removeItem(STORE); } catch (err) { /* ignore */ }
        });
      }
      applyRoot(root, null);
    }).catch(function () {
      useLocal = true;
      db = null;
      applyRoot(legacy, legacy && legacy.imports);
    });
  }

  function scheduleSave() {
    clearTimeout(saveTimer);
    saveTimer = setTimeout(saveMeta, 400);
  }

  function normalizeAnchor(book, pos) {
    var ci = clamp(pos && pos.ci ? pos.ci : 0, 0, book.chapters.length - 1);
    var paras = book.chapters[ci].paragraphs;
    var pi = clamp(pos && pos.pi ? pos.pi : 0, 0, Math.max(0, paras.length - 1));
    var len = String(paras[pi] || "").length;
    var ch = clamp(pos && pos.ch ? pos.ch : 0, 0, len);
    return { ci: ci, pi: pi, ch: ch, t: pos && pos.t ? pos.t : Date.now() };
  }

  function chapterNode(book, index, withNext) {
    var ch = book.chapters[index];
    var article = document.createElement("article");
    article.className = "chapter" + (index > 0 ? " is-follow" : "");
    article.id = "chapter-" + index;
    article.dataset.index = String(index);
    var heading = document.createElement("h2");
    heading.className = "chapter-title";
    heading.textContent = ch.title;
    article.appendChild(heading);
    var frag = document.createDocumentFragment();
    ch.paragraphs.forEach(function (text, pi) {
      var para = document.createElement("p");
      para.className = "para";
      para.dataset.ci = String(index);
      para.dataset.pi = String(pi);
      para.dataset.ch = "0";
      para.textContent = text;
      frag.appendChild(para);
    });
    article.appendChild(frag);
    // Scroll mode rebuilds #flow and drops the static #chapter_next. Keep one
    // button on the chapter being read, the same control the engine paints.
    if (withNext && index + 1 < book.chapters.length) {
      var button = document.createElement("button");
      button.type = "button";
      button.id = "chapter_next";
      button.className = "chapter-next";
      button.setAttribute("onclick", "on_next_chapter");
      button.textContent = "下一章 · " + book.chapters[index + 1].title;
      article.appendChild(button);
    }
    return article;
  }

  function shouldWindow(book) {
    var paras = 0;
    book.chapters.forEach(function (ch) { paras += ch.paragraphs.length; });
    return book.chapters.length > 12 || paras > 350 || charsOf(book) > 80000;
  }

  function viewWidth() {
    var wrap = $("flow_wrap");
    var cs = getComputedStyle(wrap);
    var pad = (parseFloat(cs.paddingLeft) || 0) + (parseFloat(cs.paddingRight) || 0);
    return Math.max(120, Math.round(wrap.clientWidth - pad));
  }

  function pageHeight() {
    return Math.max(160, $("flow_wrap").clientHeight);
  }

  function fallbackPer() {
    var cs = getComputedStyle($("flow"));
    var font = parseFloat(cs.fontSize) || 19;
    var lead = parseFloat(cs.lineHeight);
    if (!lead || lead < font) lead = font * 1.9;
    var width = Math.max(160, $("flow").clientWidth || viewWidth());
    return lead / Math.max(8, width / font);
  }

  function chapterHeight(book, index) {
    if (measured[index]) return measured[index];
    var sample = 0;
    var sampleChars = 0;
    Object.keys(measured).forEach(function (key) {
      var i = parseInt(key, 10);
      if (!book.chapters[i]) return;
      var chars = 0;
      book.chapters[i].paragraphs.forEach(function (p) { chars += String(p).length; });
      sample += measured[key];
      sampleChars += chars;
    });
    var chars = 0;
    book.chapters[index].paragraphs.forEach(function (p) { chars += String(p).length; });
    var per = sampleChars ? sample / sampleChars : fallbackPer();
    return Math.max(64, Math.round(chars * per + 48));
  }

  function syncPads(book) {
    var top = $("pad_top");
    var bot = $("pad_bot");
    if (!top || !bot) return;
    var above = 0;
    var below = 0;
    for (var i = 0; i < windowFrom; i++) above += chapterHeight(book, i);
    for (var j = windowTo + 1; j < book.chapters.length; j++) below += chapterHeight(book, j);
    top.style.height = above + "px";
    bot.style.height = below + "px";
  }

  function measureArticles() {
    var nodes = $("flow").querySelectorAll("article.chapter");
    Array.prototype.forEach.call(nodes, function (node) {
      measured[node.dataset.index] = node.offsetHeight;
    });
  }

  function renderWindow(ci) {
    var book = books[current];
    var full = !shouldWindow(book);
    windowFrom = full ? 0 : ci;
    windowTo = full ? book.chapters.length - 1 : Math.min(book.chapters.length - 1, ci + 1);
    var flow = $("flow");
    flow.textContent = "";
    var padTop = document.createElement("div");
    padTop.id = "pad_top";
    var padBot = document.createElement("div");
    padBot.id = "pad_bot";
    flow.appendChild(padTop);
    for (var i = windowFrom; i <= windowTo; i++) flow.appendChild(chapterNode(book, i, i === ci));
    flow.appendChild(padBot);
    measureArticles();
    syncPads(book);
  }

  function topInWrap(node) {
    var wrap = $("flow_wrap");
    return node.getBoundingClientRect().top - wrap.getBoundingClientRect().top + wrap.scrollTop;
  }

  function setScrollTop(top, smooth) {
    var wrap = $("flow_wrap");
    var next = Math.max(0, top);
    if (smooth && !reduceMotion) {
      wrap.scrollTo({ top: next, behavior: "smooth" });
      return;
    }
    scrollLock += 1;
    wrap.scrollTop = next;
    scrollLock -= 1;
  }

  function paraNode(ci, pi) {
    return $("flow").querySelector('.para[data-ci="' + ci + '"][data-pi="' + pi + '"]');
  }

  function ensureChapter(ci) {
    if (ci >= windowFrom && ci <= windowTo && $("chapter-" + ci)) return;
    renderWindow(ci);
  }

  function scrollToAnchor(pos, smooth) {
    var book = books[current];
    var target = normalizeAnchor(book, pos);
    ensureChapter(target.ci);
    var para = paraNode(target.ci, target.pi);
    var node = para || $("chapter-" + target.ci);
    if (!node) return;
    var top = topInWrap(node);
    if (para && target.ch && para.offsetHeight) {
      var len = para.textContent.length || 1;
      top += para.offsetHeight * Math.min(0.95, target.ch / len);
    }
    setScrollTop(top - 12, smooth);
  }

  function anchorFromScroll() {
    var wrap = $("flow_wrap");
    var edge = wrap.getBoundingClientRect().top + 28;
    var nodes = $("flow").querySelectorAll(".para");
    if (!nodes.length) return null;
    var pick = nodes[0];
    for (var i = 0; i < nodes.length; i++) {
      if (nodes[i].getBoundingClientRect().bottom >= edge) {
        pick = nodes[i];
        break;
      }
      pick = nodes[i];
    }
    var rect = pick.getBoundingClientRect();
    var ratio = 0;
    if (rect.top < edge && pick.offsetHeight > 0) {
      ratio = (edge - rect.top) / pick.offsetHeight;
      if (ratio < 0) ratio = 0;
      if (ratio > 0.98) ratio = 0.98;
    }
    return {
      ci: parseInt(pick.dataset.ci, 10) || 0,
      pi: parseInt(pick.dataset.pi, 10) || 0,
      ch: Math.floor(ratio * (pick.textContent.length || 1)),
      t: Date.now()
    };
  }

  function prependIfNeeded(book) {
    if (windowFrom <= 0) return false;
    var wrap = $("flow_wrap");
    var pad = $("pad_top");
    if (!pad || wrap.scrollTop > pad.offsetHeight + 900) return false;
    var index = windowFrom - 1;
    var est = chapterHeight(book, index);
    var node = chapterNode(book, index);
    pad.after(node);
    var actual = node.offsetHeight;
    measured[index] = actual;
    windowFrom = index;
    syncPads(book);
    var delta = actual - est;
    if (delta) setScrollTop(wrap.scrollTop + delta, false);
    return true;
  }

  function appendIfNeeded(book) {
    if (windowTo >= book.chapters.length - 1) return false;
    var wrap = $("flow_wrap");
    var gap = $("flow").scrollHeight - (wrap.scrollTop + wrap.clientHeight);
    if (gap > 1400) return false;
    var index = windowTo + 1;
    var node = chapterNode(book, index);
    $("pad_bot").before(node);
    measured[index] = node.offsetHeight;
    windowTo = index;
    syncPads(book);
    return true;
  }

  function expandWindow() {
    if (current < 0 || pageMode()) return;
    var book = books[current];
    var guard = 0;
    while (guard < 4 && prependIfNeeded(book)) guard += 1;
    guard = 0;
    while (guard < 4 && appendIfNeeded(book)) guard += 1;
  }

  function syncMeasureKey() {
    var key = [prefs.size, prefs.lead, prefs.font, $("flow_wrap").clientWidth].join("|");
    if (key !== measureKey) {
      measureKey = key;
      measured = {};
    }
  }

  function layoutScroll(pos) {
    syncMeasureKey();
    var book = books[current];
    var key = book.key;
    if (legacyScroll[key] != null && !shouldWindow(book)) {
      renderWindow(0);
      setScrollTop(legacyScroll[key], false);
      delete legacyScroll[key];
      anchor = anchorFromScroll() || normalizeAnchor(book, { ci: 0, pi: 0, ch: 0 });
      return;
    }
    var target = normalizeAnchor(book, pos);
    renderWindow(target.ci);
    scrollToAnchor(target, false);
    anchor = anchorFromScroll() || target;
  }

  function renderItem(item) {
    if (item.kind === "h2") {
      var heading = document.createElement("h2");
      heading.className = "chapter-title";
      heading.textContent = item.text;
      return heading;
    }
    if (item.kind === "p") {
      var para = document.createElement("p");
      para.className = item.cont ? "para para-cont" : "para";
      para.dataset.ci = String(item.ci);
      para.dataset.pi = String(item.pi);
      para.dataset.ch = String(item.ch || 0);
      para.textContent = item.text;
      return para;
    }
    if (item.kind === "next") {
      var button = document.createElement("button");
      button.type = "button";
      button.className = "chapter-next";
      button.setAttribute("onclick", "on_next_chapter");
      button.textContent = "下一章 · " + item.text;
      return button;
    }
    var end = document.createElement("p");
    end.className = "chapter-end";
    end.textContent = "全书完";
    return end;
  }

  function ensureProbe() {
    if (probe) return probe;
    probe = document.createElement("div");
    probe.className = "flow page page-probe";
    probe.setAttribute("aria-hidden", "true");
    document.body.appendChild(probe);
    return probe;
  }

  function soften(text, cut) {
    if (cut >= text.length || cut < 16) return cut;
    var slice = text.slice(cut - 16, cut);
    for (var i = slice.length - 1; i >= 0; i--) {
      if ("，。！？、；：,.!?;".indexOf(slice.charAt(i)) >= 0) return cut - 16 + i + 1;
    }
    return cut;
  }

  function paginate(chapter, ci) {
    var book = books[current];
    var box = ensureProbe();
    var limit = pageHeight();
    box.style.width = viewWidth() + "px";
    box.textContent = "";
    var pages = [];
    var currentItems = [];

    function overflow() {
      return box.scrollHeight > limit + 1;
    }
    function pushCurrent() {
      if (!currentItems.length) return;
      pages.push(currentItems);
      currentItems = [];
      box.textContent = "";
    }
    function addWhole(item) {
      var node = renderItem(item);
      box.appendChild(node);
      if (!overflow()) {
        currentItems.push(item);
        return true;
      }
      box.removeChild(node);
      return false;
    }
    function fitCut(item) {
      var text = item.text;
      var lo = 1;
      var hi = text.length - 1;
      var best = 1;
      while (lo <= hi) {
        var mid = (lo + hi) >> 1;
        box.textContent = "";
        box.appendChild(renderItem({
          kind: "p",
          text: text.slice(0, mid),
          ci: item.ci,
          pi: item.pi,
          ch: item.ch,
          cont: item.cont
        }));
        if (!overflow()) {
          best = mid;
          lo = mid + 1;
        } else {
          hi = mid - 1;
        }
      }
      box.textContent = "";
      var cut = soften(text, best);
      if (cut < 1 || cut >= text.length) cut = best;
      box.appendChild(renderItem({
        kind: "p",
        text: text.slice(0, cut),
        ci: item.ci,
        pi: item.pi,
        ch: item.ch,
        cont: item.cont
      }));
      if (overflow()) cut = best;
      box.textContent = "";
      if (cut < text.length && text.charCodeAt(cut) >= 0xdc00 && text.charCodeAt(cut) <= 0xdfff) cut -= 1;
      if (cut < 1) cut = 1;
      return cut;
    }
    function addParagraph(item) {
      var rest = item;
      var guard = 0;
      while (rest && rest.text && guard < 5000) {
        guard += 1;
        if (addWhole(rest)) return;
        if (currentItems.length) {
          pushCurrent();
          continue;
        }
        var cut = fitCut(rest);
        if (cut >= rest.text.length) {
          currentItems.push(rest);
          box.appendChild(renderItem(rest));
          return;
        }
        currentItems.push({
          kind: "p",
          text: rest.text.slice(0, cut),
          ci: rest.ci,
          pi: rest.pi,
          ch: rest.ch,
          cont: rest.cont
        });
        box.appendChild(renderItem(currentItems[currentItems.length - 1]));
        pushCurrent();
        rest = {
          kind: "p",
          text: rest.text.slice(cut),
          ci: rest.ci,
          pi: rest.pi,
          ch: rest.ch + cut,
          cont: true
        };
      }
    }

    var head = { kind: "h2", text: chapter.title };
    if (!addWhole(head)) {
      currentItems.push(head);
      box.appendChild(renderItem(head));
    }
    chapter.paragraphs.forEach(function (text, pi) {
      addParagraph({ kind: "p", text: String(text), ci: ci, pi: pi, ch: 0, cont: false });
    });
    var tail = ci < book.chapters.length - 1
      ? { kind: "next", text: book.chapters[ci + 1].title, to: ci + 1 }
      : { kind: "end" };
    if (!addWhole(tail)) {
      pushCurrent();
      currentItems.push(tail);
      box.appendChild(renderItem(tail));
    }
    pushCurrent();
    box.textContent = "";
    return pages.length ? pages : [[{ kind: "end" }]];
  }

  function pagesFor(ci) {
    var key = [prefs.size, prefs.lead, prefs.font, viewWidth(), pageHeight()].join("|");
    if (pageState.key !== key) pageState = { key: key, map: {} };
    if (!pageState.map[ci]) pageState.map[ci] = paginate(books[current].chapters[ci], ci);
    return pageState.map[ci];
  }

  function pageForAnchor(pages, pos) {
    var fallback = 0;
    if (!pos) return 0;
    for (var i = 0; i < pages.length; i++) {
      var items = pages[i];
      for (var j = 0; j < items.length; j++) {
        var item = items[j];
        if (item.kind !== "p" || item.ci !== pos.ci || item.pi !== pos.pi) continue;
        var start = item.ch || 0;
        var end = start + item.text.length;
        fallback = i;
        if ((pos.ch || 0) >= start && (pos.ch || 0) < end) return i;
      }
    }
    return fallback;
  }

  function anchorFromItems(items, ci) {
    for (var i = 0; i < items.length; i++) {
      if (items[i].kind === "p") {
        return { ci: items[i].ci, pi: items[i].pi, ch: items[i].ch || 0, t: Date.now() };
      }
    }
    return { ci: ci, pi: 0, ch: 0, t: Date.now() };
  }

  function paintPage(items, dir) {
    var flow = $("flow");
    flow.textContent = "";
    var page = document.createElement("div");
    page.className = "page";
    if (!reduceMotion && dir) page.classList.add(dir < 0 ? "from-left" : "from-right");
    items.forEach(function (item) { page.appendChild(renderItem(item)); });
    flow.appendChild(page);
    if (findQuery) paintHits(findQuery);
  }

  function showPage(ci, spec, dir) {
    var book = books[current];
    pageChapter = clamp(ci, 0, book.chapters.length - 1);
    var pages = pagesFor(pageChapter);
    if (spec === "end") pageIndex = pages.length - 1;
    else if (typeof spec === "number") pageIndex = clamp(spec, 0, pages.length - 1);
    else pageIndex = pageForAnchor(pages, spec);
    paintPage(pages[pageIndex], dir || 0);
    anchor = anchorFromItems(pages[pageIndex], pageChapter);
    place[book.key] = anchor;
  }

  function layoutPages(pos, dir) {
    pageState = { key: "", map: {} };
    var book = books[current];
    var target = normalizeAnchor(book, pos);
    showPage(target.ci, target, dir || 0);
  }

  function relayout(keep) {
    if (current < 0 || !$("reader").classList.contains("open")) return;
    var wrap = $("flow_wrap");
    if (wrap.clientWidth < 40 || wrap.clientHeight < 40) {
      requestAnimationFrame(function () { relayout(keep); });
      return;
    }
    var pos = keep ? anchor : { ci: 0, pi: 0, ch: 0 };
    if (pageMode()) layoutPages(pos, 0);
    else layoutScroll(pos);
    syncChrome();
    if (findQuery) paintHits(findQuery);
  }

  function remember() {
    if (current < 0 || !books[current] || !books[current].chapters) return;
    var next = pageMode() ? anchor : anchorFromScroll();
    if (!next) return;
    next.t = Date.now();
    anchor = next;
    var book = books[current];
    var chapter = book.chapters[next.ci];
    place[book.key] = {
      ci: next.ci,
      pi: next.pi,
      ch: next.ch,
      t: next.t,
      pct: percentOf(book, next),
      chapterTitle: chapter ? chapter.title : ""
    };
    syncChrome();
  }

  function setCurrent(index) {
    var nodes = document.querySelectorAll("#chapters .ch");
    Array.prototype.forEach.call(nodes, function (node, i) {
      var on = i === index;
      node.classList.toggle("current", on);
      if (on) node.setAttribute("aria-current", "true");
      else node.removeAttribute("aria-current");
    });
    var node = nodes[index];
    var list = $("chapters");
    if (!node || !list) return;
    var listRect = list.getBoundingClientRect();
    var rect = node.getBoundingClientRect();
    if (rect.top < listRect.top) list.scrollTop -= listRect.top - rect.top + 8;
    else if (rect.bottom > listRect.bottom) list.scrollTop += rect.bottom - listRect.bottom + 8;
  }

  function updateCurrent() {
    if (pageMode()) {
      if (pageChapter !== shown) {
        shown = pageChapter;
        setCurrent(pageChapter);
      }
      return;
    }
    var nodes = $("flow").querySelectorAll(".chapter");
    if (!nodes.length) return;
    var edge = $("flow_wrap").getBoundingClientRect().top + 56;
    var index = windowFrom;
    for (var i = 0; i < nodes.length; i++) {
      if (nodes[i].getBoundingClientRect().top <= edge) index = parseInt(nodes[i].dataset.index, 10) || 0;
    }
    if (index === shown) return;
    shown = index;
    setCurrent(index);
  }

  function nearMark(pos) {
    if (current < 0 || !pos) return null;
    var key = books[current].key;
    for (var i = 0; i < marks.length; i++) {
      var mark = marks[i];
      if (mark.key === key && mark.ci === pos.ci && mark.pi === pos.pi && Math.abs((mark.ch || 0) - (pos.ch || 0)) < 24) {
        return mark;
      }
    }
    return null;
  }

  function syncChrome() {
    if (current < 0 || !books[current]) return;
    var book = books[current];
    var pos = anchor;
    var chapter = book.chapters[pos.ci] || book.chapters[0];
    var pct = percentOf(book, pos);
    $("status_chapter").textContent = chapter ? chapter.title : "";
    var progress = pct + "%";
    if (pageMode()) {
      var pages = pageState.map[pageChapter];
      var total = pages ? pages.length : 1;
      progress = (pageIndex + 1) + "/" + total + " · " + pct + "%";
    }
    $("status_progress").textContent = progress;
    $("status_fill").style.width = pct + "%";
    var marked = !!nearMark(pos);
    var button = $("bookmark_btn");
    button.classList.toggle("is-on", marked);
    button.setAttribute("aria-pressed", marked ? "true" : "false");
    document.title = (chapter ? chapter.title + " · " : "") + book.title + " · 小竹阅读";
    updateCurrent();
    renderMarks();
  }

  function filterToc() {
    var input = $("toc_query");
    if (!input) return;
    var query = input.value.trim();
    var nodes = document.querySelectorAll("#chapters .ch");
    Array.prototype.forEach.call(nodes, function (node) {
      node.hidden = !!(query && node.textContent.indexOf(query) < 0);
    });
  }

  function renderToc(book) {
    var host = clearHost("chapters");
    var tpl = host.querySelector("template");
    var frag = document.createDocumentFragment();
    book.chapters.forEach(function (ch, index) {
      var node = tpl.content.firstElementChild.cloneNode(true);
      node.id = "ch" + index;
      node.setAttribute("data-index", String(index));
      node.textContent = ch.title;
      frag.appendChild(node);
    });
    host.appendChild(frag);
    var label = document.querySelector(".toc-head span");
    if (label) label.textContent = "目录 · " + book.chapters.length;
    shown = -1;
    filterToc();
  }

  function renderMarks() {
    var host = $("marks");
    if (!host) return;
    host.textContent = "";
    var key = current >= 0 && books[current] ? books[current].key : "";
    var list = marks.filter(function (mark) { return mark.key === key; });
    $("marks_empty").hidden = list.length > 0;
    list.slice().sort(function (a, b) { return b.t - a.t; }).forEach(function (mark) {
      var row = document.createElement("div");
      row.className = "mark-row";
      var button = document.createElement("button");
      button.type = "button";
      button.className = "mark";
      button.setAttribute("onclick", "on_mark");
      button.dataset.id = mark.id;
      var title = document.createElement("span");
      title.className = "mark-title";
      title.textContent = mark.label || "书签";
      var snip = document.createElement("span");
      snip.className = "mark-snip";
      snip.textContent = mark.snip || "";
      button.appendChild(title);
      button.appendChild(snip);
      var del = document.createElement("button");
      del.type = "button";
      del.className = "mark-del";
      del.setAttribute("onclick", "on_unmark");
      del.dataset.id = mark.id;
      del.textContent = "删除";
      row.appendChild(button);
      row.appendChild(del);
      host.appendChild(row);
    });
  }

  function locate(text, query) {
    if (!query) return -1;
    var hay = text;
    var needle = query;
    if (/^[\u0000-\u007f]+$/.test(query)) {
      hay = text.toLowerCase();
      needle = query.toLowerCase();
    }
    return hay.indexOf(needle);
  }

  function paintHits(query) {
    var nodes = $("flow").querySelectorAll(".para");
    Array.prototype.forEach.call(nodes, function (para) {
      if (para.querySelector("mark")) {
        var plain = para.textContent;
        para.textContent = plain;
      }
      if (!query) return;
      var text = para.textContent;
      var ranges = [];
      var hay = text;
      var needle = query;
      if (/^[\u0000-\u007f]+$/.test(query)) {
        hay = text.toLowerCase();
        needle = query.toLowerCase();
      }
      var from = 0;
      while (from < hay.length) {
        var at = hay.indexOf(needle, from);
        if (at < 0) break;
        ranges.push(at);
        from = at + needle.length;
      }
      if (!ranges.length) return;
      para.textContent = "";
      var cursor = 0;
      ranges.forEach(function (at) {
        if (at > cursor) para.appendChild(document.createTextNode(text.slice(cursor, at)));
        var mark = document.createElement("mark");
        mark.className = "hit-mark";
        mark.textContent = text.slice(at, at + needle.length);
        para.appendChild(mark);
        cursor = at + needle.length;
      });
      if (cursor < text.length) para.appendChild(document.createTextNode(text.slice(cursor)));
    });
  }

  function runFind(query) {
    findQuery = query.trim();
    var host = $("find_hits");
    var count = $("find_count");
    host.textContent = "";
    if (!findQuery || current < 0) {
      count.textContent = findQuery ? "" : "输入词语，查找这一本的正文。";
      paintHits("");
      return;
    }
    var book = books[current];
    var hits = [];
    book.chapters.forEach(function (ch, ci) {
      ch.paragraphs.forEach(function (text, pi) {
        var at = locate(String(text), findQuery);
        if (at >= 0) hits.push({ ci: ci, pi: pi, ch: at, title: ch.title, text: String(text) });
      });
    });
    count.textContent = hits.length ? hits.length + " 处" + (hits.length > 100 ? "，这里列出前 100 处" : "") : "没有找到";
    hits.slice(0, 100).forEach(function (hit) {
      var button = document.createElement("button");
      button.type = "button";
      button.className = "hit";
      button.setAttribute("onclick", "on_hit");
      button.dataset.ci = String(hit.ci);
      button.dataset.pi = String(hit.pi);
      button.dataset.ch = String(hit.ch);
      var title = document.createElement("span");
      title.className = "hit-title";
      title.textContent = hit.title;
      var snip = document.createElement("span");
      snip.className = "hit-snip";
      var start = Math.max(0, hit.ch - 14);
      var end = Math.min(hit.text.length, hit.ch + findQuery.length + 18);
      snip.textContent = (start ? "…" : "") + hit.text.slice(start, end) + (end < hit.text.length ? "…" : "");
      button.appendChild(title);
      button.appendChild(snip);
      host.appendChild(button);
    });
    paintHits(findQuery);
  }

  function goTo(pos, smooth) {
    if (current < 0) return;
    anchor = normalizeAnchor(books[current], pos);
    if (pageMode()) layoutPages(anchor, 0);
    else scrollToAnchor(anchor, smooth && !shouldWindow(books[current]));
    place[books[current].key] = anchor;
    if (findQuery) paintHits(findQuery);
    syncChrome();
    scheduleSave();
  }

  function turn(dir) {
    if (current < 0) return;
    if (!pageMode()) {
      var book = books[current];
      var ci = clamp((anchor.ci || 0) + dir, 0, book.chapters.length - 1);
      if (ci === anchor.ci) return;
      goTo({ ci: ci, pi: 0, ch: 0 }, true);
      return;
    }
    var pages = pagesFor(pageChapter);
    var next = pageIndex + dir;
    if (next >= 0 && next < pages.length) {
      pageIndex = next;
      paintPage(pages[pageIndex], dir);
      anchor = anchorFromItems(pages[pageIndex], pageChapter);
      place[books[current].key] = anchor;
      syncChrome();
      scheduleSave();
      return;
    }
    var ci = pageChapter + dir;
    if (ci < 0 || ci >= books[current].chapters.length) return;
    showPage(ci, dir < 0 ? "end" : 0, dir);
    syncChrome();
    scheduleSave();
  }

  function hashUrl(key) {
    if (!key) return location.pathname + location.search;
    return "#book=" + encodeURIComponent(key);
  }

  function presentBook(index, historyMode) {
    if (index < 0 || index >= books.length || !books[index].chapters) return;
    var opening = current < 0;
    current = index;
    var book = books[index];
    $("book_title").textContent = book.title;
    findQuery = "";
    if ($("find_query")) $("find_query").value = "";
    $("find_hits").textContent = "";
    $("find_count").textContent = "输入词语，查找这一本的正文。";
    if ($("toc_query")) $("toc_query").value = "";
    $("side_toc").checked = true;
    pageState = { key: "", map: {} };
    measured = {};
    measureKey = "";
    var saved = place[book.key];
    if (saved && typeof saved === "object" && typeof saved.ci === "number") {
      anchor = normalizeAnchor(book, saved);
    } else if (book.chapters[0] && book.chapters[0].title === "简介" && book.chapters.length > 1) {
      anchor = { ci: 1, pi: 0, ch: 0, t: Date.now() };
    } else {
      anchor = { ci: 0, pi: 0, ch: 0, t: Date.now() };
    }
    anchor.t = Date.now();
    place[book.key] = {
      ci: anchor.ci,
      pi: anchor.pi,
      ch: anchor.ch,
      t: anchor.t,
      pct: percentOf(book, anchor),
      chapterTitle: book.chapters[anchor.ci] ? book.chapters[anchor.ci].title : ""
    };
    $("reader").classList.add("open");
    toggleUi(false);
    $("settings_open").checked = false;
    closeMenu();
    acquireWake();
    relayout(true);
    renderMarks();
    setTimeout(function () {
      if (current < 0 || !books[current] || books[current].key !== book.key) return;
      renderToc(book);
      syncChrome();
    }, 0);
    if (historyMode === "push" && opening) {
      history.pushState({ pushed: true, key: book.key }, "", hashUrl(book.key));
    } else if (historyMode === "replace") {
      history.replaceState({ pushed: false, key: book.key }, "", hashUrl(book.key));
    }
    scheduleSave();
  }

  function openBook(index, historyMode) {
    if (index < 0 || index >= books.length) return;
    var item = books[index];
    if (item.chapters && item.chapters.length) {
      presentBook(index, historyMode);
      return;
    }
    var token = ++loadToken;
    var opening = current < 0;
    $("reader").classList.add("open");
    $("book_title").textContent = item.title;
    toggleUi(false);
    $("settings_open").checked = false;
    closeMenu();
    var wait = document.createElement("p");
    wait.className = "chapter-end";
    wait.textContent = "正在打开《" + item.title + "》，先读进度附近的几章。";
    $("flow").textContent = "";
    $("flow").appendChild(wait);
    loadText(item).then(function (text) {
      if (!text || token !== loadToken) return null;
      return new Promise(function (resolve) {
        setTimeout(function () {
          if (token !== loadToken) resolve(null);
          else resolve({ book: parseBook(item.key, text), text: text });
        }, 20);
      });
    }).then(function (got) {
      if (!got || token !== loadToken) return;
      var book = got.book;
      book.file = item.file || item.key;
      book.source = item.source || "file";
      book.imported = !!item.imported;
      loaded[book.key] = book;
      books = library();
      var at = -1;
      for (var i = 0; i < books.length; i++) {
        if (books[i].key === book.key) at = i;
      }
      if (at < 0) return;
      presentBook(at, opening ? historyMode : "none");
      setTimeout(function () { saveText(book.key, got.text); }, 800);
    }).catch(function () {
      if (token !== loadToken) return;
      toast("没能打开《" + item.title + "》");
      if (current < 0) $("reader").classList.remove("open");
    });
  }

  function closeReader(historyMode) {
    loadToken += 1;
    if (current >= 0) remember();
    releaseWake();
    current = -1;
    shown = -1;
    $("reader").classList.remove("open");
    $("settings_open").checked = false;
    document.title = "小竹阅读";
    renderShelf();
    if (historyMode === "replace") {
      history.replaceState({ pushed: false }, "", location.pathname + location.search);
    }
    saveMeta();
  }

  function resetClear() {
    clearArmed = false;
    var btn = $("clear_btn");
    btn.textContent = "清空书架";
    btn.classList.remove("armed");
  }

  function closeMenu() {
    $("shelf_menu").checked = false;
    resetClear();
  }

  function markHint() {
    if (prefs.hintSeen) return;
    prefs.hintSeen = true;
    $("reader").classList.add("hint-seen");
    saveMeta();
  }

  function tocNodes() {
    return [$("toc_panel"), $("tray_scrim"), $("toolbar"), $("toc_toggle")].filter(Boolean);
  }

  function setTocHidden(hiddenNow) {
    prefs.tocHidden = hiddenNow;
    tocNodes().forEach(function (n) { n.classList.toggle("toc-hidden", hiddenNow); });
    var toggle = $("toc_toggle") || document.querySelector(".toc-toggle");
    if (toggle) toggle.setAttribute("aria-expanded", hiddenNow ? "false" : "true");
    saveMeta();
  }

  function uiIsOn() {
    var bar = $("toolbar");
    return !!(bar && bar.classList.contains("ui-on"));
  }

  /* The toolbar and status bar are a transient overlay, not stored state: the page
     starts without chrome and a tap on the text brings it back. */
  function toggleUi(force) {
    var on = typeof force === "boolean" ? force : !uiIsOn();
    [$("toolbar"), $("status")].forEach(function (n) {
      if (n) n.classList.toggle("ui-on", on);
    });
  }

  /* Back to the page: chrome away, both overlays closed. */
  function leaveReaderUi() {
    toggleUi(false);
    if ($("settings_open").checked) {
      $("settings_open").checked = false;
      syncSettingsButton();
    }
    if (!prefs.tocHidden) setTocHidden(true);
  }

  /* Aa is its own entry: reading preferences and a table of contents are two different
     jobs, so they are two different panels. */
  function setSettingsOpen(open) {
    $("settings_open").checked = !!open;
    syncSettingsButton();
    if (open && !prefs.tocHidden) setTocHidden(true);
  }

  function openSide(name) {
    var input = $("side_" + name);
    if (input) input.checked = true;
    if (prefs.tocHidden) setTocHidden(false);
    setSettingsOpen(false);
    toggleUi(true);
    markHint();
    if (name === "find") setTimeout(function () { $("find_query").focus(); }, 30);
    if (name === "marks") renderMarks();
  }

  function bookIndex(node) {
    var wrap = node.closest(".card-wrap");
    if (!wrap) return -1;
    return parseInt(wrap.dataset.index, 10);
  }

  function removeBook(key) {
    var openKey = current >= 0 && books[current] ? books[current].key : "";
    imports = imports.filter(function (book) { return book.key !== key; });
    hidden[key] = true;
    delete loaded[key];
    delete place[key];
    marks = marks.filter(function (mark) { return mark.key !== key; });
    idbDel("books", key);
    idbDel("texts", key);
    if (openKey === key) closeReader("replace");
    saveMeta();
    renderShelf();
    toast("已移出书架");
  }

  function ingest(files) {
    var list = Array.prototype.filter.call(files || [], isTxt);
    var skipped = (files ? files.length : 0) - list.length;
    if (!list.length) {
      toast(skipped ? "只支持 txt" : "没有选中文件");
      return;
    }
    toast(list.length > 1 ? "正在读入 " + list.length + " 本" : "正在读入");
    Promise.all(list.map(function (file) {
      return readFile(file).then(function (text) {
        if (!String(text).trim()) return null;
        var book = parseBook(file.name, text);
        book.imported = true;
        book.source = "import";
        book.file = file.name;
        return { book: book, text: text };
      });
    })).then(function (parsed) {
      var added = [];
      parsed.forEach(function (item) {
        if (!item) return;
        var book = item.book;
        delete hidden[book.key];
        loaded[book.key] = book;
        imports = imports.filter(function (entry) { return entry.key !== book.key; });
        imports.push(lightOf(book));
        added.push(book);
        saveText(book.key, item.text);
      });
      if (!added.length) {
        toast("没有读出正文");
        return;
      }
      if (added.length) cleared = false;
      saveMeta();
      renderShelf();
      toast(added.length === 1 ? "已放入书架：" + added[0].title : "已放入 " + added.length + " 本");
      if (added.length === 1 && current < 0) {
        var index = -1;
        for (var i = 0; i < books.length; i++) {
          if (books[i].key === added[0].key) index = i;
        }
        if (index >= 0) openBook(index, "push");
      } else if (current >= 0) {
        var key = books[current].key;
        for (var j = 0; j < added.length; j++) {
          if (added[j].key === key) {
            openBook(current, "none");
            break;
          }
        }
      }
    });
  }

  function acquireWake() {
    if (!navigator.wakeLock || document.visibilityState !== "visible") return;
    navigator.wakeLock.request("screen").then(function (lock) {
      wakeLock = lock;
    }).catch(function () { wakeLock = null; });
  }

  function releaseWake() {
    if (!wakeLock) return;
    var lock = wakeLock;
    wakeLock = null;
    lock.release().catch(function () {});
  }

  function typingTarget(node) {
    return node && node.closest && node.closest("input, textarea, select, [contenteditable]");
  }

  window.on_import = function () {
    $("open_file").click();
    closeMenu();
  };

  window.on_clear = function () {
    if (!clearArmed) {
      clearArmed = true;
      $("clear_btn").textContent = "确认清空";
      $("clear_btn").classList.add("armed");
      return;
    }
    imports.forEach(function (item) {
      idbDel("texts", item.key);
    });
    imports = [];
    place = {};
    marks = [];
    hidden = {};
    cleared = false;
    loaded = {};
    current = -1;
    closeReader("replace");
    closeMenu();
    saveMeta();
    renderShelf();
    toast("已清空阅读进度和导入的书");
  };

  window.on_open = function (event) {
    openBook(bookIndex(event.target), "push");
  };

  window.on_resume = function () {
    var key = $("resume").dataset.key;
    for (var i = 0; i < books.length; i++) {
      if (books[i].key === key) {
        openBook(i, "push");
        return;
      }
    }
  };

  window.on_remove = function (event) {
    var index = bookIndex(event.target);
    if (index < 0 || !books[index]) return;
    var key = books[index].key;
    if (removeArmed !== key) {
      removeArmed = key;
      event.target.textContent = "确认";
      clearTimeout(removeTimer);
      removeTimer = setTimeout(function () {
        removeArmed = "";
        document.querySelectorAll(".card-remove").forEach(function (button) {
          button.textContent = "移出";
        });
      }, 2500);
      return;
    }
    removeArmed = "";
    removeBook(key);
  };

  window.on_sort = function () {
    prefs.sort = prefs.sort === "title" ? "recent" : "title";
    renderShelf();
    saveMeta();
  };

  window.on_back = function () {
    if (history.state && history.state.pushed) {
      history.back();
      return;
    }
    closeReader("replace");
  };

  window.on_toc = function () {
    var hidden = prefs.tocHidden;
    setTocHidden(!hidden);
    // Opening the tray implies the bars, since the tray's close button lives there.
    toggleUi(hidden);
    // Two overlays at once would just cover each other.
    if (hidden) setSettingsOpen(false);
  };

  window.on_settings = function () {
    toggleUi(true);
    setSettingsOpen(!$("settings_open").checked);
  };

  window.on_tray_scrim = function () {
    leaveReaderUi();
  };

  /* Tapping the text is the reading view's only chrome control: the overlays give way
     first, then the bars. The narrow layout keeps its own gesture, and the page script
     shares this hook with the C++ example so both engines behave alike. */
  window.on_reading_tap = function (event) {
    if (current < 0) return;
    if (event && event.target && event.target.closest &&
        event.target.closest("button, a, input, label")) return;
    if (!prefs.tocHidden || $("settings_open").checked) {
      leaveReaderUi();
      return;
    }
    toggleUi();
    markHint();
  };

  window.on_chapter = function (event) {
    var node = event.target.closest("[data-index]");
    if (!node) return;
    goTo({ ci: parseInt(node.getAttribute("data-index"), 10) || 0, pi: 0, ch: 0 }, true);
    leaveReaderUi();
  };

  window.on_prev = function () {
    var edge = document.activeElement;
    if (edge && edge.classList && edge.classList.contains("page-edge")) edge.blur();
    turn(-1);
  };

  window.on_next = function () {
    var edge = document.activeElement;
    if (edge && edge.classList && edge.classList.contains("page-edge")) edge.blur();
    turn(1);
  };

  // Native body/search onkeydown stubs; the page keeps its document keydown listener.
  window.on_accel = function () {};
  window.on_type_key = function () {};

  window.on_next_chapter = function () {
    if (current < 0) return;
    var ci = (pageMode() ? pageChapter : anchor.ci) + 1;
    if (ci >= books[current].chapters.length) return;
    goTo({ ci: ci, pi: 0, ch: 0 }, false);
  };

  window.on_bookmark = function () {
    if (current < 0) return;
    remember();
    var book = books[current];
    var found = nearMark(anchor);
    if (found) {
      marks = marks.filter(function (mark) { return mark.id !== found.id; });
      toast("已取消书签");
    } else {
      var chapter = book.chapters[anchor.ci];
      var text = chapter && chapter.paragraphs[anchor.pi] ? String(chapter.paragraphs[anchor.pi]) : "";
      marks.push({
        id: Date.now().toString(36) + Math.random().toString(36).slice(2, 6),
        key: book.key,
        ci: anchor.ci,
        pi: anchor.pi,
        ch: anchor.ch || 0,
        label: chapter ? chapter.title : book.title,
        snip: text.slice(anchor.ch || 0, (anchor.ch || 0) + 42),
        t: Date.now()
      });
      toast("已添加书签");
    }
    syncChrome();
    saveMeta();
  };

  window.on_mark = function (event) {
    var node = event.target.closest("[data-id]");
    if (!node) return;
    var id = node.dataset.id;
    var mark = null;
    for (var i = 0; i < marks.length; i++) {
      if (marks[i].id === id) mark = marks[i];
    }
    if (!mark) return;
    goTo({ ci: mark.ci, pi: mark.pi, ch: mark.ch || 0 }, false);
    leaveReaderUi();
  };

  window.on_unmark = function (event) {
    var node = event.target.closest("[data-id]");
    if (!node) return;
    marks = marks.filter(function (mark) { return mark.id !== node.dataset.id; });
    syncChrome();
    saveMeta();
  };

  window.on_hit = function (event) {
    var node = event.target.closest(".hit");
    if (!node) return;
    goTo({
      ci: parseInt(node.dataset.ci, 10) || 0,
      pi: parseInt(node.dataset.pi, 10) || 0,
      ch: parseInt(node.dataset.ch, 10) || 0
    }, false);
    leaveReaderUi();
  };

  document.addEventListener("click", function (event) {
    if (removeArmed && !(event.target.closest && event.target.closest(".card-remove"))) {
      removeArmed = "";
      document.querySelectorAll(".card-remove").forEach(function (button) {
        button.textContent = "移出";
      });
    }
    var node = event.target.closest ? event.target.closest("[onclick]") : null;
    if (!node) return;
    var name = node.getAttribute("onclick");
    if (name && typeof window[name] === "function") window[name](event);
  });

  document.addEventListener("change", function (event) {
    var target = event.target;
    if (!target) return;
    if (target.id === "open_file") {
      var files = Array.prototype.slice.call(target.files || []);
      target.value = "";
      ingest(files);
      return;
    }
    if (target.name === "size" || target.name === "lead" || target.name === "theme"
      || target.name === "font" || target.name === "mode") {
      var before = {
        size: prefs.size,
        lead: prefs.lead,
        font: prefs.font,
        mode: prefs.mode
      };
      readPrefsFromRadios();
      applyThemeColor();
      updateHint();
      saveMeta();
      if (prefsNeedLayout(before)) {
        pageState = { key: "", map: {} };
        measured = {};
        measureKey = "";
        void $("flow_wrap").offsetWidth;
        relayout(true);
      }
      return;
    }
    if (target.id === "shelf_menu" && !target.checked) resetClear();
    if (target.id === "settings_open") {
      syncSettingsButton();
      if (target.checked) markHint();
    }
  });

  $("shelf_query").addEventListener("input", function () {
    renderShelf();
  });

  $("find_form").addEventListener("submit", function (event) {
    event.preventDefault();
    runFind($("find_query").value || "");
  });

  $("find_query").addEventListener("input", function () {
    clearTimeout(findTimer);
    var value = $("find_query").value || "";
    findTimer = setTimeout(function () { runFind(value); }, 160);
  });

  document.addEventListener("keydown", function (event) {
    var typing = typingTarget(event.target);
    if (event.key === "Escape") {
      if ($("shelf_menu").checked) {
        closeMenu();
        event.preventDefault();
        return;
      }
      // One Escape does what one tap on the text does: both overlays close, bars away.
      if (current >= 0 &&
          (!prefs.tocHidden || $("settings_open").checked)) {
        leaveReaderUi();
        event.preventDefault();
        return;
      }
      if (current >= 0 && !$("side_toc").checked) {
        $("side_toc").checked = true;
        event.preventDefault();
        return;
      }
      if (current >= 0 && uiIsOn()) {
        toggleUi(false);
        event.preventDefault();
      }
      return;
    }
    if (typing) return;
    if ((event.ctrlKey || event.metaKey) && (event.key === "f" || event.key === "F")) {
      event.preventDefault();
      if (current < 0) $("shelf_query").focus();
      else openSide("find");
      return;
    }
    if ((event.ctrlKey || event.metaKey) && (event.key === "b" || event.key === "B") && current >= 0) {
      event.preventDefault();
      window.on_bookmark();
      return;
    }
    if (event.key === "/" && current < 0) {
      event.preventDefault();
      $("shelf_query").focus();
      return;
    }
    if (current < 0) return;
    var chrome = event.target.closest && event.target.closest(".toolbar, .toc, .settings, .topbar, .menu");
    if (chrome) return;
    if (event.key === "ArrowLeft") {
      event.preventDefault();
      turn(-1);
    } else if (event.key === "ArrowRight") {
      event.preventDefault();
      turn(1);
    } else if (event.key === "PageDown" || event.key === " ") {
      event.preventDefault();
      if (pageMode()) turn(event.shiftKey ? -1 : 1);
      else $("flow_wrap").scrollBy({ top: (event.shiftKey ? -1 : 1) * $("flow_wrap").clientHeight * 0.9, behavior: reduceMotion ? "auto" : "smooth" });
    } else if (event.key === "PageUp") {
      event.preventDefault();
      if (pageMode()) turn(-1);
      else $("flow_wrap").scrollBy({ top: -$("flow_wrap").clientHeight * 0.9, behavior: reduceMotion ? "auto" : "smooth" });
    } else if (event.key === "ArrowDown" && !pageMode()) {
      event.preventDefault();
      $("flow_wrap").scrollBy({ top: 64, behavior: "auto" });
    } else if (event.key === "ArrowUp" && !pageMode()) {
      event.preventDefault();
      $("flow_wrap").scrollBy({ top: -64, behavior: "auto" });
    } else if (event.key === "Home") {
      event.preventDefault();
      goTo(event.ctrlKey ? { ci: 0, pi: 0, ch: 0 } : { ci: anchor.ci, pi: 0, ch: 0 }, false);
    } else if (event.key === "End" && books[current]) {
      event.preventDefault();
      var last = event.ctrlKey ? books[current].chapters.length - 1 : anchor.ci;
      var pi = books[current].chapters[last].paragraphs.length - 1;
      goTo({ ci: last, pi: pi, ch: 0 }, false);
    }
  });

  var wrap = $("flow_wrap");
  wrap.addEventListener("pointerdown", function (event) {
    if (event.button !== 0) return;
    downX = event.clientX;
    downY = event.clientY;
    downT = Date.now();
    swiped = false;
  });
  wrap.addEventListener("pointerup", function (event) {
    if (!pageMode() || current < 0) return;
    var dx = event.clientX - downX;
    var dy = event.clientY - downY;
    if (Date.now() - downT > 420) return;
    if (Math.abs(dx) < 56 || Math.abs(dx) < Math.abs(dy) * 1.2) return;
    var selection = window.getSelection();
    if (selection && !selection.isCollapsed) return;
    swiped = true;
    turn(dx < 0 ? 1 : -1);
  });
  wrap.addEventListener("click", function (event) {
    if (swiped) {
      swiped = false;
      return;
    }
    if (event.target.closest("button, a, input, label")) return;
    if (current < 0) return;
    if (Math.abs(event.clientX - downX) > 8 || Math.abs(event.clientY - downY) > 8) return;
    var selection = window.getSelection();
    if (selection && !selection.isCollapsed) return;
    var box = wrap.getBoundingClientRect();
    var ratio = (event.clientX - box.left) / (box.width || 1);
    if (pageMode()) {
      if (ratio < 0.28) turn(-1);
      else if (ratio > 0.72) turn(1);
    }
    // Showing and hiding the bars is `on_reading_tap`'s job, on every width.
  });
  wrap.addEventListener("scroll", function () {
    if (scrollLock || pageMode()) return;
    if (scrollScheduled) return;
    scrollScheduled = true;
    requestAnimationFrame(function () {
      scrollScheduled = false;
      if (scrollLock || pageMode() || current < 0) return;
      expandWindow();
      remember();
      scheduleSave();
    });
  }, { passive: true });
  wrap.addEventListener("wheel", function (event) {
    if (!pageMode() || current < 0) return;
    event.preventDefault();
    if (Date.now() < wheelLock) return;
    wheelLock = Date.now() + 280;
    if (event.deltaY > 0 || event.deltaX > 0) turn(1);
    else turn(-1);
  }, { passive: false });

  if (window.ResizeObserver) {
    var lastBox = "";
    new ResizeObserver(function () {
      if (current < 0) return;
      var box = wrap.clientWidth + "x" + wrap.clientHeight;
      if (box === lastBox) return;
      lastBox = box;
      relayout(true);
    }).observe(wrap);
  } else {
    window.addEventListener("resize", function () {
      if (current >= 0) relayout(true);
    });
  }

  var dragDepth = 0;
  function hasFileDrag(event) {
    return event.dataTransfer && Array.prototype.indexOf.call(event.dataTransfer.types || [], "Files") >= 0;
  }
  document.addEventListener("dragenter", function (event) {
    if (!hasFileDrag(event)) return;
    event.preventDefault();
    dragDepth += 1;
    $("shelf").classList.add("is-drag");
  });
  document.addEventListener("dragover", function (event) {
    if (!hasFileDrag(event)) return;
    event.preventDefault();
  });
  document.addEventListener("dragleave", function () {
    dragDepth = Math.max(0, dragDepth - 1);
    if (!dragDepth) $("shelf").classList.remove("is-drag");
  });
  document.addEventListener("drop", function (event) {
    if (!hasFileDrag(event)) return;
    event.preventDefault();
    dragDepth = 0;
    $("shelf").classList.remove("is-drag");
    ingest(event.dataTransfer.files);
  });

  window.addEventListener("pagehide", function () {
    if (current >= 0) remember();
    saveMeta();
  });
  document.addEventListener("visibilitychange", function () {
    if (document.visibilityState === "visible" && current >= 0) acquireWake();
  });
  window.matchMedia("(prefers-reduced-motion: reduce)").addEventListener("change", function (event) {
    reduceMotion = event.matches;
  });

  window.addEventListener("popstate", function () {
    var hashed = /^#book=(.*)$/.exec(location.hash);
    if (!hashed) {
      if (current >= 0) closeReader("none");
      return;
    }
    var key = decodeURIComponent(hashed[1]);
    for (var i = 0; i < books.length; i++) {
      if (books[i].key === key) {
        if (i !== current) openBook(i, "none");
        return;
      }
    }
  });

  if ("scrollRestoration" in history) history.scrollRestoration = "manual";
  if (!history.state) history.replaceState({ pushed: false }, "", location.href);

  var tocQuery = $("toc_query");
  if (tocQuery) tocQuery.addEventListener("input", filterToc);

  loadState().then(loadCatalog).then(function () {
    applyPrefs();
    renderShelf();
    var hashed = /^#book=(.*)$/.exec(location.hash);
    if (hashed) {
      var key = decodeURIComponent(hashed[1]);
      for (var i = 0; i < books.length; i++) {
        if (books[i].key === key) {
          openBook(i, "replace");
          break;
        }
      }
    }
    if ("serviceWorker" in navigator && (location.protocol === "http:" || location.protocol === "https:")) {
      navigator.serviceWorker.register("sw.js").catch(function () {});
    }
  });
}());
