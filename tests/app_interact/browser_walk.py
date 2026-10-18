"""Drive every inventoried example/applications interaction in the difftest browser.

Imported by browser_oracle.py. The expected readback is whatever this script measures.
A launch or walk failure must not write the oracle.
"""

from __future__ import annotations

import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
CENSUS_PATH = REPO / "tests" / "interaction_census.json"

# Pages under example/applications. example/tests stays out of this walk.
PAGES = [
    "album",
    "calendar",
    "chat",
    "contacts",
    "file-explorer",
    "kanban",
    "particle",
    "reader",
    "share",
    "writer",
    "zdb",
    "zmd",
]

SHOWN = [
    "listing-root", "listing-docs", "listing-notes", "listing-reports", "listing-images",
    "listing-code", "crumbs-root", "crumbs-docs", "crumbs-notes", "crumbs-reports",
    "crumbs-images", "crumbs-code", "count-root", "count-docs", "count-notes",
    "count-reports", "count-images", "count-code", "cols", "preview-empty", "pv-name-empty",
    "pv-name-guide", "pv-name-lantern", "pv-name-ferry", "pv-name-ledger", "pv-name-shelf",
    "pv-name-mark", "pv-name-coast", "pv-name-card", "pv-name-theme", "pv-name-catalog",
]

LISTING = [
    {"id": "0", "name": "folder", "kind": "dir", "size": 0, "path": "folder"},
    {"id": "1", "name": "a.txt", "kind": "doc", "size": 1200, "path": "a.txt"},
    {"id": "2", "name": "b.txt", "kind": "doc", "size": 12, "path": "b.txt"},
]

PAGE_URL = {
    "album": "/example/applications/album/index.html",
    "calendar": "/example/applications/calendar/index.html",
    "chat": "/example/applications/chat/index.html",
    "contacts": "/example/applications/contacts/index.html",
    "file-explorer": "/example/applications/file-explorer/index.html",
    "kanban": "/example/applications/kanban/index.html",
    "particle": "/example/applications/particle/index.html",
    "reader": "/example/applications/reader/reader.html",
    "share": "/example/applications/share/index.html",
    "writer": "/example/applications/writer/index.html",
    "zdb": "/example/applications/zdb/index.html",
    "zmd": "/example/applications/zmd/index.html",
}

READY = {
    "album": "document.body.innerText.indexOf('渡口')>=0?true:null",
    "calendar": "document.body.innerText.indexOf('校对第三章')>=0?true:null",
    "chat": "document.body.innerText.indexOf('林晚')>=0?true:null",
    "contacts": "document.body.innerText.indexOf('林晚')>=0?true:null",
    "file-explorer": "document.getElementById('dir-root')?true:null",
    "kanban": "document.body.innerText.indexOf('校对第三章')>=0?true:null",
    "particle": "document.body.innerText.indexOf('数量')>=0?true:null",
    "reader": "document.body.innerText.indexOf('样例灯塔')>=0?true:null",
    "share": "typeof window.__zuiShare==='object'?true:null",
    "writer": "document.getElementById('save')&&document.getElementById('storage-apply')&&document.getElementById('new-piece')?true:null",
    "zdb": "document.body.innerText.indexOf('打开一个数据库')>=0?true:null",
    "zmd": "document.getElementById('mdc-a-source')&&document.getElementById('mdc-a-stats')?true:null",
}


def _step(op, covers, read, **extra):
    row = {"op": op, "covers": list(covers), "read": read}
    row.update(extra)
    return row


def _g(id_, field, as_, **extra):
    probe = {"op": "get", "id": id_, "field": field, "as": as_}
    probe.update(extra)
    return probe


def _has(id_, token, as_, **extra):
    probe = {"op": "class_has", "id": id_, "token": token, "as": as_}
    probe.update(extra)
    return probe


def _count(cls, as_):
    return {"op": "count", "class": cls, "as": as_}


def _any(text, as_):
    return {"op": "any", "text": text, "as": as_}


def _names(cls, as_):
    return {"op": "names", "class": cls, "as": as_}


def _screen():
    return {"op": "screen", "as": "screen"}


def _until(kind, **extra):
    gate = {"kind": kind}
    gate.update(extra)
    return gate


