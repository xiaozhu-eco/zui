"""PNG decoding for the L4 layer.

Only the stdlib is used (`zlib`), so the harness has no image dependency. Supports the 8-bit
non-interlaced colour types browser screenshots produce: 0 (grey),
2 (RGB), 4 (grey+alpha), 6 (RGBA).
"""

from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass

CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}


@dataclass
class Image:
    width: int
    height: int
    rgba: bytearray

    def pixel(self, x: int, y: int) -> tuple[int, int, int, int]:
        i = (y * self.width + x) * 4
        return (self.rgba[i], self.rgba[i + 1], self.rgba[i + 2], self.rgba[i + 3])


def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def decode_png(data: bytes) -> Image:
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    offset = 8
    width = height = 0
    bit_depth = 8
    color_type = 6
    interlace = 0
    idat = bytearray()
    palette = b""
    while offset + 8 <= len(data):
        length = struct.unpack(">I", data[offset : offset + 4])[0]
        chunk_type = data[offset + 4 : offset + 8]
        payload = data[offset + 8 : offset + 8 + length]
        offset += 12 + length
        if chunk_type == b"IHDR":
            width, height, bit_depth, color_type, _, _, interlace = struct.unpack(
                ">IIBBBBB", payload
            )
        elif chunk_type == b"IDAT":
            idat.extend(payload)
        elif chunk_type == b"PLTE":
            palette = payload
        elif chunk_type == b"IEND":
            break
    if bit_depth != 8:
        raise ValueError(f"unsupported bit depth {bit_depth}")
    if interlace != 0:
        raise ValueError("interlaced PNG is not supported")
    if color_type not in CHANNELS:
        raise ValueError(f"unsupported colour type {color_type}")

    channels = CHANNELS[color_type]
    stride = width * channels
    raw = zlib.decompress(bytes(idat))
    expected = (stride + 1) * height
    if len(raw) < expected:
        raise ValueError(f"truncated PNG data: {len(raw)} < {expected}")

    out = bytearray()
    previous = bytearray(stride)
    pos = 0
    for _ in range(height):
        filter_type = raw[pos]
        pos += 1
        line = bytearray(raw[pos : pos + stride])
        pos += stride
        if filter_type == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif filter_type == 2:
            for i in range(stride):
                line[i] = (line[i] + previous[i]) & 0xFF
        elif filter_type == 3:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((left + previous[i]) >> 1)) & 0xFF
        elif filter_type == 4:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                up_left = previous[i - channels] if i >= channels else 0
                line[i] = (line[i] + _paeth(left, previous[i], up_left)) & 0xFF
        elif filter_type != 0:
            raise ValueError(f"unknown row filter {filter_type}")
        previous = line
        if color_type == 6:
            out.extend(line)
        elif color_type == 2:
            for i in range(0, stride, 3):
                out.extend((line[i], line[i + 1], line[i + 2], 255))
        elif color_type == 4:
            for i in range(0, stride, 2):
                out.extend((line[i], line[i], line[i], line[i + 1]))
        elif color_type == 0:
            for i in range(stride):
                out.extend((line[i], line[i], line[i], 255))
        else:  # palette
            for i in range(stride):
                index = line[i] * 3
                out.extend((palette[index], palette[index + 1], palette[index + 2], 255))
    return Image(width=width, height=height, rgba=out)
