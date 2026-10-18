"""Compare one reader interaction scenario on Chrome and on the native session.

Chrome loads example/applications/reader with its script and the fixture shelf
(tests/fixtures/reader). zui_reader_oracle loads the same HTML, binds the C++
session, and plays the same actions. Both write a mark → id → field record.
Class values compare as token sets. Text and checked compare as strings.

A click is DOM activation of that id (element.click / frame::Access::click_id), not a
pixel hit. Hit-testing stays in the difftest dispatch cases.

  python tests/interact/reader_oracle.py --oracle build/windows-x64/zui_reader_oracle.exe
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import threading
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests" / "difftest" / "chrome"))

from cdp import CdpError, CdpSession, find_page_target  # noqa: E402

FIXTURE = ROOT / "tests" / "fixtures" / "reader"
PAGE = ROOT / "example" / "applications" / "reader"
ENV = json.loads((ROOT / "tests" / "difftest" / "env.json").read_text(encoding="utf-8"))


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def find_browser() -> str:
    override = os.environ.get("ZUI_DIFFTEST_BROWSER", "")
    if override and Path(override).is_file():
        return override
    for candidate in ENV["browser"]["search_paths"]:
        if Path(candidate).is_file():
            return candidate
    raise SystemExit("no Chrome/Edge binary; set ZUI_DIFFTEST_BROWSER")


def stage_site(dest: Path) -> None:
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True)
    for name in ("reader.html", "reader.css", "reader.js", "manifest.webmanifest", "icon.svg"):
        source = PAGE / name
        if source.is_file():
            shutil.copy2(source, dest / name)
    books = dest / "books"
    books.mkdir()
    shutil.copy2(FIXTURE / "catalog.json", books / "catalog.json")
    shutil.copy2(FIXTURE / "sample.txt", books / "sample.txt")


class Site:
    def __init__(self, root: Path):
        handler = partial(SimpleHTTPRequestHandler, directory=str(root))
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        self.server.daemon_threads = True
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)

    @property
    def url(self) -> str:
        host, port = self.server.server_address[:2]
        return f"http://{host}:{port}/reader.html"

    def __enter__(self) -> "Site":
        self.thread.start()
        return self

    def __exit__(self, *exc) -> None:
        self.server.shutdown()
        self.thread.join(timeout=5)


class Chrome:
    def __init__(self, exe: str):
        self.exe = exe
        self.port = free_port()
        self.profile = Path(os.environ.get("TEMP", ".")) / f"zui-reader-oracle-{self.port}"
        self.process: subprocess.Popen | None = None
        self.session: CdpSession | None = None

    def __enter__(self) -> "Chrome":
        if self.profile.exists():
            shutil.rmtree(self.profile, ignore_errors=True)
        args = [self.exe, *ENV["browser"]["pinned_flags"],
                f"--remote-debugging-port={self.port}",
                f"--user-data-dir={self.profile}",
                "--window-size=1100,720", "about:blank"]
        flags = 0
        if os.name == "nt":
            flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
        self.process = subprocess.Popen(
            args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=flags
        )
        ws = find_page_target(self.port, timeout=30)
        self.session = CdpSession(ws, timeout=60)
        self.session.call("Page.enable")
        self.session.call("Runtime.enable")
        self.session.call(
            "Emulation.setDeviceMetricsOverride",
            {"width": 1100, "height": 720, "deviceScaleFactor": 1, "mobile": False,
             "screenWidth": 1100, "screenHeight": 720},
        )
        return self

    def __exit__(self, *exc) -> None:
        if self.session is not None:
            try:
                self.session.close()
            except Exception:
                pass
        if self.process is not None:
            self.process.terminate()
            try:
                self.process.wait(timeout=8)
            except subprocess.TimeoutExpired:
                self.process.kill()
        shutil.rmtree(self.profile, ignore_errors=True)

    def evaluate(self, expression: str, await_promise: bool = False):
        result = self.session.call(
            "Runtime.evaluate",
            {"expression": expression, "returnByValue": True, "awaitPromise": await_promise,
             "userGesture": True},
            timeout=30,
        )
        if result.get("exceptionDetails"):
            detail = result["exceptionDetails"]
            raise CdpError(detail.get("text", "evaluate failed"))
        return result.get("result", {}).get("value")

    def goto(self, url: str) -> None:
        self.session.call("Page.navigate", {"url": url})
        self.session.wait_event("Page.loadEventFired", timeout=30)
        self.evaluate(
            "document.fonts.ready.then(function(){return true;})", await_promise=True
        )

    def wait_boot(self) -> None:
        script = (
            "(function(){return new Promise(function(resolve){"
            "var n=0;function tick(){"
            "var shelf=document.getElementById('shelf');"
            "var card=document.querySelector('.card');"
            "if(shelf && !shelf.classList.contains('is-booting') && card){resolve('ok');return;}"
            "if(++n>100){resolve('timeout');return;}"
            "setTimeout(tick,50);}"
            "tick();});})()"
        )
        status = self.evaluate(script, await_promise=True)
        if status != "ok":
            raise SystemExit("browser shelf did not finish booting")

    def settle(self) -> None:
        self.evaluate(
            "(function(){return new Promise(function(resolve){var n=4;"
            "(function tick(){if(n--<=0){resolve(true);return;}requestAnimationFrame(tick);})();"
            "});})()",
            await_promise=True,
        )

    def wait_chapter(self) -> None:
        script = (
            "(function(){return new Promise(function(resolve){"
            "var n=0;function tick(){"
            "var title=document.querySelector('.chapter-title');"
            "if(title && title.textContent){resolve('ok');return;}"
            "if(++n>80){resolve('timeout');return;}"
            "setTimeout(tick,50);}"
            "tick();});})()"
        )
        self.evaluate(script, await_promise=True)

    def click(self, action: dict) -> None:
        selector = action.get("browser") or ("#" + action["id"])
        script = (
            "(function(){var el=document.querySelector(%s);"
            "if(!el)return 'missing';el.click();return 'ok';})()"
            % json.dumps(selector)
        )
        found = self.evaluate(script)
        if found != "ok":
            raise SystemExit(f"browser has no {selector}")
        self.settle()

    def key(self, key: str) -> None:
        self.evaluate("if(document.activeElement)document.activeElement.blur();document.body.focus();")
        for kind in ("keyDown", "keyUp"):
            self.session.call(
                "Input.dispatchKeyEvent",
                {"type": kind, "key": key, "code": key, "windowsVirtualKeyCode": 0,
                 "nativeVirtualKeyCode": 0},
            )
        self.settle()

    def observe(self, query: list) -> dict:
        script = (
            "(function(){var spec=%s;var out={};spec.forEach(function(q){"
            "var el=q.selector?document.querySelector(q.selector):document.getElementById(q.id);"
            "var node={};(q.fields||[]).forEach(function(f){"
            "if(!el){node[f]='';return;}"
            "if(f==='text')node.text=el.textContent||'';"
            "else if(f==='class')node.class=el.className||'';"
            "else if(f==='checked')node.checked=el.checked?'true':'false';"
            "});out[q.id]=node;});return JSON.stringify(out);})()"
            % json.dumps(query)
        )
        return json.loads(self.evaluate(script))

    def play(self, scenario: dict) -> dict:
        marks = {"boot": self.observe(scenario["query"])}
        opened = False
        for action in scenario["actions"]:
            op = action["do"]
            if op == "click":
                self.click(action)
                if action.get("id") == "open0" and not opened:
                    self.wait_chapter()
                    opened = True
            elif op == "key":
                self.key(action["key"])
            elif op == "mark":
                marks[action["name"]] = self.observe(scenario["query"])
            else:
                raise SystemExit(f"unknown action {op}")
        return marks


def tokens(value: str) -> list[str]:
    return sorted(part for part in str(value).split() if part)


def diff_marks(browser: dict, native: dict) -> list[str]:
    rows = []
    names = list(dict.fromkeys([*browser.keys(), *native.keys()]))
    for name in names:
        left = browser.get(name)
        right = native.get(name)
        if left is None or right is None:
            rows.append(f"{name}: missing on {'browser' if left is None else 'native'}")
            continue
        ids = list(dict.fromkeys([*left.keys(), *right.keys()]))
        for ident in ids:
            lnode = left.get(ident, {})
            rnode = right.get(ident, {})
            fields = list(dict.fromkeys([*lnode.keys(), *rnode.keys()]))
            for field in fields:
                lv = lnode.get(field, "")
                rv = rnode.get(field, "")
                if field == "class":
                    if tokens(lv) != tokens(rv):
                        rows.append(f"{name} #{ident} class browser={tokens(lv)} native={tokens(rv)}")
                elif lv != rv:
                    rows.append(f"{name} #{ident} {field}\n  browser: {lv}\n  native:  {rv}")
    return rows


def run_native(oracle: Path, scenario: Path, out: Path) -> dict:
    result = subprocess.run(
        [str(oracle), "--scenario", str(scenario), "--out", str(out)],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise SystemExit(result.stderr or result.stdout or f"oracle exited {result.returncode}")
    return json.loads(out.read_text(encoding="utf-8"))


def main() -> int:
    parser = argparse.ArgumentParser(description="reader interaction oracle")
    parser.add_argument("--oracle", type=Path, required=True)
    parser.add_argument("--scenario", type=Path, required=True)
    args = parser.parse_args()
    scenario = json.loads(args.scenario.read_text(encoding="utf-8"))
    report_dir = ROOT / "build" / "interact" / scenario["name"]
    report_dir.mkdir(parents=True, exist_ok=True)
    stage = report_dir / "site"
    stage_site(stage)
    native_path = report_dir / "native.json"
    native = run_native(args.oracle, args.scenario, native_path)
    with Site(stage) as site, Chrome(find_browser()) as chrome:
        chrome.goto(site.url)
        chrome.wait_boot()
        browser = chrome.play(scenario)
    (report_dir / "browser.json").write_text(
        json.dumps(browser, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    rows = diff_marks(browser, native)
    if not rows:
        print(f"{scenario['name']}: browser and native agree ({len(browser)} marks)")
        return 0
    print(f"{scenario['name']}: {len(rows)} interaction differences")
    for row in rows:
        print(row)
    print(f"records: {report_dir}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
