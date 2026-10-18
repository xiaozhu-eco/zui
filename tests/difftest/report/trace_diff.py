"""L1/L2 trace diff (TESTPLAN sections 9 and 11).

Schema is ``zui.difftest.trace/2``. A golden that is still schema 1, or that is missing a
required field, is a mechanism error: the step is not scored as geometry. Lengths are compared
relative to the containing block. A shared delta under one containing block collapses to the
earliest in-flow box; the rest of the group is a consequence and does not add to the count.

* **structure** — element presence, line count, breaks, containing-block path, overflow rect
  presence. Tolerance is 0.
* **length** — relative position and size, client size, scroll offset, line advance, first-line
  offset. Tolerance is 1/64 px.
* **mechanism** — schema, a containing-block path that the two sides do not agree on, a missing
  readback file. Red, and not a layout bug.
"""

from __future__ import annotations

import math
import re

_NUMBER = re.compile(r"^(-?\d+(?:\.\d+)?)(px|%)?$")

DEFAULT_LENGTH_TOLERANCE = 0.015625  # 1/64 px
DEFAULT_L1_NUMERIC_TOLERANCE = 0.01
TRACE_SCHEMA = "zui.difftest.trace/2"
_BOX_FIELDS = ("rel_x", "rel_y", "w", "h")


def parse_number(value):
    if not isinstance(value, str):
        return None
    match = _NUMBER.match(value.strip())
    if not match:
        return None
    return float(match.group(1)), (match.group(2) or "")


def values_equal(expected, actual, tolerance: float) -> tuple[bool, float]:
    if isinstance(expected, (int, float)) and isinstance(actual, (int, float)):
        delta = abs(float(expected) - float(actual))
        return delta <= tolerance, delta
    if isinstance(expected, str) and isinstance(actual, str):
        left = parse_number(expected)
        right = parse_number(actual)
        if left is not None and right is not None and left[1] == right[1]:
            delta = abs(left[0] - right[0])
            return delta <= tolerance, delta
        return expected == actual, 0.0 if expected == actual else float("inf")
    return expected == actual, 0.0 if expected == actual else float("inf")


def _chrome_int(value: float) -> float:
    # CSSOM `scrollWidth` / `clientWidth` are integers. Blink snaps with half away from zero
    # (`floor(x + 0.5)` for positive x). Python `round` uses half-to-even, so 104.5 became 104
    # while Edge reports 105. The border box stays the subpixel width.
    number = float(value)
    if number >= 0:
        return float(math.floor(number + 0.5 + 1e-9))
    return float(math.ceil(number - 0.5 - 1e-9))


def _round_pair(expected, actual) -> tuple[float, float]:
    return (_chrome_int(expected), _chrome_int(actual))


def _parent_path(path: str) -> str | None:
    if not path or "/" not in path:
        return None
    return path.rsplit("/", 1)[0]


def _box(element: dict, key: str) -> list[float]:
    raw = element.get(key) or [0, 0, 0, 0]
    return [float(raw[0]), float(raw[1]), float(raw[2]), float(raw[3])]


def _cb_mode(element: dict) -> tuple[str, str | None]:
    """How this element's containing block is named.

    ``parent`` uses the DOM parent. ``path`` names another element. ``anonymous`` is a
    containing block that is not in the trace (the viewport, an anonymous box), so the
    compare stays in world coordinates. ``fragment`` does not subtract a containing block:
    the border box is a fragment union.
    """
    if element.get("fragmented"):
        return "fragment", None
    if "containing_block" in element:
        named = element.get("containing_block")
        if named is None:
            return "anonymous", None
        return "path", named
    parent = _parent_path(element.get("path") or "")
    if parent is None:
        return "root", None
    return "parent", parent


