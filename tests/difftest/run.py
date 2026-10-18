"""Differential test harness for zui — the orchestrator (docs/TESTPLAN.md).

    python tests/difftest/run.py list
    python tests/difftest/run.py probe
    python tests/difftest/run.py record [--case NAME] [--browser PATH]
    python tests/difftest/run.py check  [--case NAME] [--engine-only] [--with-browser]
    python tests/difftest/run.py update-golden [--case NAME]

Division of labour, straight from the test plan:

* golden comes from the **browser** on the pinned build (section 10), never from the engine;
* `check` replays a case through the **engine** and diffs it against golden;
* `--with-browser` re-runs the browser and diffs browser-vs-golden, which is how a browser
  upgrade's drift is separated from an engine regression (section 10).
"""

from __future__ import annotations

import argparse
import contextlib
import fnmatch
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
REPORT_DIR = HERE / "report"
CHROME_DIR = HERE / "chrome"
ENGINE_DIR = HERE / "engine"
CASES_DIR = HERE / "cases"
BUILD_DIR = REPO / "build" / "difftest"

sys.path.insert(0, str(REPORT_DIR))
sys.path.insert(0, str(CHROME_DIR))

from report import build_report, write_report  # noqa: E402
from trace_diff import TraceDiffer, compare_state  # noqa: E402


def load_json(path: Path):
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def step_index(name: str) -> int:
    """`step-004-freeze.diagnostics.json` -> 4. -1 when the name carries no step number."""
    parts = name.split("-")
    if len(parts) < 2 or parts[0] != "step":
        return -1
    try:
        return int(parts[1])
    except ValueError:
        return -1


def golden_trace_for(golden_dir: Path, capture: Path):
    """The last trace the golden recorded at or before this capture.

    The browser records no trace at a `capture` step, so a mask built only from
    `*.capture.trace.json` was always empty and L4 then compared text pixels with the non-text
    tolerance (0). The mask is data - the line fragments of the layout the PNG was taken from - so
    the trace recorded just before the capture is the right one.
    """
    limit = step_index(capture.name)
    best = None
    for candidate in sorted(golden_dir.glob("*-trace.trace.json")):
        if step_index(candidate.name) <= limit:
            best = candidate
    return load_json(best) if best is not None else None