INSTALL = r"""
window.__walk = {
  shownNames: %%SHOWN%%,
  visible(el) {
    if (!el) return false;
    const cs = getComputedStyle(el);
    if (cs.display === 'none' || cs.visibility === 'hidden') return false;
    const rect = el.getBoundingClientRect();
    return rect.width > 0.5 && rect.height > 0.5;
  },
  resolve(spec) {
    if (spec.id) {
      const byId = document.getElementById(spec.id);
      if (byId) return byId;
    }
    if (spec.class != null && spec.nth != null) {
      const nodes = [...document.querySelectorAll('.' + spec.class)].filter((el) => this.visible(el));
      return nodes[spec.nth] || null;
    }
    if (spec.selector) return document.querySelector(spec.selector);
    return null;
  },
  textOf(el) {
    if (!el) return '';
    if (el.tagName === 'INPUT' || el.tagName === 'TEXTAREA') return '';
    for (const node of el.childNodes) {
      if (node.nodeType === 3 && node.nodeValue) return node.nodeValue;
    }
    return '';
  },
  content(el) {
    return el ? (el.textContent || '') : '';
  },
  field(el, name) {
    if (!el) return '';
    if (name === 'text') return this.textOf(el);
    if (name === 'value') return 'value' in el ? String(el.value) : '';
    if (name === 'checked') return el.checked ? 'true' : 'false';
    if (name === 'visible') return el.hidden ? 'false' : 'true';
    if (name === 'disabled') return el.disabled ? 'true' : 'false';
    if (name === 'class') return typeof el.className === 'string' ? el.className : '';
    if (name === 'href') return el.getAttribute('href') || '';
    // A `data-…` / `aria-…` name reads the attribute; the engine's Document::get
    // answers the same way, which is how a state that the sheet selects on
    // (`[data-state="typing"]`) can be walked.
    if (/^(data|aria)-/.test(name)) return el.getAttribute(name) || '';
    return '';
  },
  measure(reads) {
    const out = {};
    for (const spec of reads || []) {
      const key = spec.as || spec.op;
      if (spec.op === 'get') out[key] = this.field(this.resolve(spec), spec.field);
      else if (spec.op === 'class_has') {
        const el = this.resolve(spec);
        out[key] = !!(el && el.classList && el.classList.contains(spec.token));
      } else if (spec.op === 'count') {
        out[key] = [...document.querySelectorAll('.' + spec.class)].length;
      } else if (spec.op === 'names') {
        out[key] = [...document.querySelectorAll('.' + spec.class)].map((el) => (el.textContent || '').trim());
      } else if (spec.op === 'any') {
        out[key] = (document.body.innerText || '').indexOf(spec.text) >= 0;
      } else if (spec.op === 'has_text') {
        out[key] = this.content(this.resolve(spec)).indexOf(spec.text) >= 0;
      } else if (spec.op === 'last_text') {
        const nodes = [...document.querySelectorAll('.' + spec.class)];
        const last = nodes.length ? nodes[nodes.length - 1] : null;
        out[key] = last ? (last.textContent || '').trim() : '';
      } else if (spec.op === 'selected_name') {
        const el = document.querySelector('.obj.is-selected .obj-name');
        out[key] = el ? (el.textContent || '').trim() : '';
      } else if (spec.op === 'screen') {
        const app = document.getElementById('app');
        let name = '';
        if (app) for (const token of app.classList) if (token.startsWith('screen-')) name = token;
        out[key] = name;
      } else if (spec.op === 'files') {
        const checked = {};
        for (const el of document.querySelectorAll('input[type=radio]')) checked[el.id] = !!el.checked;
        const shown = {};
        for (const name of this.shownNames) {
          shown[name] = [...document.querySelectorAll('.' + name)].some((el) => this.visible(el));
        }
        out[key] = {checked: checked, shown: shown};
      }
    }
    return out;
  },
  fireInput(el, value) {
    el.focus();
    el.value = value;
    el.dispatchEvent(new InputEvent('input', {bubbles: true}));
  },
  clickEl(el) {
    if (!el) return 'ERR missed';
    // tel: and mailto: leave the page to the OS. The in-page result is the
    // link that stays. Cancelling the launch matches the engine's host seam.
    const href = el.getAttribute ? (el.getAttribute('href') || '') : '';
    if (/^(tel:|mailto:)/i.test(href)) {
      el.addEventListener('click', (event) => event.preventDefault(), {capture: true, once: true});
    }
    el.click();
    return true;
  },
  pointerClick(el) {
    if (!el) return 'ERR missed';
    const rect = el.getBoundingClientRect();
    const x = rect.left + rect.width / 2;
    const y = rect.top + Math.min(12, rect.height / 2);
    const down = { bubbles: true, clientX: x, clientY: y, button: 0, pointerId: 1, pointerType: 'mouse' };
    el.dispatchEvent(new PointerEvent('pointerdown', down));
    document.dispatchEvent(new PointerEvent('pointerup', down));
    return true;
  },
  act(step) {
    const op = step.op;
    if (op === 'read' || op === 'flush') return true;
    if (op === 'click' || op === 'tap') {
      const el = this.resolve(step);
      return op === 'tap' ? this.pointerClick(el) : this.clickEl(el);
    }
    if (op === 'click_text' || op === 'tap_text') {
      const nodes = [...document.querySelectorAll(step.class ? ('.' + step.class) : 'label, button, span, a')];
      for (const el of nodes) {
        const text = (el.textContent || '').trim();
        const hit = step.exact ? text === step.text : text.indexOf(step.text) >= 0;
        if (!hit || !this.visible(el)) continue;
        return op === 'tap_text' ? this.pointerClick(el) : this.clickEl(el);
      }
      return 'ERR missed text ' + step.text;
    }
    if (op === 'click_labelled') {
      for (const el of document.querySelectorAll('label, button, span, a')) {
        if ((el.textContent || '').trim() !== step.text || !this.visible(el)) continue;
        return this.clickEl(el.closest('label') || el);
      }
      return 'ERR missed label ' + step.text;
    }
    if (op === 'click_nth') {
      const nodes = [...document.querySelectorAll('.' + step.class)].filter((el) => this.visible(el));
      if (step.nth >= nodes.length) return 'ERR missed nth ' + step.class;
      return this.clickEl(nodes[step.nth]);
    }
    if (op === 'click_within') {
      for (const outer of document.querySelectorAll('.' + step.outer)) {
        if ((outer.textContent || '').indexOf(step.text) < 0 || !this.visible(outer)) continue;
        for (const el of outer.querySelectorAll('label, button, span, a')) {
          if ((el.textContent || '').trim() !== step.label) continue;
          return this.clickEl(el);
        }
      }
      return 'ERR missed within ' + step.label;
    }
    if (op === 'replace' || op === 'commit' || op === 'enter') {
      const el = this.resolve(step);
      if (!el) return 'ERR missed field ' + (step.id || step.selector || '');
      this.fireInput(el, step.value == null ? '' : step.value);
      if (op === 'commit') {
        el.dispatchEvent(new Event('change', {bubbles: true}));
        el.blur();
      }
      if (op === 'enter') {
        const form = el.form || el.closest('form');
        if (form && form.requestSubmit) form.requestSubmit();
        else el.dispatchEvent(new KeyboardEvent('keydown', {key: 'Enter', bubbles: true}));
      }
      return true;
    }
    if (op === 'press') {
      const el = this.resolve(step);
      if (!el) return 'ERR missed press';
      el.focus();
      el.dispatchEvent(new KeyboardEvent('keydown', {key: step.key, bubbles: true, shiftKey: !!step.shift}));
      return true;
    }
    if (op === 'key') {
      // A document-level key: the page accelerator (`<body onkeydown>` / the document listener),
      // not a control's own handler.
      document.body.dispatchEvent(new KeyboardEvent('keydown', {key: step.key, bubbles: true, shiftKey: !!step.shift}));
      return true;
    }
    if (op === 'wheel') {
      const el = this.resolve(step);
      if (!el) return 'ERR missed wheel';
      const dy = step.dy || 0;
      el.scrollTop = el.scrollTop + dy;
      el.dispatchEvent(new Event('scroll', {bubbles: true}));
      el.dispatchEvent(new WheelEvent('wheel', {deltaY: dy, bubbles: true, cancelable: true, ctrlKey: !!step.ctrl}));
      return true;
    }
    if (op === 'drag') {
      const el = this.resolve(step);
      if (!el) return 'ERR missed drag';
      const rect = el.getBoundingClientRect();
      const x = rect.left + rect.width / 2;
      const y = rect.top + rect.height / 2;
      const dx = step.dx || 0;
      const common = {bubbles: true, pointerId: 1, pointerType: 'mouse', button: 0};
      el.dispatchEvent(new PointerEvent('pointerdown', Object.assign({clientX: x, clientY: y}, common)));
      for (let i = 1; i <= 4; i++) {
        el.dispatchEvent(new PointerEvent('pointermove', Object.assign({clientX: x + dx * i / 4, clientY: y}, common)));
      }
      el.dispatchEvent(new PointerEvent('pointerup', Object.assign({clientX: x + dx, clientY: y}, common)));
      return true;
    }
    if (op === 'select_label') {
      const el = this.resolve(step);
      if (!el || !el.options) return 'ERR missed select';
      const opt = [...el.options].find((item) => (item.textContent || '').trim() === step.text);
      if (!opt) return 'ERR missed option ' + step.text;
      el.value = opt.value;
      el.dispatchEvent(new Event('change', {bubbles: true}));
      return true;
    }
    if (op === 'click_cell') {
      const el = document.querySelector('[data-scroll] td') || document.querySelector('.zcell:not(.zhead-btn)');
      return this.clickEl(el);
    }
    if (op === 'share_listing' || op === 'share_reopen') {
      if (!window.__zuiShare) return 'ERR no share';
      window.__zuiShare.applyListing(step.files || []);
      return true;
    }
    if (op === 'share_peer') {
      window.__zuiShare.stagePeer({id: step.id, name: step.name, ip: step.ip});
      return true;
    }
    if (op === 'share_incoming') {
      window.__zuiShare.stageIncoming({fromName: step.from, nfiles: step.nfiles, nbytes: step.nbytes});
      return true;
    }
    if (op === 'share_screen') {
      window.__zuiShare.showScreen(step.screen);
      return true;
    }
    return 'ERR unknown ' + op;
  },
  gate(spec) {
    if (!spec) return true;
    if (spec.kind === 'any') return (document.body.innerText || '').indexOf(spec.text) >= 0;
    if (spec.kind === 'eq') return this.field(this.resolve(spec), spec.field) === spec.value;
    if (spec.kind === 'contains') return this.field(this.resolve(spec), spec.field || 'text').indexOf(spec.text) >= 0;
    if (spec.kind === 'min_len') return document.querySelectorAll('.' + spec.class).length >= spec.n;
    if (spec.kind === 'screen') {
      const app = document.getElementById('app');
      if (!app) return false;
      return [...app.classList].indexOf(spec.value) >= 0;
    }
    if (spec.kind === 'truth') return !!this.measure([{op: spec.probe || 'any', text: spec.text, as: 'v', id: spec.id, field: spec.field}]).v;
    return false;
  }
};
if (location.pathname.indexOf('/share/') >= 0) {
  window.setTimeout = function () { return 0; };
  window.setInterval = function () { return 0; };
}
true
""".replace("%%SHOWN%%", json.dumps(SHOWN, ensure_ascii=False))


