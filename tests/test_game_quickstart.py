"""Game tier: Quick start (package bbt.quick-start, src/mods/quick_start.c)
in the 2-player build with debug tools (build-dev).

With the default options: the three start-up logos (levels 0x4A,
0x4B, 0x49, 300 frames each, NOTES.md "Quick start") are not shown, so the
language screen (level 2) comes at once; NEW GAME -> TWO PLAYERS -> START
GAME goes to Granwich without running the intro (level 0x53: its first
frame would leave 0x53 in the HUD's level, 0x8008DB64), in fewer frames
than skipping it with ✕ at once takes (~670); there the
game plays as after skipping the intro with ✕: both pads move their
character, the HUD runs (its first-frame set-up: 0x8008DB64 = 4, mask
0x800100AB = 0xFF, the mask the pause screen's selection loop needs), no
cinema bars (0x8001009F = 0), and the pause screen opens, takes input and
closes. A level change takes far less than the original loading screen
(its closing circle lasts size * 60 / 350000 frames: VICA01 410; measured
470 frames on a stock load, ~166 with the iris shortened and 2x CD speed),
the CD speed is the default 2x, and the level loads with both players.
With "skip_logos" off, the logos are shown as before.
Screenshots go to build-tests/out/quickstart*/."""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from bbt import config  # noqa: E402
from bbt.client import pad  # noqa: E402
from bbt.game import LEVEL_EXIT, LEVEL_GRANNY, LEVEL_ID, LEVEL_LANGUAGE, LEVEL_VICA01, TYPE_BUGS, TYPE_TAZ  # noqa: E402
from test_game_stock import moved, start_run  # noqa: E402

Q = "bbt.quick-start."
LEVEL_CCINTRO = 0x53
LOGO_LEVELS = (0x4A, 0x4B, 0x49)
STOCK_LANGUAGE_FRAME = 1347      # first frame of level 2 on a stock boot (NOTES.md)
HUD_LEVEL = 0x8008DB64           # level id the HUD took on its first frame
HUD_MASK = 0x800100AB            # set to 0xFF by the HUD set-up (FUN_80060B50)
CINEMA_BARS = 0x8001009F         # letterbox state, 0 = none (FUN_8005E6E8)
PAUSED = 0x80093959              # 1 while the pause screen is up (main)
LOADING = 0x8008DDBC             # 1 from FUN_8006B500 until the iris has closed
STOCK_VICA01_LOAD = 470          # frames, level jump Granwich -> VICA01, stock


def quick_start(**values) -> dict:
    return {"bbt.quick-start/quick-start": {"enabled": True, "values": values}}


