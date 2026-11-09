#!/usr/bin/env python3
"""Make the game's desktop icon on the player's PC, from their own disc.

The players' ZIP carries nothing from the game, so the icon is drawn by the
game itself: the freshly built exe is started hidden (headless, no window)
until its title screen (the game's logo over Bugs), the debug server takes
a screenshot, and a square around the logo becomes a Windows .ico (256, 48,
32 and 16 pixels, PNG inside). Run by tools/bbt_setup.py after a build;
a failure only means the shortcut keeps the default icon.

    python tools/bbt_icon.py --exe build-game/BugsBunnyTaz_Recompiled.exe
        --game build-game/game_3p.toml --disc disc/BugsBunnyTaz_TimeBusters_USA.cue
        --out BugsBunnyTaz.ico [--png icon.png]

Standard library only (the embeddable Python has no image library).
"""
from __future__ import annotations

import argparse
import json
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time
import zlib
from pathlib import Path

LEVEL_ID = 0x80010000          # the current level (tests/bbt/game.py)
LEVEL_TITLE = 0                # title / main menu level
SETTLE_FRAMES = 150            # after the title level is up: its fade-in
# The cut from the native 512 x 240 frame: the logo (x ~130..380, y ~5..110)
# on the red spiral, above the menu text (from y ~185); scaled for other
# frame sizes and padded to a square with transparent rows.
CROP_X0, CROP_Y0, CROP_W, CROP_H, REF_W, REF_H = 128, 0, 256, 180, 512, 240
SIZES = (256, 48, 32, 16)


# ---- debug server -----------------------------------------------------
class Debug:
    def __init__(self, port: int):
        self.port, self.id = port, 0

    def send(self, obj: dict, timeout: float = 30.0) -> dict:
        self.id += 1
        msg = (json.dumps(dict(obj, id=self.id)) + "\n").encode()
        with socket.create_connection(("127.0.0.1", self.port), timeout=timeout) as s:
            s.sendall(msg)
            buf = b""
            while True:
                chunk = s.recv(1 << 20)
                if not chunk:
                    break
                buf += chunk
                try:
                    return json.loads(buf.replace(b"\n", b""))
                except ValueError:
                    continue
        raise OSError(f"no reply to {obj.get('cmd')}")

    def ping(self) -> bool:
        try:
            self.send({"cmd": "ping"}, timeout=2.0)
            return True
        except OSError:
            return False

    def frame(self) -> int:
        return int(self.send({"cmd": "frame"}).get("frame", -1))

    def u32(self, addr: int) -> int:
        r = self.send({"cmd": "read_ram", "addr": f"0x{addr:08X}", "len": 4})
        return struct.unpack("<I", bytes.fromhex(r["hex"]))[0]


def free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


# ---- PNG in / out (8-bit RGB / RGBA, non-interlaced) ------------------
def png_read(path: Path) -> tuple[int, int, list[list[tuple[int, int, int, int]]]]:
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, idat, w = 8, b"", 0
    while pos < len(data):
        n = struct.unpack(">I", data[pos:pos + 4])[0]
        kind, body = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, inter = struct.unpack(">IIBBBBB", body)
            if depth != 8 or ctype not in (2, 6) or inter:
                raise ValueError(f"unsupported PNG (depth {depth}, type {ctype})")
            bpp = 3 if ctype == 2 else 4
        elif kind == b"IDAT":
            idat += body
    raw = zlib.decompress(idat)
    stride, rows, prev = w * bpp, [], bytearray(w * bpp)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        rows.append([(line[x * bpp], line[x * bpp + 1], line[x * bpp + 2],
                      line[x * bpp + 3] if bpp == 4 else 255) for x in range(w)])
    return w, h, rows