def album_steps():
    err = [_g("album-form-error", "text", "err"), _g("album-dialog", "visible", "dialog")]
    light = [_g("lightbox", "visible", "light"), _g("light-name", "text", "name"), _g("zoom-label", "text", "zoom")]
    tools = [_g("light-inspector", "checked", "tools")]
    opened = light + tools
    return [
        _step("click_text", ["stage"], [_count("tile", "tiles"), _any("海岸", "coast")], **{"class": "album-btn", "text": "渡口", "seam": True}),
        _step("click_text", [], opened, **{"class": "tile", "text": "海岸"}),
        _step("click", ["lightbox"], [_g("lightbox", "visible", "light")], id="lightbox"),
        _step("click_text", [], opened, **{"class": "tile", "text": "海岸"}),
        _step("key", ["light-inspector"], tools, key="i"),
        _step("click", ["zoom-in"], light + tools, id="zoom-in"),
        _step("click", ["zoom-out"], light + tools, id="zoom-out"),
        _step("replace", ["light-note"], [_g("light-note", "value", "note")], id="light-note", value="窗台"),
        _step("click", ["light-next"], light, id="light-next"),
        _step("click", ["light-prev"], light, id="light-prev"),
        _step("wheel", ["stage"], light, id="stage", dy=-80, ctrl=True),
        _step("click", ["light-close"], [_g("lightbox", "visible", "light")], id="light-close"),
        _step("click_text", [], opened, **{"class": "tile", "text": "海岸"}),
        _step("select_label", ["light-album"], [_g("lightbox", "visible", "light"), _any("灯塔", "moved")], id="light-album", text="灯塔", until=_until("eq", id="lightbox", field="visible", value="false")),
        _step("click_text", [], [_any("海岸", "there")], **{"class": "album-btn", "text": "灯塔", "seam": True}),
        _step("click_text", [], opened, **{"class": "tile", "text": "海岸"}),
        _step("click", ["light-delete"], [_g("confirm", "visible", "confirm")], id="light-delete", until=_until("eq", id="confirm", field="visible", value="true")),
        _step("click", ["confirm-cancel"], [_g("confirm", "visible", "confirm")], id="confirm-cancel"),
        _step("click", ["light-close"], [_g("lightbox", "visible", "light")], id="light-close"),
        _step("click_text", [], [_any("渡口", "back")], **{"class": "album-btn", "text": "渡口", "seam": True}),
        _step("click", ["delete-album"], [_g("confirm", "visible", "confirm")], id="delete-album", until=_until("eq", id="confirm", field="visible", value="true")),
        _step("click", ["confirm-cancel"], [_g("confirm", "visible", "confirm"), _any("渡口", "kept")], id="confirm-cancel"),
        _step("click", ["add-album"], err, id="add-album"),
        _step("click", ["album-form-cancel"], [_g("album-dialog", "visible", "dialog")], id="album-form-cancel"),
        _step("click", ["add-album"], err, id="add-album"),
        _step("click_labelled", [], err, text="创建"),
        _step("replace", [], [_g("album-form-name", "value", "name")], id="album-form-name", value="海雾"),
        _step("click_labelled", [], [_g("album-dialog", "visible", "dialog"), _any("海雾", "made")], text="创建", until=_until("eq", id="album-dialog", field="visible", value="false")),
        _step("replace", ["album-name"], [_any("海雾集", "renamed")], id="album-name", value="海雾集"),
        _step("click", [], [_g("restore", "visible", "restore")], id="sample-open"),
        _step("click", ["restore"], [_g("confirm", "visible", "confirm")], id="restore", until=_until("eq", id="confirm", field="visible", value="true")),
        _step("click", ["confirm-ok"], [_g("confirm", "visible", "confirm"), _any("渡口", "seed")], id="confirm-ok", until=_until("eq", id="confirm", field="visible", value="false")),
    ]