class QuickStart(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        start_run(cls, config.game_build(), "quickstart", quick_start())

    def need(self, stage: str) -> None:
        if stage not in self.reached:
            self.skipTest(f"an earlier stage ({stage}) failed")

    def test_1_no_logos(self):
        c, p = self.c, self.p
        seen = set()

        def at_language() -> bool:
            seen.add(p.level())
            return p.level() == LEVEL_LANGUAGE
        self.assertTrue(c.wait_until(at_language, 3000, 2), f"level {p.level()}")
        first = c.frame()
        self.assertFalse(seen & set(LOGO_LEVELS), f"a logo level was loaded: {sorted(seen)}")
        self.assertLess(first, STOCK_LANGUAGE_FRAME - 800, f"language screen only at frame {first}")
        self.assertEqual(c.counters().get(Q + "logos_skipped"), 1)
        c.wait_frames(200)                    # the screen takes input after its fade-in
        self.game.shot("1_language")
        print(f"\n    language screen at frame {first} (stock {STOCK_LANGUAGE_FRAME})", file=sys.stderr)
        self.reached.add("language")

    def test_2_start_game_goes_to_granwich(self):
        self.need("language")
        c, p = self.c, self.p
        self.assertTrue(p.language_to_title(), f"level {p.level()}")
        c.wait_frames(240); c.press(pad("start")); c.wait_frames(120)
        c.press(pad("cross")); c.wait_frames(60)          # NEW GAME
        c.release(port=2); c.wait_frames(30)              # plug in pad 2
        c.press(pad("down")); c.wait_frames(30)
        c.press(pad("cross")); c.wait_frames(90)          # TWO PLAYERS
        self.assertTrue(p.two_player_mode(), "TWO PLAYERS not selected")
        start = c.frame()
        c.press(pad("cross"))                             # START GAME
        hud_levels = set()

        def ready() -> bool:
            hud_levels.add(c.u32(HUD_LEVEL))
            return p.level() == LEVEL_GRANNY and p.players_ready()
        self.assertTrue(c.wait_until(ready, 3000, 2), f"level {p.level()}")
        took = c.frame() - start
        self.game.shot("2_granwich")
        self.assertNotIn(LEVEL_CCINTRO, hud_levels, "the intro ran")
        self.assertLess(took, 600, "as slow as loading the intro")
        self.assertEqual(c.counters().get(Q + "intro_skipped"), 1)
        self.assertEqual(p.actor_type(p.actor(1)), TYPE_BUGS)
        self.assertEqual(p.actor_type(p.actor(2)), TYPE_TAZ)
        print(f"\n    START GAME -> Granwich ready in {took} frames", file=sys.stderr)
        self.reached.add("granwich")

    def test_3_plays_like_after_skipping_the_intro(self):
        self.need("granwich")
        c, p = self.c, self.p
        self.assertTrue(p.until_control(), "pad 2 never moved Taz (dialogs not closed?)")
        c.wait_frames(60)
        self.assertEqual(c.u32(HUD_LEVEL), LEVEL_GRANNY, "the HUD did not set itself up in Granwich")
        self.assertEqual(c.u8(HUD_MASK), 0xFF)
        self.assertEqual(c.u8(CINEMA_BARS), 0)
        bugs, taz = p.actor(1), p.actor(2)
        for port, mover in ((1, bugs), (2, taz)):
            m0 = p.pos(mover)
            c.hold(pad("right"), port=port); c.wait_frames(40); c.release(port=port); c.wait_frames(20)
            with self.subTest(port=port):
                self.assertGreater(moved(m0, p.pos(mover)), 16, f"port {port} did not move its character")
            c.hold(pad("left"), port=port); c.wait_frames(40); c.release(port=port); c.wait_frames(20)
        self.game.shot("3_control")
        self.reached.add("control")

    def test_4_pause_screen(self):
        self.need("control")
        c = self.c
        c.press(pad("start"))
        self.assertTrue(c.wait_until(lambda: c.u8(PAUSED) != 0, 120, 2), "the pause screen did not open")
        c.wait_frames(30)
        self.game.shot("4_pause")
        for b in ("right", "right", "down", "left", "up"):
            c.press(pad(b)); c.wait_frames(20)
        f = c.frame()
        c.wait_frames(10)
        self.assertGreater(c.frame(), f, "the game stopped running in the pause screen")
        c.press(pad("start"))
        self.assertTrue(c.wait_until(lambda: c.u8(PAUSED) == 0, 300, 2), "the pause screen did not close")
        self.reached.add("paused")

    def test_5_short_level_loads(self):
        self.need("control")
        c, p = self.c, self.p
        self.assertEqual(c.counters().get(Q + "cd_speed"), 2, "default CD speed not applied")
        before = c.counters().get(Q + "loading_shortened", 0)
        times = {}
        for level in (LEVEL_VICA01, LEVEL_GRANNY):
            t = c.frame()
            c.write_u32(LEVEL_ID, level)
            c.write_u32(LEVEL_EXIT, 1)
            self.assertTrue(c.wait_until(lambda: c.u8(LOADING) == 1, 300, 1), "no loading screen")
            done = c.wait_until(lambda: p.level() == level and p.players_ready()
                                and c.u8(LOADING) == 0 and c.u32(LEVEL_EXIT) == 0, 3000, 2)
            self.assertTrue(done, f"level {level} did not load")
            times[level] = c.frame() - t
            c.wait_frames(60)
            self.game.shot(f"5_level_{level}")
        print(f"\n    loads: {times} frames (stock VICA01 {STOCK_VICA01_LOAD})", file=sys.stderr)
        self.assertLess(times[LEVEL_VICA01], STOCK_VICA01_LOAD // 2)
        self.assertEqual(c.counters().get(Q + "loading_shortened", 0) - before, 2)


class QuickStartLogosOn(unittest.TestCase):
    """skip_logos = false: the logos are shown as on a stock boot."""

    @classmethod
    def setUpClass(cls):
        start_run(cls, config.game_build(), "quickstart-logos", quick_start(skip_logos="false"))

    def test_logos_are_shown(self):
        c, p = self.c, self.p
        seen = []

        def at_language() -> bool:
            if p.level() not in seen:
                seen.append(p.level())
            return p.level() == LEVEL_LANGUAGE
        self.assertTrue(c.wait_until(at_language, 3000, 2), f"level {p.level()}")
        self.assertEqual([lv for lv in seen if lv in LOGO_LEVELS], list(LOGO_LEVELS))
        self.assertGreater(c.frame(), STOCK_LANGUAGE_FRAME - 100)
        self.assertNotIn(Q + "logos_skipped", c.counters())


if __name__ == "__main__":
    unittest.main()
