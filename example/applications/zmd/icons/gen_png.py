"""Draw Lucide-like toolbar glyphs as PNG. Native SVG decode needs compute shaders."""
from __future__ import annotations

import math
import os
import struct
import zlib

W = 32
STROKE = (100, 103, 111, 255)
FILL = STROKE
SW = 2.2


def png(w: int, h: int, rgba: bytearray) -> bytes:
    raw = bytearray()
    row = w * 4
    for y in range(h):
        raw.append(0)
        raw.extend(rgba[y * row : (y + 1) * row])

    def chunk(tag: bytes, data: bytes) -> bytes:
        return (
            struct.pack(">I", len(data))
            + tag
            + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
        )

    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + chunk(b"IEND", b"")
    )


class Canvas:
    def __init__(self, n: int = W) -> None:
        self.n = n
        self.px = bytearray(n * n * 4)

    def _blend(self, x: int, y: int, a: float) -> None:
        if a <= 0 or x < 0 or y < 0 or x >= self.n or y >= self.n:
            return
        i = (y * self.n + x) * 4
        src_a = int(min(1.0, a) * 255)
        if src_a <= 0:
            return
        dr, dg, db, da = self.px[i : i + 4]
        out_a = src_a + da * (255 - src_a) // 255
        if out_a == 0:
            return
        inv = 255 - src_a
        self.px[i] = (STROKE[0] * src_a + dr * inv) // 255
        self.px[i + 1] = (STROKE[1] * src_a + dg * inv) // 255
        self.px[i + 2] = (STROKE[2] * src_a + db * inv) // 255
        self.px[i + 3] = out_a

    def stamp(self, x: float, y: float, cover: float) -> None:
        if cover <= 0:
            return
        x0 = int(math.floor(x))
        y0 = int(math.floor(y))
        for iy in (y0, y0 + 1):
            for ix in (x0, x0 + 1):
                wx = 1.0 - abs(x - ix)
                wy = 1.0 - abs(y - iy)
                if wx > 0 and wy > 0:
                    self._blend(ix, iy, cover * wx * wy)

    def disk(self, x: float, y: float, r: float, fill: bool = True) -> None:
        r2 = r + 1.2
        x0, x1 = int(x - r2), int(x + r2) + 1
        y0, y1 = int(y - r2), int(y + r2) + 1
        for iy in range(y0, y1 + 1):
            for ix in range(x0, x1 + 1):
                d = math.hypot(ix - x, iy - y)
                if fill:
                    self._blend(ix, iy, max(0.0, min(1.0, r + 0.6 - d)))
                else:
                    self._blend(ix, iy, max(0.0, min(1.0, 1.1 - abs(d - r))))

    def line(self, x0: float, y0: float, x1: float, y1: float, width: float = SW) -> None:
        dx, dy = x1 - x0, y1 - y0
        length = math.hypot(dx, dy) or 1.0
        steps = int(length * 3) + 1
        r = width * 0.5
        for i in range(steps + 1):
            t = i / steps
            self.disk(x0 + dx * t, y0 + dy * t, r, fill=True)

    def polyline(self, pts: list[tuple[float, float]], width: float = SW) -> None:
        for a, b in zip(pts, pts[1:]):
            self.line(*a, *b, width)

    def rect(self, x: float, y: float, w: float, h: float, width: float = SW) -> None:
        self.polyline([(x, y), (x + w, y), (x + w, y + h), (x, y + h), (x, y)], width)

    def roundrect(
        self, x: float, y: float, w: float, h: float, r: float, width: float = SW
    ) -> None:
        r = min(r, w * 0.5, h * 0.5)
        self.line(x + r, y, x + w - r, y, width)
        self.line(x + w, y + r, x + w, y + h - r, width)
        self.line(x + w - r, y + h, x + r, y + h, width)
        self.line(x, y + h - r, x, y + r, width)
        for cx, cy, a0, a1 in (
            (x + w - r, y + r, -0.5 * math.pi, 0),
            (x + w - r, y + h - r, 0, 0.5 * math.pi),
            (x + r, y + h - r, 0.5 * math.pi, math.pi),
            (x + r, y + r, math.pi, 1.5 * math.pi),
        ):
            steps = 10
            for i in range(steps + 1):
                t0 = a0 + (a1 - a0) * i / steps
                t1 = a0 + (a1 - a0) * min(i + 1, steps) / steps
                self.line(
                    cx + r * math.cos(t0),
                    cy + r * math.sin(t0),
                    cx + r * math.cos(t1),
                    cy + r * math.sin(t1),
                    width,
                )

    def save(self, path: str) -> None:
        with open(path, "wb") as f:
            f.write(png(self.n, self.n, self.px))


def icon(name: str, draw) -> None:
    c = Canvas()
    draw(c)
    c.save(os.path.join(os.path.dirname(__file__), name + ".png"))


