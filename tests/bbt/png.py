"""Row brightness of a PNG screenshot, with the standard library only (the
runtime writes 8-bit RGB or RGBA, non-interlaced). Slow in pure Python: keep
images small (a few megapixels at most)."""
from __future__ import annotations

import struct
import zlib
from pathlib import Path


def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def row_means(path: Path) -> list[float]:
    """Mean of R, G and B per row (0..255)."""
    data = Path(path).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos, idat, width = 8, b"", 0
    while pos < len(data):
        n, kind = struct.unpack_from(">I4s", data, pos)
        chunk = data[pos + 8: pos + 8 + n]
        if kind == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", chunk)
            if depth != 8 or color not in (2, 6) or interlace:
                raise ValueError(f"{path}: unsupported PNG (depth {depth}, color {color})")
            bpp = 3 if color == 2 else 4
        elif kind == b"IDAT":
            idat += chunk
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = width * bpp
    prev = bytearray(stride)
    out = []
    for y in range(height):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1: (y + 1) * (stride + 1)])
        if f == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif f == 3:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif f == 4:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                up_left = prev[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + _paeth(left, prev[i], up_left)) & 0xFF
        total = sum(line) - (sum(line[3::4]) if bpp == 4 else 0)
        out.append(total / (width * 3))
        prev = line
    return out


def dark_rows(means: list[float], lo: float, hi: float, limit: float = 3.0) -> list[float]:
    """Positions (0..1 of the height) of rows in [lo, hi) darker than limit."""
    h = len(means)
    return [y / h for y in range(int(lo * h), int(hi * h)) if means[y] < limit]