def calendar_steps():
    title = [_g("title", "text", "title")]
    editor = [_g("editor", "visible", "editor")]
    steps = [
        _step("click", ["today"], title, id="today"),
        _step("click", ["prev"], title, id="prev"),
        _step("click", ["next"], title, id="next"),
        _step("click", ["today"], title, id="today"),
        _step("click", ["view-week"], title + [_has("view-week", "is-on", "week")], id="view-week"),
    ]
    for i in range(7):
        steps.append(_step("click", [f"dow-{i}"], title + [_has(f"dow-{i}", "is-on", "on", **{"class": "dow", "nth": i})], id=f"dow-{i}", **{"class": "dow", "nth": i}))
    for i in range(7):
        steps.append(_step("click", [f"day-{i}"], editor, id=f"day-{i}", **{"class": "day", "nth": i, "at": "top"}, until=_until("eq", id="editor", field="visible", value="true")))
        steps.append(_step("click", ["editor-cancel"], editor, id="editor-cancel"))
    steps += [
        _step("click", ["view-month"], title + [_has("view-month", "is-on", "month")], id="view-month"),
        _step("click", ["create"], editor, id="create", until=_until("eq", id="editor", field="visible", value="true")),
        _step("click", ["color-green"], [_g("color-green", "checked", "on")], id="color-green"),
        _step("click", ["color-clay"], [_g("color-clay", "checked", "on")], id="color-clay"),
        _step("click", ["color-sea"], [_g("color-sea", "checked", "on")], id="color-sea"),
        _step("click", ["color-gold"], [_g("color-gold", "checked", "on")], id="color-gold"),
        _step("click", ["editor-cancel"], editor, id="editor-cancel"),
        _step("click_text", [], editor, **{"class": "chip", "text": "校对第三章"}, until=_until("eq", id="editor", field="visible", value="true")),
        _step("click", ["editor-remove"], [_g("confirm", "visible", "confirm")], id="editor-remove", until=_until("eq", id="confirm", field="visible", value="true")),
        _step("click", ["confirm-cancel"], [_g("confirm", "visible", "confirm"), _any("校对第三章", "kept")], id="confirm-cancel"),
        _step("click", [], [_g("restore", "visible", "menu")], id="sample-open"),
        _step("click", ["restore"], [_g("confirm-body", "text", "body")], id="restore", until=_until("eq", id="confirm", field="visible", value="true")),
        _step("click", ["confirm-ok"], [_g("confirm", "visible", "confirm"), _any("校对第三章", "seed")], id="confirm-ok"),
    ]
    return steps


def chat_steps():
    return [
        _step("replace", ["q"], [_g("conv-empty", "visible", "empty"), _count("conv", "n")], id="q", value="zzzz"),
        _step("replace", ["q"], [_count("conv", "n"), _any("林晚", "back")], id="q", value=" "),
        _step("click_text", [], [_g("thread-name", "text", "who")], **{"class": "conv", "text": "渡口书局"}),
        _step("click_text", [], [_g("thread-name", "text", "who")], **{"class": "conv", "text": "林晚"}),
        _step("replace", ["draft"], [_g("draft", "value", "draft")], id="draft", value="走查一句"),
        _step("click_labelled", [], [_any("走查一句", "sent"), _g("draft", "value", "draft")], text="发送"),
        _step("enter", ["draft"], [_any("回车一句", "sent")], id="draft", value="回车一句"),
        _step("wheel", ["log"], [_g("jump", "visible", "jump")], id="log", dy=-150),
        _step("wheel", ["log"], [_g("jump", "visible", "jump")], id="log", dy=-150),
        _step("click", ["jump"], [_g("jump", "visible", "jump")], id="jump"),
        _step("click", ["create"], [_g("creator", "visible", "creator")], id="create"),
        _step("click", ["creator-cancel"], [_g("creator", "visible", "creator")], id="creator-cancel"),
        _step("click", ["create"], [_g("creator", "visible", "creator")], id="create"),
        _step("replace", [], [_g("creator-name", "value", "name")], id="creator-name", value="走查"),
        _step("click_labelled", [], [_any("走查", "made"), _count("conv", "n")], text="创建"),
        _step("click", ["remove"], [_g("confirm-body", "text", "body")], id="remove", until=_until("eq", id="confirm", field="visible", value="true")),
        _step("click", ["confirm-cancel"], [_g("confirm", "visible", "confirm"), _count("conv", "n")], id="confirm-cancel"),
        _step("click", [], [_g("restore", "visible", "menu")], id="sample-open"),
        _step("click", ["restore"], [_g("confirm-body", "text", "body")], id="restore", until=_until("eq", id="confirm", field="visible", value="true")),
        _step("click", ["confirm-ok"], [_count("conv", "n"), _any("林晚", "seed")], id="confirm-ok"),
    ]


