"""Report assembly and the pass-line verdict (TESTPLAN section 15).

A diff is not a verdict. This module turns per-step diff results into per-layer verdicts
against passline.json, and renders both the machine report and a human page. Layers that the
current stage does not implement yet are reported as `not-implemented`, never as `green`: the
acceptance declaration must not be able to claim a layer nobody ran.
"""

from __future__ import annotations

import html
import json
from datetime import datetime, timezone

SCHEMA = "zui.difftest.report/1"

# Every example case is now a required kernel case: the nine application shells (album, calendar,
# chat, contacts, kanban, reader, zdb, zmd, file-explorer) were promoted on 2026-10-03 so their
# Chrome diff gates the kernel channels like any other case. Their still-red channels are recorded
# in tests/difftest/known_red.json, not swept into a separate bucket. The set is kept so the
# "shell" concept can return if a future case needs it; it is empty on purpose.
SHELL_CASES: set[str] = set()


def _layer(status: str, detail: str, **extra) -> dict:
    return {"status": status, "detail": detail, **extra}


def evaluate_layers(case_results: list[dict], probe: dict | None, passline: dict,
                    selfcheck: dict | None = None) -> dict:
    """Verdicts for the layers this stage can decide; the rest say why they cannot."""
    kernel_only = [case for case in case_results if case.get("name") not in SHELL_CASES]
    case_results = kernel_only
    l1_entries = []
    l2_entries = []
    l3_entries = []
    l4_entries = []
    l5_entries = []
    mechanism_entries = []
    l3_ran = False
    l5_ran = False
    convergence_entries = []
    architecture_entries = []
    capture_unmeasured = False
    pixel_compared = False
    for case in case_results:
        convergence = case.get("convergence")
        if convergence and not convergence.get("ok", True) and not convergence.get("expected"):
            convergence_entries.append((case["name"], convergence))
        architecture = case.get("architecture")
        if architecture and not architecture.get("ok", True):
            for violation in architecture.get("violations", []):
                architecture_entries.append((case["name"], violation))
        for step in case["steps"]:
            reason = step.get("skipped_reason") or ""
            if reason.startswith("framebuffer readback is not implemented"):
                capture_unmeasured = True
            if step.get("pixels"):
                pixel_compared = True
            if step.get("skipped_reason"):
                continue
            if step.get("readback_layer") == "l3":
                l3_ran = True
            if step.get("readback_layer") == "l5":
                l5_ran = True
            for entry in step.get("entries", []):
                if entry["kind"] == "l1":
                    l1_entries.append((case["name"], step["step"], entry))
                elif entry["kind"] in ("structure", "length"):
                    l2_entries.append((case["name"], step["step"], entry))
                elif entry["kind"] == "l3":
                    l3_entries.append((case["name"], step["step"], entry))
                elif entry["kind"] == "l5":
                    l5_entries.append((case["name"], step["step"], entry))
                elif entry["kind"] == "mechanism":
                    mechanism_entries.append((case["name"], step["step"], entry))
            pixel = step.get("pixels")
            if pixel and pixel.get("classification") not in (None, "clean"):
                l4_entries.append((case["name"], step["step"], pixel))
                if pixel.get("classification") == "mechanism":
                    mechanism_entries.append((case["name"], step["step"], pixel))
    # While stacking_paint_order returns the child list in tree order, a full-page mask
    # drawn under the content has to fail L4. A green mask page would mean the zones
    # were loosened past the point where that bug is visible.
    for case in case_results:
        if case.get("name") != "example-transition-mask":
            continue
        pixels = [step.get("pixels") for step in case.get("steps", []) if step.get("pixels")]
        if pixels and all(pixel.get("classification") == "clean" for pixel in pixels):
            l4_entries.append((case["name"], "guard", {
                "classification": "paint",
                "differing": 1,
                "message": "层叠仍按树序绘制，整页遮罩用例的 L4 必须为红",
            }))

    layers: dict[str, dict] = {}

    if probe is None:
        layers["L0"] = _layer("not-run", "probe was not executed")
    else:
        failures = probe.get("failures", [])
        environment_failures = [f for f in failures
                                if not isinstance(f, dict) or f.get("class") != "engine_geometry"]
        engine_failures = [f for f in failures if isinstance(f, dict)
                           and f.get("class") == "engine_geometry"]
        # The probe measures two things: the environment (flags, golden drift, pinned build)
        # and one engine parameter. Only the first is an environment verdict; an engine
        # parameter that disagrees with the browser is a geometry divergence and must not
        # masquerade as "the environment is not pinned".
        ok = not environment_failures
        detail = "环境钉死：flag 集、golden、钉死版本都一致" if ok \
            else f"{len(environment_failures)} 项环境自检失败"
        if engine_failures:
            detail += f"；另有 {len(engine_failures)} 项引擎参数分歧（归 L2，不算环境问题）"
        layers["L0"] = _layer(
            "green" if ok else "red",
            detail,
            measurements=probe.get("measurements", {}),
            failures=[f["message"] if isinstance(f, dict) else f for f in environment_failures],
            engine_geometry=[f["message"] if isinstance(f, dict) else f for f in engine_failures],
        )
        for failure in engine_failures:
            l2_entries.append(("L0-probe", "probe", {
                "element": "documentElement",
                "field": failure.get("field", "probe"),
                "kind": "length",
                "expected": "= browser",
                "actual": "engine parameter differs",
                "tolerance": 0,
                "delta": None,
                "message": failure["message"] if isinstance(failure, dict) else str(failure),
            }))

    if not kernel_only:
        layers["L1"] = _layer("not-run", "本轮没有内核用例")
        layers["L2"] = _layer("not-run", "本轮没有内核用例")
    else:
        layers["L1"] = _layer(
            "green" if not l1_entries else "red",
            "computed-style dump matches on the props.json list" if not l1_entries
            else f"{len(l1_entries)} property differences",
            entries=[[a, b, c] for a, b, c in l1_entries[:200]],
        )
        layers["L2"] = _layer(
            "green" if not l2_entries else "red",
            "structure fields exact, lengths within 1/64 px" if not l2_entries
            else f"{len(l2_entries)} geometry differences",
            entries=[[a, b, c] for a, b, c in l2_entries[:200]],
        )

    # L3 is the state sequence: every readback in the required set. The anchoring readback
    # (`scroll-anchor`'s insert step, TESTPLAN section 4) is in that set, so a clean run is
    # green; `scrollIntoView` is still an open case, tracked as such, not a reason to withhold
    # the verdict.
    if not l3_ran and not l3_entries:
        l3_status, l3_detail = "not-implemented", "还没有进入必过集的滚动读回"
    elif l3_entries:
        l3_status, l3_detail = "red", f"{len(l3_entries)} 条滚动读回不一致"
    else:
        l3_status, l3_detail = "green", "必过集里的状态序列读回一致"
    layers["L3"] = _layer(l3_status, l3_detail, entries=[[a, b, c] for a, b, c in l3_entries[:100]])

    if not kernel_only:
        layers["L4"] = _layer("not-run", "本轮没有内核用例")
    elif l4_entries:
        layers["L4"] = _layer(
            "red",
            f"{len(l4_entries)} frames differ",
            entries=[[a, b, c] for a, b, c in l4_entries[:50]],
        )
    elif capture_unmeasured and not pixel_compared:
        # No presented frame was read. A green L4 here would claim the pixels matched.
        layers["L4"] = _layer(
            "not-implemented",
            "帧缓冲读回未实现（ZUI_PROBE_FRAME=0）。测试光栅已删除，本层没有程序画面可对",
        )
    else:
        layers["L4"] = _layer(
            "green",
            "masked pixel compare clean",
            entries=[],
        )

    if not l5_ran and not l5_entries:
        l5_status, l5_detail = "not-implemented", "还没有行为短页的读回"
    elif l5_entries:
        l5_status, l5_detail = "red", f"{len(l5_entries)} 条行为读回不一致"
    else:
        l5_status, l5_detail = "green", "已进入必过集的行为读回一致"
    layers["L5"] = _layer(l5_status, l5_detail, entries=[[a, b, c] for a, b, c in l5_entries[:100]])
    if mechanism_entries:
        layers["mechanism"] = _layer(
            "red", f"{len(mechanism_entries)} 条机制错误",
            entries=[[a, b, c] for a, b, c in mechanism_entries[:50]],
        )
    l6_ok = selfcheck is not None and selfcheck["status"] == "pass" \
        and not convergence_entries and not architecture_entries
    if selfcheck is None:
        l6_detail = ("zui_difftest_selfcheck is not built (ctest target: "
                     "zui_difftest_selfcheck)")
        l6_status = "not-run"
    elif not l6_ok:
        l6_detail = "; ".join(
            [f"selfcheck: {selfcheck['status']}"]
            + [f"{name}: converged == false at {c['steps'][0]['step']}"
               for name, c in convergence_entries[:5]]
            + [f"{name}: {v['rule']} ({v['detail']})" for name, v in architecture_entries[:5]]
        )
        l6_status = "red"
    else:
        l6_detail = "engine-local architecture asserts passed"
        l6_status = "green"
    layers["L6"] = _layer(
        l6_status,
        l6_detail,
        detail_output=(selfcheck or {}).get("detail", ""),
        convergence=[[name, c] for name, c in convergence_entries[:20]],
        architecture=[[name, v] for name, v in architecture_entries[:20]],
    )
    layers["L7"] = _layer(
        "not-implemented",
        "WPT reftest subset and fuzz differential are stage 4",
    )
    return layers


