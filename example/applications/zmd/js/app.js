(function () {
  var root = document.getElementById("md-editor-mock");
  if (!root) return;

  var saved = {};
  try {
    if (window.openai && window.openai.widgetState) saved = window.openai.widgetState;
  } catch (e) { saved = {}; }

  function merge(base, extra) {
    var out = {};
    Object.keys(base).forEach(function (k) { out[k] = base[k]; });
    if (extra) Object.keys(extra).forEach(function (k) {
      if (typeof out[k] === typeof extra[k] || out[k] === null) out[k] = extra[k];
    });
    return out;
  }

  function save() {
    try {
      if (!window.openai || typeof window.openai.setWidgetState !== "function") return;
      Promise.resolve(window.openai.setWidgetState({
        modelContent: {
          正文宽度: stA.width,
          正文字号: stA.fontSize,
          行距: stA.lineH,
          衬线正文: stA.serif,
          聚焦模式: stA.focus,
          主题: stA.theme,
          工作目录: stA.workspace,
          图片存放: stA.assets,
          自动保存: stA.autoSave,
          文件面板常驻: stA.pinL,
          目录面板常驻: stA.pinR,
          预览字号: stB.size,
          文件树: stB.tree,
          行号: stB.numbers,
          大纲宽度: stC.outlineW,
          块手柄: stC.handles,
          命令面板: stC.palette
        },
        privateContent: { A: stA, B: stB, C: stC }
      })).catch(function () {});
    } catch (e) { /* host without widget state */ }
  }

  /* ---------- 无缝沉浸 ---------- */
  var elA = root.querySelector("#mdc-a");

  function workspaceName() {
    var parts = String(stA.workspace || "").split(/[\\/]/).filter(Boolean);
    return parts.length ? parts[parts.length - 1] : "工作目录";
  }

  function selectTreeRow(btn) {
    var list = btn.closest ? btn.closest(".mdw-tree-list") : null;
    if (!list) return;
    list.querySelectorAll(".mdw-item").forEach(function (item) {
      item.classList.remove("is-active");
      item.removeAttribute("aria-current");
    });
    btn.classList.add("is-active");
    btn.setAttribute("aria-current", "true");
  }

  function newTreeItem(scope, kind) {
    var list = scope ? scope.querySelector(".mdw-tree-list") : null;
    if (!list || list.querySelector(".mdw-tree-input")) return;
    var row = document.createElement("div");
    row.className = "mdw-tree-new";
    var icon = document.createElement("i");
    icon.setAttribute("data-lucide", kind === "folder" ? "folder" : "file-text");
    icon.setAttribute("aria-hidden", "true");
    var input = document.createElement("input");
    input.type = "text";
    input.className = "mdw-tree-input";
    input.spellcheck = false;
    input.value = kind === "folder" ? "新建文件夹" : "未命名.md";
    input.setAttribute("aria-label", kind === "folder" ? "文件夹名称" : "文件名");
    row.appendChild(icon);
    row.appendChild(input);
    var anchor = list.querySelector(".mdw-item.is-active") || list.querySelector(".mdw-node");
    if (anchor && anchor.nextSibling && anchor.parentNode === list) {
      list.insertBefore(row, anchor.nextSibling);
    } else {
      list.appendChild(row);
    }
    if (globalThis.lucide && globalThis.lucide.createIcons) {
      globalThis.lucide.createIcons({ attrs: { width: 14, height: 14 } });
    }
    input.focus();
    input.select();

    var done = false;

    function finish(commit) {
      if (done) return;
      done = true;
      var name = input.value.trim();
      if (!row.parentNode) return;
      if (!commit || !name) {
        row.parentNode.removeChild(row);
        return;
      }
      var replacement;
      if (kind === "folder") {
        replacement = document.createElement("div");
        replacement.className = "mdw-node";
        replacement.innerHTML =
          '<i data-lucide="folder" aria-hidden="true"></i><span></span>';
      } else {
        replacement = document.createElement("button");
        replacement.type = "button";
        replacement.className = "mdw-item";
        replacement.innerHTML =
          '<i data-lucide="file-text" aria-hidden="true"></i><span></span>';
      }
      replacement.querySelector("span").textContent = name;
      row.parentNode.replaceChild(replacement, row);
      if (globalThis.lucide && globalThis.lucide.createIcons) {
        globalThis.lucide.createIcons({ attrs: { width: 14, height: 14 } });
      }
      if (kind !== "folder") selectTreeRow(replacement);
    }

    input.addEventListener("keydown", function (ev) {
      if (ev.key === "Enter") { ev.preventDefault(); finish(true); }
      else if (ev.key === "Escape") { ev.preventDefault(); finish(false); }
      ev.stopPropagation();
    });
    input.addEventListener("blur", function () { finish(true); });
  }

  var stA = merge(
    {
      accent: "#4a6cf7",
      radius: 12,
      width: 660,
      fontSize: 16,
      lineH: 1.72,
      serif: false,
      focus: false,
      theme: "system",
      bubble: true,
      settings: false,
      workspace: "~/Documents/写作",
      newInRoot: true,
      assets: "assets/",
      autoSave: true,
      pinL: false,
      pinR: false
    },
    saved.A
  );

  function syncControlsA() {
    elA.querySelectorAll("[data-set]").forEach(function (el) {
      var key = el.getAttribute("data-set");
      if (!(key in stA)) return;
      var cur = stA[key];
      if (el.tagName === "INPUT") {
        if (document.activeElement === el) return;
        if (el.type === "range") el.value = String(Number(cur));
        else if (el.type === "checkbox") el.checked = !!cur;
        else el.value = String(cur);
        return;
      }
      var val = el.getAttribute("data-val");
      if (val === null) return;
      var on = String(cur) === val;
      el.classList.toggle("is-active", on);
      el.setAttribute("aria-pressed", String(on));
    });
    elA.querySelectorAll("[data-val-of]").forEach(function (el) {
      var key = el.getAttribute("data-val-of");
      var value = stA[key];
      var shown = key === "lineH" ? Number(value).toFixed(2) : String(value);
      el.textContent = shown + (el.getAttribute("data-unit") || "");
    });
  }

  var docEl = elA ? elA.querySelector("#mdc-a-doc") : null;
  var scrollEl = elA ? elA.querySelector("#mdc-a-scroll") : null;
  var bubbleEl = elA ? elA.querySelector(".mdw-bubble") : null;
  var statEl = elA ? elA.querySelector("#mdc-a-stats") : null;
  var saveEl = elA ? elA.querySelector("#mdc-a-save") : null;
  var saveTimer = null;
  var BLOCKS = "p,div,h1,h2,h3,h4,h5,h6,li,blockquote,pre,td,th";

  function hideBubble() {
    if (bubbleEl) bubbleEl.classList.remove("is-open");
  }

  function placeBubbleForRange(range) {
    if (!bubbleEl || !docEl || !stA.bubble) return false;
    var width = bubbleEl.offsetWidth;
    var height = bubbleEl.offsetHeight;
    if (!width) return false;
    var rects = range.getClientRects();
    if (!rects.length) return false;
    var first = rects[0];
    var last = rects[rects.length - 1];
    var union = range.getBoundingClientRect();
    var host = docEl.getBoundingClientRect();
    var top = first.top - host.top - height - 8;
    if (first.top - host.top < height + 10) top = last.bottom - host.top + 8;
    var left = union.left + union.width / 2 - host.left - width / 2;
    left = Math.max(6, Math.min(left, docEl.clientWidth - width - 6));
    bubbleEl.style.left = Math.round(left) + "px";
    bubbleEl.style.top = Math.round(top) + "px";
    bubbleEl.classList.add("is-open");
    return true;
  }

  function placeBubbleForElement(el) {
    var range = document.createRange();
    range.selectNodeContents(el);
    return placeBubbleForRange(range);
  }

  function liveSelection() {
    var sel = window.getSelection();
    if (!sel || !sel.rangeCount || !docEl) return null;
    if (!docEl.contains(sel.anchorNode) || !docEl.contains(sel.focusNode)) return null;
    return sel;
  }

  function refreshBubble() {
    if (!bubbleEl || !stA.bubble) { hideBubble(); return; }
    var sel = liveSelection();
    if (sel && !sel.isCollapsed && placeBubbleForRange(sel.getRangeAt(0))) return;
    var demo = docEl ? docEl.querySelector(".mdw-sel") : null;
    if (demo && placeBubbleForElement(demo)) return;
    hideBubble();
  }

  function clearDemo() {
    if (!docEl) return;
    var caret = docEl.querySelector(".mdw-caret");
    if (caret) caret.remove();
    var span = docEl.querySelector(".mdw-sel");
    if (span && span.parentNode) {
      var parent = span.parentNode;
      while (span.firstChild) parent.insertBefore(span.firstChild, span);
      parent.removeChild(span);
      parent.normalize();
    }
  }

  function caretBlock() {
    var sel = liveSelection();
    if (!sel) return null;
    var node = sel.anchorNode;
    if (node && node.nodeType === 3) node = node.parentNode;
    if (!node || node === docEl || !node.closest) return null;
    var block = node.closest(BLOCKS);
    if (!block || block === bubbleEl || bubbleEl.contains(block)) return null;
    if (!docEl.contains(block)) return null;
    return block;
  }

  function topBlock(block) {
    var node = block;
    while (node && node.parentNode && node.parentNode !== docEl) node = node.parentNode;
    return node && node !== docEl ? node : null;
  }

  function updateCurrentBlock() {
    if (!docEl) return;
    var block = topBlock(caretBlock());
    var current = docEl.querySelectorAll(".mdw-cur");
    if (current.length === 1 && current[0] === block) return;
    current.forEach(function (el) { el.classList.remove("mdw-cur"); });
    if (block && block !== docEl) block.classList.add("mdw-cur");
  }

  function updateStats() {
    if (!docEl || !statEl) return;
    var text = docEl.innerText || "";
    var cjk = (text.match(/[\u3400-\u4dbf\u4e00-\u9fff]/g) || []).length;
    var latin = (text.match(/[A-Za-z0-9_]+/g) || []).length;
    var lines = text.split("\n").filter(function (line) { return line.trim(); }).length;
    statEl.textContent =
      (cjk + latin).toLocaleString("en-US") + " 字 · " + lines + " 行";
  }

  function markEditing() {
    if (saveEl) {
      saveEl.setAttribute("data-state", "typing");
      saveEl.setAttribute("aria-label", "正在保存");
    }
    if (elA) elA.classList.add("is-typing");
    if (saveTimer) window.clearTimeout(saveTimer);
    saveTimer = window.setTimeout(function () {
      if (saveEl) {
        saveEl.setAttribute("data-state", "saved");
        saveEl.setAttribute("aria-label", "已保存");
      }
      if (elA) elA.classList.remove("is-typing");
    }, 1200);
  }

  function ancestorWithTag(sel, tag) {
    var node = sel.anchorNode;
    if (node && node.nodeType === 3) node = node.parentNode;
    while (node && node !== docEl) {
      if (node.nodeName === tag) return node;
      node = node.parentNode;
    }
    return null;
  }

  function updateFmtButtons() {
    if (!elA) return;
    var sel = liveSelection();
    elA.querySelectorAll("[data-fmt]").forEach(function (btn) {
      var fmt = btn.getAttribute("data-fmt");
      var on = false;
      if (sel) {
        if (fmt === "bold") on = document.queryCommandState("bold");
        else if (fmt === "italic") on = document.queryCommandState("italic");
        else {
          var tag = fmt === "code" ? "CODE" : fmt === "mark" ? "MARK" : "A";
          on = !!ancestorWithTag(sel, tag);
        }
      }
      btn.setAttribute("aria-pressed", String(!!on));
    });
  }

  function unwrapElement(el) {
    var parent = el.parentNode;
    if (!parent) return;
    while (el.firstChild) parent.insertBefore(el.firstChild, el);
    parent.removeChild(el);
    parent.normalize();
  }

  function wrapSelection(tag) {
    var sel = window.getSelection();
    if (!sel || !sel.rangeCount) return;
    var range = sel.getRangeAt(0);
    var el = document.createElement(tag);
    if (tag === "A") el.setAttribute("href", "#");
    try {
      el.appendChild(range.extractContents());
      range.insertNode(el);
    } catch (err) {
      return;
    }
    var after = document.createRange();
    after.selectNodeContents(el);
    sel.removeAllRanges();
    sel.addRange(after);
  }

  function applyFormat(fmt) {
    var sel = liveSelection();
    if (!sel) return;
    if (fmt === "bold" || fmt === "italic") {
      document.execCommand(fmt, false, null);
      updateFmtButtons();
      return;
    }
    if (sel.isCollapsed) return;
    var tag = fmt === "code" ? "CODE" : fmt === "mark" ? "MARK" : "A";
    var existing = ancestorWithTag(sel, tag);
    if (existing) unwrapElement(existing);
    else wrapSelection(tag);
    updateFmtButtons();
  }

  function selectBlockContents(block) {
    var range = document.createRange();
    range.selectNodeContents(block);
    var sel = window.getSelection();
    sel.removeAllRanges();
    sel.addRange(range);
  }

  function placeCaret(node, atStart) {
    var range = document.createRange();
    range.selectNodeContents(node);
    range.collapse(atStart !== false);
    var sel = window.getSelection();
    sel.removeAllRanges();
    sel.addRange(range);
  }

  function wrapTyped(node, start, end, tag, strip) {
    var range = document.createRange();
    range.setStart(node, start);
    range.setEnd(node, end);
    var text = range.toString();
    var el = document.createElement(tag);
    el.textContent = text.slice(strip, text.length - strip);
    range.deleteContents();
    range.insertNode(el);
    var after = document.createRange();
    after.setStartAfter(el);
    after.collapse(true);
    var sel = window.getSelection();
    sel.removeAllRanges();
    sel.addRange(after);
  }

  function inlineRules() {
    var sel = window.getSelection();
    if (!sel || !sel.rangeCount) return;
    var range = sel.getRangeAt(0);
    var node = range.startContainer;
    if (!node || node.nodeType !== 3) return;
    var offset = range.startOffset;
    var text = node.data.slice(0, offset);
    var m;
    if ((m = /\*\*([^*\s](?:[^*]*[^*\s])?)\*\*$/.exec(text))) {
      wrapTyped(node, offset - m[0].length, offset, "strong", 2);
      return;
    }
    if ((m = /`([^`\s](?:[^`]*[^`\s])?)`$/.exec(text))) {
      wrapTyped(node, offset - m[0].length, offset, "code", 1);
      return;
    }
    if ((m = /(^|[^*])\*([^*\s](?:[^*]*[^*\s])?)\*$/.exec(text))) {
      wrapTyped(node, offset - m[0].length + (m[1] ? 1 : 0), offset, "em", 1);
    }
  }

  function blockRules() {
    var sel = window.getSelection();
    if (!sel || !sel.rangeCount || !sel.isCollapsed) return;
    var block = caretBlock();
    if (!block) return;
    var text = block.textContent.replace(/\u00a0/g, " ");
    var m;
    if ((m = /^(#{1,3}) $/.exec(text)) || /^[-*] $/.test(text) || /^1\. $/.test(text) || /^> $/.test(text)) {
      var level = m ? m[1].length : 0;
      selectBlockContents(block);
      document.execCommand("delete", false, null);
      if (level) document.execCommand("formatBlock", false, "h" + level);
      else if (/^> $/.test(text)) document.execCommand("formatBlock", false, "blockquote");
      else if (/^1\. $/.test(text)) document.execCommand("insertOrderedList", false, null);
      else document.execCommand("insertUnorderedList", false, null);
      return;
    }
    inlineRules();
  }

  function insertParagraphAfter(block) {
    var p = document.createElement("p");
    p.appendChild(document.createElement("br"));
    block.parentNode.insertBefore(p, block.nextSibling);
    placeCaret(p, true);
    return p;
  }

  function renderA() {
    if (!elA) return;
    elA.style.setProperty("--w-accent", String(stA.accent));
    elA.style.setProperty("--w-radius", Number(stA.radius) + "px");
    elA.style.setProperty("--w-doc-w", Number(stA.width) + "px");
    elA.style.setProperty("--w-doc-size", Number(stA.fontSize) + "px");
    elA.style.setProperty("--w-line-h", String(stA.lineH));
    if (stA.theme === "light") elA.style.colorScheme = "light";
    else if (stA.theme === "dark") elA.style.colorScheme = "dark";
    else elA.style.removeProperty("color-scheme");
    elA.classList.toggle("mdw-serif", !!stA.serif);
    elA.classList.toggle("is-focusing", !!stA.focus);
    elA.classList.toggle("no-bubble", !stA.bubble);
    elA.classList.toggle("set-closed", !stA.settings);
    elA.classList.toggle("pin-l", !!stA.pinL);
    elA.classList.toggle("pin-r", !!stA.pinR);
    var b = elA.querySelector("[data-act='focus']");
    if (b) b.setAttribute("aria-pressed", String(!!stA.focus));
    var pl = elA.querySelector("[data-act='pin-l']");
    var pr = elA.querySelector("[data-act='pin-r']");
    if (pl) pl.setAttribute("aria-pressed", String(!!stA.pinL));
    if (pr) pr.setAttribute("aria-pressed", String(!!stA.pinR));
    syncControlsA();
    var rootLabel = elA.querySelector(".mdw-tree-list > .mdw-node > span");
    if (rootLabel) rootLabel.textContent = workspaceName();
    refreshBubble();
  }

  if (elA) {
    var focusBtn = elA.querySelector("[data-act='focus']");
    if (focusBtn) focusBtn.addEventListener("click", function () {
      stA.focus = !stA.focus;
      renderA();
      save();
    });
    elA.addEventListener("click", function (ev) {
      if (ev.target.getAttribute && ev.target.getAttribute("data-act") === "set-scrim") {
        stA.settings = false;
        renderA();
        save();
        return;
      }
      var btn = ev.target.closest ? ev.target.closest("button") : null;
      if (!btn || !elA.contains(btn)) return;
      var act = btn.getAttribute("data-act");
      if (act === "settings") { stA.settings = true; renderA(); save(); return; }
      if (act === "new-file") { newTreeItem(elA, "file"); return; }
      if (act === "new-folder") { newTreeItem(elA, "folder"); return; }
      if (act === "pin-l") { stA.pinL = !stA.pinL; renderA(); save(); return; }
      if (act === "pin-r") { stA.pinR = !stA.pinR; renderA(); save(); return; }
      var val = btn.getAttribute("data-val");
      var key = btn.getAttribute("data-set");
      if (key && val !== null && key in stA) {
        stA[key] = val === "true" ? true : val === "false" ? false : val;
        renderA();
        save();
        return;
      }
      if (btn.classList.contains("mdw-set-tab")) {
        var pane = btn.getAttribute("data-pane");
        elA.querySelectorAll(".mdw-set-tab").forEach(function (tab) {
          tab.classList.toggle("is-active", tab === btn);
        });
        elA.querySelectorAll(".mdw-set-pane").forEach(function (body) {
          body.classList.toggle("is-active", body.getAttribute("data-pane") === pane);
        });
        return;
      }
      if (btn.hasAttribute("data-fmt")) { applyFormat(btn.getAttribute("data-fmt")); return; }
      if (btn.classList.contains("mdw-tbtn")) {
        btn.setAttribute("aria-pressed", String(btn.getAttribute("aria-pressed") !== "true"));
        return;
      }
      var group = btn.classList.contains("mdw-ol-item") ? ".mdw-ol-item"
        : btn.classList.contains("mdw-item") ? ".mdw-item" : null;
      if (!group) return;
      elA.querySelectorAll(group).forEach(function (it) {
        it.classList.remove("is-active");
        it.removeAttribute("aria-current");
      });
      btn.classList.add("is-active");
      btn.setAttribute("aria-current", "true");
    });
    elA.addEventListener("input", function (ev) {
      var t = ev.target;
      if (!t || !t.getAttribute) return;
      var key = t.getAttribute("data-set");
      if (!key || !(key in stA)) return;
      if (t.type === "range") { stA[key] = Number(t.value); renderA(); }
      else if (t.type === "color") { stA[key] = t.value; renderA(); }
      else if (t.type === "text") { stA[key] = t.value; renderA(); }
    });
    elA.addEventListener("change", function (ev) {
      var t = ev.target;
      if (!t || !t.getAttribute) return;
      var key = t.getAttribute("data-set");
      if (key && key in stA) {
        if (t.type === "checkbox") stA[key] = t.checked;
        else if (t.type === "range") stA[key] = Number(t.value);
        else if (t.type === "color") stA[key] = t.value;
        else if (t.type === "text") stA[key] = t.value;
        renderA();
        save();
        return;
      }
      if (t.type === "checkbox") save();
    });
    document.addEventListener("keydown", function (ev) {
      if (ev.key !== "Escape" || !stA.settings) return;
      stA.settings = false;
      renderA();
      save();
    });
  }
  renderA();

  if (elA && elA.querySelector(".mdw-doc")) {
    window.addEventListener("resize", refreshBubble);
    if (window.ResizeObserver) {
      new ResizeObserver(function () { refreshBubble(); }).observe(
        elA.querySelector(".mdw-doc")
      );
    }
    window.setTimeout(refreshBubble, 80);
  }

  /* ---------- 无缝沉浸：编辑引擎 ---------- */
  if (docEl) {
    docEl.addEventListener("pointerdown", clearDemo);
    docEl.addEventListener("focus", clearDemo);

    docEl.addEventListener("input", function () {
      blockRules();
      updateStats();
      updateCurrentBlock();
      markEditing();
      refreshBubble();
    });

    docEl.addEventListener("keydown", function (ev) {
      if (ev.key !== "Enter" || ev.shiftKey) return;
      var block = caretBlock();
      if (!block) return;
      if (/^H[1-6]$/.test(block.nodeName) || block.nodeName === "BLOCKQUOTE") {
        ev.preventDefault();
        insertParagraphAfter(block);
        return;
      }
      if (block.textContent.trim() === "---") {
        ev.preventDefault();
        var hr = document.createElement("hr");
        block.parentNode.replaceChild(hr, block);
        insertParagraphAfter(hr);
      }
    });

    document.addEventListener("selectionchange", function () {
      updateCurrentBlock();
      var sel = liveSelection();
      if (!sel || sel.isCollapsed) { hideBubble(); updateFmtButtons(); return; }
      if (placeBubbleForRange(sel.getRangeAt(0))) updateFmtButtons();
    });

    document.addEventListener("keydown", function (ev) {
      if (!(ev.metaKey || ev.ctrlKey) || docEl !== document.activeElement) return;
      var key = ev.key.toLowerCase();
      if (key === "b") { ev.preventDefault(); applyFormat("bold"); }
      else if (key === "i") { ev.preventDefault(); applyFormat("italic"); }
      else if (key === "e") { ev.preventDefault(); applyFormat("code"); }
    });

    if (bubbleEl) {
      bubbleEl.addEventListener("pointerdown", function (ev) { ev.preventDefault(); });
    }

    window.setTimeout(function () {
      updateStats();
      refreshBubble();
    }, 120);
  }

  /* ---------- 无缝沉浸：目录 / 命令面板 / 拖放 / 失焦 ---------- */
  if (elA) {
    var outlineEl = elA.querySelector("#mdc-a-outline");
    var outlineBar = elA.querySelector(".mdw-outline-bar");
    var outlineHeads = [];
    var outlineTimer = null;

    function buildOutline() {
      if (!outlineEl || !docEl) return;
      outlineHeads = Array.prototype.slice.call(docEl.querySelectorAll("h1,h2,h3"));
      outlineEl.innerHTML = "";
      outlineHeads.forEach(function (head, index) {
        var btn = document.createElement("button");
        btn.type = "button";
        btn.className = "mdw-ol-item lvl-" + head.tagName.slice(1);
        btn.setAttribute("data-index", String(index));
        btn.textContent = head.textContent.trim() || "未命名标题";
        outlineEl.appendChild(btn);
      });
      syncOutline();
    }

    function syncOutline() {
      if (!outlineEl || !scrollEl || !outlineHeads.length) return;
      var anchor = scrollEl.scrollTop + 64;
      var active = 0;
      outlineHeads.forEach(function (head, index) {
        if (head.offsetTop <= anchor) active = index;
      });
      outlineEl.querySelectorAll(".mdw-ol-item").forEach(function (item, index) {
        item.classList.toggle("is-active", index === active);
      });
      if (outlineBar) {
        var max = scrollEl.scrollHeight - scrollEl.clientHeight;
        var ratio = max > 0 ? Math.min(1, scrollEl.scrollTop / max) : 0;
        outlineBar.style.height = Math.round(ratio * 100) + "%";
      }
    }

    function scheduleOutline() {
      if (outlineTimer) window.clearTimeout(outlineTimer);
      outlineTimer = window.setTimeout(buildOutline, 420);
    }

    if (scrollEl) scrollEl.addEventListener("scroll", syncOutline, { passive: true });

    if (outlineEl) {
      outlineEl.addEventListener("click", function (ev) {
        var btn = ev.target.closest ? ev.target.closest(".mdw-ol-item") : null;
        if (!btn) return;
        var head = outlineHeads[Number(btn.getAttribute("data-index"))];
        if (head && head.scrollIntoView) {
          head.scrollIntoView({ block: "start", behavior: "smooth" });
        }
      });
    }

    function scrollToTop() {
      if (scrollEl) scrollEl.scrollTo({ top: 0, behavior: "smooth" });
    }

    /* ---- command palette ---- */
    var palInput = elA.querySelector("#mdc-a-pal-input");
    var palList = elA.querySelector("#mdc-a-pal-list");
    var palEmpty = elA.querySelector(".mdw-pal-empty");
    var palMode = "cmd";

    function paletteCommands() {
      return [
        { label: "聚焦模式", hint: "⌘⇧F", icon: "align-left", run: function () { stA.focus = !stA.focus; renderA(); save(); } },
        { label: "打开设置", hint: "⌘,", icon: "settings", run: function () { stA.settings = true; renderA(); } },
        { label: "浅色主题", icon: "sun", run: function () { stA.theme = "light"; renderA(); save(); } },
        { label: "深色主题", icon: "moon", run: function () { stA.theme = "dark"; renderA(); save(); } },
        { label: "跟随系统主题", icon: "monitor", run: function () { stA.theme = "system"; renderA(); save(); } },
        { label: "固定 / 收起文件面板", hint: "⌘⇧1", icon: "pin", run: function () { stA.pinL = !stA.pinL; renderA(); save(); } },
        { label: "固定 / 收起目录面板", hint: "⌘⇧2", icon: "pin", run: function () { stA.pinR = !stA.pinR; renderA(); save(); } },
        { label: "回到顶部", icon: "arrow-up", run: scrollToTop },
        { label: "加粗", hint: "⌘B", icon: "bold", run: function () { applyFormat("bold"); } },
        { label: "斜体", hint: "⌘I", icon: "italic", run: function () { applyFormat("italic"); } },
        { label: "行内代码", hint: "⌘E", icon: "code", run: function () { applyFormat("code"); } },
        { label: "高亮", icon: "highlighter", run: function () { applyFormat("mark"); } },
        { label: "导出为 PDF", icon: "download", run: function () {} },
        { label: "导出为 HTML", icon: "download", run: function () {} }
      ];
    }

    function paletteFiles() {
      var out = [];
      elA.querySelectorAll(".mdw-tree-list .mdw-item").forEach(function (item) {
        var name = (item.textContent || "").trim();
        if (!name) return;
        out.push({
          label: name,
          icon: "file-text",
          run: function () {
            selectTreeRow(item);
            var nameEl = elA.querySelector("#mdc-a-name");
            if (nameEl) nameEl.textContent = name;
          }
        });
      });
      return out;
    }

    function paletteRows() {
      return Array.prototype.slice.call(palList ? palList.querySelectorAll(".mdw-pal-row") : []);
    }

    function visiblePaletteRows() {
      return paletteRows().filter(function (row) {
        return !row.parentNode.hidden;
      });
    }

    function setPaletteActive(row) {
      paletteRows().forEach(function (item) {
        item.classList.toggle("is-active", item === row);
      });
    }

    function filterPalette() {
      var query = palInput ? palInput.value.trim().toLowerCase() : "";
      var shown = 0;
      paletteRows().forEach(function (row) {
        var hit =
          !query ||
          String(row.getAttribute("data-label") || "").toLowerCase().indexOf(query) !== -1;
        row.parentNode.hidden = !hit;
        if (hit) shown++;
      });
      if (palEmpty) palEmpty.hidden = shown > 0;
      var visible = visiblePaletteRows();
      if (visible.length && !visible.some(function (r) { return r.classList.contains("is-active"); })) {
        setPaletteActive(visible[0]);
      }
    }

    function renderPalette() {
      if (!palList) return;
      var items = palMode === "file" ? paletteFiles() : paletteCommands();
      palList.innerHTML = "";
      items.forEach(function (item) {
        var li = document.createElement("li");
        var btn = document.createElement("button");
        btn.type = "button";
        btn.className = "mdw-pal-row";
        btn.setAttribute("data-label", item.label);
        btn.innerHTML =
          '<i data-lucide="' + item.icon + '" aria-hidden="true"></i>' +
          '<span class="mdw-pal-label"></span>' +
          (item.hint ? '<span class="mdw-kbd">' + item.hint + "</span>" : "");
        btn.querySelector(".mdw-pal-label").textContent = item.label;
        btn.addEventListener("click", function () {
          closePalette();
          if (item.run) item.run();
        });
        li.appendChild(btn);
        palList.appendChild(li);
      });
      if (globalThis.lucide && globalThis.lucide.createIcons) {
        globalThis.lucide.createIcons({ attrs: { width: 15, height: 15 } });
      }
      filterPalette();
    }

    function openPalette(mode) {
      palMode = mode || "cmd";
      if (palInput) {
        palInput.value = "";
        palInput.placeholder = palMode === "file" ? "输入文件名…" : "输入命令…";
      }
      renderPalette();
      elA.classList.remove("pal-closed");
      window.setTimeout(function () {
        try { palInput.focus(); } catch (e) { /* noop */ }
      }, 0);
    }

    function closePalette() {
      elA.classList.add("pal-closed");
      hideBubble();
    }

    if (palInput) {
      palInput.addEventListener("input", filterPalette);
      palInput.addEventListener("keydown", function (ev) {
        var visible = visiblePaletteRows();
        var current = visible.findIndex(function (row) {
          return row.classList.contains("is-active");
        });
        if (ev.key === "ArrowDown" || ev.key === "ArrowUp") {
          ev.preventDefault();
          if (!visible.length) return;
          var step = ev.key === "ArrowDown" ? 1 : -1;
          var next = (current + step + visible.length) % visible.length;
          setPaletteActive(visible[next]);
          visible[next].scrollIntoView({ block: "nearest" });
          return;
        }
        if (ev.key === "Enter") {
          ev.preventDefault();
          var active = visible[current < 0 ? 0 : current];
          if (active) active.click();
          return;
        }
        if (ev.key === "Escape") {
          ev.preventDefault();
          closePalette();
        }
        ev.stopPropagation();
      });
    }

    document.addEventListener("keydown", function (ev) {
      if (ev.key === "Escape" && !elA.classList.contains("pal-closed")) {
        closePalette();
        return;
      }
      if (!(ev.metaKey || ev.ctrlKey)) return;
      var key = ev.key.toLowerCase();
      if (key === "k" && !ev.shiftKey) { ev.preventDefault(); openPalette("cmd"); return; }
      if (key === "p" && !ev.shiftKey) { ev.preventDefault(); openPalette("file"); return; }
      if (ev.shiftKey && ev.code === "Digit1") {
        ev.preventDefault(); stA.pinL = !stA.pinL; renderA(); save(); return;
      }
      if (ev.shiftKey && ev.code === "Digit2") {
        ev.preventDefault(); stA.pinR = !stA.pinR; renderA(); save(); return;
      }
      if (ev.shiftKey && key === "f") {
        ev.preventDefault(); stA.focus = !stA.focus; renderA(); save();
      }
    });

    /* ---- drop files ---- */
    var hasFiles = function (ev) {
      var types = ev.dataTransfer && ev.dataTransfer.types;
      return !!types && Array.prototype.indexOf.call(types, "Files") !== -1;
    };

    window.addEventListener("dragenter", function (ev) {
      if (!hasFiles(ev)) return;
      elA.classList.add("is-dropping");
    });

    window.addEventListener("dragover", function (ev) {
      if (hasFiles(ev)) ev.preventDefault();
    });

    window.addEventListener("dragleave", function (ev) {
      if (!ev.relatedTarget) elA.classList.remove("is-dropping");
    });

    window.addEventListener("drop", function (ev) {
      if (!ev.dataTransfer) return;
      ev.preventDefault();
      elA.classList.remove("is-dropping");
      var files = ev.dataTransfer.files;
      if (files && files.length) insertDroppedFile(files[0], ev.clientX, ev.clientY);
    });

    function insertDroppedFile(file, x, y) {
      var range = null;
      if (document.caretRangeFromPoint) {
        try { range = document.caretRangeFromPoint(x, y); } catch (e) { range = null; }
      }
      if (!range || !range.startContainer || !docEl.contains(range.startContainer)) {
        range = document.createRange();
        range.selectNodeContents(docEl);
        range.collapse(false);
      }
      var block = document.createElement("div");
      block.className = "mdw-upload";
      block.setAttribute("contenteditable", "false");
      var isImage = /^image\//.test(file.type);
      range.insertNode(block);
      if (isImage) {
        var img = document.createElement("img");
        img.src = URL.createObjectURL(file);
        img.alt = file.name;
        block.appendChild(img);
        var pill = document.createElement("span");
        pill.className = "mdw-upload-pill";
        pill.textContent = "保存到 assets/ …";
        block.appendChild(pill);
        window.setTimeout(function () {
          block.classList.add("is-done");
          pill.remove();
        }, 1100);
      } else {
        block.classList.add("is-done");
        block.textContent = "📄 " + file.name;
      }
      updateStats();
      markEditing();
    }

    /* ---- window focus state ---- */
    window.addEventListener("blur", function () { elA.classList.add("is-blurred"); });
    window.addEventListener("focus", function () { elA.classList.remove("is-blurred"); });

    window.setTimeout(function () {
      buildOutline();
      updateStats();
    }, 200);

    var inputHook = function () { scheduleOutline(); };
    if (docEl) docEl.addEventListener("input", inputHook);

    elA.addEventListener("click", function (ev) {
      var actAttr = ev.target.getAttribute ? ev.target.getAttribute("data-act") : null;
      if (actAttr === "pal-scrim") { closePalette(); return; }
      var btn = ev.target.closest ? ev.target.closest("button") : null;
      if (!btn) return;
      var act = btn.getAttribute("data-act");
      if (act === "palette") { openPalette("cmd"); return; }
      if (act === "to-top") scrollToTop();
    });
  }

  /* ---------- 双栏源码 ---------- */
  var elB = root.querySelector("#mdc-b");
  var stB = merge({ size: 14, tree: true, numbers: true }, saved.B);

  function renderB() {
    if (!elB) return;
    elB.classList.toggle("no-tree", !stB.tree);
    elB.classList.toggle("no-numbers", !stB.numbers);
    elB.style.setProperty("--w-prev-size", Number(stB.size) + "px");
    var t = elB.querySelector("[data-act='tree']");
    var n = elB.querySelector("[data-act='numbers']");
    if (t) t.setAttribute("aria-pressed", String(!!stB.tree));
    if (n) n.setAttribute("aria-pressed", String(!!stB.numbers));
  }

  if (elB) {
    elB.addEventListener("click", function (ev) {
      var btn = ev.target.closest ? ev.target.closest("button") : null;
      if (!btn) return;
      var act = btn.getAttribute("data-act");
      if (act === "tree") { stB.tree = !stB.tree; renderB(); save(); return; }
      if (act === "numbers") { stB.numbers = !stB.numbers; renderB(); save(); return; }
      if (act === "new-file") { newTreeItem(elB, "file"); return; }
      if (act === "new-folder") { newTreeItem(elB, "folder"); return; }
      if (btn.classList.contains("mdw-item")) {
        elB.querySelectorAll(".mdw-item").forEach(function (it) {
          it.classList.remove("is-active");
          it.removeAttribute("aria-current");
        });
        btn.classList.add("is-active");
        btn.setAttribute("aria-current", "true");
      }
    });
  }
  renderB();

  /* ---------- 大纲驱动 ---------- */
  var elC = root.querySelector("#mdc-c");
  var stC = merge({ outlineW: 232, handles: true, palette: true }, saved.C);
  var input = root.querySelector("#mdc-c-input");
  var rows = Array.prototype.slice.call(root.querySelectorAll(".mdw-pal-row"));
  var empty = root.querySelector(".mdw-pal-empty");
  var count = root.querySelector("#mdc-c-count");

  function visibleRows() {
    return rows.filter(function (r) { return !r.hidden; });
  }

  function setActive(row) {
    rows.forEach(function (r) { r.classList.toggle("is-active", r === row); });
  }

  function filterPal() {
    var q = input ? input.value.trim().toLowerCase() : "";
    var n = 0;
    rows.forEach(function (r) {
      var hit = !q || String(r.getAttribute("data-label") || "").toLowerCase().indexOf(q) !== -1;
      r.hidden = !hit;
      if (hit) n++;
    });
    if (empty) empty.hidden = n > 0;
    var vis = visibleRows();
    if (vis.length && !vis.some(function (r) { return r.classList.contains("is-active"); })) {
      setActive(vis[0]);
    }
    if (count) count.textContent = n + " 个命令可用";
  }

  function renderC(pushSave, focusInput) {
    if (!elC) return;
    elC.classList.toggle("pal-closed", !stC.palette);
    elC.classList.toggle("no-handles", !stC.handles);
    elC.style.setProperty("--w-outline-w", Number(stC.outlineW) + "px");
    var railBtn = elC.querySelector("[data-act='pal']");
    if (railBtn) railBtn.setAttribute("aria-pressed", String(!!stC.palette));
    if (stC.palette && focusInput && input) {
      window.setTimeout(function () { try { input.focus(); } catch (e) {} }, 0);
    }
    if (pushSave) save();
  }

  if (elC) {
    elC.addEventListener("click", function (ev) {
      var btn = ev.target.closest ? ev.target.closest("button") : null;
      if (!btn) {
        if (ev.target.getAttribute && ev.target.getAttribute("data-act") === "scrim") {
          stC.palette = false;
          renderC(false);
          save();
        }
        return;
      }
      if (btn.getAttribute("data-act") === "pal") {
        stC.palette = !stC.palette;
        renderC(false, stC.palette);
        save();
        return;
      }
      if (btn.classList.contains("mdw-pal-row")) { setActive(btn); return; }
      if (btn.classList.contains("mdw-ol-item")) {
        elC.querySelectorAll(".mdw-ol-item").forEach(function (it) {
          it.classList.remove("is-active");
          it.removeAttribute("aria-current");
        });
        btn.classList.add("is-active");
        btn.setAttribute("aria-current", "true");
      }
    });

    if (input) {
      input.addEventListener("input", filterPal);
      input.addEventListener("keydown", function (ev) {
        if (ev.key !== "Escape") return;
        input.value = "";
        filterPal();
        stC.palette = false;
        renderC(false);
        save();
      });
    }

    document.addEventListener("keydown", function (ev) {
      if (ev.key !== "Escape" || !stC.palette || document.activeElement === input) return;
      stC.palette = false;
      renderC(false);
      save();
    });
  }
  filterPal();
  renderC(false);

  /* ---------- design controls ---------- */
  if (globalThis.Tweak) {
    if (elA) {
      var tA = new Tweak({ container: elA, onChange: function () { renderA(); save(); } });
      tA.addColorPicker(stA, "accent", { label: "强调色", reference: "--w-accent" });
      tA.addSlider(stA, "radius", { label: "窗口圆角", min: 0, max: 24, unit: "px" });
      tA.addSlider(stA, "width", { label: "正文宽度", min: 520, max: 760, step: 10, unit: "px" });
      tA.addSlider(stA, "fontSize", { label: "正文字号", min: 14, max: 18, unit: "px" });
      tA.addSlider(stA, "lineH", { label: "行距", min: 1.4, max: 2.2, step: 0.02 });
      tA.addToggle(stA, "serif", { label: "衬线正文" });
      tA.addToggle(stA, "focus", { label: "聚焦模式" });
      tA.addToggle(stA, "bubble", { label: "选区工具条" });
      tA.addToggle(stA, "pinL", { label: "文件面板常驻" });
      tA.addToggle(stA, "pinR", { label: "目录面板常驻" });
    }
    if (elB) {
      var tB = new Tweak({ container: elB, onChange: function () { renderB(); save(); } });
      tB.addSlider(stB, "size", { label: "预览字号", min: 13, max: 17, unit: "px" });
      tB.addToggle(stB, "tree", { label: "文件树" });
      tB.addToggle(stB, "numbers", { label: "行号" });
    }
    if (elC) {
      var tC = new Tweak({ container: elC, onChange: function () { renderC(false); save(); } });
      tC.addSlider(stC, "outlineW", { label: "大纲面板宽度", min: 180, max: 300, step: 4, unit: "px" });
      tC.addToggle(stC, "handles", { label: "块手柄" });
      tC.addToggle(stC, "palette", { label: "命令面板" });
    }
  }

  window.addEventListener("openai:set_globals", function (ev) {
    var s = ev && ev.detail && ev.detail.globals && ev.detail.globals.widgetState;
    if (!s) return;
    stA = merge(stA, s.A);
    stB = merge(stB, s.B);
    stC = merge(stC, s.C);
    renderA();
    renderB();
    renderC(false);
  });
})();
