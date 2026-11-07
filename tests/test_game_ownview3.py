"""Game tier: three views (bbt.own-view "two-views" with the third player,
bbt.third-character "p3", and Quick start) in the 5-seat build with debug
tools (build-p3), with a 3-player copy of game.toml (multitap on port 2).

With the third actor in a 2-player level the plugin raises the view count to
3: view 2 follows the third actor (0x80093C58[2]), every camera is a
single-player camera (record flags +0xE0: 0x80 set, 0x100 clear) looking at
its own actor, seat 2 (driven with the p3 plugin's test command) turns the
third actor to camera 2's forward direction, and the game keeps running at
close to its stock rate (game.toml [timing], NOTES.md "Third player's own
view"). The two plugins that need Actor_Update share one override
(bbt.shared/actor-update, activated implicitly). Screenshots go to
build-tests/out/ownview3/."""
from __future__ import annotations

import math
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from bbt import config  # noqa: E402
from bbt.client import pad  # noqa: E402
from bbt.game import LEVEL_GRANNY, three_player_toml  # noqa: E402
from test_game_stock import moved, start_run  # noqa: E402

V = "bbt.own-view.two-views."
VIEW_COUNT, FOLLOWED = 0x8008DE00, 0x80093C58
CAM_RECORD, CAM_RECORD_BYTES = 0x800964C4, 0x2C0
CAM_FOLLOW_ONE, CAM_FRAME_BOTH = 0x80, 0x100
VBLANKS, VIEW_PASSES = 0x8008DE20, 0x8008DE88
MAX_TURN = 30


def camera(c, view: int):
    r = CAM_RECORD + view * CAM_RECORD_BYTES
    return (struct.unpack("<3i", c.read(r + 4, 12)), struct.unpack("<3i", c.read(r + 0x10, 12)),
            c.u32(r + 0xE0))


def flat(a, b) -> int:
    return round(math.hypot(a[0] - b[0], a[2] - b[2]))


def heading(dx: int, dz: int) -> float:
    return math.degrees(math.atan2(dx, dz)) % 360


def turn(a: float, b: float) -> float:
    return abs((a - b + 180) % 360 - 180)


class ThreeViews(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        toml = config.out_dir() / "game_3p_ownview3.toml"
        three_player_toml(config.game_toml(), toml)
        start_run(cls, config.p3_build(), "ownview3",
                  {"p3": True, "bbt.quick-start/quick-start": True, "bbt.own-view/two-views": True},
                  toml=toml)

    def need(self, stage: str) -> None:
        if stage not in self.reached:
            self.skipTest(f"an earlier stage ({stage}) failed")

    def clone(self) -> int:
        return self.c.u32(int(self.c.mod_cmd("bbt.p3actor", "show")["state"], 16) + 4)

    def seat2(self, word: int | None) -> None:
        self.c.mod_cmd("bbt.p3", "off" if word is None else f"{word:04X}")

    def test_1_three_views_follow_the_three_players(self):
        c, p = self.c, self.p
        self.assertTrue(c.wait_until(lambda: p.level() == 2, 3000, 2), "no language screen")
        c.wait_frames(200)
        self.assertTrue(p.language_to_title(), "no title")
        c.wait_frames(240); c.press(pad("start")); c.wait_frames(120)
        c.press(pad("cross")); c.wait_frames(60); c.release(port=2); c.wait_frames(30)
        c.press(pad("down")); c.wait_frames(30); c.press(pad("cross")); c.wait_frames(150)
        c.press(pad("cross"))
        self.assertTrue(c.wait_until(lambda: p.level() == LEVEL_GRANNY and p.players_ready(), 3000, 2))
        self.assertTrue(c.wait_until(lambda: c.counters().get(V + "views_3"), 900, 5), "no third view")
        self.assertIn("bbt.shared/actor-update activated implicitly", self.game.log_tail(400))
        for _ in range(10):                               # the opening dialogs
            c.press(pad("cross")); c.wait_frames(150)
        self.assertTrue(c.wait_until(lambda: c.u8(VIEW_COUNT) == 3, 60, 1), "the view count fell back")
        actors = [p.actor(1), p.actor(2), self.clone()]
        followed = list(struct.unpack("<3I", c.read(FOLLOWED, 12)))
        self.assertEqual(followed, actors, "views do not follow Bugs, Taz and the third actor")
        for v, own in enumerate(actors):
            _, ref, flags = camera(c, v)
            with self.subTest(view=v):
                self.assertEqual(flags & (CAM_FOLLOW_ONE | CAM_FRAME_BOTH), CAM_FOLLOW_ONE)
                self.assertLess(flat(ref, p.pos(own)), 600, f"camera {v} does not look at its actor")
        self.game.shot("1_three_views")
        self.reached.add("three views")

    def test_2_seat_2_steers_by_camera_2(self):
        self.need("three views")
        c, p = self.c, self.p
        clone = self.clone()
        self.seat2(pad("right")); c.wait_frames(40); self.seat2(None); c.wait_frames(120)
        vp, ref, _ = camera(c, 2)
        fwd = heading(ref[0] - vp[0], ref[2] - vp[2])
        x0 = p.pos(clone)
        self.seat2(pad("up")); c.wait_frames(40)
        facing = (c.u16(clone + 0xF6) & 0xFFF) * 360 / 4096
        self.seat2(None); c.wait_frames(20)
        self.assertGreater(moved(x0, p.pos(clone)), 32, "seat 2 did not move the third actor")
        self.assertLess(turn(facing, fwd), MAX_TURN, f"the third actor faces {facing:.0f}, camera 2 {fwd:.0f}")

    def test_3_three_views_keep_the_game_speed(self):
        self.need("three views")
        c = self.c
        c.hold(pad("up"), port=1); c.hold(pad("up"), port=2); self.seat2(pad("up"))
        c.wait_frames(30)
        v0, f0 = c.u32(VBLANKS), c.u32(VIEW_PASSES)
        c.wait_frames(240)
        v1, f1 = c.u32(VBLANKS), c.u32(VIEW_PASSES)
        c.release(port=1); c.release(port=2); self.seat2(None)
        rate = (f1 - f0) / 3 / max(v1 - v0, 1)
        self.assertGreaterEqual(rate, 0.44, f"{rate:.3f} frames per vblank (stock: 0.5)")
        self.assertNotIn("FATAL", self.game.log_tail(200))
        self.game.shot("3_running")


if __name__ == "__main__":
    unittest.main()