def contacts_steps():
    return [
        _step("replace", ["q"], [_g("list-empty", "visible", "empty"), _count("person", "n")], id="q", value="zzzz"),
        _step("replace", ["q"], [_count("person", "n")], id="q", value=" "),
        _step("click", ["star-only"], [_g("star-only", "checked", "on"), _count("person", "n")], id="star-only"),
        _step("click", ["star-only"], [_g("star-only", "checked", "on"), _count("person", "n")], id="star-only"),
        _step("click_text", [], [_g("d-name", "text", "who"), _g("d-phone", "href", "phone"), _g("d-email", "href", "email")], **{"class": "person", "text": "林晚"}),
        _step("click", ["d-phone"], [_g("d-phone", "href", "phone"), _g("d-name", "text", "who")], id="d-phone"),
        _step("click", ["d-email"], [_g("d-email", "href", "email"), _g("d-name", "text", "who")], id="d-email"),
        _step("click_labelled", [], [_g("editor", "visible", "editor")], text="编辑", until=_until("eq", id="editor", field="visible", value="true")),
        _step("click", ["g-work"], [_has("g-work", "is-on", "on")], id="g-work"),
        _step("click", ["g-family"], [_has("g-family", "is-on", "on")], id="g-family"),
        _step("click", ["g-friend"], [_has("g-friend", "is-on", "on")], id="g-friend"),
        _step("click", ["g-other"], [_has("g-other", "is-on", "on")], id="g-other"),
        _step("click", ["editor-cancel"], [_g("editor", "visible", "editor")], id="editor-cancel"),
        _step("click", ["create"], [_g("editor", "visible", "editor")], id="create"),
        _step("click", ["editor-cancel"], [_g("editor", "visible", "editor")], id="editor-cancel"),
        _step("click_labelled", [], [_g("confirm", "visible", "confirm")], text="删除", until=_until("eq", id="confirm", field="visible", value="true")),
        _step("click", ["confirm-cancel"], [_g("confirm", "visible", "confirm"), _any("林晚", "kept")], id="confirm-cancel"),
        _step("click", [], [_g("restore", "visible", "menu")], id="sample-open"),
        _step("click", ["restore"], [_g("confirm-body", "text", "body")], id="restore", until=_until("eq", id="confirm", field="visible", value="true")),
        _step("click", ["confirm-ok"], [_count("person", "n"), _any("林晚", "seed")], id="confirm-ok"),
    ]


def kanban_steps():
    titles = [_names("card-title", "titles"), _count("card", "cards"), _count("column", "cols")]
    return [
        _step("commit", ["name-{{id}}", "col-{{id}}"], [_g("name-todo", "value", "name", selector="input.col-name")], id="name-todo", selector="input.col-name", value="本周待办", blur="add-column"),
        _step("enter", ["add-{{id}}"], titles + [_any("第六节图表", "added")], id="add-todo", selector=".adder input", value="第六节图表"),
        _step("tap_text", ["card-{{id}}"], [_g("editor", "visible", "editor"), _g("f-title", "value", "title")], **{"class": "card", "text": "校对第三章"}, until=_until("eq", id="editor", field="visible", value="true")),
        _step("click_within", [], [_any("校对第三章", "moved")], outer="card", text="校对第三章", label="下移"),
        _step("click_within", [], [_any("校对第三章", "moved")], outer="card", text="校对第三章", label="上移"),
        _step("click", ["editor-cancel"], [_g("editor", "visible", "editor")], id="editor-cancel"),
        _step("tap_text", [], [_g("editor", "visible", "editor")], **{"class": "card", "text": "校对第三章"}, until=_until("eq", id="editor", field="visible", value="true")),
        _step("click", ["editor-remove"], [_g("confirm", "visible", "confirm")], id="editor-remove", until=_until("eq", id="confirm", field="visible", value="true")),
        _step("click", ["confirm-cancel"], [_g("confirm", "visible", "confirm"), _any("校对第三章", "kept")], id="confirm-cancel"),
        _step("click", ["add-column"], titles + [_any("新列", "col")], id="add-column"),
        _step("drag", ["board"], titles, id="board", dx=8),
        _step("click", [], [_g("restore", "visible", "menu")], id="sample-open"),
        _step("click", ["restore"], [_g("confirm-body", "text", "body")], id="restore", until=_until("eq", id="confirm", field="visible", value="true")),
        _step("click", ["confirm-ok"], titles + [_any("校对第三章", "seed")], id="confirm-ok", until=_until("eq", id="confirm", field="visible", value="false")),
    ]


def particle_steps():
    log = [{"op": "last_text", "class": "log-line", "as": "log"}]
    bench = [_has("bench", "tree-3", "t3"), _has("bench", "param-3", "p3")]
    return [
        _step("read", [], [_g("count-value", "text", "count")] + log),
        _step("click", ["effect-snow"], [_has("effect-snow", "is-on", "on")] + log, id="effect-snow"),
        _step("click", ["effect-fountain"], [_has("effect-fountain", "is-on", "on")] + log, id="effect-fountain"),
        _step("click", ["effect-orbit"], [_has("effect-orbit", "is-on", "on")] + log, id="effect-orbit"),
        _step("click", ["effect-snow"], [_has("effect-snow", "is-on", "on")] + log, id="effect-snow"),
        _step("click", ["count-inc"], [_g("count-value", "text", "count")] + log, id="count-inc"),
        _step("click", ["count-dec"], [_g("count-value", "text", "count")] + log, id="count-dec"),
        _step("click", ["speed-inc"], log + [_g("speed-value", "text", "speed")], id="speed-inc"),
        _step("click", ["speed-dec"], log, id="speed-dec"),
        _step("click", ["spread-inc"], log + [_g("spread-value", "text", "spread")], id="spread-inc"),
        _step("click", ["spread-dec"], log, id="spread-dec"),
        _step("drag", ["split-tree"], [_has("bench", "tree-5", "tree")] + bench, id="split-tree", dx=80),
        _step("drag", ["split-param"], [_has("bench", "param-1", "param")], id="split-param", dx=80),
        _step("replace", ["shader-source"], [_g("shader-source", "value", "src")], id="shader-source", value="void main(){}"),
        _step("replace", ["shader-path"], [_g("shader-path", "value", "path")], id="shader-path", value=""),
        _step("click", ["shader-import"], log, id="shader-import"),
        _step("replace", ["shader-out"], [_g("shader-out", "value", "out")], id="shader-out", value=""),
        _step("click", ["shader-export"], log, id="shader-export"),
    ]


