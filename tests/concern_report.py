import argparse
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

SESSIONS = (
    "album",
    "calendar",
    "chat",
    "contacts",
    "kanban",
    "reader",
    "zdb",
    "zmd",
    "share",
    "writer",
    "particle",
)
RENDER_PRESENT = "render-present"
RENDER_GPU = "render-gpu"


def _configure_stdout():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")


def _local(tag):
    return tag.rsplit("}", 1)[-1]


def _case_name(elem):
    name = elem.get("name") or ""
    classname = elem.get("classname") or ""
    if "." in name:
        return name
    if classname and name:
        if "." in classname and classname.endswith(name):
            return classname
        return f"{classname}.{name}"
    return name or classname


def _case_status(elem):
    for child in list(elem):
        tag = _local(child.tag)
        if tag in ("failure", "error"):
            return "failed"
        if tag == "skipped":
            return "skipped"
    status = (elem.get("status") or "").lower()
    if status in ("failed", "failure", "error"):
        return "failed"
    if status in ("skipped", "disabled", "notrun"):
        return "skipped"
    return "passed"


def parse_junit(path):
    root = ET.parse(path).getroot()
    results = {}
    nodes = [root]
    if _local(root.tag) != "testcase":
        nodes = [elem for elem in root.iter() if _local(elem.tag) == "testcase"]
    for elem in nodes:
        name = _case_name(elem)
        if not name:
            continue
        status = _case_status(elem)
        if results.get(name) != "failed":
            results[name] = status
    return results


def _judge(statuses, missing=0):
    if any(status == "failed" for status in statuses):
        return "fail", "fail"
    if not statuses or all(status == "skipped" for status in statuses):
        return "not-run", "not-run"
    if all(status == "passed" for status in statuses):
        if missing:
            return "pass", "partial"
        return "pass", "pass"
    return "not-run", "partial"


def _statuses_for(entries, results, tag):
    found = []
    missing = 0
    for entry in entries:
        # `difftest-case` entries are judged by the kernel channels, not by a JUnit testcase;
        # everything else with a testcase name (`gtest`, `oracle`) contributes to its line.
        if entry.get("kind") == "difftest-case":
            continue
        tags = entry.get("tags") or []
        if tag not in tags:
            continue
        name = entry.get("name")
        if name in results:
            found.append(results[name])
        else:
            missing += 1
    return found, missing


def _covered_sessions(entries, results, tag):
    covered = set()
    for entry in entries:
        if entry.get("kind") == "difftest-case":
            continue
        tags = set(entry.get("tags") or [])
        if tag not in tags or tags <= {"smoke", "guard"}:
            continue
        if results.get(entry.get("name")) != "passed":
            continue
        for session in entry.get("sessions") or []:
            if session in SESSIONS:
                covered.add(session)
    return covered


def _uncovered(entries, results, tag):
    covered = _covered_sessions(entries, results, tag)
    return [session for session in SESSIONS if session not in covered]


def _render_parent(present, gpu):
    if present[0] == "fail" or gpu[0] == "fail":
        return "fail", "fail"
    if present[0] == "not-run" and gpu[0] == "not-run":
        return "not-run", "not-run"
    if present[0] == "pass" and gpu[0] in ("pass", "not-run"):
        return "pass", "partial"
    return "not-run", "partial"


def _line(name, job, scope):
    return f"{name} job={job} scope={scope}"


# The nine application shells were promoted to required kernel cases on 2026-10-03, so their Chrome
# diff now feeds `_scan_report` like every other case. Kept as a set for the same reason as
# `report.SHELL_CASES`: the concept can return, but nothing is excluded today.
SHELLS: set[str] = set()
CHANNELS = ("missing", "l1", "geometry", "mechanism", "l3", "l5", "paint", "size-mismatch")
CORRECTNESS = {"missing", "l1", "geometry", "mechanism"}
TRANSFER = {"l3", "l5"}
RENDER_KERNEL = {"paint", "size-mismatch"}
UNSTABLE_OK = {"paint", "size-mismatch"}