class TraceDiffer:
    def __init__(self, length_tolerance: float = DEFAULT_LENGTH_TOLERANCE,
                 l1_numeric_tolerance: float = DEFAULT_L1_NUMERIC_TOLERANCE,
                 pixels: str = "strict"):
        self.length_tolerance = length_tolerance
        self.l1_numeric_tolerance = l1_numeric_tolerance
        self.pixels = pixels
        self.entries: list[dict] = []
        self.warnings: list[dict] = []
        self.l1_differences = 0
        self.l2_differences = 0
        self.mechanism = 0
        self.deferred = 0
        self._order: dict[str, int] = {}
        self._box_diffs: list[dict] = []

    def _add(self, kind: str, element: str, field: str, expected, actual, tolerance: float,
             delta: float, **extra) -> None:
        self.entries.append(
            {
                "element": element,
                "field": field,
                "kind": kind,
                "expected": expected,
                "actual": actual,
                "tolerance": tolerance,
                "delta": delta if delta != float("inf") else None,
                **extra,
            }
        )
        if kind == "mechanism":
            self.mechanism += 1

    def compare_structure(self, element: str, field: str, expected, actual) -> None:
        if expected != actual:
            self._add("structure", element, field, expected, actual, 0, float("inf"))

    def compare_length(self, element: str, field: str, expected, actual, tolerance: float | None = None) -> None:
        limit = self.length_tolerance if tolerance is None else tolerance
        equal, delta = values_equal(expected, actual, limit)
        if not equal:
            self._add("length", element, field, expected, actual, limit, delta)

    def compare_style(self, element: str, prop: str, expected, actual) -> None:
        equal, delta = values_equal(expected, actual, self.l1_numeric_tolerance)
        if equal:
            return
        self.l1_differences += 1
        self._add("l1", element, f"style.{prop}", expected, actual, self.l1_numeric_tolerance, delta)

    def _require_schema(self, golden: dict, actual: dict) -> bool:
        for label, trace in (("golden", golden), ("engine", actual)):
            if trace.get("schema") != TRACE_SCHEMA:
                self._add(
                    "mechanism", "document", "schema", TRACE_SCHEMA, trace.get("schema"), 0,
                    float("inf"),
                    message=f"{label} trace 不是 {TRACE_SCHEMA}，需要重录",
                )
                return False
            for element in trace.get("elements", []):
                if not element.get("has_box"):
                    continue
                for key in ("client_box", "client_rects", "scroll_rects", "border_box", "scroll_offset"):
                    if key not in element:
                        self._add(
                            "mechanism", element.get("path", "?"), key, "present", "missing", 0,
                            float("inf"),
                            message=f"{label} 缺 schema 2 字段 {key}，需要重录",
                        )
                        return False
                lines = element.get("lines")
                if lines is not None and "first_line_offset" not in lines:
                    self._add(
                        "mechanism", element.get("path", "?"), "lines.first_line_offset",
                        "present", "missing", 0, float("inf"),
                        message=f"{label} 缺首行顶距，需要重录",
                    )
                    return False
        return True

    def _relative(self, element: dict, by_path: dict) -> tuple[str, str | None, list[float] | None]:
        mode, named = _cb_mode(element)
        box = _box(element, "border_box")
        if mode in ("fragment", "anonymous", "root"):
            return mode, named, box
        cb = by_path.get(named or "")
        if cb is None or not cb.get("has_box"):
            return "anonymous", named, box
        origin = _box(cb, "border_box")
        return mode, named, [box[0] - origin[0], box[1] - origin[1], box[2], box[3]]

    def compare_element(self, golden: dict, actual: dict, golden_by_path: dict, actual_by_path: dict) -> None:
        name = self._name(actual if actual.get("path") else golden)
        path = golden.get("path") or actual.get("path") or "?"
        self.compare_structure(name, "tag", golden.get("tag"), actual.get("tag"))
        self.compare_structure(name, "has_box", golden.get("has_box", False), actual.get("has_box", False))
        transformed = bool(golden.get("transformed")) or bool(actual.get("transformed"))
        self.compare_structure(name, "transformed", bool(golden.get("transformed", False)),
                               bool(actual.get("transformed", False)))
        self.compare_structure(name, "fragmented", bool(golden.get("fragmented", False)),
                               bool(actual.get("fragmented", False)))
        self.compare_structure(name, "out_of_flow", bool(golden.get("out_of_flow", False)),
                               bool(actual.get("out_of_flow", False)))

        g_mode, g_cb = _cb_mode(golden)
        a_mode, a_cb = _cb_mode(actual)
        if g_mode != a_mode or g_cb != a_cb:
            self._add(
                "mechanism", name, "containing_block",
                {"mode": g_mode, "path": g_cb}, {"mode": a_mode, "path": a_cb}, 0, float("inf"),
                message="包含块路径两端不同",
            )
            # The path mismatch is a mechanism error. The box's own world rectangle is still
            # measured, so a fixed or absolute element in the wrong place is not dropped.
            if golden.get("has_box") and actual.get("has_box"):
                g_box = _box(golden, "border_box")
                a_box = _box(actual, "border_box")
                for index, field in enumerate(("border_box.x", "border_box.y", "border_box.w", "border_box.h")):
                    self.compare_length(name, field, g_box[index], a_box[index])
        elif golden.get("has_box") and actual.get("has_box"):
            _, _, g_rel = self._relative(golden, golden_by_path)
            _, _, a_rel = self._relative(actual, actual_by_path)
            fields = ("x", "y", "w", "h") if g_mode in ("fragment", "anonymous", "root") else _BOX_FIELDS
            # World fields keep the border_box.* names when we cannot subtract a containing block.
            names = ("border_box.x", "border_box.y", "border_box.w", "border_box.h") if fields[0] == "x" else fields
            assert g_rel is not None and a_rel is not None
            for index, field in enumerate(names):
                signed = a_rel[index] - g_rel[index]
                if abs(signed) > self.length_tolerance:
                    self._box_diffs.append({
                        "element": name,
                        "path": path,
                        "field": field,
                        "expected": g_rel[index],
                        "actual": a_rel[index],
                        "signed": signed,
                        "cb": g_cb if g_mode in ("parent", "path") else path,
                        "in_flow": not golden.get("out_of_flow") and not actual.get("out_of_flow"),
                        "groupable": field in ("rel_x", "rel_y", "w", "h"),
                    })
            for index, axis in enumerate(("x", "y")):
                self.compare_length(name, f"scroll_offset.{axis}",
                                    (golden.get("scroll_offset") or [0, 0])[index],
                                    (actual.get("scroll_offset") or [0, 0])[index])
            for axis, index in (("w", 0), ("h", 1)):
                expected_size = _round_pair((golden.get("scroll_size") or [0, 0])[index],
                                            (actual.get("scroll_size") or [0, 0])[index])
                self.compare_length(name, f"scroll_size.{axis}", expected_size[0], expected_size[1], 0.5)
            # Chrome's clientWidth/clientHeight are integers, same as scrollWidth. A fractional
            # engine client box is not a layout difference until it rounds to another pixel.
            g_client = _box(golden, "client_box")
            a_client = _box(actual, "client_box")
            for axis, index in (("w", 2), ("h", 3)):
                rounded = _round_pair(g_client[index], a_client[index])
                self.compare_length(name, f"client_box.{axis}", rounded[0], rounded[1], 0.5)
            self.compare_structure(name, "scroll_rects", len(golden.get("scroll_rects") or []),
                                   len(actual.get("scroll_rects") or []))
            self.compare_structure(name, "client_rects", len(golden.get("client_rects") or []),
                                   len(actual.get("client_rects") or []))

        self._compare_lines(name, golden.get("lines"), actual.get("lines"))
        golden_style = golden.get("style") or {}
        actual_style = actual.get("style") or {}
        for prop in golden_style:
            if prop not in actual_style:
                self.compare_style(name, prop, golden_style[prop], None)
                continue
            self.compare_style(name, prop, golden_style[prop], actual_style[prop])
        if (golden.get("style_digest") and actual.get("style_digest")
                and golden["style_digest"] != actual["style_digest"]
                and golden_style == actual_style
                and not any(e["element"] == name and e["kind"] == "l1" for e in self.entries)):
            self.warnings.append({
                "element": name,
                "warning": "style_digest differs while every property matched "
                           "(normalization drift between the two collectors)",
                "golden": golden["style_digest"],
                "actual": actual["style_digest"],
            })

    def _compare_lines(self, name: str, golden_lines, actual_lines) -> None:
        self.compare_structure(name, "lines.present", golden_lines is not None, actual_lines is not None)
        if not golden_lines or not actual_lines:
            return
        self.compare_structure(name, "lines.count", golden_lines.get("count"), actual_lines.get("count"))
        if "baseline_defined" in golden_lines or "baseline_defined" in actual_lines:
            self.compare_structure(name, "lines.baseline_defined",
                                   bool(golden_lines.get("baseline_defined", True)),
                                   bool(actual_lines.get("baseline_defined", True)))
        golden_tops = golden_lines.get("tops") or []
        actual_tops = actual_lines.get("tops") or []
        for index in range(min(len(golden_tops), len(actual_tops)) - 1):
            self.compare_length(name, f"lines.advance[{index}]",
                                golden_tops[index + 1] - golden_tops[index],
                                actual_tops[index + 1] - actual_tops[index])
        if "first_line_offset" in golden_lines or "first_line_offset" in actual_lines:
            self.compare_length(name, "lines.first_line_offset",
                                golden_lines.get("first_line_offset", 0),
                                actual_lines.get("first_line_offset", 0))
        if golden_lines.get("breaks") or actual_lines.get("breaks"):
            self.compare_structure(name, "lines.breaks", golden_lines.get("breaks"), actual_lines.get("breaks"))
        for line_index, advances in enumerate(golden_lines.get("advances") or []):
            actual_advances = actual_lines.get("advances") or []
            if line_index >= len(actual_advances):
                break
            if not advances and not actual_advances[line_index]:
                continue
            self.compare_structure(name, f"lines.advances[{line_index}].count",
                                   len(advances), len(actual_advances[line_index]))
            for index, value in enumerate(advances):
                if index < len(actual_advances[line_index]):
                    self.compare_length(name, f"lines.advances[{line_index}][{index}]", value,
                                        actual_advances[line_index][index])

    @staticmethod
    def _name(element: dict) -> str:
        path = element.get("path", "?")
        tag = element.get("tag", "?")
        element_id = element.get("id") or ""
        classes = element.get("classes") or ""
        label = f"{tag}"
        if element_id:
            label += f"#{element_id}"
        if classes:
            label += "." + ".".join(str(classes).split())
        return f"{path} {label}"

    def _aggregate(self) -> None:
        """Collapse a shared (containing block, field, delta) into one root cause.

        A later sibling whose y moved by the same amount an earlier sibling grew is the same
        cause: the height group absorbs the position group.
        """
        groups: dict[tuple, list[dict]] = {}
        loose: list[dict] = []
        for item in self._box_diffs:
            if not item["groupable"]:
                loose.append(item)
                continue
            key = (item["cb"], item["field"], round(item["signed"], 5))
            groups.setdefault(key, []).append(item)
        roots: list[tuple[dict, list[dict]]] = []
        for members in groups.values():
            members.sort(key=lambda member: self._order.get(member["path"], 10**9))
            inflow = [member for member in members if member["in_flow"]]
            root = inflow[0] if inflow else members[0]
            roots.append((root, [member for member in members if member is not root]))

        def earlier_size(field: str, delta: float, cb, path: str):
            size_field = "h" if field == "rel_y" else "w" if field == "rel_x" else None
            if size_field is None:
                return None
            order = self._order.get(path, 10**9)
            found = None
            for root, _rest in roots:
                if root["field"] != size_field or root["cb"] != cb:
                    continue
                if abs(root["signed"] - delta) > self.length_tolerance:
                    continue
                if self._order.get(root["path"], 10**9) < order:
                    found = root
            return found

        absorbed: set[int] = set()
        for index, (root, rest) in enumerate(roots):
            if root["field"] not in ("rel_x", "rel_y"):
                continue
            host = earlier_size(root["field"], root["signed"], root["cb"], root["path"])
            if host is None:
                continue
            absorbed.add(index)
            for other_root, other_rest in roots:
                if other_root is host:
                    other_rest.append(root)
                    other_rest.extend(rest)
                    break
        for index, (root, rest) in enumerate(roots):
            if index in absorbed:
                continue
            self._emit_box(root, rest)
        for item in loose:
            self._emit_box(item, [])

    def _emit_box(self, item: dict, rest: list[dict]) -> None:
        self._add(
            "length", item["element"], item["field"], item["expected"], item["actual"],
            self.length_tolerance, abs(item["signed"]),
            consequences=[member["element"] + " " + member["field"] for member in rest],
        )

    def compare(self, golden: dict, actual: dict) -> dict:
        self.entries = []
        self.warnings = []
        self._box_diffs = []
        self.l1_differences = 0
        self.mechanism = 0
        self.deferred = 0
        if not self._require_schema(golden, actual):
            self.l2_differences = 0
            return self._result("mechanism")

        golden_elements = {e.get("path"): e for e in golden.get("elements", [])}
        actual_elements = {e.get("path"): e for e in actual.get("elements", [])}
        self._order = {}
        for index, element in enumerate(golden.get("elements", [])):
            self._order[element.get("path")] = index

        for path in golden_elements:
            if path not in actual_elements:
                self.compare_structure(golden_elements[path].get("path", path), "element.present", True, False)
        for path in actual_elements:
            if path not in golden_elements:
                self.compare_structure(actual_elements[path].get("path", path), "element.present", False, True)
        for path in golden_elements:
            if path in actual_elements:
                self.compare_element(golden_elements[path], actual_elements[path],
                                     golden_elements, actual_elements)

        self.compare_length("document", "scroll.x", (golden.get("scroll") or [0, 0])[0],
                            (actual.get("scroll") or [0, 0])[0])
        self.compare_length("document", "scroll.y", (golden.get("scroll") or [0, 0])[1],
                            (actual.get("scroll") or [0, 0])[1])
        self._aggregate()
        self.l2_differences = sum(1 for entry in self.entries if entry["kind"] in ("structure", "length"))
        if self.mechanism:
            return self._result("mechanism")
        return self._result(self._classify())

    def _result(self, classification: str) -> dict:
        root_causes = sum(1 for entry in self.entries
                          if entry["kind"] == "length" and entry.get("consequences") is not None)
        return {
            "entries": self.entries,
            "warnings": self.warnings,
            "l1_differences": self.l1_differences,
            "l2_differences": self.l2_differences,
            "root_causes": root_causes,
            "mechanism": self.mechanism,
            "deferred": self.deferred,
            "classification": classification,
        }

    def _classify(self) -> str:
        if self.l1_differences > 0:
            return "input-misaligned"
        if self.l2_differences > 0:
            return "geometry"
        return "clean"


