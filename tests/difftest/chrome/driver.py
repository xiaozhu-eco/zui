"""Browser-side driver for the differential test harness.

Runs the same `steps` the engine driver runs (see docs/TESTPLAN.md section 3) and writes one
trace JSON plus one PNG per `trace`/`capture` step, in the same layout, so the differ can pair
the artifacts by file name.

The browser is launched with the flag set pinned in env.json (section 8). `eval` is translated
from the closed action DSL, never from raw JS, so a case cannot use an API the engine side has
no equivalent for.

Usage:
  python tests/difftest/chrome/driver.py --case <case.json> --out <dir> [--env env.json]
                                         [--browser <exe>] [--quiet]
"""

from __future__ import annotations

import argparse
import base64
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from cdp import CdpError, CdpSession, find_page_target  # noqa: E402

HERE = Path(__file__).resolve().parent
DEFAULT_ENV = HERE.parent / "env.json"
COLLECTOR = HERE / "collector.js"


def parent_dir(path: str) -> str:
    return os.path.dirname(os.path.abspath(path))


def step_name(index: int, op: str) -> str:
    return f"step-{index:03d}-{op}"


def load_env(path: Path) -> dict:
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def find_browser(env: dict, override: str | None = None) -> str:
    if override:
        return override
    from_env = os.environ.get("ZUI_DIFFTEST_BROWSER")
    if from_env:
        return from_env
    for candidate in env.get("browser", {}).get("search_paths", []):
        if os.path.exists(candidate):
            return candidate
    raise RuntimeError(
        "no browser found. Install Chrome/Edge or set ZUI_DIFFTEST_BROWSER to its path."
    )


def file_url(path: str) -> str:
    return "file:///" + os.path.abspath(path).replace("\\", "/").lstrip("/")


# ---------------------------------------------------------------- action DSL


def _target_js(selector: str) -> str:
    return (
        "(function(){var s=%s;if(s===''||s==='window'||s===':root'){return document.documentElement;}"
        "if(s.charAt(0)==='#'){return document.getElementById(s.slice(1));}"
        "return document.querySelector(s);})()" % json.dumps(selector)
    )


def action_to_js(action: dict) -> str:
    """Translates one case action into the browser equivalent.

    The vocabulary is closed on purpose (TESTPLAN section 3: `eval` may only use the API subset
    both ends implement). An unknown action is a hard error, not a silent skip.
    """
    what = action.get("do")
    target = action.get("target", "")
    node = _target_js(target)
    if what == "set_style":
        props = json.dumps(action.get("props", {}))
        return (
            "(function(){var el=%s;if(!el){throw new Error('set_style: no node');}"
            "el.style.cssText='';var p=%s;for(var k in p){el.style.setProperty(k,p[k]);}})()"
            % (node, props)
        )
    if what == "add_class":
        return f"(function(){{var el={node};if(!el)throw new Error('add_class: no node');el.classList.add({json.dumps(action.get('name', ''))});}})()"
    if what == "remove_class":
        return f"(function(){{var el={node};if(!el)throw new Error('remove_class: no node');el.classList.remove({json.dumps(action.get('name', ''))});}})()"
    if what == "set_text":
        return f"(function(){{var el={node};if(!el)throw new Error('set_text: no node');el.textContent={json.dumps(action.get('text', ''))};}})()"
    if what == "set_checked":
        return (
            f"(function(){{var el={node};if(!el)throw new Error('set_checked: no node');"
            f"el.checked={str(bool(action.get('value', True))).lower()};}})()"
        )
    if what == "set_attr":
        return (
            f"(function(){{var el={node};if(!el)throw new Error('set_attr: no node');"
            f"el.setAttribute({json.dumps(action.get('name', ''))},{json.dumps(action.get('value', ''))});}})()"
        )
    if what == "remove_attr":
        return (
            f"(function(){{var el={node};if(!el)throw new Error('remove_attr: no node');"
            f"el.removeAttribute({json.dumps(action.get('name', ''))});}})()"
        )
    if what == "select_text":
        # Document-level selection: the engine sets `doc.selection` on the same half-open range of
        # `node->text`, and both ends paint from that. `end` omitted → select to the end of the text.
        start = int(action.get("start", 0))
        end = action.get("end")
        end_expr = "n" if end is None else f"Math.min({int(end)},n)"
        return (
            f"(function(){{var el={node};if(!el)throw new Error('select_text: no node');"
            f"var t=el.firstChild;if(!t||t.nodeType!==3)throw new Error('select_text: no text node');"
            f"var n=t.textContent.length;var r=document.createRange();"
            f"r.setStart(t,Math.max(0,Math.min({start},n)));r.setEnd(t,{end_expr});"
            f"var s=window.getSelection();s.removeAllRanges();s.addRange(r);}})()"
        )
    if what == "scroll":
        return (
            f"(function(){{var el={node};if(!el)throw new Error('scroll: no node');"
            f"el.scrollLeft={float(action.get('x', 0))};el.scrollTop={float(action.get('y', 0))};}})()"
        )
    if what == "remove":
        return (
            f"(function(){{var el={node};if(!el)throw new Error('remove: no node');el.remove();}})()"
        )
    if what == "insert":
        props = json.dumps(action.get("props", {}))
        before = action.get("before", "")
        anchor = _target_js(before) if before else "null"
        return (
            "(function(){var host=%s;if(!host)throw new Error('insert: no node');"
            "var el=document.createElement(%s);"
            "el.id=%s;el.className=%s;el.textContent=%s;"
            "var p=%s;for(var k in p){el.style.setProperty(k,p[k]);}"
            "var anchor=%s;"
            "if(anchor){host.insertBefore(el,anchor);}else{host.appendChild(el);}})()"
            % (
                node,
                json.dumps(action.get("tag", "div")),
                json.dumps(action.get("id", "")),
                json.dumps(action.get("class", "")),
                json.dumps(action.get("text", "")),
                props,
                anchor,
            )
        )
    raise RuntimeError(f"unsupported action '{what}'")