def required_set_status(case_results: list[dict], declared_required: dict[str, bool],
                        enforce: bool) -> dict:
    missing = [name for name, required in declared_required.items() if required
               and name not in {c["name"] for c in case_results}]
    failing = [c["name"] for c in case_results
               if c["status"] != "green" and c.get("name") not in SHELL_CASES]
    return {
        "required_cases": sorted(n for n, r in declared_required.items() if r),
        "missing": missing,
        "failing": failing,
        "ok": not missing and (not enforce or not failing),
    }


def declaration_status(layers: dict, required: dict, enabled: bool) -> dict:
    blocked = [name for name, layer in layers.items()
               if layer["status"] in ("red", "not-implemented", "partial")]
    coverage = required.get("matrix_coverage") or {"ok": True, "missing": []}
    active = bool(enabled) and not blocked and required["ok"] and coverage["ok"]
    reason = "all applicable layers green"
    if not enabled:
        reason = "declaration disabled in passline.json until the pass-line numbers are frozen"
    elif blocked:
        reason = "blocked by layer(s): " + ", ".join(sorted(blocked))
    elif not coverage["ok"]:
        reason = "matrix「一致」特性还没有 golden 用例：" + ", ".join(coverage["missing"])
    elif not required["ok"]:
        reason = "required case set is not fully green"
    return {
        "active": active,
        "reason": reason,
        "blocking_layers": sorted(blocked),
        "missing_features": coverage["missing"],
    }