def writer_steps():
    return [
        _step("click", ["new-piece"], [_g("doc-title", "text", "title"), _g("status", "text", "status"), _any("未命名文稿", "tree")], id="new-piece"),
        _step("click", ["new-folder"], [_any("新建文件夹", "folder")], id="new-folder"),
        # The pane is display:none until show-tree. Id click still runs; the twig
        # click below requires a box, so the toggle has to land first.
        _step("click", ["tree-toggle"], [_has("app", "show-tree", "open")], id="tree-toggle"),
        _step("click_text", ["node-{{id}}"], [_g("doc-title", "text", "title")], **{"class": "twig", "text": "未命名文稿"}),
        _step("replace", [], [_g("rename-name", "value", "name")], id="rename-name", value="走查稿"),
        _step("click", ["rename-apply"], [_g("doc-title", "text", "title")], id="rename-apply"),
        _step("click", ["mode-read"], [_has("mode-read", "is-on", "read")], id="mode-read"),
        _step("click", ["mode-edit"], [_has("mode-edit", "is-on", "edit")], id="mode-edit"),
        _step("replace", ["editor"], [_g("editor", "value", "body")], id="editor", value="旧稿"),
        _step("click", ["save"], [_g("status", "text", "status")], id="save"),
        _step("replace", ["editor"], [_g("editor", "value", "body")], id="editor", value="旧稿 新稿"),
        _step("click", ["save"], [_g("status", "text", "status")], id="save"),
        _step("click", ["view-history"], [_count("snap", "snaps")], id="view-history"),
        _step("click_nth", ["snap-{{id}}"], [_g("history-body", "text", "body"), _g("editor", "value", "editor")], **{"class": "snap", "nth": 0}),
        _step("click", ["view-tree"], [_has("view-tree", "is-on", "tree")], id="view-tree"),
        _step("click", ["view-calendar"], [_has("view-calendar", "is-on", "cal")], id="view-calendar"),
        _step("click", ["view-tree"], [], id="view-tree"),
        _step("click", ["tree-toggle"], [_g("tree-toggle", "text", "toggle")], id="tree-toggle"),
        _step("replace", [], [_g("storage-path", "value", "path")], id="storage-path", value=""),
        _step("click", ["storage-apply"], [_g("status", "text", "status")], id="storage-apply"),
        _step("click", ["delete-doc"], [_g("status", "text", "status")], id="delete-doc"),
    ]


def reader_steps():
    return [
        _step("read", [], [_any("样例灯塔", "title"), _g("sort_btn", "text", "sort")]),
        _step("click", ["sort_btn"], [_g("sort_btn", "text", "sort")], id="sort_btn"),
        _step("click", ["sort_btn"], [_g("sort_btn", "text", "sort")], id="sort_btn"),
        _step("replace", ["shelf_query"], [_any("没有匹配的书", "empty")], id="shelf_query", value="zzzz"),
        _step("replace", ["shelf_query"], [_any("样例灯塔", "back")], id="shelf_query", value=""),
        _step("click_text", ["open{{index}}"], [_any("第一次点灯", "chapter")], **{"class": "card", "text": "样例灯塔"}, until=_until("any", text="第一次点灯")),
        _step("click", ["chapter_next"], [_g("status_chapter", "text", "chapter")], id="chapter_next", until=_until("contains", id="status_chapter", field="text", text="灯灭的时候")),
        _step("click", ["bookmark_btn"], [_g("toast", "text", "toast")], id="bookmark_btn"),
        _step("click", ["toc_toggle"], [_has("toc_panel", "toc-hidden", "toc")], id="toc_toggle"),
        _step("replace", ["toc_query"], [_g("toc_query", "value", "q")], id="toc_query", value="灯"),
        _step("click_text", ["ch{{index}}"], [_g("status_chapter", "text", "chapter")], **{"class": "ch", "text": "第一次点灯"}, until=_until("contains", id="status_chapter", field="text", text="第一次点灯")),
        _step("replace", ["find_query"], [_g("find_query", "value", "q")], id="find_query", value="灯"),
        _step("click", ["flow_wrap"], [_has("reader", "open", "open")], id="flow_wrap"),
        _step("click", ["tray_scrim"], [_has("toc_panel", "toc-hidden", "toc")], id="tray_scrim"),
        _step("click_labelled", [], [_any("样例灯塔", "shelf")], text="书架"),
        _step("click", ["resume"], [_any("灯", "back")], id="resume", until=_until("any", text="灯")),
        _step("click_labelled", [], [_any("样例灯塔", "shelf")], text="书架"),
        _step("click_nth", [], [_g("clear_btn", "visible", "clear")], **{"class": "plus", "nth": 0}),
        _step("click", ["clear_btn"], [_g("toast", "text", "toast")], id="clear_btn"),
        _step("click_text", ["remove{{index}}"], [_g("remove0", "text", "arm", selector=".card-remove")], **{"class": "card-remove", "text": "移出", "exact": True}),
        _step("click_text", ["remove{{index}}"], [_any("样例灯塔", "gone")], **{"class": "card-remove", "text": "确认", "exact": True}),
    ]


def zdb_steps():
    return [
        _step("read", ["open-file"], [_g("open-file", "disabled", "open"), _g("export-db", "disabled", "export")], until=_until("eq", id="open-sample", field="disabled", value="false", before=True)),
        _step("click", ["open-sample"], [_g("selected_name", "text", "sel") if False else {"op": "selected_name", "as": "sel"}, _any("渡口手记", "row")], id="open-sample", until=_until("any", text="渡口手记")),
        _step("click_text", ["obj-{{index}}"], [{"op": "selected_name", "as": "sel"}], **{"class": "obj-name", "text": "books", "exact": True}),
        _step("click", ["mode-structure"], [_g("mode-structure", "aria-selected", "on")], id="mode-structure"),
        _step("click_text", [], [{"op": "selected_name", "as": "sel"}], **{"class": "obj-name", "text": "idx_books_author", "exact": True}),
        _step("click_labelled", ["struct-parent"], [{"op": "selected_name", "as": "sel"}], text="打开 books"),
        _step("click", ["mode-data"], [_g("mode-data", "aria-selected", "on")], id="mode-data"),
        _step("click", ["limit-50"], [_has("limit-50", "is-on", "on"), _g("page-range", "text", "range")], id="limit-50"),
        _step("click", ["limit-25"], [_has("limit-25", "is-on", "on")], id="limit-25"),
        _step("click", ["limit-100"], [_has("limit-100", "is-on", "on")], id="limit-100"),
        _step("click", ["limit-200"], [_has("limit-200", "is-on", "on")], id="limit-200"),
        _step("click", ["limit-25"], [_has("limit-25", "is-on", "on")], id="limit-25"),
        _step("click", ["page-next"], [_g("page-next", "disabled", "next"), _g("page-range", "text", "range")], id="page-next"),
        _step("click", ["page-prev"], [_g("page-prev", "disabled", "prev")], id="page-prev"),
        _step("replace", ["object-filter"], [_count("obj", "n")], id="object-filter", value="idx"),
        _step("replace", ["object-filter"], [_count("obj", "n")], id="object-filter", value=""),
        _step("click", ["mode-query"], [_g("mode-query", "aria-selected", "on")], id="mode-query"),
        _step("replace", ["sql-input"], [_g("sql-input", "value", "sql")], id="sql-input", value="select title from books"),
        _step("click", ["run-sql"], [_any("渡口手记", "cell")], id="run-sql", until=_until("any", text="渡口手记")),
        _step("click_cell", [], [_g("cell-detail", "visible", "detail")], until=_until("eq", id="cell-detail", field="visible", value="true")),
        _step("click", ["detail-close"], [_g("cell-detail", "visible", "detail")], id="detail-close"),
        _step("click", ["clear-sql"], [_g("sql-input", "value", "sql")], id="clear-sql"),
        _step("read", ["export-db"], [_g("export-db", "disabled", "export"), _g("open-file", "disabled", "open")]),
    ]


