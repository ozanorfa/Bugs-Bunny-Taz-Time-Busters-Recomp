"""Client for the PSXRecomp TCP debug server (psxrecomp/docs/TCP_COMMANDS.md):
one JSON object per line, one connection per command, localhost only."""
from __future__ import annotations

import json
import socket
import struct
import time

# PS1 pad word bits (active low: 0 = pressed). NOTES.md "Controller bit layout".
BUTTONS = {
    "select": 0, "l3": 1, "r3": 2, "start": 3, "up": 4, "right": 5, "down": 6, "left": 7,
    "l2": 8, "r2": 9, "l1": 10, "r1": 11, "triangle": 12, "circle": 13, "cross": 14, "square": 15,
}
RELEASED = 0xFFFF


def pad(*names: str) -> int:
    """Active-low pad word with the named buttons pressed."""
    v = RELEASED
    for n in names:
        v &= ~(1 << BUTTONS[n])
    return v


class DebugError(RuntimeError):
    pass


class DebugClient:
    def __init__(self, port: int, host: str = "127.0.0.1"):
        self.host, self.port = host, port
        self._id = 0

    # ---- transport ------------------------------------------------------
    def send(self, obj: dict, timeout: float = 60.0) -> dict:
        self._id += 1
        msg = (json.dumps(dict(obj, id=self._id)) + "\n").encode()
        with socket.create_connection((self.host, self.port), timeout=timeout) as s:
            s.sendall(msg)
            buf = bytearray()
            while not buf.endswith(b"\n"):
                chunk = s.recv(1 << 22)
                if not chunk:
                    break
                buf += chunk
        if not buf:
            raise DebugError(f"no reply to {obj.get('cmd')}")
        return json.loads(buf.split(b"\n", 1)[0])

    def ok(self, obj: dict, timeout: float = 60.0) -> dict:
        r = self.send(obj, timeout)
        if not r.get("ok", False):
            raise DebugError(f"{obj.get('cmd')} failed: {r}")
        return r

    def ping(self) -> bool:
        try:
            return bool(self.send({"cmd": "ping"}, timeout=2.0))
        except (OSError, ValueError, DebugError):
            return False

    # ---- time -----------------------------------------------------------
    def frame(self) -> int:
        return int(self.send({"cmd": "frame"})["frame"])

    def wait_frames(self, n: int, timeout_s: float | None = None) -> int:
        target = self.frame() + n
        deadline = time.monotonic() + (timeout_s if timeout_s else 30.0 + n / 20.0)
        while True:
            f = self.frame()
            if f >= target:
                return f
            if time.monotonic() > deadline:
                raise DebugError(f"frame counter stuck at {f} (waiting for {target})")
            time.sleep(0.01)

    def wait_until(self, pred, max_frames: int, poll_frames: int = 2):
        """Polls `pred()` until it returns a truthy value or `max_frames`
        game frames have passed. Returns the value (falsy on timeout)."""
        end = self.frame() + max_frames
        deadline = time.monotonic() + 30.0 + max_frames / 20.0
        while True:
            v = pred()
            if v:
                return v
            f = self.frame()
            if f >= end or time.monotonic() > deadline:
                return v
            self.wait_frames(poll_frames)

    # ---- input ----------------------------------------------------------
    def press(self, buttons: int, frames: int = 6, port: int = 1) -> None:
        """Press and release. Port 1 uses the server's timed press. Port 2
        is held and released with set_input instead: a timed press on port
        2 unplugs the virtual pad when it ends, and the game then pauses
        with "NO CONTROLLER IN CONTROLLER PORT 2"."""
        if port == 1:
            self.send({"cmd": "press", "buttons": buttons, "frames": frames})
            return
        self.hold(buttons, port)
        self.wait_frames(frames)
        self.release(port)

    def hold(self, buttons: int, port: int = 1) -> None:
        cmd = {"cmd": "set_input", "buttons": f"0x{buttons:04X}"}
        if port != 1:
            cmd.update(port=port, connected=1)
        self.send(cmd)

    def release(self, port: int = 1) -> None:
        self.hold(RELEASED, port)

    # ---- memory ---------------------------------------------------------
    def read(self, addr: int, n: int) -> bytes:
        r = self.ok({"cmd": "read_ram", "addr": f"0x{addr:08X}", "len": n})
        return bytes.fromhex(r["hex"])

    def u8(self, a: int) -> int:
        return self.read(a, 1)[0]

    def u16(self, a: int) -> int:
        return struct.unpack("<H", self.read(a, 2))[0]

    def s16(self, a: int) -> int:
        return struct.unpack("<h", self.read(a, 2))[0]

    def u32(self, a: int) -> int:
        return struct.unpack("<I", self.read(a, 4))[0]

    def write_u32(self, a: int, v: int) -> None:
        """Main RAM only (write_bulk refuses mod memory)."""
        self.ok({"cmd": "write_bulk", "addr": f"0x{a:08X}", "hex": struct.pack("<I", v).hex()})

    # ---- mods -----------------------------------------------------------
    def counters(self) -> dict[str, int]:
        return {c["name"]: int(c["count"]) for c in self.send({"cmd": "mod_counters"}).get("counters", [])}

    def sio_trace(self, count: int) -> dict:
        """The last `count` SIO bytes ({seq, tx, rx, func, ...}). The reply
        spans one line per entry, so it is read up to its closing ]}."""
        self._id += 1
        msg = (json.dumps({"cmd": "sio_trace", "count": count, "id": self._id}) + "\n").encode()
        with socket.create_connection((self.host, self.port), timeout=60) as s:
            s.sendall(msg)
            buf = bytearray()
            while not buf.rstrip().endswith(b"]}"):
                chunk = s.recv(1 << 22)
                if not chunk:
                    break
                buf += chunk
        return json.loads(bytes(buf).replace(b"\n", b""))

    def pad_commands(self, frames: int, step: int = 10) -> dict[int, int]:
        """The pad command bytes (the byte after each 0x01 address byte) the
        game sends over the next `frames` frames, read every `step` frames so
        no entry is lost: {command: count}."""
        out: dict[int, int] = {}
        last = self.sio_trace(1)["total"]
        for _ in range(max(1, frames // step)):
            self.wait_frames(step)
            now = self.sio_trace(1)["total"]
            ents = [e for e in self.sio_trace(now - last + 64)["entries"] if e["seq"] >= last]
            for x, y in zip(ents, ents[1:]):
                if x["tx"] == "0x01" and y["tx"] != "0x01":
                    k = int(y["tx"], 16)
                    out[k] = out.get(k, 0) + 1
            last = now
        return out

    def mod_cmd(self, name: str, args: str = "") -> dict:
        r = self.send({"cmd": "mod_cmd", "name": name, "args": args})
        if not r.get("ok"):
            raise DebugError(f"mod_cmd {name} failed: {r}")
        return json.loads(r["reply"])

    # ---- video ----------------------------------------------------------
    def screenshot(self, path) -> None:
        self.send({"cmd": "screenshot", "path": str(path)}, timeout=60.0)

    def vram(self, x: int, y: int, w: int, h: int) -> list[int]:
        hx = self.ok({"cmd": "vram_peek", "x": x, "y": y, "w": w, "h": h})["hex"]
        return [int(hx[i:i + 4], 16) for i in range(0, len(hx), 4)]

    def gpu_prims(self, min_count: int = 50) -> list[dict]:
        """GPU commands of the newest completely captured frame (the game
        draws at 30 Hz, so every other ring frame is empty)."""
        newest = self.send({"cmd": "gpu_ring_stats"}).get("newest_frame")
        if newest is None:
            raise DebugError("gpu_ring_stats has no newest_frame (debug tools off?)")
        for fr in range(newest - 1, newest - 10, -1):
            r = self.send({"cmd": "gpu_frame_dump", "frame": fr, "count": 20000}, timeout=120.0)
            if r.get("count", 0) >= min_count:
                return r["entries"]
        return []

    def quit(self) -> None:
        try:
            self.send({"cmd": "quit"}, timeout=5.0)
        except (OSError, ValueError, DebugError):
            pass


def fnv1a32_pixels(pixels) -> int:
    """FNV-1a 32 over 16-bit pixels as little-endian bytes (the plugin's
    bbt.trca01 command hashes uploaded pixels the same way)."""
    h = 2166136261
    for p in pixels:
        for b in (p & 0xFF, p >> 8):
            h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


# Textured polygon opcodes (GPU GP0 0x24-0x27, 0x2C-0x2F, 0x34-0x37, 0x3C-0x3F);
# their CLUT is the high half of word 2.
TEXTURED_POLY_OPS = set(range(0x24, 0x28)) | set(range(0x2C, 0x30)) | set(range(0x34, 0x38)) | set(range(0x3C, 0x40))


def prim_cluts(entries) -> dict[int, int]:
    """CLUT field -> number of textured polygons drawn with it."""
    out: dict[int, int] = {}
    for e in entries:
        op = int(e["op"], 16)
        if op in TEXTURED_POLY_OPS and len(e.get("w", [])) > 2:
            clut = int(e["w"][2], 16) >> 16
            out[clut] = out.get(clut, 0) + 1
    return out
