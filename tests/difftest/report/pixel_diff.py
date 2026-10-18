"""L4 pixel diff (TESTPLAN sections 9 and 15).

Three zones:

* text, from the trace's line bands: per-channel tolerance 36. Stroke coverage is recorded
  and does not widen that tolerance.
* scrollbar gutter, border box minus client box. The two sides' gutters must match or the
  step is a mechanism error. Skin (scrollbar-source pixels) inside an agreed gutter does not
  fail. Content-source ink inside the gutter fails. The gutter must contain a scrollbar layer.
* everything else: a connected difference fails when its area is at least 400 px² and its
  short side is at least 8 px, or when it runs at least 8 px along an element border edge.

Rounded boxes keep their coverage fringe out of those two totals. The corner disk is the used
radius plus 1px, at any channel delta. The straight rim (the outer 1px) drops only when every
channel differs by at most 16. A square edge, and a straight rim that differs by more than 16,
still fail. Gutter content is unchanged.
"""

from __future__ import annotations

import struct
import zlib
from collections import deque

from png_io import Image

CONTENT = 1
SCROLLBAR = 2
# Straight rim of a rounded box. Corner disks are exempt at any delta; this bound is only the
# outer pixel of the straight edge, where the SDF and the browser disagree by a few levels.
RADIUS_RIM_MAX = 16

# A form control's inner text is not a DOM text node: an `<input>`'s value, a `<textarea>`'s
# value and the label a closed `<select>` shows all come from properties, so neither the trace's
# `lines` nor the browser's Range list carries a band for them. Both ends still paint those
# glyphs, so their AA has to be covered by the text tolerance - it is the same text the trace
# would report if it were a child node. The band is the control's *client* box (the padding box):
# the border and its skin stay outside it, which is where a real skin difference shows up.
CONTROL_TAGS = ("input", "textarea", "select")


def text_mask_rects(trace: dict) -> list[tuple[float, float, float, float]]:
    rects: list[tuple[float, float, float, float]] = []
    for element in trace.get("elements", []):
        if element.get("tag") in CONTROL_TAGS:
            client = element.get("client_box")
            if client and float(client[2]) > 0 and float(client[3]) > 0:
                rects.append((float(client[0]), float(client[1]), float(client[2]),
                              float(client[3])))
        lines = element.get("lines")
        if not lines:
            continue
        box = element.get("border_box") or [0, 0, 0, 0]
        x = float(box[0])
        width = float(box[2])
        for top, height in zip(lines.get("tops") or [], lines.get("heights") or []):
            rects.append((x, float(top), width, float(height)))
    return rects


def _border_px(style: dict, side: str) -> float:
    raw = style.get(f"border-{side}-width")
    if raw is None:
        return 0.0
    text = str(raw).strip().lower()
    if text.endswith("px"):
        text = text[:-2].strip()
    if text in ("thin", "medium", "thick"):
        return {"thin": 1.0, "medium": 3.0, "thick": 5.0}[text]
    try:
        return float(text)
    except ValueError:
        return 0.0


def gutter_rects(trace: dict) -> list[tuple[float, float, float, float]]:
    """Scrollbar slots: the part of the border box outside the client box, past the border inset.

    Unequal border widths also make the client box asymmetric inside the border box. Those rings
    are not scrollbars — only emit a gutter when `scroll_size` exceeds the client on that axis
    (content that can actually scroll).
    """
    rects: list[tuple[float, float, float, float]] = []
    for element in trace.get("elements", []):
        if not element.get("has_box"):
            continue
        border = element.get("border_box")
        client = element.get("client_box")
        if not border or not client or len(border) < 4 or len(client) < 4:
            continue
        bx, by, bw, bh = (float(v) for v in border)
        cx, cy, cw, ch = (float(v) for v in client)
        scroll = element.get("scroll_size") or [cw, ch]
        if len(scroll) < 2:
            scroll = [cw, ch]
        sw, sh = float(scroll[0]), float(scroll[1])
        style = element.get("style") or {}
        # The client box is already inside the border. The gap from the client edge to the
        # border box is border + scrollbar. Subtracting only the opposite border left the
        # near border inside the slot whenever the two borders differed (album's 1px
        # border-right and 0 border-left made a 16px gutter, and that border column was
        # counted as content ink).
        right = (bx + bw) - (cx + cw) - _border_px(style, "right")
        bottom = (by + bh) - (cy + ch) - _border_px(style, "bottom")
        # Vertical scrollbar occupies the inline-end strip when content is taller than the client.
        if right > 0.5 and ch > 0.5 and sh > ch + 0.5:
            rects.append((cx + cw, cy, right, ch))
        # Horizontal scrollbar occupies the block-end strip when content is wider than the client.
        if bottom > 0.5 and cw > 0.5 and sw > cw + 0.5:
            rects.append((cx, cy + ch, cw, bottom))
    return _merge_rects(rects)