def zmd_steps():
    """The screen's own state, and the one surface both sides can write.

    The screen declares every action as `data-act="view-source"` / `data-fmt="bold"` /
    `data-set="theme"`; zui binds by `on*` attribute name, so of the whole page only the
    source textarea and the find field can reach C++. And the seamless view hides the
    source textarea (`display: none`), which is a surface no keystroke can reach - in the
    browser either, where js/zmd.js only sets `.value` and dispatches `input`
    programmatically. So the walk covers the state the page loads in and the source
    surface itself. Every control it cannot drive is in
    example/applications/zmd/README.md.
    """
    shown = [
        _has("md-editor-mock", "view-live", "live"),
        _has("mdc-a", "pal-closed", "pal"),
        _g("mdc-a-save", "data-state", "save"),
    ]
    return [
        _step("read", [], shown),
        _step("replace", ["mdc-a-source"], [_g("mdc-a-source", "value", "src")],
              id="mdc-a-source", value="# 走查\n\n正文\n"),
        # The find field is the page's other `oninput` control. Nothing can type into it while
        # its bar is `hidden`, on either side, so the walk only holds both ends to the same
        # value - which is what the census asks for: the id is addressable.
        _step("replace", ["zmd-find-q"], [_g("zmd-find-q", "value", "q")],
              id="zmd-find-q", value="正文"),
    ]


def file_steps():
    files = [{"op": "files", "as": "files"}]
    clicks = [
        ("文档", ["dir-docs"]),
        ("笔记", ["dir-notes"]),
        ("灯塔摘录.txt", ["file-lantern"]),
        ("返回", []),
        ("此电脑", ["dir-root"]),
        ("使用说明.md", ["file-guide"]),
    ]
    steps = [_step("read", ["file-none", "dir-root", "view-grid"], files)]
    for text, covers in clicks:
        steps.append(_step("click_labelled", covers, files, text=text))
    steps.append(_step("click_nth", ["view-list"], files, **{"class": "view-btn", "nth": 1}))
    steps.append(_step("click_nth", ["view-details"], files, **{"class": "view-btn", "nth": 2}))
    steps.append(_step("click_nth", ["view-grid"], files, **{"class": "view-btn", "nth": 0}))
    for text, covers in [
        ("报表", ["dir-reports"]),
        ("三月账单.csv", ["file-ledger"]),
        ("书架.json", ["file-shelf"]),
        ("图片", ["dir-images"]),
        ("竹标.svg", ["file-mark"]),
        ("海岸.svg", ["file-coast"]),
        ("代码", ["dir-code"]),
        ("名片.html", ["file-card"]),
        ("主题.css", ["file-theme"]),
        ("书目.xml", ["file-catalog"]),
        ("笔记", ["dir-notes"]),
        ("渡口手记.md", ["file-ferry"]),
    ]:
        steps.append(_step("click_labelled", covers, files, text=text))
    return steps


def share_steps(save_dir, listing_dir, phone=False):
    screen = [_screen(), _g("app-error", "text", "error")]
    names = [_names("fname", "names"), _g("sel-label", "text", "sel")]
    if phone:
        return [
            _step("replace", ["name-input"], [_g("name-input", "value", "name")], id="name-input", value="阿竹"),
            _step("click", ["start"], screen + [_g("me-name", "text", "me")], id="start", until=_until("screen", value="screen-browse")),
            _step("share_listing", ["row-{{id}}"], names, files=LISTING, dir=listing_dir),
            _step("share_peer", ["peer-{{id}}"], [_g("peer-count", "text", "peers")], id=42, name="书房电脑", ip="10.0.0.8"),
            _step("click", ["chip-{{id}}"], screen, id="chip-42"),
        ]
    return [
        _step("replace", ["name-input"], [_g("name-input", "value", "name")], id="name-input", value=""),
        _step("click", ["start"], screen, id="start"),
        _step("replace", ["name-input"], [_g("name-input", "value", "name")], id="name-input", value="阿竹"),
        _step("click", ["start"], screen + [_g("me-name", "text", "me")], id="start", until=_until("screen", value="screen-browse")),
        _step("share_listing", ["row-{{id}}"], names, files=LISTING, dir=listing_dir),
        _step("click", ["row-{{id}}"], names, id="row-1"),
        _step("click", ["clear"], names, id="clear"),
        _step("click", ["row-{{id}}"], names, id="row-1"),
        _step("click", ["share"], screen, id="share", until=_until("screen", value="screen-pick")),
        _step("click", ["pick-back"], screen, id="pick-back"),
        _step("share_peer", ["peer-{{id}}"], [_g("peer-count", "text", "peers"), _any("书房电脑", "peer")], id=42, name="书房电脑", ip="10.0.0.8"),
        _step("click", ["share"], screen, id="share", until=_until("screen", value="screen-pick")),
        _step("click", ["send-{{id}}"], screen, id="send-42", until=_until("screen", value="screen-wait")),
        _step("click", ["wait-cancel"], screen, id="wait-cancel"),
        _step("click", ["peer-{{id}}"], screen, id="peer-42"),
        _step("click", ["row-{{id}}"], names, id="row-0"),
        _step("click", ["up"], names, id="up"),
        _step("click", ["refresh"], names + screen, id="refresh"),
        _step("share_reopen", [], names, files=LISTING, dir=listing_dir),
        _step("commit", ["path"], names + screen, id="path", value=listing_dir, blur="refresh"),
        _step("click", ["settings"], screen, id="settings", until=_until("screen", value="screen-settings")),
        _step("replace", ["set-name"], [_g("set-name", "value", "name")], id="set-name", value=""),
        _step("click", ["save-name"], screen, id="save-name"),
        _step("replace", ["set-name"], [_g("set-name", "value", "name")], id="set-name", value="阿竹"),
        _step("click", ["save-name"], screen + [_g("me-name", "text", "me")], id="save-name"),
        _step("replace", ["save-dir"], [_g("set-dir", "value", "dir")], id="set-dir", value=""),
        _step("click", ["save-dir"], screen, id="save-dir"),
        _step("replace", ["save-dir"], [_g("set-dir", "value", "dir")], id="set-dir", value=save_dir),
        _step("click", ["save-dir"], screen + [_g("set-dir", "value", "dir")], id="save-dir"),
        _step("click", ["use-cwd"], screen, id="use-cwd"),
        _step("click", ["settings-back"], screen, id="settings-back"),
        _step("share_incoming", ["reject"], [_g("inc-name", "text", "who"), _g("inc-line1", "text", "line"), _has("app", "incoming", "incoming")], **{"from": "客厅电脑", "nfiles": 2, "nbytes": 1200}),
        _step("click", ["reject"], screen + [_has("app", "incoming", "incoming")], id="reject"),
        _step("share_incoming", ["accept"], [_g("inc-name", "text", "who"), _has("app", "incoming", "incoming")], **{"from": "客厅电脑", "nfiles": 2, "nbytes": 1200}),
        _step("click", ["accept"], screen + [_has("app", "incoming", "incoming")], id="accept", until=_until("screen", value="screen-transfer")),
        _step("click", ["xfer-cancel"], screen, id="xfer-cancel"),
        _step("share_screen", ["done-ok"], screen, screen="done"),
        _step("click", ["done-ok"], screen, id="done-ok"),
        _step("share_screen", ["fail-back"], screen, screen="fail"),
        _step("click", ["fail-back"], screen, id="fail-back"),
    ]