_KEY_CODES = {
    "ArrowDown": (40, "ArrowDown"),
    "ArrowUp": (38, "ArrowUp"),
    "ArrowLeft": (37, "ArrowLeft"),
    "ArrowRight": (39, "ArrowRight"),
    "PageDown": (34, "PageDown"),
    "PageUp": (33, "PageUp"),
    " ": (32, "Space"),
    "Space": (32, "Space"),
    "Tab": (9, "Tab"),
    "Enter": (13, "Enter"),
}


def readback_js(spec: dict) -> str:
    return "JSON.stringify(window.__zuiDifftestReadback(%s))" % json.dumps(spec)


def scroll_into_view_js(target: str, block: str) -> str:
    node = _target_js(target)
    # The engine applies one alignment to both axes. Match that, including `behavior: instant`
    # so the readback is the end position and not a smooth-scroll frame.
    return (
        "(function(){var el=%s;if(!el)throw new Error('scroll_into_view: no node');"
        "el.scrollIntoView({block:%s,inline:%s,behavior:'instant'});return true;})()"
        % (node, json.dumps(block), json.dumps(block))
    )


def scroll_step_js(target: str, x: float, y: float) -> str:
    node = _target_js(target)
    if target in ("", "window", ":root"):
        return (
            f"window.scrollTo({{left:{x},top:{y},behavior:'instant'}});"
            "JSON.stringify([window.scrollX,window.scrollY])"
        )
    return (
        f"(function(){{var el={node};if(!el)throw new Error('scroll: no node');"
        f"el.scrollLeft={x};el.scrollTop={y};return JSON.stringify([el.scrollLeft,el.scrollTop]);}})()"
    )


def freeze_js(milliseconds: float) -> str:
    # Every animation on the document, including CSS transitions, is on the same timeline;
    # replacing rAF would not reach them (TESTPLAN section 5).
    return (
        "(function(){var n=0;document.getAnimations({subtree:true}).forEach(function(a){"
        f"a.pause();a.currentTime={milliseconds};n++;}});return n;}})()"
    )


