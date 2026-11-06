"""Game tier: the unmodified game (no mod feature enabled), from a cold boot
to two-player gameplay in Granwich, played through the debug server like a
person would. Guards the base game against regressions from the recomp
build or from mod code that leaks into the stock path.

Screenshots of each stage go to build-tests/out/stock/."""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from bbt import config  # noqa: E402
from bbt.client import pad  # noqa: E402
from bbt.game import (GameRun, Player, LEVEL_GRANNY, TYPE_BUGS, TYPE_TAZ, in_ram)  # noqa: E402


def start_run(test_cls, build: Path, name: str, features: dict, toml: Path | None = None) -> None:
    """Shared setUpClass part: skip without a build or disc, start the game."""
    if config.exe_in(build) is None:
        raise unittest.SkipTest(f"no game executable in {build}")
    cue = config.disc_cue()
    if cue is None or not cue.is_file():
        raise unittest.SkipTest("disc not found (game.toml [game] disc or BBT_DISC)")
    test_cls.game = GameRun(build, name, features, toml=toml).__enter__()
    # Class cleanups also run when the rest of setUpClass fails, so the game
    # never outlives the tests and the build's mod selection is restored.
    test_cls.addClassCleanup(test_cls.game.__exit__, None, None, None)
    test_cls.p = Player(test_cls.game)
    test_cls.c = test_cls.game.client
    test_cls.reached = set()


def moved(a, b) -> int:
    return abs(a[0] - b[0]) + abs(a[2] - b[2])


class StockGame(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        start_run(cls, config.game_build(), "stock", {})

    def need(self, stage: str) -> None:
        if stage not in self.reached:
            self.skipTest(f"an earlier stage ({stage}) failed")

    def test_1_boots_to_the_language_select(self):
        self.assertTrue(self.p.boot_to_language_select(),
                        f"level {self.p.level()} at frame {self.c.frame()}:\n{self.game.log_tail()}")
        self.game.shot("1_language")
        self.reached.add("language")

    def test_2_language_leads_to_the_title(self):
        self.need("language")
        self.assertTrue(self.p.language_to_title(), f"level {self.p.level()}")
        self.game.shot("2_title")
        self.reached.add("title")

    def test_3_two_players_start_in_granwich(self):
        """TWO PLAYERS is selectable once pad 2 is connected; the game
        then loads Granwich with Bugs as player 1 and Taz as player 2."""
        self.need("title")
        ok = self.p.title_to_two_player_game()
        self.game.shot("3_granwich")
        self.assertTrue(self.p.two_player_mode(), "2-player mode flag not set")
        self.assertTrue(ok, f"level {self.p.level()}, players {self.p.actor(1):08X} {self.p.actor(2):08X}")
        self.assertEqual(self.p.level(), LEVEL_GRANNY)
        self.assertEqual(self.p.actor_type(self.p.actor(1)), TYPE_BUGS)
        self.assertEqual(self.p.actor_type(self.p.actor(2)), TYPE_TAZ)
        self.reached.add("granwich")

    def test_4_each_pad_moves_its_own_character(self):
        self.need("granwich")
        self.assertTrue(self.p.until_control(), "pad 2 never moved Taz (dialogs not closed?)")
        self.game.shot("4_control")
        c, bugs, taz = self.c, self.p.actor(1), self.p.actor(2)
        for port, mover, other in ((2, taz, bugs), (1, bugs, taz)):
            m0, o0 = self.p.pos(mover), self.p.pos(other)
            c.hold(pad("right"), port=port)
            c.wait_frames(40)
            c.release(port=port)
            c.wait_frames(20)
            m1, o1 = self.p.pos(mover), self.p.pos(other)
            with self.subTest(port=port):
                self.assertGreater(moved(m0, m1), 16, f"port {port} did not move its character")
                self.assertLess(moved(o0, o1), 8, f"port {port} moved the other character")
            c.hold(pad("left"), port=port)
            c.wait_frames(40)
            c.release(port=port)
            c.wait_frames(20)
        self.reached.add("control")

    def test_5_taz_has_his_own_animations(self):
        self.need("granwich")
        recs = self.p.anim_records(self.p.actor(2))
        self.assertEqual(len(recs), 67)
        self.assertEqual([r for r in recs if not in_ram(r[3])], [], "keyframes outside main RAM")

    def test_6_no_mod_activity(self):
        self.need("granwich")
        bbt = {k: v for k, v in self.c.counters().items() if k.startswith("bbt.")}
        self.assertEqual(bbt, {}, "a bbt plugin ran with every feature off")


if __name__ == "__main__":
    unittest.main()