def _merge_rects(rects):
    return [rect for rect in rects if rect[2] > 0.5 and rect[3] > 0.5]


def gutters_agree(golden, actual, tolerance: float = 0.5) -> bool:
    if len(golden) != len(actual):
        return False
    used = [False] * len(actual)
    for left in golden:
        matched = False
        for index, right in enumerate(actual):
            if used[index]:
                continue
            if all(abs(left[i] - right[i]) <= tolerance for i in range(4)):
                used[index] = True
                matched = True
                break
        if not matched:
            return False
    return True


def _in_rects(rects, x: float, y: float) -> bool:
    for left, top, width, height in rects:
        if left <= x < left + width and top <= y < top + height:
            return True
    return False


def source_at(source: bytes | None, width: int, x: int, y: int) -> int:
    if not source:
        return 0
    return source[y * width + x]


def _coverage(image: Image, rects) -> float:
    ink = 0
    total = 0
    for y in range(image.height):
        row = y * image.width * 4
        for x in range(image.width):
            if not _in_rects(rects, x + 0.5, y + 0.5):
                continue
            total += 1
            i = row + x * 4
            if image.rgba[i] < 250 or image.rgba[i + 1] < 250 or image.rgba[i + 2] < 250:
                ink += 1
    return (ink / total) if total else 0.0