class Browser:
    def __init__(self, exe: str, env: dict, viewport: tuple[int, int], log=None):
        self.exe = exe
        self.env = env
        self.viewport = viewport
        self.log = log or (lambda *_: None)
        self.profile = tempfile.mkdtemp(prefix="zui-difftest-")
        self.process: subprocess.Popen | None = None
        self.session: CdpSession | None = None
        self.browser_version = ""
        self.extra_unsupported: list[dict] = []

    # -- lifecycle ---------------------------------------------------------

    def start(self) -> None:
        port = int(self.env.get("browser", {}).get("debug_port", 9333))
        args = [self.exe]
        args += list(self.env.get("browser", {}).get("pinned_flags", []))
        args.append(f"--remote-debugging-port={port}")
        args.append(f"--user-data-dir={self.profile}")
        args.append(f"--window-size={self.viewport[0]},{self.viewport[1]}")
        args.append("about:blank")
        self.log(f"  browser: {self.exe}")
        self.log(f"  flags:   {' '.join(args[2:-1])}")
        creationflags = 0
        if os.name == "nt":
            creationflags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
        self.process = subprocess.Popen(
            args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=creationflags
        )
        ws_url = find_page_target(port, timeout=float(self.env.get("browser", {}).get("navigate_timeout_s", 30)))
        self.session = CdpSession(ws_url, timeout=60.0)
        self.session.call("Page.enable")
        self.session.call("Runtime.enable")
        version = self.session.call("Browser.getVersion")
        self.browser_version = version.get("product", "")
        self.set_viewport(*self.viewport)

    def stop(self) -> None:
        if self.session is not None:
            try:
                self.session.close()
            except Exception:  # noqa: BLE001 - teardown must not mask the test result
                pass
        if self.process is not None:
            self.process.terminate()
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.process.kill()
        shutil.rmtree(self.profile, ignore_errors=True)

    def __enter__(self) -> "Browser":
        self.start()
        return self

    def __exit__(self, *exc) -> None:
        self.stop()

    # -- primitives --------------------------------------------------------

    def set_viewport(self, width: int, height: int) -> None:
        self.viewport = (int(width), int(height))
        self.session.call(
            "Emulation.setDeviceMetricsOverride",
            {
                "width": int(width),
                "height": int(height),
                "deviceScaleFactor": 1,
                "mobile": False,
                "screenWidth": int(width),
                "screenHeight": int(height),
            },
        )

    def install_instrumentation(self, config: dict) -> None:
        prelude = "window.__zuiDifftest = " + json.dumps(config) + ";"
        collector = COLLECTOR.read_text(encoding="utf-8")
        # Kept as a new-document script for the normal case, and re-evaluated on demand for a
        # document whose own scripts are disabled (see `ensure_collector`).
        self.instrumentation_source = prelude + "\n" + collector
        self.session.call(
            "Page.addScriptToEvaluateOnNewDocument", {"source": self.instrumentation_source}
        )

    def ensure_collector(self) -> None:
        installed = self.session.evaluate("typeof window.__zuiDifftestCollect")
        if installed != "function":
            self.session.evaluate(self.instrumentation_source)

    def navigate(self, path: str, timeout: float = 30.0) -> None:
        self.session.call("Page.navigate", {"url": file_url(path)})
        self.session.wait_event("Page.loadEventFired", timeout=timeout)
        # TESTPLAN section 8: nothing may be sampled before document.fonts.ready.
        self.session.evaluate(
            "document.fonts.ready.then(function(){return true;})", await_promise=True, timeout=timeout
        )
        self.settle()

    def settle(self, frames: int = 2) -> None:
        self.session.evaluate(
            "(function(){return new Promise(function(resolve){var n=%d;"
            "(function tick(){if(n--<=0){resolve(true);return;}requestAnimationFrame(tick);})();"
            "});})()" % frames,
            await_promise=True,
        )

    def evaluate(self, expression: str):
        return self.session.evaluate(expression)

    def capture_png(self) -> bytes:
        result = self.session.call("Page.captureScreenshot", {"format": "png", "fromSurface": True})
        return base64.b64decode(result["data"])

    def set_virtual_time(self, budget_ms: float) -> None:
        try:
            self.session.call(
                "Emulation.setVirtualTimePolicy",
                {"policy": "advance", "budget": float(budget_ms)},
            )
            self.session.wait_event("Emulation.virtualTimeBudgetExpired", timeout=30.0)
        except CdpError as exc:
            self.extra_unsupported.append(
                {"op": "advance", "reason": f"virtual time policy unavailable: {exc}"}
            )