def _hits(case):
    found = set()
    unmeasured = set()
    saw_pixels = False
    counts = case.get("counts") or {}
    if counts.get("missing", 0) > 0:
        found.add("missing")
    for name in ("l1", "geometry", "mechanism", "l3", "l5"):
        if counts.get(name, 0) > 0:
            found.add(name)
    for step in case.get("steps") or []:
        reason = step.get("skipped_reason") or ""
        if reason.startswith("framebuffer readback is not implemented"):
            unmeasured.add("paint")
            unmeasured.add("size-mismatch")
        elif reason == "engine produced no capture" or reason.startswith("PNG decode failed"):
            found.add("missing")
        kind = (step.get("pixels") or {}).get("classification")
        if kind == "paint":
            found.add("paint")
            saw_pixels = True
        elif kind == "size-mismatch":
            found.add("size-mismatch")
            saw_pixels = True
        elif kind == "clean":
            saw_pixels = True
    if saw_pixels:
        unmeasured.discard("paint")
        unmeasured.discard("size-mismatch")
    return found, unmeasured


def _scan_report(payload):
    found = set()
    unmeasured = set()
    for case in payload.get("cases") or []:
        name = case.get("name")
        if name in SHELLS:
            continue
        hits, skipped = _hits(case)
        for channel in hits:
            found.add((name, channel))
        for channel in skipped:
            unmeasured.add((name, channel))
    return found, unmeasured


def _load_waivers(path):
    import json

    data = json.loads(Path(path).read_text(encoding="utf-8"))
    waivers = []
    for entry in data.get("entries") or []:
        channel = entry["channel"]
        if channel not in CHANNELS:
            raise SystemExit(f"known_red channel {channel} is not in the channel table")
        unstable = bool(entry.get("unstable", False))
        if unstable and channel not in UNSTABLE_OK:
            raise SystemExit(f"unstable is only for paint or size-mismatch ({entry})")
        waivers.append((entry["case"], channel, unstable))
    return waivers


def _kernel_diff(found, waivers, unmeasured=None):
    unmeasured = unmeasured or set()
    waived = {(case, channel): unstable for case, channel, unstable in waivers}
    missing = sorted(item for item in found if item not in waived)
    # A channel that was not measured is not clean. Do not demand its waiver be deleted.
    delete = sorted(
        item
        for item, unstable in waived.items()
        if item not in found and not unstable and item not in unmeasured
    )
    unstable_clean = sorted(
        item
        for item, unstable in waived.items()
        if item not in found and unstable and item not in unmeasured
    )
    return missing, delete, unstable_clean


def _admit_report(path, junit_path, expect_run_id, require):
    import json

    if path is None:
        return None, (require is True)
    if not path.is_file():
        return None, require
    payload = json.loads(path.read_text(encoding="utf-8"))
    run_id = payload.get("run_id") or ""
    stale = payload.get("stale_binaries") or []
    older = junit_path is not None and path.stat().st_mtime < junit_path.stat().st_mtime
    mismatched = expect_run_id is None or run_id != expect_run_id
    if mismatched or older or stale:
        return None, require
    return payload, False


def _apply_kernel(text, job_scope, kernel_job, kernel_scope):
    name, job, scope = job_scope
    if kernel_job == "fail":
        job = "fail"
        scope = "fail"
    elif kernel_scope == "partial" and scope != "fail":
        scope = "partial"
    elif kernel_job == "not-run" and scope == "pass":
        scope = "partial"
    text.append(_line(name, job, scope))


def _kernel_jobs(kernel):
    idle = ("not-run", "not-run")
    if kernel is None:
        return {"kernel-transfer": idle, "interaction-transfer": idle, "render-kernel": idle}, [], 0
    if kernel.get("rejected"):
        failed = ("fail", "fail")
        lines = ["内核报告缺失、陈旧，或二进制比源码旧"]
        return {
            "kernel-transfer": failed,
            "interaction-transfer": failed,
            "render-kernel": failed,
        }, lines, 1
    unmeasured = kernel.get("unmeasured") or set()
    missing, delete, unstable_clean = _kernel_diff(
        kernel["found"], kernel["waivers"], unmeasured
    )
    still = {channel for _case, channel in kernel["found"]}
    lines = [
        "(a) " + (" ".join(f"{case}:{channel}" for case, channel in missing) or "-"),
        "(b) " + (" ".join(f"{case}:{channel}" for case, channel in delete) or "-"),
        "(c) " + (" ".join(f"{case}:{channel}" for case, channel in unstable_clean) or "-"),
    ]

    def side(names):
        bad = [item for item in missing + delete if item[1] in names]
        if bad:
            return "fail", "fail"
        if any(channel in names for channel in still) or any(
            item[1] in names for item in unstable_clean
        ):
            return "pass", "partial"
        if any(channel in names for _case, channel in unmeasured):
            return "not-run", "not-run"
        return "pass", "pass"

    return {
        "kernel-transfer": side(CORRECTNESS),
        "interaction-transfer": side(TRANSFER),
        "render-kernel": side(RENDER_KERNEL),
    }, lines, (1 if missing or delete else 0)


