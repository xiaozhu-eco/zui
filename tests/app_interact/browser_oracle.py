"""Record the browser readback for example/applications pages.

Drives each page's own scripts (share, album, kanban, particle, writer) or its CSS radios
(file-explorer) in the difftest browser and writes tests/app_interact/browser_oracle.json.
The engine tests compare against this file. If the browser cannot be launched, the script
exits without writing it.

    python tests/app_interact/browser_oracle.py
    python tests/app_interact/browser_oracle.py --out <path>
"""

from __future__ import annotations

import argparse
import json
import sys
import threading
import time
import traceback
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlparse

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
CHROME = REPO / "tests" / "difftest" / "chrome"
sys.path.insert(0, str(CHROME))

from driver import Browser, find_browser, load_env  # noqa: E402

ENV_PATH = REPO / "tests" / "difftest" / "env.json"
VIEWPORT = (1100, 720)

SHOWN = [
    "listing-root", "listing-docs", "listing-notes", "listing-reports", "listing-images",
    "listing-code", "crumbs-root", "crumbs-docs", "crumbs-notes", "crumbs-reports",
    "crumbs-images", "crumbs-code", "count-root", "count-docs", "count-notes",
    "count-reports", "count-images", "count-code", "cols", "preview-empty", "pv-name-empty",
    "pv-name-guide", "pv-name-lantern", "pv-name-ferry", "pv-name-ledger", "pv-name-shelf",
    "pv-name-mark", "pv-name-coast", "pv-name-card", "pv-name-theme", "pv-name-catalog",
]


def log(message: str) -> None:
    print(message, file=sys.stderr, flush=True)


BOOKS_PREFIX = "/example/applications/reader/books"
FIXTURE_BOOKS = REPO / "tests" / "fixtures" / "reader"


class QuietHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(REPO), **kwargs)

    def log_message(self, format, *args):  # noqa: A003
        return

    def do_GET(self):  # noqa: N802
        # The shipped novels are tens of megabytes. The walk and the engine both read the
        # shared fixture under tests/fixtures/reader so neither side parses those books.
        path = unquote(urlparse(self.path).path)
        if path.rstrip("/") == BOOKS_PREFIX:
            body = (
                "<!DOCTYPE html><html><body>"
                '<a href="sample.txt">sample.txt</a>'
                '<a href="catalog.json">catalog.json</a>'
                "</body></html>"
            ).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        prefix = BOOKS_PREFIX + "/"
        if path.startswith(prefix):
            rel = path[len(prefix):]
            parts = rel.replace("\\", "/").split("/")
            if not rel or ".." in parts:
                self.send_error(404)
                return
            root = FIXTURE_BOOKS.resolve()
            file = (root / rel).resolve()
            if root not in file.parents or not file.is_file():
                self.send_error(404)
                return
            data = file.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return
        super().do_GET()