def _write_readback(browser: Browser, out_dir: str, name: str, spec: dict, summary: dict) -> None:
    browser.ensure_collector()
    payload = browser.evaluate(readback_js(spec))
    state = json.loads(payload)
    artifact = os.path.join(out_dir, name + ".state.json")
    with open(artifact, "w", encoding="utf-8") as handle:
        json.dump(state, handle, indent=2, ensure_ascii=False)
        handle.write("\n")
    summary["artifacts"].append(artifact)


def _dispatch_cdp(browser: Browser, event: dict) -> None:
    kind = event.get("type", "")
    if kind in ("keyDown", "keyUp"):
        key = event.get("key", "")
        code = _KEY_CODES.get(key, (0, key))
        browser.session.call(
            "Input.dispatchKeyEvent",
            {
                "type": kind,
                "key": key,
                "code": code[1],
                "windowsVirtualKeyCode": code[0],
                "nativeVirtualKeyCode": code[0],
            },
        )
        return
    params = {
        "type": kind,
        "x": float(event.get("x", 0)),
        "y": float(event.get("y", 0)),
        "button": event.get("button", "left"),
        "clickCount": int(event.get("count", 1)),
    }
    if kind == "mouseWheel":
        params["deltaX"] = float(event.get("deltaX", 0))
        params["deltaY"] = float(event.get("deltaY", 0))
    browser.session.call("Input.dispatchMouseEvent", params)


# ---------------------------------------------------------------- case runner