def report(entries, results, kernel=None):
    jobs, kernel_lines, kernel_code = _kernel_jobs(kernel)
    text = []
    correct_job, correct_scope = _judge(*_statuses_for(entries, results, "正确性"))
    _apply_kernel(text, ("正确性", correct_job, correct_scope), *jobs["kernel-transfer"])
    text.append(_line("kernel-transfer", *jobs["kernel-transfer"]))

    present = _judge(*_statuses_for(entries, results, RENDER_PRESENT))
    gpu = _judge(*_statuses_for(entries, results, RENDER_GPU))
    render_job, render_scope = _render_parent(present, gpu)
    _apply_kernel(text, ("渲染", render_job, render_scope), *jobs["render-kernel"])
    text.append(_line("render-kernel", *jobs["render-kernel"]))
    text.append(_line(RENDER_PRESENT, present[0], present[1]))
    text.append(_line(RENDER_GPU, gpu[0], gpu[1]))

    interact_job, interact_scope = _judge(*_statuses_for(entries, results, "交互"))
    interact_missing = _uncovered(entries, results, "交互")
    if interact_job == "fail":
        interact_scope = "fail"
    elif interact_missing:
        # Coverage, not the verdict, decides this downgrade. The kernel half (interaction-transfer)
        # is merged in below by _apply_kernel, which already turns a not-run half into `partial`;
        # downgrading here as well made `交互 scope=pass` unreachable even on a full green run.
        interact_scope = "partial"
    _apply_kernel(text, ("交互", interact_job, interact_scope), *jobs["interaction-transfer"])
    text.append(_line("interaction-transfer", *jobs["interaction-transfer"]))
    if interact_missing:
        text.append("交互 uncovered: " + " ".join(interact_missing))

    perf_job, perf_scope = _judge(*_statuses_for(entries, results, "性能"))
    text.append(_line("性能", perf_job, perf_scope))

    app_job, app_scope = _judge(*_statuses_for(entries, results, "app"))
    app_missing = _uncovered(entries, results, "app")
    if app_job == "fail":
        app_scope = "fail"
    elif app_missing:
        app_scope = "partial"
    text.append(_line("app", app_job, app_scope))
    if app_missing:
        text.append("app uncovered: " + " ".join(app_missing))

    text.extend(kernel_lines)
    text.append("file-explorer out-of-scope")
    failed = sum(1 for status in results.values() if status == "failed")
    failing = sorted(name for name, status in results.items() if status == "failed")
    if failing:
        text.append("失败: " + " ".join(failing))
        # A red test that no manifest entry claims would otherwise only show up as an exit code.
        listed = {entry.get("name") for entry in entries if entry.get("kind") != "difftest-case"}
        unattributed = [name for name in failing if name not in listed]
        if unattributed:
            text.append("未登记但失败: " + " ".join(unattributed))
    return "\n".join(text) + "\n", (1 if failed or kernel_code else 0)


def _load_manifest(path):
    import json

    data = json.loads(Path(path).read_text(encoding="utf-8"))
    return data["entries"]