def find_engine_binary(override: str | None) -> Path:
    if override:
        return Path(override)
    candidates = [
        REPO / "build" / "windows-x64" / "zui_difftest.exe",
        REPO / "build" / "windows-x64" / "zui_difftest",
        REPO / "build" / "linux" / "zui_difftest",
        REPO / "build" / "macos" / "zui_difftest",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    raise SystemExit(
        "zui_difftest not built. Run: cmake --build build/windows-x64 --target zui_difftest"
    )


def load_env() -> dict:
    return load_json(HERE / "env.json")


def case_name_matches(pattern: str, name: str) -> bool:
    """A `--case` selector is an exact name or a shell-style glob (`example-*`, `grid-?`)."""
    if pattern == name:
        return True
    if any(char in pattern for char in "*?["):
        return fnmatch.fnmatchcase(name, pattern)
    return False


def failed_cases(report_path: Path) -> set[str]:
    """Names that were red in a previous report, for `--failed` (fix-one-then-recheck)."""
    if not report_path.exists():
        raise SystemExit(
            f"--failed: no report at {report_path}; run a full `check` first "
            f"(or pass --report)"
        )
    report = load_json(report_path)
    import report as difftest_report
    return {
        case["name"]
        for case in report.get("cases", [])
        if case.get("status") == "red" and case.get("name") not in difftest_report.SHELL_CASES
    }


def discover_cases(selected: list[str] | None, only_failed: set[str] | None = None) -> list[Path]:
    """Select cases by name or glob. No selector means the full suite.

    Single case (`--case flow-basic`), a family (`--case 'example-*'`), several of either
    (repeat the flag), the whole suite (no flag), and the red set of the last report
    (`--failed`) all land here, so every command - `list`, `record`, `check`, `coverage` -
    understands the same selection.
    """
    names = {case_json.parent.name: case_json for case_json in CASES_DIR.glob("*/case.json")}
    if not selected:
        out = sorted(names)
    else:
        chosen: set[str] = set()
        for pattern in selected:
            hits = {name for name in names if case_name_matches(pattern, name)}
            if not hits:
                raise SystemExit(f"unknown case(s): {pattern}")
            chosen |= hits
        out = sorted(chosen)
    if only_failed is not None:
        out = [name for name in out if name in only_failed]
        if not out:
            print("没有红用例：--failed 选出来的集合是空的")
    return [names[name] for name in out]


def engine_exe_path() -> str:
    return str(find_engine_binary(engine_binary_override))


engine_binary_override: str | None = None


def run_engine(case_json: Path, out_dir: Path) -> dict:
    out_dir.mkdir(parents=True, exist_ok=True)
    env = load_env()
    command = [
        engine_exe_path(),
        "--case", str(case_json),
        "--repo", str(REPO),
        "--out", str(out_dir),
        # LAYOUT.md section 9: the scrollbar thickness is injected, and the comparison pins it
        # to the browser's measured value (env.json), not to our platform's own number.
        "--scrollbar-thickness",
        str(env.get("engine", {}).get("scrollbar_thickness_px", 15)),
        "--quiet",
    ]
    completed = subprocess.run(command, capture_output=True, text=True)
    summary_path = out_dir / "engine-summary.json"
    if not summary_path.exists():
        raise SystemExit(
            f"engine driver produced no summary for {case_json}\n"
            f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    return load_json(summary_path)


def run_browser(case_json: Path, out_dir: Path, env: dict, browser: str | None,
                quiet: bool = True) -> dict:
    import driver as browser_driver  # local import: the module inserts its own sys.path

    out_dir.mkdir(parents=True, exist_ok=True)
    exe = browser_driver.find_browser(env, browser)
    summary = browser_driver.run_case(
        str(case_json), str(out_dir), env, exe, log=(lambda *_: None) if quiet else print
    )
    return summary


# ---------------------------------------------------------------- probe


def probe_browser(env: dict, browser: str | None) -> dict:
    import driver as browser_driver

    exe = browser_driver.find_browser(env, browser)
    page = HERE / "probe" / "probe.html"
    probe_cfg = env.get("probe", {})
    text = probe_cfg.get("advance_probe_text", "Hamburgefonstiv")
    spaces_text = probe_cfg.get("advance_spaces_probe_text", "Hamburge fonstiv a b c d e")
    kern_text = probe_cfg.get("advance_kern_probe_text", "AVAVAVAVAVAVA")
    font_px = float(probe_cfg.get("advance_probe_font_px", 16))

    result: dict = {"engine": "chromium"}

    with browser_driver.Browser(exe, env, (800, 600)) as browser:
        result["browser_version"] = browser.browser_version
        browser.navigate(str(page))
        # Scrollbar thickness is the difference between the viewport and the layout client
        # width once the page overflows — the same quantity the engine reads off the root's
        # padding box.
        result["scrollbar_thickness_px"] = browser.evaluate(
            "window.innerWidth - document.documentElement.clientWidth"
        )
        result["client_width"] = browser.evaluate("document.documentElement.clientWidth")
        result["advance_px"] = browser.evaluate(
            "(function(){var el=document.getElementById('advance');"
            "var node=el.firstChild;var r=document.createRange();"
            "r.setStart(node,0);r.setEnd(node,node.nodeValue.length);"
            "var rect=r.getBoundingClientRect();return rect.width;})()"
        )
        result["advance_spaces_px"] = browser.evaluate(
            "(function(){var el=document.getElementById('advance-spaces');"
            "if(!el||!el.firstChild)return null;"
            "var node=el.firstChild;var r=document.createRange();"
            "r.setStart(node,0);r.setEnd(node,node.nodeValue.length);"
            "return r.getBoundingClientRect().width;})()"
        )
        result["advance_kern_px"] = browser.evaluate(
            "(function(){var el=document.getElementById('advance-kern');"
            "if(!el||!el.firstChild)return null;"
            "var node=el.firstChild;var r=document.createRange();"
            "r.setStart(node,0);r.setEnd(node,node.nodeValue.length);"
            "return r.getBoundingClientRect().width;})()"
        )
        result["font_px"] = font_px
        result["text"] = text
        result["spaces_text"] = spaces_text
        result["kern_text"] = kern_text
    return result


def probe_engine(env: dict) -> dict:
    exe = engine_exe_path()
    probe_cfg = env.get("probe", {})
    command = [
        exe,
        "--probe",
        "--page", str(HERE / "probe" / "probe.html"),
        "--probe-text", probe_cfg.get("advance_probe_text", "Hamburgefonstiv"),
        "--probe-font-px", str(probe_cfg.get("advance_probe_font_px", 16)),
        "--probe-family", env.get("engine", {}).get("font_families", "sans-serif"),
        "--probe-spaces-text",
        probe_cfg.get("advance_spaces_probe_text", "Hamburge fonstiv a b c d e"),
        "--probe-kern-text",
        probe_cfg.get("advance_kern_probe_text", "AVAVAVAVAVAVA"),
        "--scrollbar-thickness", str(env.get("engine", {}).get("scrollbar_thickness_px", 15)),
    ]
    completed = subprocess.run(command, capture_output=True, text=True)
    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError:
        return {"engine": "zui", "error": completed.stdout + completed.stderr}


def compare_probe(env: dict, browser_probe: dict, engine_probe: dict) -> dict:
    probe_cfg = env.get("probe", {})
    failures: list[dict] = []
    measurements: dict = {}

    def fail(kind: str, message: str, field: str = "probe") -> None:
        """`kind` decides who owns the fix: the environment (flags, golden, fonts) or the
        engine (a geometry divergence that happens to be measurable before any case runs)."""
        failures.append({"class": kind, "field": field, "message": message})

    declared_engine_thickness = float(probe_cfg.get("engine_scrollbar_thickness_px", 11))
    browser_thickness = float(browser_probe.get("scrollbar_thickness_px") or 0)
    engine_thickness = float(engine_probe.get("scrollbar_thickness_px") or 0)
    measurements["scrollbar_thickness_px"] = {
        "engine_declared": declared_engine_thickness,
        "engine_measured": engine_thickness,
        "browser_measured": browser_thickness,
    }
    # Each side against its own declaration first: that catches a flag that silently stopped
    # working (section 8), or a platform injection that stopped reaching the layout context.
    if abs(engine_thickness - declared_engine_thickness) > 0.01:
        fail(
            "engine_geometry",
            f"引擎自检失败：视口滚动条厚度实测 {engine_thickness:g}px，LAYOUT 第 9 节声明的平台"
            f"注入值是 {declared_engine_thickness:g}px。要么注入没生效，要么根的可用宽度没有因为"
            f"滚动条收缩。",
            "probe.scrollbar_thickness_px",
        )
    if browser_thickness <= 0.0:
        fail(
            "environment",
            "浏览器自检失败：经典滚动条厚度实测 0（overlay）。第 8 节的 flag 集没把滚动条形态"
            "钉住，先改 flag（注意不要写 `--hide-scrollbars=false`，它会被当成布尔开关）。"
        )
    # Then cross-check: same parameter, two engines. Not a flag problem any more — it is the
    # platform value itself, and LAYOUT section 9 is where it lives.
    elif abs(browser_thickness - engine_thickness) > 0.01:
        fail(
            "engine_geometry",
            f"几何分歧：浏览器经典滚动条 {browser_thickness:g}px vs 引擎 {engine_thickness:g}px。"
            f"LAYOUT 第 9 节把厚度定为平台注入参数，Windows 上实测是 {browser_thickness:g}px；"
            f"把注入值对齐到实测值即可，机制不变。",
            "probe.scrollbar_thickness_px",
        )

    tolerance = float(probe_cfg.get("advance_tolerance_px", 0.5))
    browser_advance = float(browser_probe.get("advance_px") or 0)
    engine_advance = float(engine_probe.get("advance_px") or 0)
    measurements["advance_px"] = {
        "text": probe_cfg.get("advance_probe_text", ""),
        "font_px": float(probe_cfg.get("advance_probe_font_px", 16)),
        "tolerance": tolerance,
        "browser": browser_advance,
        "engine": engine_advance,
    }
    if abs(browser_advance - engine_advance) > tolerance:
        fail(
            "engine_geometry",
            f"环境分歧：探针串 advance 浏览器 {browser_advance:.3f}px vs 引擎 "
            f"{engine_advance:.3f}px，超出 {tolerance}px。两端 shaping 引擎对该字体的处理本就"
            f"不同，或者用例还在用系统字体。TESTPLAN 第 6 节要求用例 @font-face 内嵌 woff2，"
            f"落到系统字体即 L0 失败：把字体文件放进 tests/difftest/fonts/ 并在 env.json 的 "
            f"fonts.embedded 里登记。",
            "probe.advance_px",
        )

    # The same comparison for a string with spaces. A single word cannot see a difference in how
    # the two stacks advance a space, and that difference moves every line break in a paragraph.
    browser_spaces = float(browser_probe.get("advance_spaces_px") or 0)
    engine_spaces = float(engine_probe.get("advance_spaces_px") or 0)
    measurements["advance_spaces_px"] = {
        "text": probe_cfg.get("advance_spaces_probe_text", ""),
        "font_px": float(probe_cfg.get("advance_probe_font_px", 16)),
        "tolerance": tolerance,
        "browser": browser_spaces,
        "engine": engine_spaces,
    }
    if abs(browser_spaces - engine_spaces) > tolerance:
        fail(
            "engine_geometry",
            f"带空格探针串 advance 浏览器 {browser_spaces:.3f}px vs 引擎 "
            f"{engine_spaces:.3f}px，超出 {tolerance}px。单词串一致而带空格的串不一致，说明分歧"
            f"在空格（或词间）前进量上，而它会平移整段文本的断行位置。",
            "probe.advance_spaces_px",
        )

    # And the same for a string whose width is mostly kerning. A stack that does not apply the
    # font's GPOS `kern` feature measures an unkerned string exactly right and still lays every
    # paragraph out wider than the browser, so the plain and spaced probes cannot see it.
    browser_kern = float(browser_probe.get("advance_kern_px") or 0)
    engine_kern = float(engine_probe.get("advance_kern_px") or 0)
    measurements["advance_kern_px"] = {
        "text": probe_cfg.get("advance_kern_probe_text", ""),
        "font_px": float(probe_cfg.get("advance_probe_font_px", 16)),
        "tolerance": tolerance,
        "browser": browser_kern,
        "engine": engine_kern,
    }
    if abs(browser_kern - engine_kern) > tolerance:
        fail(
            "engine_geometry",
            f"字距探针串 advance 浏览器 {browser_kern:.3f}px vs 引擎 {engine_kern:.3f}px，超出 "
            f"{tolerance}px。没有字距对的串两端一致、而这条不一致，说明引擎没有应用字体的 GPOS "
            f"字距（或只在部分对上应用），长串上会累积到 L2 的 1/64px 容差之外。",
            "probe.advance_kern_px",
        )

    golden_path = HERE / "probe" / "golden.json"
    if golden_path.exists():
        golden = load_json(golden_path)
        recorded = float(golden.get("advance_px", {}).get("browser", 0) or 0)
        if recorded and abs(browser_advance - recorded) > tolerance:
            fail(
                "environment",
                f"golden 漂移：本次浏览器 advance {browser_advance:.3f}px，录制时 "
                f"{recorded:.3f}px。按第 10 节走跨版本对照并更新 probe/golden.json。"
            )
        recorded_thickness = golden.get("scrollbar_thickness_px", {}).get("browser")
        if recorded_thickness is not None and abs(
            browser_thickness - float(recorded_thickness)
        ) > 0.01:
            fail(
                "environment",
                f"golden 漂移：本次滚动条厚度 {browser_thickness:g}px，录制时 "
                f"{float(recorded_thickness):g}px。flag 集或浏览器构建变了，先按第 10 节重录。"
            )

    pinned_version = str(env.get("browser", {}).get("pinned_version") or "")
    browser_version = str(browser_probe.get("browser_version") or "")
    if pinned_version and browser_version and pinned_version != browser_version:
        fail(
            "environment",
            f"浏览器版本不是钉死的那个：本次 {browser_version}，env.json 的 pinned_version 是 "
            f"{pinned_version}。golden 只在钉死的构建上有效（第 10 节）。"
        )

    summary = (
        f"探针全过：滚动条 引擎 {engine_thickness:g}px / 浏览器 {browser_thickness:g}px，"
        f"advance 浏览器 {browser_advance:.3f}px / 引擎 {engine_advance:.3f}px"
        if not failures
        else f"{len(failures)} 项探针失败"
    )
    return {
        "verdict": "pass" if not failures else "fail",
        "summary": summary,
        "failures": failures,
        "measurements": measurements,
    }


def cmd_probe(args, env: dict) -> int:
    print("L0 探针（TESTPLAN 第 8 节）")
    browser_result = probe_browser(env, args.browser)
    engine_result = probe_engine(env)
    comparison = compare_probe(env, browser_result, engine_result)
    out = {
        "schema": "zui.difftest.probe-run/1",
        "browser": browser_result,
        "engine": engine_result,
        **comparison,
    }
    path = BUILD_DIR / "probe" / "probe-result.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(out, handle, indent=2, ensure_ascii=False)
        handle.write("\n")
    print(json.dumps(comparison, indent=2, ensure_ascii=False))
    print(f"→ {path}")
    return 0 if comparison["verdict"] == "pass" else 1


# ---------------------------------------------------------------- record / check


@contextlib.contextmanager
def http_fixture(case_json: Path, case: dict | None = None):
    """Serve `http_fixture.root` on `http_fixture.port` while the case runs (remote stylesheets)."""
    case = case if case is not None else load_json(case_json)
    cfg = case.get("http_fixture")
    if not cfg:
        yield None
        return
    port = int(cfg.get("port", 18765))
    root = (case_json.parent / cfg.get("root", ".")).resolve()

    class QuietHandler(SimpleHTTPRequestHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=str(root), **kwargs)

        def log_message(self, format, *args):  # noqa: A003
            return

    server = ThreadingHTTPServer(("127.0.0.1", port), QuietHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        yield server
    finally:
        server.shutdown()
        thread.join(timeout=5)


def materialize_page(case_json: Path, case: dict) -> None:
    """Write the page a `scripts: false` case compares, from the page it names as `source`.

    The engine has no script engine, so a page whose DOM a script builds cannot be compared: every
    element the script added reads as "present in the golden, missing in the engine". The harness
    therefore compares the document's HTML and CSS with the page's own scripts removed, and both
    ends load the same generated file (removing them only from the browser's copy would shift every
    element path after them).
    """
    if case.get("scripts", True) is not False:
        return
    source = case.get("source")
    if not source:
        return
    src = (case_json.parent / source).resolve()
    html = src.read_text(encoding="utf-8")
    # Relative URLs keep pointing at the example directory once the file moves into the case dir.
    prefix = os.path.relpath(src.parent, case_json.parent).replace("\\", "/")
    def rebase(attr: str) -> str:
        def replace(match: "re.Match[str]") -> str:
            value = match.group(2)
            if "://" in value or value.startswith("/") or value.startswith("#"):
                return match.group(0)
            return f'{match.group(1)}="{prefix}/{value}"'
        return replace
    html = re.sub(r'(<script\b[^>]*>.*?</script>)', "", html, flags=re.S | re.I)
    for attr in ("href", "src"):
        html = re.sub(rf'({attr})="([^"]+)"', rebase(attr), html)
    target = case_json.parent / case.get("page", "page.html")
    target.write_text(html, encoding="utf-8")


def _case_page(case_json: Path) -> str:
    return load_json(case_json).get("page", "page.html")


def record_case(case_json: Path, env: dict, browser: str | None, into_golden: bool) -> dict:
    name = case_json.parent.name
    case = load_json(case_json)
    materialize_page(case_json, case)
    # Record only what the case really points at. A page path that does not resolve makes the
    # browser record its own file-not-found page, which then compares equal to nothing and would
    # silently produce a "green" case that never loaded the page (TESTPLAN section 8).
    page = (case_json.parent / _case_page(case_json)).resolve()
    if not page.is_file():
        return {
            "status": "error",
            "error": f"page for {name} does not exist: {page}",
            "artifacts": [],
        }
    with http_fixture(case_json, case):
        return _record_case_body(case_json, env, browser, into_golden, name)


def _record_case_body(case_json: Path, env: dict, browser: str | None, into_golden: bool,
                      name: str) -> dict:
    out_dir = BUILD_DIR / "record" / name
    if out_dir.exists():
        shutil.rmtree(out_dir)
    summary = run_browser(case_json, out_dir, env, browser)
    if summary.get("status") != "ok":
        return summary

    if into_golden:
        golden = case_json.parent / "golden"
        if golden.exists():
            shutil.rmtree(golden)
        golden.mkdir(parents=True)
        for artifact in summary.get("artifacts", []):
            shutil.copy2(artifact, golden / Path(artifact).name)
        meta = {
            "schema": "zui.difftest.golden-meta/1",
            "case": name,
            "browser": summary.get("browser", ""),
            "browser_version": summary.get("browser_version", ""),
            "flags": env.get("browser", {}).get("pinned_flags", []),
            "fonts": env.get("fonts", {}).get("embedded", []),
            "scrollbar_thickness_px": env.get("engine", {}).get("scrollbar_thickness_px"),
            "recorded": True,
        }
        with open(golden / "meta.json", "w", encoding="utf-8") as handle:
            json.dump(meta, handle, indent=2, ensure_ascii=False)
            handle.write("\n")
    return summary


def _readback_ops(case: dict) -> list[tuple[str, dict]]:
    """Steps that declare a readback. A missing state file is a mechanism error."""
    declared = []
    for index, step in enumerate(case.get("steps", [])):
        op = step.get("op", "")
        if op in ("scroll", "dispatch") or step.get("readback"):
            declared.append((f"step-{index:03d}-{op}", step))
    return declared


def diff_case(case_json: Path, actual_dir: Path, golden_dir: Path, case: dict) -> dict:
    passline = load_json(HERE / "passline.json")
    tolerance = passline["tolerance"]
    pixels_mode = case.get("pixels", "strict")

    steps: list[dict] = []
    counts = {"l1": 0, "geometry": 0, "pixels": 0, "deferred": 0, "missing": 0,
              "mechanism": 0, "l3": 0, "l5": 0, "root_causes": 0}
    warnings: list[dict] = []

    for golden_trace in sorted(golden_dir.glob("*.trace.json")):
        step = golden_trace.name[: -len(".trace.json")]
        actual_trace = actual_dir / golden_trace.name
        if not actual_trace.exists():
            # The golden recorded a page the engine never produced anything for: that is a
            # failure, not a step to skip. Counting it as clean is how a case whose page did not
            # even load still showed up green.
            counts["missing"] += 1
            steps.append({"step": step, "skipped_reason": "engine produced no trace for this step"})
            continue
        differ = TraceDiffer(
            length_tolerance=tolerance["length_px"],
            l1_numeric_tolerance=tolerance["l1_numeric_px"],
            pixels=pixels_mode,
        )
        result = differ.compare(load_json(golden_trace), load_json(actual_trace))
        counts["l1"] += result["l1_differences"]
        counts["geometry"] += result["l2_differences"]
        counts["deferred"] += result["deferred"]
        counts["mechanism"] += result.get("mechanism", 0)
        counts["root_causes"] += result.get("root_causes", 0)
        warnings.extend(result["warnings"])
        steps.append(
            {
                "step": step,
                "classification": result["classification"],
                "entries": result["entries"],
                "l1": result["l1_differences"],
                "geometry": result["l2_differences"],
                "mechanism": result.get("mechanism", 0),
                "deferred": result["deferred"],
            }
        )

    for step_name, step_spec in _readback_ops(case):
        golden_state = golden_dir / f"{step_name}.state.json"
        actual_state = actual_dir / f"{step_name}.state.json"
        if not golden_state.exists() or not actual_state.exists():
            counts["mechanism"] += 1
            missing_side = "golden" if not golden_state.exists() else "engine"
            steps.append({
                "step": step_name,
                "classification": "mechanism",
                "entries": [{
                    "element": step_spec.get("target", "document"),
                    "field": "state",
                    "kind": "mechanism",
                    "expected": "state.json",
                    "actual": "missing",
                    "tolerance": 0,
                    "delta": None,
                    "message": f"声明了读回但 {missing_side} 没有 {step_name}.state.json，需要重录"
                    if missing_side == "golden" else
                    f"声明了读回但引擎没有写出 {step_name}.state.json",
                }],
            })
            continue
        state = compare_state(load_json(golden_state), load_json(actual_state), tolerance["length_px"])
        kind = state["kind"]
        counts[kind] += state["differences"]
        steps.append({
            "step": step_name,
            "classification": "clean" if not state["differences"] else kind,
            "readback_layer": kind,
            "entries": state["entries"],
            kind: state["differences"],
        })

    golden_pngs = sorted(
        path for path in golden_dir.glob("*.png") if not path.name.endswith(".mask.png")
    )
    if golden_pngs:
        from png_io import decode_png
        from pixel_diff import (
            attach_corner_radii,
            decode_source,
            diff_pixels,
            gutter_rects,
            text_mask_rects,
            write_diff_png,
        )

        zone_area = int(tolerance.get("pixel_zone_area", 400))
        zone_short = int(tolerance.get("pixel_zone_short_side", 8))
        zone_edge = int(tolerance.get("pixel_zone_edge_run", 8))
        for golden_png in golden_pngs:
            step = golden_png.name[: -len(".png")]
            golden_trace = golden_trace_for(golden_dir, golden_png)
            mask = text_mask_rects(golden_trace) if golden_trace else []
            gutters = gutter_rects(golden_trace) if golden_trace else []
            actual_trace = golden_trace_for(actual_dir, golden_png)
            actual_gutters = gutter_rects(actual_trace) if actual_trace else []
            actual_png = actual_dir / golden_png.name
            if not actual_png.exists():
                # A missing PNG is a driver failure once readback exists. Until then the engine
                # records this reason and writes nothing; that is not a `missing` channel.
                reason = "engine produced no capture"
                summary_path = actual_dir / "engine-summary.json"
                if summary_path.is_file():
                    try:
                        unsupported = load_json(summary_path).get("unsupported") or []
                    except (OSError, ValueError):
                        unsupported = []
                    if any(
                        isinstance(item, dict)
                        and item.get("op") == "capture"
                        and str(item.get("reason", "")).startswith(
                            "framebuffer readback is not implemented"
                        )
                        for item in unsupported
                    ):
                        reason = "framebuffer readback is not implemented"
                steps.append({"step": step, "skipped_reason": reason})
                continue
            mask_path = actual_dir / f"{step}.mask.png"
            source = None
            try:
                golden_image = decode_png(golden_png.read_bytes())
                actual_image = decode_png(actual_png.read_bytes())
                if mask_path.exists():
                    source = decode_source(decode_png(mask_path.read_bytes()))
            except ValueError as exc:
                steps.append({"step": step, "skipped_reason": f"PNG decode failed: {exc}"})
                continue
            # A real framebuffer has no content/scrollbar bit. Gutter geometry still comes
            # from the trace. Without a mask, gutter pixels are not a mechanism error and
            # are not scored as content ink.
            pixel = diff_pixels(
                golden_image,
                actual_image,
                mask,
                text_tolerance=tolerance["pixel_text_aa"],
                nontext_tolerance=int(case.get("pixel_tolerance", 0)),
                gutter_golden=gutters,
                gutter_actual=actual_gutters,
                source=source,
                zone_area=zone_area,
                zone_short=zone_short,
                zone_edge=zone_edge,
                elements=attach_corner_radii(
                    (golden_trace or {}).get("elements"),
                    (actual_trace or {}).get("elements"),
                ),
            )
            if pixel.get("classification") == "mechanism":
                counts["mechanism"] += 1
            counts["pixels"] += pixel["differing"]
            if pixel["differing"] > 0 and pixel.get("classification") != "mechanism":
                write_diff_png(golden_image, actual_image, mask,
                               str(actual_dir / f"{step}.diff.png"), gutters)
            steps.append({"step": step, "pixels": pixel})

    status = "green"
    if (counts["l1"] or counts["geometry"] or counts["pixels"] or counts["missing"]
            or counts["mechanism"] or counts["l3"] or counts["l5"]):
        status = "red"
    return {
        "name": case_json.parent.name,
        "status": status,
        "matrix": case.get("matrix", []),
        "required": bool(case.get("required", False)),
        "steps": steps,
        "warnings": warnings,
        "counts": counts,
    }


def run_selfcheck() -> dict | None:
    """L6: the engine-local architecture asserts. Not a comparison, so it is not a diff."""
    candidates = [
        REPO / "build" / "windows-x64" / "zui_difftest_selfcheck.exe",
        REPO / "build" / "windows-x64" / "zui_difftest_selfcheck",
        REPO / "build" / "linux" / "zui_difftest_selfcheck",
        REPO / "build" / "macos" / "zui_difftest_selfcheck",
    ]
    for candidate in candidates:
        if candidate.exists():
            completed = subprocess.run([str(candidate)], capture_output=True, text=True)
            tail = completed.stdout.strip().splitlines()[-8:]
            return {
                "status": "pass" if completed.returncode == 0 else "fail",
                "detail": " | ".join(line.strip() for line in tail),
            }
    return None


def stale_binaries() -> list[str]:
    """Warn when a driver binary predates the sources it was built from.

    `check` never builds: it runs whatever `build/windows-x64` holds. A stale binary therefore
    produces a confident but wrong verdict - L6 reported a failure that disappeared as soon as
    the selfcheck target was relinked. Cheap and loud beats confident and wrong.

    The comparison is scoped per binary: both drivers compile most of `source/`, but only
    `zui_difftest` compiles `tests/difftest/engine/`. Comparing both against every `.cpp`/`.h`
    under `tests/` made an edit to the engine driver look like a stale selfcheck, which then
    rejected a fresh report.
    """
    def newest_source(paths: list[Path]) -> float:
        newest = 0.0
        for path in paths:
            if path.is_dir():
                for found in path.rglob("*"):
                    if found.suffix in (".cpp", ".h") and found.is_file():
                        newest = max(newest, found.stat().st_mtime)
            elif path.is_file() and path.suffix in (".cpp", ".h"):
                newest = max(newest, path.stat().st_mtime)
        return newest

    engine_dir = REPO / "tests" / "difftest" / "engine"
    build_inputs = {
        "zui_difftest": [REPO / "source", engine_dir],
        "zui_difftest_selfcheck": [REPO / "source", REPO / "tests" / "difftest_selfcheck"],
    }
    warned: list[str] = []
    for name, inputs in build_inputs.items():
        newest = newest_source(inputs)
        if newest <= 0.0:
            continue
        for candidate in (REPO / "build" / "windows-x64" / f"{name}.exe",
                          REPO / "build" / "windows-x64" / name):
            if candidate.exists():
                if candidate.stat().st_mtime < newest:
                    warned.append(
                        f"{candidate.name} 比最新的源码旧：先 cmake --build build/windows-x64，"
                        f"否则这一轮判决用的是旧二进制。"
                    )
                break
    return warned


def matrix_coverage(case_results: list[dict], matrix: dict) -> dict:
    """TESTPLAN section 15's required set, as a number instead of an intention.

    "必过集 = 矩阵「一致」范围每特性至少一条 golden 用例 + 全部回归用例". Without this the
    matrix is a claim nobody checks: `flow.grid` can stay `aligned` while no case ever
    exercises grid, and every layer still reports green.
    """
    aligned = [entry["id"] for entry in matrix.get("entries", [])
               if entry.get("level") == "aligned"]
    declared = {entry["id"] for entry in matrix.get("entries", [])}
    covered: dict[str, list[str]] = {}
    unknown: dict[str, list[str]] = {}
    for case in case_results:
        if case.get("status") == "skipped":
            continue  # no golden yet: the case runs nothing, so it covers nothing
        for feature in case.get("matrix", []):
            target = covered if feature in declared else unknown
            target.setdefault(feature, []).append(case["name"])
    missing = [feature for feature in aligned if feature not in covered]
    return {
        "aligned_features": len(aligned),
        # Only the「一致」features count towards coverage; a behaviour-level tag being exercised
        # is fine but must not make the number look better than it is.
        "covered_features": sum(1 for feature in aligned if feature in covered),
        "covered": {key: sorted(value) for key, value in sorted(covered.items())},
        "missing": missing,
        # A case that names a feature nobody declared is a typo or an undeclared capability: the
        # matrix and the cases have to agree in both directions, or a new feature can ride in on a
        # bogus tag and no gate ever notices.
        "unknown": {key: sorted(value) for key, value in sorted(unknown.items())},
        "ok": not missing and not unknown,
    }
def engine_observations(case: dict, summary: dict, engine_out: Path) -> dict:
    """Turns LAYOUT section 16's diagnostics into verdicts the report can gate on.

    Two architecture rules are checkable from outside the engine with nothing but the
    per-step counters, and both are rules the acceptance declaration rests on:

    * a scroll step must not run a layout frame (section 1 / section 16, "滚动一像素只改
      滚动口和 sticky，不重排");
    * an animation frame must not mark Layout dirty (section 16, self-checks);
    * an observation step must not run a frame either (section 16, "观测通道... 加不加 trace，
      布局走同一条代码").
    """
    steps = [s for s in summary.get("steps", []) if s.get("status") == "ok"]
    violations: list[dict] = []
    for step in steps:
        op = step.get("op", "")
        frames = int(step.get("frames", 0))
        dirty = int(step.get("dirty_marks", 0))
        if op == "scroll" and (frames != 0 or dirty != 0):
            violations.append({
                "rule": "scroll-does-not-relayout",
                "step": step.get("step"),
                "detail": f"scroll ran {frames} layout frame(s) and marked {dirty} box(es) dirty",
            })
        if op in ("freeze", "advance") and dirty != 0:
            violations.append({
                "rule": "animation-does-not-relayout",
                "step": step.get("step"),
                "detail": f"a sampled animation frame marked {dirty} box(es) Layout dirty",
            })
        if op in ("trace", "capture") and frames != 0:
            violations.append({
                "rule": "observation-does-not-layout",
                "step": step.get("step"),
                "detail": f"the observation step ran {frames} layout frame(s)",
            })

    # `converged == false` is a test failure unless the case says it is constructing one
    # (LAYOUT section 16 / TESTPLAN section 12). The boxes and their sources come from the
    # diagnostics files the engine writes beside each trace.
    expected = bool(case.get("expects_non_convergence", False))
    non_converged_steps = [s for s in steps if not s.get("converged", True)]
    boxes: list[dict] = []
    worst = None
    for step in non_converged_steps:
        path = engine_out / f"{step['step']}.diagnostics.json"
        if not path.exists():
            continue
        diagnostics = load_json(path)
        if worst is None or len(diagnostics.get("non_converged", [])) > len(worst.get(
                "non_converged", [])):
            worst = diagnostics
    if worst is not None:
        boxes = worst.get("non_converged", [])
    convergence = {
        "ok": expected or not non_converged_steps,
        "expected": expected,
        "steps": [{"step": s["step"], "op": s.get("op"), "frames": s.get("frames", 0)} for s
                  in non_converged_steps],
        "boxes": boxes,
    }
    counters = [{"step": s["step"], "op": s.get("op"), "frames": s.get("frames", 0),
                 "dirty_marks": s.get("dirty_marks", 0)} for s in steps]
    return {
        "convergence": convergence,
        "architecture": {"ok": not violations, "violations": violations},
        "counters": counters,
    }


def check_one_case(case_json: Path) -> dict:
    """Replay one case through the engine and turn its artifacts into a verdict entry.

    Split out of `cmd_check` so the engine phase can run cases concurrently: each case owns its
    output directory and its own subprocess, so there is no state to share (the browser phase
    stays serial - it drives one browser on one debug port).
    """
    case = load_json(case_json)
    engine_out = BUILD_DIR / "run" / case_json.parent.name / "engine"
    if engine_out.exists():
        shutil.rmtree(engine_out)
    with http_fixture(case_json, case):
        engine_summary = run_engine(case_json, engine_out)

    golden_dir = case_json.parent / "golden"
    if not golden_dir.exists() or not any(golden_dir.glob("step-*")):
        return {
            "name": case_json.parent.name,
            "status": "skipped",
            "matrix": case.get("matrix", []),
            "required": bool(case.get("required", False)),
            "steps": [],
            "warnings": [],
            "counts": {"l1": 0, "geometry": 0, "pixels": 0, "deferred": 0, "missing": 0},
            "skip_reason": "no golden yet — run: python tests/difftest/run.py record",
        }

    result = diff_case(case_json, engine_out, golden_dir, case)
    result["engine_errors"] = engine_summary.get("errors", [])
    result["engine_unsupported"] = engine_summary.get("unsupported", [])
    if engine_summary.get("status") != "ok":
        result["status"] = "red"
    # An engine that could not open the page (or otherwise failed a step) has not compared
    # anything: it must not read as green just because it produced no trace to differ.
    if engine_summary.get("errors"):
        result["status"] = "red"
    observations = engine_observations(case, engine_summary, engine_out)
    result.update(observations)
    if not observations["convergence"]["ok"] or not observations["architecture"]["ok"]:
        result["status"] = "red"
    return result


def cmd_check(args, env: dict) -> int:
    verdict = subprocess.run([sys.executable, str(REPORT_DIR / "verdict_selfcheck.py")], cwd=REPO)
    if verdict.returncode != 0:
        print("判决自检失败（TESTPLAN 第 12 节）。分区和根因聚合还不能当通过线。")
        return 1
    only_failed = failed_cases(Path(args.report)) if args.failed else None
    cases = discover_cases(args.case, only_failed)
    passline = load_json(HERE / "passline.json")
    matrix = load_json(HERE / "matrix.json")

    probe_result = None
    probe_path = BUILD_DIR / "probe" / "probe-result.json"
    # --engine-only does not run the browser probe and does not reuse a leftover
    # probe-result.json. That file staying on disk is not this invocation.
    if args.engine_only:
        probe_result = None
    elif not args.skip_probe:
        code = cmd_probe(args, env)
        del code
        if probe_path.exists():
            probe_result = load_json(probe_path)
    elif probe_path.exists():
        probe_result = load_json(probe_path)

    jobs = max(1, int(args.jobs))
    started = time.time()
    print(f"引擎回放：{len(cases)} 条用例，并发 {jobs}", flush=True)

    def run_one(case_json: Path) -> dict:
        case_started = time.time()
        result = check_one_case(case_json)
        print(
            f"  {result['name']:<20} {result['status']:<8} {time.time() - case_started:5.1f}s",
            flush=True,
        )
        return result

    if jobs > 1 and len(cases) > 1:
        with ThreadPoolExecutor(max_workers=jobs) as pool:
            case_results = list(pool.map(run_one, cases))
    else:
        case_results = [run_one(case_json) for case_json in cases]
    print(f"引擎回放完成：{time.time() - started:.1f}s", flush=True)


    if args.with_browser:
        for case_json, result in zip(cases, case_results):
            if result["status"] == "skipped":
                continue
            browser_out = BUILD_DIR / "run" / case_json.parent.name / "browser"
            if browser_out.exists():
                shutil.rmtree(browser_out)
            with http_fixture(case_json):
                summary = run_browser(case_json, browser_out, env, args.browser)
                drift = diff_case(case_json, browser_out, case_json.parent / "golden",
                                  load_json(case_json))
            result["browser_replay"] = {
                "status": drift["status"],
                "counts": drift["counts"],
                "browser_version": summary.get("browser_version", ""),
                "steps": [s for s in drift["steps"] if s.get("entries") or s.get("pixels")],
            }

    environment = {
        "repo": str(REPO),
        "browser": env.get("browser", {}).get("search_paths", [])[:1],
        "browser_version": "",
        "flags": env.get("browser", {}).get("pinned_flags", []),
        "fonts": env.get("fonts", {}).get("embedded", []),
        "length_tolerance_px": passline["tolerance"]["length_px"],
    }
    record_summaries = sorted((BUILD_DIR / "record").glob("*/browser-summary.json"))
    if record_summaries:
        environment["browser_version"] = load_json(record_summaries[-1]).get(
            "browser_version", ""
        )

    report = build_report(case_results, probe_result, passline, matrix, environment,
                          selfcheck=run_selfcheck(),
                          coverage=matrix_coverage(case_results, matrix))
    report["stale_binaries"] = stale_binaries()
    report["run_id"] = getattr(args, "run_id", None) or ""
    out_dir = BUILD_DIR / "run"
    out_dir.mkdir(parents=True, exist_ok=True)
    report_json = out_dir / "report.json"
    report_html = out_dir / "report.html"
    write_report(report, str(report_json), str(report_html))

    print("通过线")
    for name, layer in report["layers"].items():
        print(f"  {name}: {layer['status']:>15}  {layer['detail']}")
    bucket = report.get("shell_bucket") or {}
    print("\n应用壳（不计入通过线）")
    print(
        f"  红={bucket.get('red', 0)} 绿={bucket.get('green', 0)} "
        f"L1={bucket.get('l1', 0)} 几何={bucket.get('geometry', 0)} "
        f"像素={bucket.get('pixels', 0)}"
    )
    for case in bucket.get("cases", []):
        counts = case.get("counts", {})
        print(
            f"  {case['name']:<18} {case.get('status', ''):<8} L1={counts.get('l1', 0):<4} "
            f"结构/长度={counts.get('geometry', 0):<4} 像素={counts.get('pixels', 0):<6}"
        )
    if report["layers"]["L1"]["status"] == "not-run":
        print("本轮没有内核用例")
    print("\n用例")
    for case in report["cases"]:
        counts = case["counts"]
        flags = []
        if case.get("convergence") and not case["convergence"]["ok"]:
            flags.append("converged=false")
        if case.get("architecture") and not case["architecture"]["ok"]:
            flags.append("架构自检")
        print(
            f"  {case['name']:<18} {case['status']:<8} L1={counts['l1']:<4} "
            f"结构/长度={counts['geometry']:<4} 像素={counts['pixels']:<6} "
            f"延后={counts['deferred']}" + (f"  [{', '.join(flags)}]" if flags else "")
        )
    for case in report["cases"]:
        for violation in case.get("architecture", {}).get("violations", []):
            print(f"  架构: {case['name']} {violation['rule']} — {violation['detail']}")
        convergence = case.get("convergence") or {}
        if convergence and not convergence["ok"] and not convergence["expected"]:
            for box in convergence["boxes"][:5]:
                print(f"  不收敛: {case['name']} {box['path']} {box['tag']}#{box['id']} — "
                      f"{box['reason']}")

    coverage = report["matrix_coverage"]
    if report.get("stale_binaries"):
        print("\n构建告警")
        for warning in report["stale_binaries"]:
            print(f"  {warning}")
    print("\n矩阵覆盖（TESTPLAN 第 2 / 15 节）")
    print(f"  一致特性 {coverage['covered_features']}/{coverage['aligned_features']} 有 golden 用例")
    if coverage["missing"]:
        print("  缺用例: " + ", ".join(coverage["missing"]))
    print(f"\n报告: {report_json}\n      {report_html}")
    if probe_result:
        for failure in probe_result.get("failures", []):
            message = failure["message"] if isinstance(failure, dict) else failure
            owner = failure.get("class", "") if isinstance(failure, dict) else ""
            print(f"  L0{'/' + owner if owner else ''}: {message}")

    gated_names = ["L1", "L2", "L4"]
    if report["layers"]["L0"]["status"] != "not-run":
        gated_names.insert(0, "L0")
    gated = [
        report["layers"][name]["status"] == "green"
        for name in gated_names
        if report["layers"][name]["status"] != "not-run"
    ]
    for name in ("L3", "L5"):
        if report["layers"][name]["status"] == "red":
            gated.append(False)
    if report["layers"].get("mechanism", {}).get("status") == "red":
        gated.append(False)
    if args.require_coverage:
        gated.append(coverage["ok"])
    return 0 if all(gated) else 1


def cmd_record(args, env: dict) -> int:
    cases = discover_cases(args.case)
    print("录制 golden（浏览器侧，第 10 节）")
    failures = 0
    for case_json in cases:
        summary = record_case(case_json, env, args.browser, into_golden=True)
        status = summary.get("status")
        print(f"  {case_json.parent.name}: {status} ({summary.get('traces', 0)} trace, "
              f"{summary.get('captures', 0)} capture)")
        for error in summary.get("errors", []):
            print(f"    error: {error}")
        if status != "ok":
            failures += 1
    # The probe numbers are recorded beside the golden so a later run can tell "the browser
    # changed" from "our environment parameter changed".
    probe = probe_browser(env, args.browser)
    path = HERE / "probe" / "golden.json"
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(
            {
                "schema": "zui.difftest.probe-golden/1",
                "advance_px": {"browser": probe.get("advance_px")},
                "scrollbar_thickness_px": {"browser": probe.get("scrollbar_thickness_px")},
                "browser_version": probe.get("browser_version", ""),
            },
            handle,
            indent=2,
            ensure_ascii=False,
        )
        handle.write("\n")
    print(f"  probe golden → {path}")

    # Record the browser's measured scrollbar thickness back into env.json. It is a property of
    # the pinned build, so the probe compares against a recorded number rather than a guess, and
    # a build that changes it shows up as drift.
    measured = probe.get("scrollbar_thickness_px")
    if measured is not None:
        env_path = HERE / "env.json"
        raw = env_path.read_text(encoding="utf-8")
        env_data = json.loads(raw)
        if env_data["probe"].get("browser_scrollbar_thickness_px") != measured:
            env_data["probe"]["browser_scrollbar_thickness_px"] = measured
            env_path.write_text(
                json.dumps(env_data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
            )
            print(f"  env.json: browser_scrollbar_thickness_px = {measured:g}")
    return 0 if failures == 0 else 1


def cmd_list(args, env: dict) -> int:
    matrix = load_json(HERE / "matrix.json")
    levels = {e["id"]: e["level"] for e in matrix["entries"]}
    print(f"{'case':<20} {'required':<9} {'golden':<8} matrix")
    case_results = []
    for case_json in discover_cases(args.case):
        case = load_json(case_json)
        has_golden = any((case_json.parent / "golden").glob("step-*")) if (
            case_json.parent / "golden"
        ).exists() else False
        case_results.append({
            "name": case_json.parent.name,
            "status": "green" if has_golden else "skipped",
            "matrix": case.get("matrix", []),
        })
        tags = ", ".join(
            f"{tag}({levels.get(tag, '?')})" for tag in case.get("matrix", [])
        )
        print(
            f"{case_json.parent.name:<20} "
            f"{'yes' if case.get('required') else 'no':<9} "
            f"{'yes' if has_golden else 'no':<8} {tags}"
        )
    coverage = matrix_coverage(case_results, matrix)
    print(f"\n矩阵「一致」特性 {coverage['covered_features']}/{coverage['aligned_features']} 有 golden 用例")
    if coverage["missing"]:
        print("缺用例: " + ", ".join(coverage["missing"]))
    return 0


def cmd_coverage(args, env: dict) -> int:
    """TESTPLAN section 15: 必过集 is "每特性至少一条 golden 用例". Ask it directly."""
    matrix = load_json(HERE / "matrix.json")
    case_results = []
    for case_json in discover_cases(args.case):
        case = load_json(case_json)
        golden_dir = case_json.parent / "golden"
        has_golden = golden_dir.exists() and any(golden_dir.glob("step-*"))
        case_results.append({
            "name": case_json.parent.name,
            "status": "green" if has_golden else "skipped",
            "matrix": case.get("matrix", []),
        })
    coverage = matrix_coverage(case_results, matrix)
    print("矩阵覆盖（TESTPLAN 第 2 / 15 节）")
    for feature, cases in coverage["covered"].items():
        print(f"  {feature:<28} {', '.join(cases)}")
    for feature in coverage["missing"]:
        print(f"  {feature:<28} — 缺 golden 用例")
    print(f"\n一致特性 {coverage['covered_features']}/{coverage['aligned_features']} 有 golden 用例")
    return 0 if coverage["ok"] else 1


def main(argv: list[str] | None = None) -> int:
    global engine_binary_override
    parser = argparse.ArgumentParser(description="zui differential test harness")
    parser.add_argument(
        "command", choices=["list", "probe", "record", "check", "update-golden", "coverage"]
    )
    parser.add_argument("--case", action="append", default=None,
                        help="repeatable; an exact name or a glob (example-*, grid-?)")
    parser.add_argument("--failed", action="store_true",
                        help="check: only the cases that were red in the last report")
    parser.add_argument("--report", default=str(BUILD_DIR / "run" / "report.json"),
                        help="check --failed: which report to read the red set from")
    parser.add_argument("--jobs", type=int, default=1,
                        help="check: engine replays to run at once (default 1; try 8 for the "
                             "full suite)")
    parser.add_argument("--browser", default=None, help="browser executable path")
    parser.add_argument("--engine-bin", default=None, help="zui_difftest path")
    parser.add_argument("--with-browser", action="store_true",
                        help="check: also replay through the browser to separate drift from "
                             "engine regression")
    parser.add_argument("--run-id", default="", help="stamp this id into the check report")
    parser.add_argument("--engine-only", action="store_true",
                        help="check: skip the L0 probe")
    parser.add_argument("--skip-probe", action="store_true", help="check: reuse the last probe")
    parser.add_argument("--update-golden", action="store_true")
    parser.add_argument("--update-probe-golden", action="store_true")
    parser.add_argument("--require-coverage", action="store_true",
                        help="check: fail when a matrix「一致」feature has no golden case")
    args = parser.parse_args(argv)

    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    engine_binary_override = args.engine_bin
    env = load_env()

    if args.command == "list":
        return cmd_list(args, env)
    if args.command == "probe":
        return cmd_probe(args, env)
    if args.command == "coverage":
        return cmd_coverage(args, env)
    if args.command in ("record", "update-golden"):
        return cmd_record(args, env)
    return cmd_check(args, env)


if __name__ == "__main__":
    raise SystemExit(main())
