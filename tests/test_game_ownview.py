"""Game tier: the engine's own two views (experiment, package bbt.own-view
feature "two-views", src/mods/own_view.c; NOTES.md "Getting 2 views to run")
in the 2-player build with debug tools (build-dev), with Quick start.

In 2-player Granwich the plugin raises the view count 0x8008DE00 to 2: the
game keeps running (no hang, no fail-fast), view 0 follows Bugs and view 1
Taz (0x80097660), each pad moves its own character, and every frame that
draws the world (more than WORLD_PRIMS primitives) sets the top-half and
the bottom-half draw area (GP0 E3, the 2-view table at 0x80082FBC /
0x80082FEC; with 2 views the game draws a frame every 2 VSyncs, and some
small frames, e.g. the pause loop's, draw view 0 only).

Each view has a single-player camera (camera record flags +0xE0: 0x80
"follow one" set, 0x100 "2-player framing" clear): Taz walks far from Bugs
without the camera director freezing him (stock 2-player: stuck about 3050
units away), each camera looks at its own player, and each pad steers along
its own camera's forward direction.

Full speed: the game logic advances once per frame, and a 2-view frame took
~3.6 vblanks of CPU (the game at ~55% speed). game.toml [timing] charges game
code half its cycles while the view count byte is 2, so with two views the
game still makes a frame every 2 vblanks (30 fps) while both run, as stock
1-view play does; actors' +0x82 frame counters advance once per frame.
Screenshots go to build-tests/out/ownview/."""
from __future__ import annotations

import json
import math
import socket
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from bbt import config  # noqa: E402
from bbt.client import pad  # noqa: E402
from bbt.game import LEVEL_GRANNY  # noqa: E402
from test_game_stock import moved, start_run  # noqa: E402

V = "bbt.own-view.two-views."
VIEW_COUNT, FOLLOWED = 0x8008DE00, 0x80097660
# E3 draw-area top-left words of the 2-view table (top and bottom half of
# each display buffer)
TWO_VIEW_TOPS = {0xE3004200, 0xE3020200, 0xE3044200, 0xE3060200}
BOTTOM_HALF = {0xE3020200, 0xE3060200}
WORLD_PRIMS = 600
VBLANKS = 0x8008DE20       # +1 per vblank (VSync callback)
VIEW_PASSES = 0x8008DE88   # +1 per view per frame (frame loop)
# camera record of view v (GsRVIEWUNIT viewpoint at +4, reference point at +0x10)
CAM_RECORD, CAM_RECORD_BYTES = 0x800964C4, 0x2C0
CAM_FOLLOW_ONE, CAM_FRAME_BOTH = 0x80, 0x100
FAR_APART = 6000      # stock 2-player freezes Taz at about 3050
MAX_TURN = 30         # degrees between the facing after "up" and the camera's forward


def camera(c, view: int) -> tuple[tuple[int, int, int], tuple[int, int, int], int]:
    """(viewpoint, reference point, flags) of a view's camera."""
    r = CAM_RECORD + view * CAM_RECORD_BYTES
    return (struct.unpack("<3i", c.read(r + 4, 12)), struct.unpack("<3i", c.read(r + 0x10, 12)),
            c.u32(r + 0xE0))


def flat(a, b) -> int:
    return round(math.hypot(a[0] - b[0], a[2] - b[2]))


def heading(dx: int, dz: int) -> float:
    return math.degrees(math.atan2(dx, dz)) % 360


def turn(a: float, b: float) -> float:
    return abs((a - b + 180) % 360 - 180)


def gpu_frame(c, frame: int) -> list:
    """gpu_frame_dump of one frame (raw socket: the reply can be large)."""
    s = socket.create_connection(("127.0.0.1", c.port), timeout=60)
    s.sendall((json.dumps({"cmd": "gpu_frame_dump", "frame": frame, "count": 65536}) + "\n").encode())
    buf = b""
    while not buf.endswith(b"\n"):
        d = s.recv(1 << 22)
        if not d:
            break
        buf += d
    s.close()
    return json.loads(buf.decode(errors="replace")).get("entries", [])