def passes_for(page, save_dir, listing_dir):
    builders = {
        "album": album_steps,
        "calendar": calendar_steps,
        "chat": chat_steps,
        "contacts": contacts_steps,
        "kanban": kanban_steps,
        "particle": particle_steps,
        "writer": writer_steps,
        "reader": reader_steps,
        "zdb": zdb_steps,
        "zmd": zmd_steps,
        "file-explorer": file_steps,
    }
    if page == "share":
        return [
            {"width": 1100, "height": 720, "steps": share_steps(save_dir, listing_dir, False)},
            {"width": 390, "height": 720, "steps": share_steps(save_dir, listing_dir, True)},
        ]
    return [{"width": 1100, "height": 720, "steps": builders[page]()}]


def _clear_idb(browser):
    browser.session.evaluate(
        """(async () => {
          if (!indexedDB.databases) return true;
          const dbs = await indexedDB.databases();
          await Promise.all(dbs.map((d) => new Promise((resolve) => {
            const req = indexedDB.deleteDatabase(d.name);
            req.onsuccess = req.onerror = req.onblocked = () => resolve(true);
          })));
          return true;
        })()""",
        await_promise=True,
    )


def _gate_expr(gate):
    return "window.__walk.gate(%s) ? true : null" % json.dumps(gate, ensure_ascii=False)


def _absorb_files(step, expect, bag):
    files = expect.get("files") if isinstance(expect, dict) else None
    if not isinstance(files, dict):
        return
    for ident, on in (files.get("checked") or {}).items():
        if on and ident not in step["covers"]:
            step["covers"].append(ident)
        if on:
            bag.add(ident)


def record_walk(browser, base, navigate, js, wait_value, log):
    import tempfile

    census = json.loads(CENSUS_PATH.read_text(encoding="utf-8"))
    save_dir = str(Path(tempfile.gettempdir()) / "zui-walk-saved")
    listing_dir = str(Path(tempfile.gettempdir()) / "zui-walk-share")
    Path(save_dir).mkdir(parents=True, exist_ok=True)
    listing = Path(listing_dir)
    (listing / "folder").mkdir(parents=True, exist_ok=True)
    (listing / "a.txt").write_bytes(b"x" * 1200)
    (listing / "b.txt").write_bytes(b"x" * 12)
    out = {}
    for page in PAGES:
        bag = set()
        recorded_passes = []
        for spec in passes_for(page, save_dir, listing_dir):
            browser.set_viewport(spec["width"], spec["height"])
            url = base + PAGE_URL[page]
            navigate(browser, url)
            _clear_idb(browser)
            navigate(browser, url)
            js(browser, INSTALL)
            ready = READY.get(page)
            if ready:
                wait_value(browser, ready, page + " ready", seconds=20)
            browser.settle()
            steps_out = []
            for step in spec["steps"]:
                gate = step.get("until")
                if gate and gate.get("before"):
                    wait_value(browser, _gate_expr(gate), page + " before " + step["op"], seconds=20)
                acted = js(browser, "window.__walk.act(%s)" % json.dumps(step, ensure_ascii=False))
                if acted is not True:
                    raise RuntimeError(f"{page} {step['op']} {step.get('covers')}: {acted}")
                if gate and not gate.get("before"):
                    wait_value(browser, _gate_expr(gate), page + " until " + step["op"], seconds=20)
                browser.settle()
                expect = js(browser, "window.__walk.measure(%s)" % json.dumps(step["read"], ensure_ascii=False))
                row = dict(step)
                row["expect"] = expect
                _absorb_files(row, expect, bag)
                for ident in row["covers"]:
                    bag.add(ident)
                steps_out.append(row)
                log(f"walk {page} {step['op']} {step.get('id') or step.get('text') or ''}")
            recorded_passes.append({"width": spec["width"], "height": spec["height"], "steps": steps_out})
        missing = [ident for ident in census[page]["ids"] if ident not in bag]
        if missing:
            raise RuntimeError(page + " walk missed " + ", ".join(missing))
        out[page] = {"passes": recorded_passes}
        log(f"walk {page} covered {len(bag)}")
    return out