def run_case(case_path: str, out_dir: str, env: dict, browser_exe: str, log=None) -> dict:
    log = log or (lambda *_: None)
    with open(case_path, "r", encoding="utf-8") as handle:
        case = json.load(handle)
    case_dir = parent_dir(case_path)
    page = os.path.join(case_dir, case.get("page", "page.html"))
    viewport = case.get("viewport", env.get("browser", {}).get("window_size", [800, 600]))

    # The engine narrows to the case's own list when it declares one; match that exactly so
    # both ends hash the same ordered property stream.
    if "style_props" in case:
        props = list(case["style_props"])
    else:
        props = list(env.get("_props", []))
    config = {
        "props": props,
        "lines": True,
        "breaks": bool(case.get("text", {}).get("breaks", False)),
        "advances": bool(case.get("text", {}).get("advances", False)),
        "engine": "chromium",
        "engineVersion": "",
    }

    os.makedirs(out_dir, exist_ok=True)
    summary = {
        "schema": "zui.difftest.browser-summary/1",
        "case": case.get("name", "unnamed"),
        "browser": browser_exe,
        "browser_version": "",
        "steps": [],
        "artifacts": [],
        "unsupported": [],
        "errors": [],
        "traces": 0,
        "captures": 0,
    }

    with Browser(browser_exe, env, (int(viewport[0]), int(viewport[1])), log=log) as browser:
        summary["browser_version"] = browser.browser_version
        config["engineVersion"] = browser.browser_version
        browser.install_instrumentation(config)

        index = 0
        for step in case.get("steps", []):
            op = step.get("op", "")
            name = step_name(index, op)
            index += 1
            try:
                if op == "load":
                    browser.navigate(page)
                elif op == "resize":
                    browser.set_viewport(step.get("w", 800), step.get("h", 600))
                    browser.settle()
                elif op == "eval":
                    for action in step.get("actions", []):
                        if action.get("do") == "select_text":
                            # An unfocused document does not paint its selection, so the injection
                            # would capture as "no highlight". Focus emulation is scoped to the
                            # cases that set a selection: it must not change how the other goldens
                            # render (`:focus` / caret / `:focus-visible`).
                            browser.session.call(
                                "Emulation.setFocusEmulationEnabled", {"enabled": True}
                            )
                        browser.evaluate(action_to_js(action))
                    browser.settle()
                    if step.get("readback"):
                        # Scroll anchoring (TESTPLAN section 4): the insertion step has to show
                        # the scroller's offset absorbing the content it grew. A `scroll` step
                        # would set the offset it is meant to observe.
                        _write_readback(browser, out_dir, name, {
                            "op": "eval",
                            "target": step.get("target", "window"),
                            "hits": step.get("hits", []),
                        }, summary)
                elif op == "scroll":
                    browser.evaluate(
                        scroll_step_js(
                            step.get("target", "window"),
                            float(step.get("x", 0)),
                            float(step.get("y", 0)),
                        )
                    )
                    browser.settle(1)
                    _write_readback(browser, out_dir, name, {
                        "op": "scroll",
                        "target": step.get("target", "window"),
                        "hits": step.get("hits", []),
                    }, summary)
                elif op == "scroll_into_view":
                    block = step.get("block", "start")
                    if block not in ("start", "center", "end", "nearest"):
                        raise ValueError("scroll_into_view block must be start, center, end, or nearest")
                    browser.evaluate(scroll_into_view_js(step.get("target", ""), block))
                    browser.settle(1)
                    _write_readback(browser, out_dir, name, {
                        "op": "scroll_into_view",
                        "target": step.get("target", ""),
                        "hits": step.get("hits", []),
                    }, summary)
                elif op in ("freeze", "advance"):
                    if op == "freeze":
                        browser.evaluate(freeze_js(float(step.get("t", 0))))
                    else:
                        browser.set_virtual_time(float(step.get("dt", 0)))
                    summary["unsupported"].extend(browser.extra_unsupported)
                    browser.extra_unsupported = []
                elif op == "dispatch":
                    events = step.get("events", [])
                    for event in events:
                        _dispatch_cdp(browser, event)
                    browser.settle(1)
                    hits = list(step.get("hits", []))
                    if not hits:
                        for event in reversed(events):
                            if event.get("type", "").startswith("mouse") and "x" in event:
                                hits = [{"x": event.get("x", 0), "y": event.get("y", 0)}]
                                break
                    _write_readback(browser, out_dir, name, {
                        "op": "dispatch",
                        "target": step.get("target", "window"),
                        "hits": hits,
                    }, summary)
                elif op == "trace":
                    browser.ensure_collector()
                    payload = browser.evaluate("JSON.stringify(window.__zuiDifftestCollect())")
                    trace = json.loads(payload)
                    artifact = os.path.join(out_dir, name + ".trace.json")
                    with open(artifact, "w", encoding="utf-8") as handle:
                        json.dump(trace, handle, indent=2, ensure_ascii=False)
                        handle.write("\n")
                    summary["artifacts"].append(artifact)
                    summary["traces"] += 1
                    log(f"  trace {name} elements={len(trace.get('elements', []))}")
                elif op == "capture":
                    artifact = os.path.join(out_dir, name + ".png")
                    with open(artifact, "wb") as handle:
                        handle.write(browser.capture_png())
                    summary["artifacts"].append(artifact)
                    summary["captures"] += 1
                    log(f"  capture {name}")
                else:
                    raise RuntimeError(f"unknown op '{op}'")
                summary["steps"].append({"step": name, "op": op, "status": "ok"})
            except (CdpError, RuntimeError) as exc:
                summary["steps"].append({"step": name, "op": op, "status": "error"})
                summary["errors"].append({"step": name, "error": str(exc)})
                break

    summary["status"] = "ok" if not summary["errors"] else "error"
    with open(os.path.join(out_dir, "browser-summary.json"), "w", encoding="utf-8") as handle:
        json.dump(summary, handle, indent=2, ensure_ascii=False)
        handle.write("\n")
    return summary


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="zui difftest browser driver")
    parser.add_argument("--case", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--env", default=str(DEFAULT_ENV))
    parser.add_argument("--browser", default=None)
    parser.add_argument("--props", default=None, help="props.json, for the L1 property list")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args(argv)

    env = load_env(Path(args.env))
    props_path = Path(args.props) if args.props else Path(args.env).parent / "props.json"
    if props_path.exists():
        with open(props_path, "r", encoding="utf-8") as handle:
            env["_props"] = json.load(handle).get("default_props", [])

    exe = find_browser(env, args.browser)
    log = (lambda *a: None) if args.quiet else (lambda *a: print(*a, flush=True))
    summary = run_case(args.case, args.out, env, exe, log=log)
    if not args.quiet:
        print(json.dumps(summary, indent=2, ensure_ascii=False))
    return 0 if summary["status"] == "ok" else 1


if __name__ == "__main__":
    raise SystemExit(main())