def serve():
    server = ThreadingHTTPServer(("127.0.0.1", 0), QuietHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server, thread


def navigate(browser: Browser, url: str) -> None:
    browser.session.call("Page.navigate", {"url": url})
    browser.session.wait_event("Page.loadEventFired", timeout=30)
    browser.session.evaluate(
        "document.fonts.ready.then(function(){return true;})",
        await_promise=True,
    )
    browser.settle()


def wait_share(browser: Browser) -> None:
    deadline = time.time() + 10
    last = None
    while time.time() < deadline:
        last = browser.evaluate("typeof window.__zuiShare")
        if last == "object":
            return
        time.sleep(0.05)
    raise RuntimeError(f"share module did not install window.__zuiShare (typeof={last})")


def js(browser: Browser, expression: str):
    value = browser.evaluate(expression)
    if isinstance(value, dict) and value.get("__error"):
        raise RuntimeError(value["__error"])
    return value


INSTALL_SHARE = r"""
window.__oracle = {
  screen() {
    const app = document.getElementById('app');
    for (const name of app.classList) if (name.startsWith('screen-')) return name;
    return '';
  },
  error() {
    const el = document.getElementById('app-error');
    return el.hidden ? '' : (el.textContent || '');
  },
  incoming() { return document.getElementById('app').classList.contains('incoming'); },
  setField(id, value) {
    const el = document.getElementById(id);
    el.focus();
    el.value = value;
    el.dispatchEvent(new InputEvent('input', {bubbles: true}));
    return el.value;
  },
  commit(id, value) {
    const el = document.getElementById(id);
    el.focus();
    el.value = value;
    el.dispatchEvent(new Event('change', {bubbles: true}));
    return document.getElementById(id).value;
  },
  click(id) { document.getElementById(id).click(); },
  names() { return [...document.querySelectorAll('.fname')].map((el) => el.textContent.trim()); },
  text(id) { return document.getElementById(id).textContent.trim(); }
};
true
"""


def share_readback(browser: Browser, base: str) -> dict:
    navigate(browser, base + "/example/applications/share/index.html")
    wait_share(browser)
    js(browser, INSTALL_SHARE)

    empty = js(browser, """
(() => {
  window.__oracle.setField('name-input', '');
  window.__oracle.click('start');
  return {screen: window.__oracle.screen(), error: window.__oracle.error()};
})()
""")

    navigate(browser, base + "/example/applications/share/index.html")
    wait_share(browser)
    js(browser, INSTALL_SHARE)
    started = js(browser, """
(() => {
  window.__oracle.setField('name-input', '阿竹');
  const typed = window.__oracle.text('me-name');
  window.__oracle.click('start');
  return {typed, screen: window.__oracle.screen(), me: window.__oracle.text('me-name')};
})()
""")

    navigate(browser, base + "/example/applications/share/index.html")
    wait_share(browser)
    js(browser, INSTALL_SHARE)
    settings = js(browser, """
(() => {
  window.__oracle.setField('name-input', '阿竹');
  window.__oracle.click('start');
  window.__oracle.click('settings');
  const settingsScreen = window.__oracle.screen();
  window.__oracle.setField('set-name', '');
  window.__oracle.click('save-name');
  const emptyName = window.__oracle.error();
  window.__oracle.setField('set-name', '书房电脑');
  window.__oracle.click('save-name');
  const saved = window.__oracle.text('me-name');
  window.__oracle.setField('set-dir', '');
  window.__oracle.click('save-dir');
  const emptyDir = window.__oracle.error();
  const dirInput = 'ZUI-ORACLE-DIR';
  window.__oracle.setField('set-dir', dirInput);
  window.__oracle.click('save-dir');
  return {
    settingsScreen,
    emptyName,
    saved,
    emptyDir,
    dirInput,
    dirField: document.getElementById('set-dir').value,
    dirError: window.__oracle.error()
  };
})()
""")

    navigate(browser, base + "/example/applications/share/index.html")
    wait_share(browser)
    js(browser, INSTALL_SHARE)
    refresh = js(browser, """
(() => {
  window.__oracle.setField('name-input', '阿竹');
  window.__oracle.click('start');
  window.__zuiShare.applyListing([
    {name: 'a.txt', kind: 'doc', size: 1},
    {name: 'b.txt', kind: 'doc', size: 1}
  ]);
  window.__oracle.click('refresh');
  return {names: window.__oracle.names(), screen: window.__oracle.screen()};
})()
""")

    navigate(browser, base + "/example/applications/share/index.html")
    wait_share(browser)
    js(browser, INSTALL_SHARE)
    path = js(browser, """
(() => {
  window.__oracle.setField('name-input', '阿竹');
  window.__oracle.click('start');
  const committed = 'ZUI-ORACLE-PATH';
  const value = window.__oracle.commit('path', committed);
  return {committed, value, screen: window.__oracle.screen()};
})()
""")

    navigate(browser, base + "/example/applications/share/index.html")
    wait_share(browser)
    js(browser, INSTALL_SHARE)
    large = js(browser, """
(() => {
  window.__oracle.setField('name-input', '阿竹');
  window.__oracle.click('start');
  const names = [];
  for (let i = 0; i < 2000; i++) names.push('f' + i + '.txt');
  names.sort();
  window.__zuiShare.applyListing(names.map((name) => ({name, kind: 'doc', size: 1})));
  const rows = window.__oracle.names();
  return {first: rows[0] || '', last: rows.length ? rows[rows.length - 1] : ''};
})()
""")

    navigate(browser, base + "/example/applications/share/index.html")
    wait_share(browser)
    js(browser, INSTALL_SHARE)
    peer = js(browser, """
(() => {
  window.__oracle.setField('name-input', '阿竹');
  window.__oracle.click('start');
  const chip = document.querySelector('.chip');
  chip.click();
  const card = document.querySelector('.peer-card');
  card.click();
  const still = document.body.textContent.includes('客厅电脑');
  return {screen: window.__oracle.screen(), still};
})()
""")

    navigate(browser, base + "/example/applications/share/index.html")
    wait_share(browser)
    js(browser, INSTALL_SHARE)
    send = js(browser, """
(() => {
  window.__oracle.setField('name-input', '阿竹');
  window.__oracle.click('start');
  const row = document.querySelector('.frow.is-file');
  row.click();
  window.__oracle.click('share');
  document.querySelector('[id^="send-"]').click();
  return {screen: window.__oracle.screen()};
})()
""")

    navigate(browser, base + "/example/applications/share/index.html")
    wait_share(browser)
    js(browser, INSTALL_SHARE)
    wait_cancel = js(browser, """
(() => {
  window.__oracle.setField('name-input', '阿竹');
  window.__oracle.click('start');
  document.querySelector('.frow.is-file').click();
  window.__oracle.click('share');
  document.querySelector('[id^="send-"]').click();
  window.__oracle.click('wait-cancel');
  return {screen: window.__oracle.screen()};
})()
""")

    def sheet(button: str) -> dict:
        navigate(browser, base + "/example/applications/share/index.html")
        wait_share(browser)
        js(browser, INSTALL_SHARE)
        return js(browser, f"""
(() => {{
  window.__oracle.setField('name-input', '阿竹');
  window.__oracle.click('start');
  window.__zuiShare.openIncoming();
  {button}
  return {{screen: window.__oracle.screen(), incoming: window.__oracle.incoming()}};
}})()
""")

    accept = sheet("window.__oracle.click('accept');")
    reject = sheet("window.__oracle.click('reject');")
    scrim = sheet("document.querySelector('.scrim').click();")

    def jump(screen: str, button: str) -> dict:
        navigate(browser, base + "/example/applications/share/index.html")
        wait_share(browser)
        js(browser, INSTALL_SHARE)
        return js(browser, f"""
(() => {{
  window.__oracle.setField('name-input', '阿竹');
  window.__oracle.click('start');
  window.__zuiShare.showScreen('{screen}');
  const before = window.__oracle.screen();
  window.__oracle.click('{button}');
  return {{before, screen: window.__oracle.screen()}};
}})()
""")

    cancel = jump("transfer", "xfer-cancel")
    done = jump("done", "done-ok")
    fail = jump("fail", "fail-back")

    navigate(browser, base + "/example/applications/share/index.html")
    wait_share(browser)
    js(browser, INSTALL_SHARE)
    back = js(browser, """
(() => {
  window.__oracle.setField('name-input', '阿竹');
  window.__oracle.click('start');
  window.__oracle.click('settings');
  window.__oracle.click('settings-back');
  return {screen: window.__oracle.screen()};
})()
""")

    return {
        "empty_name_error": empty["error"],
        "empty_name_screen": empty["screen"],
        "typed_me": started["typed"],
        "started_me": started["me"],
        "started_screen": started["screen"],
        "settings_screen": settings["settingsScreen"],
        "empty_save_name_error": settings["emptyName"],
        "saved_name": settings["saved"],
        "empty_dir_error": settings["emptyDir"],
        "save_dir_input": settings["dirInput"],
        "save_dir_field": settings["dirField"],
        "save_dir_error": settings["dirError"],
        "refresh_names": refresh["names"],
        "path_committed": path["committed"],
        "path_value": path["value"],
        "path_screen": path["screen"],
        "large_first": large["first"],
        "large_last": large["last"],
        "peer_screen": peer["screen"],
        "peer_still": peer["still"],
        "send_screen": send["screen"],
        "wait_cancel_screen": wait_cancel["screen"],
        "accept_screen": accept["screen"],
        "accept_incoming": accept["incoming"],
        "reject_screen": reject["screen"],
        "reject_incoming": reject["incoming"],
        "scrim_incoming": scrim["incoming"],
        "fail_screen": fail["before"],
        "xfer_cancel_screen": cancel["screen"],
        "done_screen": done["screen"],
        "fail_back_screen": fail["screen"],
        "settings_back_screen": back["screen"],
        "browse_screen": back["screen"],
    }


INSTALL_FILES = r"""
window.__files = {
  visible(el) {
    if (!el) return false;
    const cs = getComputedStyle(el);
    if (cs.display === 'none' || cs.visibility === 'hidden') return false;
    const rect = el.getBoundingClientRect();
    return rect.width > 0.5 && rect.height > 0.5;
  },
  checked() {
    const out = {};
    for (const el of document.querySelectorAll('input[type=radio]')) out[el.id] = !!el.checked;
    return out;
  },
  shown() {
    const names = %s;
    const out = {};
    for (const name of names) {
      out[name] = [...document.querySelectorAll('.' + name)].some((el) => window.__files.visible(el));
    }
    return out;
  },
  snap() { return {checked: this.checked(), shown: this.shown()}; },
  clickText(text) {
    for (const el of document.querySelectorAll('label, button, span, a')) {
      if (el.textContent.trim() !== text || !this.visible(el)) continue;
      (el.closest('label') || el).click();
      return true;
    }
    return false;
  },
  clickClass(cls, nth) {
    const nodes = [...document.querySelectorAll('.' + cls)].filter((el) => this.visible(el));
    if (nth >= nodes.length) return false;
    nodes[nth].click();
    return true;
  }
};
true
""" % json.dumps(SHOWN)


def file_explorer_readback(browser: Browser, base: str) -> dict:
    navigate(browser, base + "/example/applications/file-explorer/index.html")
    js(browser, INSTALL_FILES)
    radios = js(browser, "[...document.querySelectorAll('input[type=radio]')].map((el) => el.id).sort()")
    steps = [{"action": "snap"}]
    clicks = [
        ("click_text", {"text": "文档"}),
        ("click_text", {"text": "笔记"}),
        ("click_text", {"text": "灯塔摘录.txt"}),
        ("click_text", {"text": "返回"}),
        ("click_text", {"text": "此电脑"}),
        ("click_text", {"text": "使用说明.md"}),
        ("click_class", {"class": "view-btn", "nth": 1}),
        ("click_class", {"class": "view-btn", "nth": 2}),
        ("click_class", {"class": "view-btn", "nth": 0}),
        ("click_text", {"text": "报表"}),
        ("click_text", {"text": "三月账单.csv"}),
        ("click_text", {"text": "书架.json"}),
        ("click_text", {"text": "图片"}),
        ("click_text", {"text": "竹标.svg"}),
        ("click_text", {"text": "海岸.svg"}),
        ("click_text", {"text": "代码"}),
        ("click_text", {"text": "名片.html"}),
        ("click_text", {"text": "主题.css"}),
        ("click_text", {"text": "书目.xml"}),
        ("click_text", {"text": "笔记"}),
        ("click_text", {"text": "渡口手记.md"}),
    ]
    recorded = []
    snap = js(browser, "window.__files.snap()")
    recorded.append({"action": "snap", "checked": snap["checked"], "shown": snap["shown"]})
    for action, fields in clicks:
        if action == "click_text":
            ok = js(browser, "window.__files.clickText(%s)" % json.dumps(fields["text"]))
            if ok is not True:
                raise RuntimeError("file-explorer click missed " + fields["text"])
        else:
            ok = js(
                browser,
                "window.__files.clickClass(%s, %d)" % (json.dumps(fields["class"]), fields["nth"]),
            )
            if ok is not True:
                raise RuntimeError("file-explorer click missed " + fields["class"])
        browser.settle()
        shot = js(browser, "window.__files.snap()")
        step = {"action": action, **fields, "checked": shot["checked"], "shown": shot["shown"]}
        recorded.append(step)
    seen = set()
    for step in recorded:
        for ident, on in step["checked"].items():
            if on:
                seen.add(ident)
    missing = [ident for ident in radios if ident not in seen]
    if missing:
        raise RuntimeError("file-explorer radios never checked: " + ", ".join(missing))
    return {"radios": radios, "steps": recorded, "_planned": steps}


def wait_value(browser: Browser, expression: str, what: str, seconds: float = 8.0):
    deadline = time.time() + seconds
    last = None
    while time.time() < deadline:
        last = js(browser, expression)
        if isinstance(last, str) and last.startswith("ERR "):
            raise RuntimeError(f"{what}: {last}")
        if last not in (None, False, ""):
            return last
        time.sleep(0.05)
    raise RuntimeError(f"timed out waiting for {what}: {last!r}")


def album_readback(browser: Browser, base: str) -> dict:
    navigate(browser, base + "/example/applications/album/index.html")
    wait_value(
        browser,
        """(() => {
          const status = document.getElementById('status');
          if (status && status.classList.contains('is-error')) return 'ERR ' + status.textContent;
          const ready = [...document.querySelectorAll('.album-btn')].some((el) =>
            (el.textContent || '').includes('全部'));
          return ready ? true : null;
        })()""",
        "album seed",
    )
    js(
        browser,
        """(() => {
          const node = [...document.querySelectorAll('.album-btn')].find((el) =>
            (el.textContent || '').includes('渡口'));
          if (!node) return null;
          node.click();
          return true;
        })()""",
    )
    wait_value(
        browser,
        """(() => [...document.querySelectorAll('.tile')].some((el) =>
          (el.textContent || '').includes('海岸')) ? true : null)()""",
        "album coast tile",
    )
    js(
        browser,
        """(() => {
          const node = [...document.querySelectorAll('.tile')].find((el) =>
            (el.textContent || '').includes('海岸'));
          node.click();
          return true;
        })()""",
    )
    opened = wait_value(
        browser,
        """(() => {
          const box = document.getElementById('lightbox');
          const sel = document.getElementById('light-album');
          if (!box || box.hidden || !sel || !sel.value) return null;
          const labels = [...sel.options].map((opt) => (opt.textContent || '').trim()).filter(Boolean);
          labels.sort();
          return { light_value: sel.value, option_labels: labels };
        })()""",
        "album lightbox",
    )
    js(
        browser,
        """(() => {
          const sel = document.getElementById('light-album');
          const opt = [...sel.options].find((item) => (item.textContent || '').trim() === '灯塔');
          if (!opt) return null;
          sel.value = opt.value;
          sel.dispatchEvent(new Event('change', { bubbles: true }));
          return true;
        })()""",
    )
    closed = wait_value(
        browser,
        """(() => {
          const box = document.getElementById('lightbox');
          const count = document.getElementById('album-count');
          if (!box || !box.hidden || !count) return null;
          const text = (count.textContent || '').trim();
          if (!text) return null;
          return { after_lightbox: 'false', after_count: text };
        })()""",
        "album move",
    )
    return {
        "light_value": opened["light_value"],
        "option_labels": opened["option_labels"],
        "move_to_label": "灯塔",
        "after_lightbox": closed["after_lightbox"],
        "after_count": closed["after_count"],
    }


def kanban_readback(browser: Browser, base: str) -> dict:
    navigate(browser, base + "/example/applications/kanban/index.html")
    opened = wait_value(
        browser,
        """(() => {
          const names = [...document.querySelectorAll('.col-name')].map((el) => el.value);
          const card = document.querySelector('.card');
          if (!names.length || !card) return null;
          const rect = card.getBoundingClientRect();
          const x = rect.left + rect.width / 2;
          const y = rect.top + Math.min(12, rect.height / 2);
          const down = { bubbles: true, clientX: x, clientY: y, button: 0, pointerId: 1 };
          card.dispatchEvent(new PointerEvent('pointerdown', down));
          document.dispatchEvent(new PointerEvent('pointerup', down));
          const editor = document.getElementById('editor');
          const title = document.getElementById('f-title');
          if (!editor || editor.hidden || !title || !title.value) return null;
          return {
            column_names: names,
            open_title: title.value,
            editor_visible: 'true',
            editor_title: title.value
          };
        })()""",
        "kanban editor",
    )
    js(browser, "document.getElementById('editor-cancel').click(); true")
    added = wait_value(
        browser,
        """(() => {
          const input = document.querySelector('.adder input');
          if (!input) return null;
          input.value = '第六节图表';
          input.form.requestSubmit();
          const titles = [...document.querySelectorAll('.card-title')].map((el) =>
            (el.textContent || '').trim());
          return titles.includes('第六节图表') ? '第六节图表' : null;
        })()""",
        "kanban add",
    )
    opened["added_title"] = added
    return opened


def particle_readback(browser: Browser, base: str) -> dict:
    navigate(browser, base + "/example/applications/particle/index.html")
    wait_value(
        browser,
        """(() => document.getElementById('effect-fountain') &&
          document.getElementById('effect-fountain').classList.contains('is-on') ? true : null)()""",
        "particle fountain",
    )
    js(browser, "document.getElementById('effect-snow').click(); true")
    snow = wait_value(
        browser,
        """(() => {
          const snow = document.getElementById('effect-snow');
          const count = document.getElementById('count-value');
          if (!snow || !snow.classList.contains('is-on') || !count) return null;
          const log = [...document.querySelectorAll('.log-line')].map((el) =>
            (el.textContent || '').trim()).join('\\n');
          if (!log.includes('飘雪')) return null;
          return {
            snow_log: '飘雪',
            snow_dots: document.querySelectorAll('.dot').length,
            count_text: (count.textContent || '').trim()
          };
        })()""",
        "particle snow",
    )
    js(browser, "document.getElementById('count-inc').click(); true")
    raised = wait_value(
        browser,
        """(() => {
          const count = document.getElementById('count-value');
          const text = count ? (count.textContent || '').trim() : '';
          const dots = document.querySelectorAll('.dot').length;
          if (dots !== 13 || !text.includes('13')) return null;
          const log = [...document.querySelectorAll('.log-line')].map((el) =>
            (el.textContent || '').trim()).join('\\n');
          const line = log.split('\\n').filter((item) => item.includes('数量')).pop() || '';
          return { raised_dots: dots, raised_count_text: text, raised_log: line };
        })()""",
        "particle count",
    )
    snow.update(raised)
    return snow


def writer_readback(browser: Browser, base: str) -> dict:
    navigate(browser, base + "/example/applications/writer/index.html")
    # desk.html (new-piece) is inserted before bar.html (save). Wait for the bar.
    wait_value(
        browser,
        "document.getElementById('save') && document.getElementById('new-piece') ? true : null",
        "writer page",
    )
    created = wait_value(
        browser,
        """(() => {
          document.getElementById('new-piece').click();
          const title = (document.getElementById('doc-title').textContent || '').trim();
          const status = (document.getElementById('status').textContent || '').trim();
          if (!title || title === '未选择') return null;
          return { new_title: title, new_status: status };
        })()""",
        "writer new piece",
    )

    def put(text: str) -> None:
        js(
            browser,
            """(() => {
              const el = document.getElementById('editor');
              el.focus();
              el.value = %s;
              el.dispatchEvent(new InputEvent('input', { bubbles: true }));
              return el.value;
            })()"""
            % json.dumps(text, ensure_ascii=False),
        )

    put("旧稿")
    js(browser, "document.getElementById('save').click(); true")
    put("旧稿 新稿")
    js(browser, "document.getElementById('save').click(); true")
    js(browser, "document.getElementById('view-history').click(); true")
    history = wait_value(
        browser,
        """(() => {
          const snap = document.querySelector('.snap');
          if (!snap) return null;
          snap.click();
          const body = (document.getElementById('history-body').textContent || '').trim();
          const editor = document.getElementById('editor').value;
          if (!body) return null;
          return { history_body: body, second_body: editor, first_body: '旧稿' };
        })()""",
        "writer history",
    )
    created.update(history)
    return created


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", type=Path, default=HERE / "browser_oracle.json")
    args = parser.parse_args()
    env = load_env(ENV_PATH)
    try:
        exe = find_browser(env)
    except Exception as exc:  # noqa: BLE001 - the launcher error is the result
        log(f"browser launch failed: {exc}")
        return 1
    server, thread = serve()
    port = server.server_address[1]
    base = f"http://127.0.0.1:{port}"
    log(f"serving {REPO} at {base}")
    log(f"browser: {exe}")
    browser = Browser(exe, env, VIEWPORT, log=log)
    try:
        browser.start()
        log(f"started {browser.browser_version}")
        share = share_readback(browser, base)
        log("share readback recorded")
        files = file_explorer_readback(browser, base)
        files.pop("_planned", None)
        log(f"file-explorer readback recorded ({len(files['steps'])} steps)")
        album = album_readback(browser, base)
        log("album readback recorded")
        kanban = kanban_readback(browser, base)
        log("kanban readback recorded")
        particle = particle_readback(browser, base)
        log("particle readback recorded")
        writer = writer_readback(browser, base)
        log("writer readback recorded")
        from browser_walk import record_walk

        walk = record_walk(browser, base, navigate, js, wait_value, log)
        log("walk recorded")
    except Exception as exc:  # noqa: BLE001
        log(f"browser readback failed: {exc}")
        log(traceback.format_exc())
        return 1
    finally:
        browser.stop()
        server.shutdown()
        thread.join(timeout=5)
    payload = {
        "share": share,
        "file-explorer": files,
        "album": album,
        "kanban": kanban,
        "particle": particle,
        "writer": writer,
        "walk": walk,
    }
    text = json.dumps(payload, ensure_ascii=False, indent=2) + "\n"
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(text, encoding="utf-8")
    log(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