def compare_state(golden: dict, actual: dict, tolerance: float) -> dict:
    """Readback for one step. State-sequence readbacks are L3; input readbacks are L5.

    A `dispatch` step reports the state its injected input produced, which is L5 (TESTPLAN
    section 7). A readback that follows no input - a `scroll` step's clamp assertion, or the
    `readback` on the `eval` that carries the anchoring check (section 4) - is the state
    sequence itself, which is L3.
    """
    kind = "l3" if golden.get("op") in ("scroll", "eval") else "l5"
    entries: list[dict] = []

    def add(field: str, expected, actual_value, tol: float) -> None:
        equal, delta = values_equal(expected, actual_value, tol)
        if isinstance(expected, list) or isinstance(actual_value, list):
            equal = expected == actual_value
            delta = 0.0 if equal else float("inf")
        if not equal:
            entries.append({
                "element": golden.get("target") or "document",
                "field": field,
                "kind": kind,
                "expected": expected,
                "actual": actual_value,
                "tolerance": tol,
                "delta": None if delta == float("inf") else delta,
            })

    for index, axis in enumerate(("x", "y")):
        g = (golden.get("scroll") or [0, 0])[index]
        a = (actual.get("scroll") or [0, 0])[index]
        add(f"scroll.{axis}", g, a, tolerance)
    for index, axis in enumerate(("w", "h")):
        # clientWidth/clientHeight are integers on the browser side.
        g = round(float((golden.get("client_box") or [0, 0])[index]))
        a = round(float((actual.get("client_box") or [0, 0])[index]))
        add(f"client_box.{axis}", g, a, 0.5)
    add("target", golden.get("target"), actual.get("target"), 0)
    golden_scrollers = golden.get("scrollers") or []
    actual_scrollers = actual.get("scrollers") or []
    add("scrollers", [item.get("path") for item in golden_scrollers],
        [item.get("path") for item in actual_scrollers], 0)
    target = golden.get("target")
    for index, (expected, found) in enumerate(zip(golden_scrollers, actual_scrollers)):
        # The scroll step's target is already compared as scroll.x/y.
        if expected.get("path") == target and found.get("path") == target:
            continue
        add(f"scrollers[{index}].x", expected.get("x", 0), found.get("x", 0), tolerance)
        add(f"scrollers[{index}].y", expected.get("y", 0), found.get("y", 0), tolerance)
    add("hit", golden.get("hit"), actual.get("hit"), 0)
    add("focus", golden.get("focus"), actual.get("focus"), 0)
    add("hits", golden.get("hits") or [], actual.get("hits") or [], 0)
    add("checked", golden.get("checked") or [], actual.get("checked") or [], 0)
    return {"entries": entries, "kind": kind, "differences": len(entries)}