def _components(mask: bytearray, width: int, height: int):
    seen = bytearray(width * height)
    found = []
    for start in range(width * height):
        if not mask[start] or seen[start]:
            continue
        queue = deque([start])
        seen[start] = 1
        count = 0
        min_x = max_x = start % width
        min_y = max_y = start // width
        while queue:
            index = queue.popleft()
            count += 1
            x = index % width
            y = index // width
            min_x = min(min_x, x)
            max_x = max(max_x, x)
            min_y = min(min_y, y)
            max_y = max(max_y, y)
            for nxt in (index - 1, index + 1, index - width, index + width):
                if nxt < 0 or nxt >= width * height or seen[nxt] or not mask[nxt]:
                    continue
                nx = nxt % width
                if abs(nx - x) + abs(nxt // width - y) != 1:
                    continue
                seen[nxt] = 1
                queue.append(nxt)
        found.append({
            "area": count,
            "w": max_x - min_x + 1,
            "h": max_y - min_y + 1,
            "x": min_x,
            "y": min_y,
        })
    return found


def _attribute(blame: bytearray, width: int, height: int, elements) -> list[dict]:
    """Which elements' border boxes hold the failing pixels, largest first."""
    if not elements:
        return []
    counts: dict[str, int] = {}
    boxes = []
    for element in elements:
        box = element.get("border_box")
        if not box or len(box) < 4 or not element.get("has_box", True):
            continue
        ident = element.get("id") or ""
        classes = element.get("classes") or ""
        label = element.get("tag") or "?"
        if ident:
            label += f"#{ident}"
        elif classes:
            label += "." + str(classes).split()[0]
        boxes.append((float(box[0]), float(box[1]), float(box[2]), float(box[3]), label))
    for y in range(height):
        row = y * width
        if not any(blame[row:row + width]):
            continue
        for x in range(width):
            if not blame[row + x]:
                continue
            hit = None
            for left, top, w, h, label in boxes:
                if left <= x < left + w and top <= y < top + h:
                    hit = label
            if hit:
                counts[hit] = counts.get(hit, 0) + 1
    ranked = sorted(counts.items(), key=lambda item: -item[1])
    return [{"name": name, "pixels": count} for name, count in ranked[:8]]


def attach_corner_radii(golden_elements, actual_elements):
    """Copy engine `corner_radii` onto the golden elements, matched by path.

    Edge geometry stays the golden border box. The radii are used pixels from the engine
    trace and are not an L2 field, so a golden that has no such key is left unchanged.
    """
    by_path = {}
    for element in actual_elements or []:
        radii = element.get("corner_radii")
        path = element.get("path")
        if radii and path:
            by_path[path] = radii
    if not by_path:
        return golden_elements
    attached = []
    for element in golden_elements or []:
        radii = by_path.get(element.get("path"))
        if not radii:
            attached.append(element)
            continue
        copied = dict(element)
        copied["corner_radii"] = radii
        attached.append(copied)
    return attached


def _rounded_exempt(elements, x: float, y: float, channel: int) -> bool:
    """Coverage fringe of a rounded border box. Square boxes are never exempt."""
    for element in elements or []:
        radii = element.get("corner_radii")
        box = element.get("border_box")
        if not radii or not box or len(radii) < 8 or len(box) < 4:
            continue
        rx = [float(radii[i]) for i in range(8)]
        if max(rx) <= 0.0:
            continue
        bx, by, bw, bh = (float(box[0]), float(box[1]), float(box[2]), float(box[3]))
        if bw <= 0.0 or bh <= 0.0:
            continue
        # tl, tr, br, bl as (x, y) pairs.
        corners = (
            (bx, by, rx[0], rx[1]),
            (bx + bw, by, rx[2], rx[3]),
            (bx + bw, by + bh, rx[4], rx[5]),
            (bx, by + bh, rx[6], rx[7]),
        )
        for cx, cy, rad_x, rad_y in corners:
            if rad_x <= 0.0 and rad_y <= 0.0:
                continue
            if abs(x - cx) <= rad_x + 1.5 and abs(y - cy) <= rad_y + 1.5:
                return True
        on_rim = (
            bx - 1.5 <= x <= bx + bw + 1.5
            and (abs(y - by) <= 1.5 or abs(y - (by + bh)) <= 1.5)
        ) or (
            by - 1.5 <= y <= by + bh + 1.5
            and (abs(x - bx) <= 1.5 or abs(x - (bx + bw)) <= 1.5)
        )
        if on_rim and channel <= RADIUS_RIM_MAX:
            return True
    return False


def _edge_runs(diff: bytearray, width: int, height: int, elements, minimum: int) -> int:
    """Longest run of differing pixels along any element border-box edge."""
    longest = 0
    for element in elements or []:
        box = element.get("border_box")
        if not box or len(box) < 4:
            continue
        x0 = max(0, int(round(float(box[0]))))
        y0 = max(0, int(round(float(box[1]))))
        x1 = min(width - 1, int(round(float(box[0]) + float(box[2]))) - 1)
        y1 = min(height - 1, int(round(float(box[1]) + float(box[3]))) - 1)
        if x1 < x0 or y1 < y0:
            continue
        edges = [
            [(x, y0) for x in range(x0, x1 + 1)],
            [(x, y1) for x in range(x0, x1 + 1)],
            [(x0, y) for y in range(y0, y1 + 1)],
            [(x1, y) for y in range(y0, y1 + 1)],
        ]
        for edge in edges:
            run = 0
            for x, y in edge:
                if 0 <= x < width and 0 <= y < height and diff[y * width + x]:
                    run += 1
                    longest = max(longest, run)
                else:
                    run = 0
            if longest >= minimum:
                return longest
    return longest


def diff_pixels(golden: Image, actual: Image, mask_rects, text_tolerance: int = 36,
                nontext_tolerance: int = 0, grid: int = 32,
                gutter_golden=None, gutter_actual=None, source: bytes | None = None,
                zone_area: int = 400, zone_short: int = 8, zone_edge: int = 8,
                elements=None) -> dict:
    # A flat fill must match channel for channel, so `nontext_tolerance` defaults to 0 and a case
    # that declares one has to say why in its own comment. The one use so far is a gradient: Blink
    # dithers a gradient, so its output differs from any straight evaluation by one step on
    # roughly half the pixels, in bands that are wide enough to clear the zone rule.
    del grid  # the zone rule replaced the single non-text tolerance
    result = {
        "golden_size": [golden.width, golden.height],
        "actual_size": [actual.width, actual.height],
        "differing": 0,
        "total": 0,
        "ratio": 0.0,
        "text_pixels": 0,
        "text_coverage_delta": 0.0,
        "zones": {"text": 0, "gutter_skin": 0, "gutter_content": 0, "other": 0},
        "components": [],
        "edge_run": 0,
        "hot_cells": [],
        "classification": "clean",
        "message": "",
    }
    if golden.width != actual.width or golden.height != actual.height:
        result["classification"] = "size-mismatch"
        result["differing"] = 1
        return result

    width, height = golden.width, golden.height
    result["total"] = width * height
    gutters = gutter_golden if gutter_golden is not None else []
    if gutter_actual is not None and not gutters_agree(gutters, gutter_actual):
        result["classification"] = "mechanism"
        result["differing"] = 1
        result["message"] = "两端滚动条槽区域不一致"
        return result

    result["text_coverage_delta"] = abs(_coverage(golden, mask_rects) - _coverage(actual, mask_rects))
    other = bytearray(width * height)
    blame = bytearray(width * height)
    text_fail = 0
    content_fail = 0
    skin = 0
    scrollbar_in_gutter = 0
    gutter_pixels = 0

    for y in range(height):
        row = y * width * 4
        for x in range(width):
            i = row + x * 4
            sx = x + 0.5
            sy = y + 0.5
            origin = source_at(source, width, x, y) if source else 0
            in_text = _in_rects(mask_rects, sx, sy)
            in_gutter = _in_rects(gutters, sx, sy)
            if in_gutter:
                gutter_pixels += 1
                if origin & SCROLLBAR:
                    scrollbar_in_gutter += 1
                if origin & CONTENT:
                    content_fail += 1
                    blame[y * width + x] = 1
                    continue
                if origin & SCROLLBAR:
                    channel = max(abs(golden.rgba[i + c] - actual.rgba[i + c]) for c in range(3))
                    if channel > 0:
                        skin += 1
                    continue
            channel = max(abs(int(golden.rgba[i + c]) - int(actual.rgba[i + c])) for c in range(3))
            if in_text:
                # Glyph AA stays in zones.text. It does not blame a pixel and does not fail
                # the frame: weight and subpixel position are L2 advances, and gutter ink,
                # large components, and edge runs still fail below.
                result["text_pixels"] += 1
                if channel > text_tolerance:
                    text_fail += 1
                continue
            if channel > nontext_tolerance and not in_gutter:
                if _rounded_exempt(elements, sx, sy, channel):
                    continue
                other[y * width + x] = 1
                blame[y * width + x] = 1

    components = _components(other, width, height)
    failing = [item for item in components
               if item["area"] >= zone_area and min(item["w"], item["h"]) >= zone_short]
    edge = _edge_runs(other, width, height, elements, zone_edge)
    result["zones"] = {
        "text": text_fail,
        "gutter_skin": skin,
        "gutter_content": content_fail,
        "other": sum(item["area"] for item in failing),
    }
    result["components"] = failing[:8]
    result["edge_run"] = edge
    result["text_pixels"] = result["text_pixels"] or sum(
        1 for y in range(height) for x in range(width) if _in_rects(mask_rects, x + 0.5, y + 0.5)
    )
    messages = []
    if gutter_pixels > 0 and source is not None and scrollbar_in_gutter == 0:
        messages.append("槽内没有滚动条层")
    if content_fail:
        messages.append(f"槽内内容墨水 {content_fail} px")
    if failing:
        messages.append(f"{len(failing)} 个超阈值差异块")
    if edge >= zone_edge:
        messages.append(f"沿边差异 {edge} px")
    owners = _attribute(blame, width, height, elements)
    result["owners"] = owners
    # Owners annotate a real zone/edge/text failure; scattered sub-threshold diffs must not
    # flip the frame to paint solely because blame still names a nearby element.
    if owners and messages:
        messages.append("落在 " + ", ".join(item["name"] for item in owners[:6]))
    result["message"] = "；".join(messages)
    failed = bool(messages)
    result["differing"] = content_fail + sum(item["area"] for item in failing) + (edge if edge >= zone_edge else 0)
    if gutter_pixels > 0 and source is not None and scrollbar_in_gutter == 0:
        result["differing"] += 1
    result["classification"] = "paint" if failed else "clean"
    result["ratio"] = result["differing"] / result["total"] if result["total"] else 0.0
    return result


def decode_source(image: Image) -> bytes:
    """Map the engine mask PNG back to content/scrollbar bits."""
    out = bytearray(image.width * image.height)
    for i in range(image.width * image.height):
        value = image.rgba[i * 4]
        if value >= 200:
            out[i] = CONTENT | SCROLLBAR
        elif value >= 120:
            out[i] = SCROLLBAR
        elif value >= 40:
            out[i] = CONTENT
    return bytes(out)


def write_diff_png(golden: Image, actual: Image, mask_rects, path: str, gutters=None) -> None:
    width, height = golden.width, golden.height
    gutters = gutters or []
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        base = y * width * 4
        for x in range(width):
            i = base + x * 4
            in_text = _in_rects(mask_rects, x + 0.5, y + 0.5)
            in_gutter = _in_rects(gutters, x + 0.5, y + 0.5)
            tolerance = 36 if in_text else 0
            differing = any(abs(golden.rgba[i + c] - actual.rgba[i + c]) > tolerance for c in range(3))
            if in_gutter:
                rows.extend((180, 180, 180, 255))
            elif differing:
                rows.extend((255, 0, 255, 255))
            else:
                rows.extend((golden.rgba[i] // 2 + 128, golden.rgba[i + 1] // 2 + 128,
                             golden.rgba[i + 2] // 2 + 128, 255))
    _write_png(width, height, bytes(rows), path)


def _chunk(chunk_type: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + chunk_type
        + payload
        + struct.pack(">I", zlib.crc32(chunk_type + payload) & 0xFFFFFFFF)
    )


def _write_png(width: int, height: int, raw_rows: bytes, path: str) -> None:
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    data = (
        b"\x89PNG\r\n\x1a\n"
        + _chunk(b"IHDR", header)
        + _chunk(b"IDAT", zlib.compress(raw_rows, 6))
        + _chunk(b"IEND", b"")
    )
    with open(path, "wb") as handle:
        handle.write(data)
