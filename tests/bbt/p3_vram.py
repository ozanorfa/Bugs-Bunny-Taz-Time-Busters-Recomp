"""Where the third actor's texels go: the strip and the tall area of
src/mods/p3_profile.h (P3_STRIP_*, P3_TALL_*; BUGS #25), read from the
header so the tests follow it."""
from __future__ import annotations

import re
from pathlib import Path

_HEADER = Path(__file__).resolve().parents[2] / "src" / "mods" / "p3_profile.h"
_DEFINES = {m[1]: int(m[2], 0) for m in
            re.finditer(r"#define\s+(\w+)\s+(0x[0-9A-Fa-f]+|\d+)\b", _HEADER.read_text(encoding="utf-8"))}

STRIP = tuple(_DEFINES[f"P3_STRIP_{k}"] for k in "XYWH")
TALL = tuple(_DEFINES[f"P3_TALL_{k}"] for k in "XYWH")


def inside(x: int, y: int, w: int, h: int, area: tuple[int, int, int, int]) -> bool:
    ax, ay, aw, ah = area
    return ax <= x and x + w <= ax + aw and ay <= y and y + h <= ay + ah