def _self_check():
    manifest = [
        {"name": "AppInteract.PerfOne", "kind": "gtest", "tags": ["性能"], "sessions": []},
        {"name": "AppInteract.PerfTwo", "kind": "gtest", "tags": ["性能"], "sessions": []},
    ]
    failed_text, failed_code = report(
        manifest, {"AppInteract.PerfOne": "failed"}
    )
    if failed_code == 0:
        raise SystemExit("failure junit exited 0\n" + failed_text)
    skipped_text, skipped_code = report(
        manifest,
        {"AppInteract.PerfOne": "skipped", "AppInteract.PerfTwo": "skipped"},
    )
    if skipped_code != 0:
        raise SystemExit("skipped performance exited non-zero\n" + skipped_text)
    if "性能 job=not-run scope=not-run" not in skipped_text:
        raise SystemExit("skipped performance did not print not-run\n" + skipped_text)
    if "kernel-transfer job=not-run" not in skipped_text:
        raise SystemExit("junit-only kernel was not not-run\n" + skipped_text)
    present_manifest = [
        {"name": "AppInteract.One", "kind": "gtest", "tags": ["render-present"], "sessions": []},
        {"name": "AppInteract.Two", "kind": "gtest", "tags": ["render-present"], "sessions": []},
    ]
    partial_text, partial_code = report(
        present_manifest, {"AppInteract.One": "passed"}
    )
    if partial_code != 0 or "render-present job=pass scope=partial" not in partial_text:
        raise SystemExit("a registered test missing from junit looked like full scope\n" + partial_text)
    fresh = {
        "rejected": False,
        "found": {("flow-basic", "geometry")},
        "waivers": [],
    }
    fresh_text, fresh_code = report(manifest, {}, fresh)
    if fresh_code == 0 or "(a) flow-basic:geometry" not in fresh_text:
        raise SystemExit("new geometry channel did not fail\n" + fresh_text)
    clean = {
        "rejected": False,
        "found": set(),
        "waivers": [("flow-basic", "geometry", False)],
    }
    clean_text, clean_code = report(manifest, {}, clean)
    if clean_code == 0 or "(b) flow-basic:geometry" not in clean_text:
        raise SystemExit("clean stable waiver did not fail\n" + clean_text)
    flake = {
        "rejected": False,
        "found": set(),
        "waivers": [("paint-transform-text", "paint", True)],
    }
    flake_text, flake_code = report(manifest, {}, flake)
    if flake_code != 0 or "(c) paint-transform-text:paint" not in flake_text:
        raise SystemExit("unstable clean channel failed the job\n" + flake_text)
    held = {
        "rejected": False,
        "found": set(),
        "unmeasured": {("example-hello", "paint"), ("example-hello", "size-mismatch")},
        "waivers": [("example-hello", "paint", False)],
    }
    held_text, held_code = report(manifest, {}, held)
    if held_code != 0 or "(b) example-hello:paint" in held_text:
        raise SystemExit("unmeasured paint waiver was treated as clean\n" + held_text)
    if "render-kernel job=not-run scope=not-run" not in held_text:
        raise SystemExit("unmeasured paint did not leave render-kernel not-run\n" + held_text)
    rejected_text, rejected_code = report(manifest, {}, {"rejected": True})
    if rejected_code == 0 or "内核报告缺失、陈旧，或二进制比源码旧" not in rejected_text:
        raise SystemExit("rejected report did not fail\n" + rejected_text)
    _configure_stdout()
    sys.stdout.write(skipped_text)


def main(argv):
    if "--self-check" in argv:
        _self_check()
        return 0
    parser = argparse.ArgumentParser()
    parser.add_argument("--junit", required=True)
    parser.add_argument("--difftest-report", default=None)
    parser.add_argument("--expect-run-id", default=None)
    parser.add_argument("--require-difftest-report", action="store_true")
    args = parser.parse_args(argv)
    entries = _load_manifest(Path(__file__).resolve().parent / "concern_manifest.json")
    junit = Path(args.junit)
    results = parse_junit(junit)
    kernel = None
    if args.difftest_report or args.require_difftest_report:
        payload, rejected = _admit_report(
            Path(args.difftest_report) if args.difftest_report else None,
            junit,
            args.expect_run_id,
            args.require_difftest_report,
        )
        if rejected:
            kernel = {"rejected": True}
        elif payload is not None:
            waiver_path = Path(__file__).resolve().parent / "difftest" / "known_red.json"
            found, unmeasured = _scan_report(payload)
            kernel = {
                "rejected": False,
                "found": found,
                "unmeasured": unmeasured,
                "waivers": _load_waivers(waiver_path),
            }
    text, code = report(entries, results, kernel)
    _configure_stdout()
    sys.stdout.write(text)
    return code


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