def build_report(case_results: list[dict], probe: dict | None, passline: dict, matrix: dict,
                 environment: dict, selfcheck: dict | None = None,
                 coverage: dict | None = None) -> dict:
    layers = evaluate_layers(case_results, probe, passline, selfcheck)
    required = required_set_status(
        case_results,
        {c["name"]: c.get("required", False) for c in case_results},
        passline.get("required_set", {}).get("enforce_regressions", True),
    )
    required["matrix_coverage"] = coverage or {"ok": True, "missing": []}
    if coverage is not None and not coverage.get("ok", True):
        required["ok"] = False
    declaration = declaration_status(layers, required, passline.get("declaration", {}).get("enabled", False))
    shells = [c for c in case_results if c.get("name") in SHELL_CASES]
    shell_bucket = {
        "names": sorted(SHELL_CASES),
        "red": sum(1 for c in shells if c.get("status") == "red"),
        "green": sum(1 for c in shells if c.get("status") == "green"),
        "l1": sum(c.get("counts", {}).get("l1", 0) for c in shells),
        "geometry": sum(c.get("counts", {}).get("geometry", 0) for c in shells),
        "pixels": sum(c.get("counts", {}).get("pixels", 0) for c in shells),
        "cases": shells,
    }
    totals = {
        "cases": len(case_results),
        "green": sum(1 for c in case_results if c["status"] == "green"),
        "red": sum(1 for c in case_results if c["status"] == "red"),
        "skipped": sum(1 for c in case_results if c["status"] == "skipped"),
        "l1_differences": sum(c["counts"]["l1"] for c in case_results),
        "geometry_differences": sum(c["counts"]["geometry"] for c in case_results),
        "pixel_differences": sum(c["counts"]["pixels"] for c in case_results),
        "deferred": sum(c["counts"]["deferred"] for c in case_results),
    }
    return {
        "schema": SCHEMA,
        "generated": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "environment": environment,
        "matrix_levels": matrix.get("levels", {}),
        "cases": case_results,
        "shell_bucket": shell_bucket,
        "layers": layers,
        "required_set": required,
        "matrix_coverage": required["matrix_coverage"],
        "declaration": declaration,
        "totals": totals,
    }


