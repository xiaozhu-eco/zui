"""Verdict self-check (TESTPLAN section 12).

Synthetic traces and frames, not the example pages. A box 21 px too tall plus ten
following siblings is one root cause. A gutter that only differs in scrollbar skin passes.
Content ink in that gutter fails. A full-page colour difference fails.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from pixel_diff import CONTENT, SCROLLBAR, diff_pixels  # noqa: E402
from png_io import Image  # noqa: E402
from trace_diff import TraceDiffer  # noqa: E402


def _element(path: str, x: float, y: float, w: float, h: float) -> dict:
    return {
        "path": path,
        "tag": "div",
        "id": "",
        "classes": "",
        "has_box": True,
        "transformed": False,
        "fragmented": False,
        "out_of_flow": False,
        "border_box": [x, y, w, h],
        "client_box": [x, y, w, h],
        "client_rects": [[x, y, w, h]],
        "scroll_rects": [],
        "scroll_offset": [0, 0],
        "scroll_size": [w, h],
        "style": {},
        "style_digest": "0",
    }


def _trace(elements: list[dict]) -> dict:
    return {
        "schema": "zui.difftest.trace/2",
        "scroll": [0, 0],
        "elements": elements,
    }


def _image(width: int, height: int, rgba: tuple[int, int, int, int]) -> Image:
    raw = bytearray()
    for _ in range(width * height):
        raw.extend(rgba)
    return Image(width, height, raw)


def _fill(image: Image, rect, rgba) -> None:
    x0, y0, w, h = rect
    for y in range(y0, y0 + h):
        for x in range(x0, x0 + w):
            i = (y * image.width + x) * 4
            image.rgba[i:i + 4] = bytes(rgba)


def check_root_cause() -> str | None:
    golden = [_element("0", 0, 0, 200, 500), _element("0/0", 0, 0, 100, 100)]
    actual = [_element("0", 0, 0, 200, 500), _element("0/0", 0, 0, 100, 121)]
    for index in range(10):
        golden.append(_element(f"0/{index + 1}", 0, 100 + index * 20, 100, 20))
        actual.append(_element(f"0/{index + 1}", 0, 121 + index * 20, 100, 20))
    result = TraceDiffer().compare(_trace(golden), _trace(actual))
    causes = [entry for entry in result["entries"]
              if entry["kind"] == "length" and "consequences" in entry]
    if result["classification"] == "mechanism":
        return "root-cause fixture was rejected as a mechanism error"
    if len(causes) != 1:
        fields = [(entry["field"], entry.get("consequences")) for entry in causes]
        return f"expected 1 root cause, got {len(causes)}: {fields}"
    if causes[0]["field"] != "h":
        return f"root cause field is {causes[0]['field']}, expected h"
    return None


def check_skin() -> str | None:
    golden = _image(30, 20, (255, 255, 255, 255))
    actual = _image(30, 20, (255, 255, 255, 255))
    _fill(actual, (20, 0, 10, 20), (40, 40, 40, 255))
    _fill(golden, (20, 0, 10, 20), (200, 200, 200, 255))
    gutter = [(20, 0, 10, 20)]
    source = bytearray(30 * 20)
    for y in range(20):
        for x in range(20, 30):
            source[y * 30 + x] = SCROLLBAR
    pixel = diff_pixels(golden, actual, [], gutter_golden=gutter, gutter_actual=gutter,
                        source=bytes(source), elements=[])
    if pixel["classification"] != "clean":
        return f"scrollbar skin should pass, got {pixel['classification']}: {pixel['message']}"
    return None


def check_content_ink() -> str | None:
    golden = _image(30, 20, (255, 255, 255, 255))
    actual = _image(30, 20, (255, 255, 255, 255))
    gutter = [(20, 0, 10, 20)]
    source = bytearray(30 * 20)
    for y in range(20):
        for x in range(20, 30):
            source[y * 30 + x] = CONTENT
    pixel = diff_pixels(golden, actual, [], gutter_golden=gutter, gutter_actual=gutter,
                        source=bytes(source), elements=[])
    if pixel["classification"] == "clean":
        return "content ink in the gutter should fail"
    return None


def _rounded(x, y, w, h, radius) -> dict:
    element = _element("0/0", x, y, w, h)
    element["corner_radii"] = [radius] * 8
    return element


def check_radius_corner() -> str | None:
    golden = _image(48, 48, (255, 255, 255, 255))
    actual = _image(48, 48, (255, 255, 255, 255))
    _fill(actual, (0, 0, 10, 10), (0, 0, 0, 255))
    pixel = diff_pixels(golden, actual, [], elements=[_rounded(0, 0, 40, 40, 12)])
    if pixel["classification"] != "clean":
        return f"rounded corner fringe should pass, got {pixel['message']}"
    return None


def check_radius_rim() -> str | None:
    golden = _image(48, 48, (20, 24, 32, 255))
    actual = _image(48, 48, (20, 24, 32, 255))
    # Straight bottom edge, away from the 12px corners, a few levels of coverage.
    for x in range(16, 28):
        i = (39 * actual.width + x) * 4
        actual.rgba[i:i + 4] = bytes((25, 30, 40, 255))
    pixel = diff_pixels(golden, actual, [], elements=[_rounded(0, 0, 40, 40, 12)])
    if pixel["classification"] != "clean":
        return f"rounded straight rim within 16 should pass, got {pixel['message']}"
    return None


def check_radius_solid_rim() -> str | None:
    golden = _image(80, 40, (255, 255, 255, 255))
    actual = _image(80, 40, (255, 255, 255, 255))
    _fill(actual, (20, 0, 24, 1), (0, 0, 200, 255))
    pixel = diff_pixels(golden, actual, [], elements=[_rounded(0, 0, 80, 40, 8)])
    if pixel["classification"] == "clean":
        return "a solid run on a rounded straight edge should fail"
    return None


def check_square_edge() -> str | None:
    golden = _image(40, 30, (255, 255, 255, 255))
    actual = _image(40, 30, (255, 255, 255, 255))
    _fill(actual, (4, 2, 20, 1), (0, 0, 200, 255))
    pixel = diff_pixels(golden, actual, [], elements=[_element("0/0", 2, 2, 30, 20)])
    if pixel["classification"] == "clean":
        return "a square edge run should fail"
    return None


def check_solid() -> str | None:
    golden = _image(40, 40, (255, 255, 255, 255))
    actual = _image(40, 40, (0, 0, 200, 255))
    pixel = diff_pixels(golden, actual, [], gutter_golden=[], gutter_actual=[], source=None,
                        elements=[])
    if pixel["classification"] == "clean":
        return "a full-page colour difference should fail"
    return None


def main() -> int:
    failures = []
    for name, fn in (
        ("root-cause", check_root_cause),
        ("gutter-skin", check_skin),
        ("gutter-content", check_content_ink),
        ("radius-corner", check_radius_corner),
        ("radius-rim", check_radius_rim),
        ("radius-solid-rim", check_radius_solid_rim),
        ("square-edge", check_square_edge),
        ("solid", check_solid),
    ):
        message = fn()
        if message:
            failures.append(f"{name}: {message}")
            print(f"FAIL {name}: {message}")
        else:
            print(f"ok   {name}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