def png_bytes(rows: list[list[tuple[int, int, int, int]]]) -> bytes:
    h, w = len(rows), len(rows[0])
    raw = b"".join(b"\0" + bytes(v for px in row for v in px) for row in rows)

    def chunk(kind: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def scale(rows, size: int):
    """Box-filter resize of a square image to size x size."""
    n = len(rows)
    out = []
    for y in range(size):
        y0, y1 = y * n // size, max(y * n // size + 1, (y + 1) * n // size)
        line = []
        for x in range(size):
            x0, x1 = x * n // size, max(x * n // size + 1, (x + 1) * n // size)
            acc, k = [0, 0, 0, 0], 0
            for yy in range(y0, y1):
                for xx in range(x0, x1):
                    p = rows[yy][xx]
                    for i in range(4):
                        acc[i] += p[i]
                    k += 1
            line.append(tuple(v // k for v in acc))
        out.append(line)
    return out


def ico_bytes(images: list[tuple[int, bytes]]) -> bytes:
    """An .ico with PNG-compressed entries (Windows Vista and later)."""
    head = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    dirs, blobs = b"", b""
    for size, png in images:
        dim = 0 if size >= 256 else size
        dirs += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(png), offset)
        blobs += png
        offset += len(png)
    return head + dirs + blobs


# ---- the run ----------------------------------------------------------
def capture(exe: Path, game: Path, disc: Path, shot: Path, timeout_s: float) -> None:
    port = free_port()
    cards = Path(tempfile.mkdtemp(prefix="bbt-icon-cards-"))
    cmd = [str(exe), "--game", str(game), "--disc", str(disc), "--headless",
           "--debug-port", str(port), "--memcard-dir", str(cards)]
    env = dict(os.environ, PSX_DEBUG_SERVER="1")
    flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env,
                            creationflags=flags)
    dbg = Debug(port)
    try:
        end = time.monotonic() + timeout_s
        while not dbg.ping():
            if proc.poll() is not None or time.monotonic() > end:
                raise RuntimeError("the game did not start its debug server")
            time.sleep(0.3)
        # RAM is 0 at power-on, so the level reads "title" before the game
        # runs: wait until it has been another level (the logos / language
        # screen, which Quick start passes through as well) and then stays
        # on the title for SETTLE_FRAMES.
        up, seen_other = None, False
        while time.monotonic() < end:
            f, level = dbg.frame(), dbg.u32(LEVEL_ID)
            if level != LEVEL_TITLE:
                seen_other, up = True, None
            elif seen_other:
                up = f if up is None else up
                if f - up >= SETTLE_FRAMES:
                    break
            time.sleep(0.1)
        else:
            raise RuntimeError("the title screen did not come up in time")
        dbg.send({"cmd": "screenshot", "path": str(shot)}, timeout=60.0)
        if not shot.is_file():
            raise RuntimeError("no screenshot written")
    finally:
        try:
            dbg.send({"cmd": "quit"}, timeout=5.0)
        except OSError:
            pass
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", required=True)
    ap.add_argument("--game", required=True)
    ap.add_argument("--disc", required=True)
    ap.add_argument("--out", required=True, help="the .ico to write")
    ap.add_argument("--png", default="", help="also write the 256 px image as PNG")
    ap.add_argument("--timeout", type=float, default=300.0, help="seconds")
    a = ap.parse_args()
    shot = Path(tempfile.mkdtemp(prefix="bbt-icon-")) / "title.png"
    try:
        capture(Path(a.exe), Path(a.game).resolve(), Path(a.disc).resolve(), shot.resolve(), a.timeout)
        w, h, rows = png_read(shot)
        sx, sy = w / REF_W, h / REF_H
        x0, y0 = int(CROP_X0 * sx), int(CROP_Y0 * sy)
        cw, ch = min(int(CROP_W * sx), w - x0), min(int(CROP_H * sy), h - y0)
        cut = [row[x0:x0 + cw] for row in rows[y0:y0 + ch]]
        if sx != sy:                      # e.g. a 512 x 480 frame: square pixels again
            step = sy / sx
            cut = [cut[int(i * step)] for i in range(int(len(cut) / step))]
        side = max(len(cut), len(cut[0]))
        clear = (0, 0, 0, 0)
        top = (side - len(cut)) // 2
        square = ([[clear] * side for _ in range(top)] +
                  [row + [clear] * (side - len(row)) for row in cut] +
                  [[clear] * side for _ in range(side - len(cut) - top)])
        images = [(s, png_bytes(scale(square, s))) for s in SIZES]
        Path(a.out).write_bytes(ico_bytes(images))
        if a.png:
            Path(a.png).write_bytes(images[0][1])
        print(f"icon written: {a.out} (from a {w}x{h} title screen)")
        return 0
    except Exception as exc:  # noqa: BLE001 (any failure: keep the default icon)
        print(f"no game icon ({exc}); the shortcut keeps the default icon")
        return 1


if __name__ == "__main__":
    sys.exit(main())