# ---------------------------------------------------------------- rendering

_STATUS_COLOURS = {
    "green": "#137333",
    "red": "#b3261e",
    "not-implemented": "#8a6d00",
    "not-run": "#5f6368",
    "skipped": "#5f6368",
}


def render_html(report: dict) -> str:
    parts: list[str] = []
    parts.append(
        """<!DOCTYPE html>
<html lang="zh-CN"><head><meta charset="utf-8">
<title>zui difftest report</title>
<style>
 body { font: 13px/1.55 system-ui, "Segoe UI", sans-serif; margin: 0; padding: 24px 32px;
        background: #ffffff; color: #202124; }
 h1 { font-size: 20px; margin: 0 0 4px; }
 h2 { font-size: 15px; margin: 28px 0 8px; }
 .meta { color: #5f6368; margin-bottom: 16px; }
 table { border-collapse: collapse; width: 100%; margin-bottom: 12px; }
 th, td { border: 1px solid #dadce0; padding: 5px 8px; text-align: left; vertical-align: top; }
 th { background: #f1f3f4; font-weight: 600; }
 code, .mono { font-family: ui-monospace, Consolas, monospace; font-size: 12px; }
 .badge { display: inline-block; padding: 1px 7px; border-radius: 9px; color: #fff;
          font-size: 11px; font-weight: 600; }
 .num { text-align: right; }
 .sub { color: #5f6368; }
</style></head><body>"""
    )
    env = report["environment"]
    parts.append("<h1>zui difftest 报告</h1>")
    parts.append(
        f'<div class="meta">{html.escape(report["generated"])} · '
        f'浏览器 {html.escape(str(env.get("browser_version") or "未运行"))} · '
        f'flag 集 {len(env.get("flags", []))} 项 · '
        f'用例 {report["totals"]["cases"]} 个</div>'
    )

    parts.append("<h2>通过线</h2><table><tr><th>层</th><th>状态</th><th>说明</th></tr>")
    for name, layer in report["layers"].items():
        colour = _STATUS_COLOURS.get(layer["status"], "#5f6368")
        parts.append(
            f'<tr><td class="mono">{name}</td>'
            f'<td><span class="badge" style="background:{colour}">{layer["status"]}</span></td>'
            f'<td>{html.escape(layer["detail"])}</td></tr>'
        )
    parts.append("</table>")

    coverage = report.get("matrix_coverage", {})
    if coverage:
        parts.append("<h2>矩阵覆盖（TESTPLAN 第 2 / 15 节）</h2>")
        parts.append(
            f'<p>「一致」特性 <b>{coverage.get("covered_features", 0)}'
            f'/{coverage.get("aligned_features", 0)}</b> 有 golden 用例。'
            '必过集要求每特性至少一条（第 15 节）。</p>'
        )
        if coverage.get("missing"):
            parts.append('<p class="mono">缺用例：'
                         + html.escape(", ".join(coverage["missing"])) + "</p>")
        parts.append("<table><tr><th>特性</th><th>用例</th></tr>")
        for feature, names in coverage.get("covered", {}).items():
            parts.append(f'<tr><td class="mono">{html.escape(feature)}</td>'
                         f'<td class="mono">{html.escape(", ".join(names))}</td></tr>')
        parts.append("</table>")

    decl = report["declaration"]
    parts.append("<h2>验收声明</h2>")
    parts.append(
        f'<p><span class="badge" style="background:'
        f'{"#137333" if decl["active"] else "#b3261e"}">'
        f'{"生效" if decl["active"] else "未生效"}</span> {html.escape(decl["reason"])}</p>'
    )

    parts.append("<h2>用例</h2><table>"
                 "<tr><th>用例</th><th>状态</th><th>矩阵</th>"
                 '<th class="num">L1</th><th class="num">结构/长度</th>'
                 '<th class="num">像素</th><th class="num">延后</th></tr>')
    for case in report["cases"]:
        colour = _STATUS_COLOURS.get(case["status"], "#5f6368")
        counts = case["counts"]
        parts.append(
            f'<tr><td class="mono">{html.escape(case["name"])}</td>'
            f'<td><span class="badge" style="background:{colour}">{case["status"]}</span></td>'
            f'<td class="sub">{html.escape(", ".join(case.get("matrix", [])))}</td>'
            f'<td class="num">{counts["l1"]}</td>'
            f'<td class="num">{counts["geometry"]}</td>'
            f'<td class="num">{counts["pixels"]}</td>'
            f'<td class="num">{counts["deferred"]}</td></tr>'
        )
    parts.append("</table>")

    for case in report["cases"]:
        convergence = case.get("convergence")
        if convergence and not convergence.get("ok", True) and not convergence.get("expected"):
            parts.append(f'<h2>不收敛诊断 · {html.escape(case["name"])}</h2>')
            parts.append('<p class="sub">LAYOUT 第 16 节的 converged 诊断：为假的盒子与来源。'
                         '除非用例显式构造，否则这是失败。</p>')
            parts.append("<table><tr><th>路径</th><th>元素</th><th>来源</th></tr>")
            for box in convergence.get("boxes", [])[:50]:
                label = f'{box.get("tag", "?")}#{box.get("id", "")}'
                parts.append(f'<tr><td class="mono">{html.escape(str(box.get("path")))}</td>'
                             f'<td class="mono">{html.escape(label)}</td>'
                             f'<td>{html.escape(str(box.get("reason")))}</td></tr>')
            parts.append("</table>")
        architecture = case.get("architecture")
        if architecture and not architecture.get("ok", True):
            parts.append(f'<h2>架构自检 · {html.escape(case["name"])}</h2>')
            parts.append("<table><tr><th>规则</th><th>步骤</th><th>说明</th></tr>")
            for violation in architecture.get("violations", []):
                parts.append(f'<tr><td class="mono">{html.escape(violation["rule"])}</td>'
                             f'<td class="mono">{html.escape(str(violation["step"]))}</td>'
                             f'<td>{html.escape(violation["detail"])}</td></tr>')
            parts.append("</table>")

        entries = [(step["step"], e) for step in case["steps"] for e in step.get("entries", [])]
        if not entries:
            continue
        parts.append(f'<h2>差异明细 · {html.escape(case["name"])}</h2>')
        parts.append(
            "<table><tr><th>步骤</th><th>元素</th><th>字段</th><th>类别</th>"
            "<th>期望</th><th>实际</th>"
            '<th class="num">容差</th><th class="num">偏差</th></tr>'
        )
        for step_name, entry in entries[:300]:
            parts.append(
                f'<tr><td class="mono">{html.escape(step_name)}</td>'
                f'<td class="mono">{html.escape(str(entry.get("element")))}</td>'
                f'<td class="mono">{html.escape(str(entry.get("field")))}</td>'
                f'<td>{html.escape(str(entry.get("kind")))}</td>'
                f'<td class="mono">{html.escape(str(entry.get("expected")))}</td>'
                f'<td class="mono">{html.escape(str(entry.get("actual")))}</td>'
                f'<td class="num">{html.escape(str(entry.get("tolerance")))}</td>'
                f'<td class="num">{html.escape(str(entry.get("delta")))}</td></tr>'
            )
        parts.append("</table>")
        if len(entries) > 300:
            parts.append(f'<p class="sub">只显示前 300 条，共 {len(entries)} 条。</p>')

    warnings = [(case["name"], w) for case in report["cases"] for w in case.get("warnings", [])]
    if warnings:
        parts.append("<h2>驱动/归一化告警</h2><ul>")
        for name, warning in warnings[:50]:
            parts.append(f'<li><span class="mono">{html.escape(name)}</span>: '
                         f'{html.escape(warning.get("warning", ""))}</li>')
        parts.append("</ul>")

    parts.append("</body></html>")
    return "\n".join(parts)


def write_report(report: dict, json_path: str, html_path: str) -> None:
    with open(json_path, "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=2, ensure_ascii=False)
        handle.write("\n")
    with open(html_path, "w", encoding="utf-8") as handle:
        handle.write(render_html(report))