def main() -> None:
    icon("h1", lambda c: (
        c.line(6, 8, 6, 24),
        c.line(14, 8, 14, 24),
        c.line(6, 16, 14, 16),
        c.line(20, 14, 20, 24),
        c.line(18, 14, 23, 14),
    ))
    icon("h2", lambda c: (
        c.line(5, 8, 5, 24),
        c.line(13, 8, 13, 24),
        c.line(5, 16, 13, 16),
        c.polyline([(17, 15), (21, 13), (24, 15), (17, 24), (25, 24)]),
    ))
    icon("quote", lambda c: (
        c.line(8, 10, 8, 18),
        c.line(8, 10, 12, 10),
        c.line(8, 18, 12, 22),
        c.line(18, 10, 18, 18),
        c.line(18, 10, 22, 10),
        c.line(18, 18, 22, 22),
    ))
    icon("list", lambda c: (
        c.disk(7, 9, 1.4),
        c.disk(7, 16, 1.4),
        c.disk(7, 23, 1.4),
        c.line(11, 9, 26, 9),
        c.line(11, 16, 26, 16),
        c.line(11, 23, 26, 23),
    ))
    icon("ol", lambda c: (
        c.line(6, 8, 6, 11),
        c.line(8, 8, 8, 11),
        c.line(6, 15, 8, 15),
        c.line(8, 15, 8, 18),
        c.line(6, 22, 8, 22),
        c.line(6, 22, 6, 25),
        c.line(6, 25, 8, 25),
        c.line(12, 9.5, 26, 9.5),
        c.line(12, 16.5, 26, 16.5),
        c.line(12, 23.5, 26, 23.5),
    ))
    icon("table", lambda c: (
        c.roundrect(6, 8, 20, 16, 2.5),
        c.line(6, 14, 26, 14),
        c.line(13, 14, 13, 24),
    ))
    icon("image", lambda c: (
        c.roundrect(6, 8, 20, 16, 2.5),
        c.disk(12, 13, 1.8),
        c.polyline([(8, 22), (14, 16), (18, 20), (21, 17), (26, 22)]),
    ))
    icon("minus", lambda c: c.line(8, 16, 24, 16))
    icon("bold", lambda c: (
        c.line(9, 8, 9, 24),
        c.polyline([(9, 8), (18, 8), (21, 11), (21, 14), (18, 16), (9, 16)]),
        c.polyline([(9, 16), (19, 16), (22, 19), (22, 22), (19, 24), (9, 24)]),
    ))
    icon("italic", lambda c: (
        c.line(14, 8, 22, 8),
        c.line(10, 24, 18, 24),
        c.line(18, 8, 14, 24),
    ))
    icon("code", lambda c: (
        c.polyline([(13, 8), (8, 16), (13, 24)]),
        c.polyline([(19, 8), (24, 16), (19, 24)]),
    ))
    icon("link", lambda c: (
        c.polyline([(13, 12), (11, 12), (8, 15), (8, 17), (11, 20), (13, 20)]),
        c.polyline([(19, 12), (21, 12), (24, 15), (24, 17), (21, 20), (19, 20)]),
        c.line(12, 16, 20, 16),
    ))
    icon("search", lambda c: (
        c.disk(14, 14, 7, fill=False),
        c.line(19, 19, 25, 25),
    ))
    icon("settings", lambda c: (
        c.disk(16, 16, 4, fill=False),
        *[
            c.line(
                16 + 7 * math.cos(a),
                16 + 7 * math.sin(a),
                16 + 11 * math.cos(a),
                16 + 11 * math.sin(a),
            )
            for a in [i * math.pi / 3 for i in range(6)]
        ],
    ))
    icon("align-left", lambda c: (
        c.line(7, 9, 25, 9),
        c.line(7, 14, 20, 14),
        c.line(7, 19, 25, 19),
        c.line(7, 24, 18, 24),
    ))
    icon("file-plus", lambda c: (
        c.polyline([(10, 7), (18, 7), (22, 11), (22, 25), (10, 25), (10, 7)]),
        c.line(18, 7, 18, 11),
        c.line(18, 11, 22, 11),
        c.line(13, 17, 19, 17),
        c.line(16, 14, 16, 20),
    ))
    icon("folder-plus", lambda c: (
        c.polyline([(6, 12), (6, 10), (13, 10), (15, 12), (26, 12), (26, 24), (6, 24), (6, 12)]),
        c.line(13, 18, 19, 18),
        c.line(16, 15, 16, 21),
    ))
    icon("pin", lambda c: (
        c.disk(16, 13, 5, fill=False),
        c.line(16, 18, 16, 26),
        c.line(12, 13, 20, 13),
    ))
    icon("arrow-up", lambda c: (
        c.polyline([(8, 16), (16, 8), (24, 16)]),
        c.line(16, 8, 16, 24),
    ))


if __name__ == "__main__":
    main()
