"""Game tier: the mod defaults a player gets with nothing selected
(2026-11-08), offline in the 5-seat build (build-p3) with the 3-player
config: Quick start on (logos and the new-game intro skipped), the own view
on but keeping the game's shared screen on one PC (one view in a 2-player
level), and the third player on but not appearing without a third player
(this test process gives seat 2 no controller). A LAN room with the same
defaults: test_game_lanroom.py test_two_pcs_in_a_three_seat_room."""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from bbt import config  # noqa: E402
from bbt.client import pad  # noqa: E402
from bbt.game import three_player_toml  # noqa: E402
from test_game_stock import start_run  # noqa: E402

A3 = "bbt.third-character.p3-actor."
V = "bbt.own-view.two-views."
Q = "bbt.quick-start."
VIEW_COUNT = 0x8008DE00


class Defaults(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        toml = config.out_dir() / "game_3p_defaults.toml"
        three_player_toml(config.game_toml(), toml)
        start_run(cls, config.p3_build(), "defaults", {"_defaults": True}, toml=toml)

    def test_1_nothing_selected(self):
        c, p = self.c, self.p
        self.assertTrue(p.boot_to_language_select(), "no language screen")
        k = c.counters()
        self.assertEqual(k.get(Q + "logos_skipped"), 1, "Quick start is not on by default")
        self.assertTrue(p.language_to_title(), "no title")
        self.assertTrue(p.title_to_two_player_game(), "no 2-player Granwich")
        for _ in range(8):                                 # the opening dialogs
            c.press(pad("cross")); c.wait_frames(150)
        c.wait_frames(120)
        k = c.counters()
        self.assertEqual(k.get(Q + "intro_skipped"), 1, "the intro was not skipped")
        # the third player: on (its plugins ran the level load), not spawned
        self.assertGreaterEqual(k.get(A3 + "no_third_player", 0), 1, "the rule did not run")
        self.assertEqual(k.get(A3 + "spawned", 0), 0, "Daffy appeared without a third player")
        # the own view: one PC keeps the shared screen
        self.assertEqual(c.u8(VIEW_COUNT), 1, "the screen is split on one PC")
        self.assertEqual(k.get(V + "views_set", 0) + k.get(V + "views_3", 0), 0)
        self.game.shot("1_granwich")


if __name__ == "__main__":
    unittest.main()