class TwoViews(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        start_run(cls, config.game_build(), "ownview",
                  {"bbt.quick-start/quick-start": True, "bbt.own-view/two-views": True})

    def need(self, stage: str) -> None:
        if stage not in self.reached:
            self.skipTest(f"an earlier stage ({stage}) failed")

    def test_1_granwich_runs_with_two_views(self):
        c, p = self.c, self.p
        self.assertTrue(c.wait_until(lambda: p.level() == 2, 3000, 2), "no language screen")
        c.wait_frames(200)
        self.assertTrue(p.language_to_title(), "no title")
        c.wait_frames(240); c.press(pad("start")); c.wait_frames(120)
        c.press(pad("cross")); c.wait_frames(60); c.release(port=2); c.wait_frames(30)
        c.press(pad("down")); c.wait_frames(30); c.press(pad("cross")); c.wait_frames(90)
        c.press(pad("cross"))
        self.assertTrue(c.wait_until(lambda: p.level() == LEVEL_GRANNY and p.players_ready(), 3000, 2))
        self.assertTrue(c.wait_until(lambda: c.counters().get(V + "views_set"), 600, 2),
                        "the view count was not raised")
        c.wait_frames(600)                                # runs on (a hang raises here)
        # the count can read 1 while the update walk runs (work-around 2)
        self.assertTrue(c.wait_until(lambda: c.u8(VIEW_COUNT) == 2, 60, 1), "the view count fell back")
        followed = struct.unpack("<2I", c.read(FOLLOWED, 8))
        self.assertEqual(followed, (p.actor(1), p.actor(2)), "views do not follow player 1 and 2")
        self.game.shot("1_two_views")
        self.reached.add("two views")

    def test_2_each_pad_moves_its_own_character(self):
        self.need("two views")
        c, p = self.c, self.p
        for _ in range(6):                                # close the dialogs
            c.press(pad("cross")); c.wait_frames(150)
        for port in (2, 1):
            a = p.actor(port)
            o = p.actor(3 - port)
            c.wait_frames(120)                            # both at rest
            m0, o0 = p.pos(a), p.pos(o)
            c.hold(pad("down" if port == 2 else "up"), port=port); c.wait_frames(150); c.release(port=port)
            c.wait_frames(20)
            with self.subTest(port=port):
                self.assertGreater(moved(m0, p.pos(a)), 64, f"port {port} did not move its character")
                self.assertLess(moved(o0, p.pos(o)), 64, f"port {port} moved the other character")
        self.game.shot("2_moved")

    def test_3_every_frame_draws_both_halves(self):
        self.need("two views")
        c = self.c
        state = (f"views {c.u8(VIEW_COUNT)} level {self.p.level()} "
                 f"counters {({k: v for k, v in c.counters().items() if k.startswith(V)})}")
        newest = c.send({"cmd": "gpu_ring_stats"})["newest_frame"]
        checked = 0
        for f in range(newest - 1, newest - 60, -1):
            ents = gpu_frame(c, f)
            if sum(1 for e in ents if 0x20 <= int(e["op"], 16) <= 0x7F) <= WORLD_PRIMS:
                continue                                  # no frame, or a small one
            tops = {int(e["w"][0], 16) for e in ents if e["op"] == "0xE3"}
            with self.subTest(frame=f):
                self.assertTrue(tops & BOTTOM_HALF, f"no bottom half: {[hex(t) for t in tops]}; {state}")
                self.assertTrue(tops <= TWO_VIEW_TOPS, f"draw areas {[hex(t) for t in tops]}")
            checked += 1
            if checked == 3:
                break
        self.assertGreaterEqual(checked, 3, "no drawn frames in the GP0 ring")
        self.assertNotIn("FATAL", self.game.log_tail(200))

    def test_4_each_camera_follows_its_own_player(self):
        self.need("two views")
        c, p = self.c, self.p
        bugs, taz = p.actor(1), p.actor(2)
        far = 0
        for d in ("up", "right", "up", "left", "up", "right"):    # walk Taz away from Bugs
            c.hold(pad(d), port=2)
            for _ in range(10):
                c.wait_frames(45)
                far = max(far, flat(p.pos(bugs), p.pos(taz)))
            c.release(port=2); c.wait_frames(20)
            if far > FAR_APART:
                break
        self.assertGreater(far, FAR_APART, "Taz could not get far from Bugs (the 2-player leash?)")
        c.wait_frames(120)
        for v, own, other in ((0, bugs, taz), (1, taz, bugs)):
            _, ref, flags = camera(c, v)
            with self.subTest(view=v):
                self.assertEqual(flags & (CAM_FOLLOW_ONE | CAM_FRAME_BOTH), CAM_FOLLOW_ONE,
                                 f"camera {v} flags {flags:08X}: not a single-player camera")
                self.assertLess(flat(ref, p.pos(own)), 1500, f"camera {v} does not look at its player")
                self.assertGreater(flat(ref, p.pos(other)), FAR_APART // 2,
                                   f"camera {v} looks at the other player")
        self.game.shot("4_far_apart")
        self.reached.add("far apart")

    def test_5_each_pad_steers_by_its_own_camera(self):
        self.need("far apart")
        c, p = self.c, self.p
        for port, view in ((2, 1), (1, 0)):
            a = p.actor(port)
            # face another way first (the character turns over several frames)
            c.hold(pad("right"), port=port); c.wait_frames(40); c.release(port=port)
            c.wait_frames(120)
            vp, ref, _ = camera(c, view)
            fwd = heading(ref[0] - vp[0], ref[2] - vp[2])
            x0 = p.pos(a)
            c.hold(pad("up"), port=port); c.wait_frames(40)
            facing = (c.u16(a + 0xF6) & 0xFFF) * 360 / 4096    # yaw, same convention as heading()
            c.release(port=port); c.wait_frames(20)
            with self.subTest(port=port):
                self.assertGreater(moved(x0, p.pos(a)), 64, f"port {port} did not move")
                self.assertLess(turn(facing, fwd), MAX_TURN,
                                f"port {port} faces {facing:.0f} deg, its camera {fwd:.0f} deg")
        self.game.shot("5_steered")

    def test_6_two_views_run_at_full_speed(self):
        self.need("two views")
        c, p = self.c, self.p
        c.hold(pad("up"), port=1); c.hold(pad("up"), port=2); c.wait_frames(30)
        v0, f0 = c.u32(VBLANKS), c.u32(VIEW_PASSES)
        c.wait_frames(240)
        v1, f1 = c.u32(VBLANKS), c.u32(VIEW_PASSES)
        c.release(port=1); c.release(port=2)
        frames = (f1 - f0) / 2                              # one pass per view per frame
        self.assertGreaterEqual(frames / max(v1 - v0, 1), 0.45,
                                f"{frames:.0f} frames in {v1 - v0} vblanks (stock: one per 2 vblanks)")


if __name__ == "__main__":
    unittest.main()
