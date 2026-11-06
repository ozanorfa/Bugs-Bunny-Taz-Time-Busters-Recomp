"""Game tier: Phase 4 third-player input (feature "p3"), in a 5-seat build
(build-p3, -DBBT_MAX_PLAYERS=5) with a 3-player copy of game.toml (an
SCPH-1070 multitap on port 2, written to the test output).

Checks that the multitap does not break the 2-player game (TWO PLAYERS is
selectable and pad 2 still moves Taz through tap slot A), and that the
plugin's third input record follows seat 2 in the game's bit order, every
game frame, without leaking into players 1 and 2. Seat 2 is driven with
the plugin's test command (the debug server only drives ports 1 and 2)."""
from __future__ import annotations

import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from bbt import config  # noqa: E402
from bbt.client import pad  # noqa: E402
from bbt.game import three_player_toml  # noqa: E402
from test_game_stock import moved, start_run  # noqa: E402

P = "bbt.third-character.p3."

# (seat-2 pad word, active low) -> (held bits in the game's order, stick x, stick y)
CASES = [
    (pad("cross"), 0x0040, 0, 0),
    (pad("right"), 0x2000, 127, 0),
    (pad("left"), 0x8000, -128, 0),
    (pad("up"), 0x1000, 0, 127),
    (pad("down"), 0x4000, 0, -128),
    (pad("square", "l1"), 0x0084, 0, 0),
    (pad("start"), 0x0800, 0, 0),
]


class ThirdPlayerInput(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        toml = config.out_dir() / "game_3p.toml"
        three_player_toml(config.game_toml(), toml)
        start_run(cls, config.p3_build(), "p3", {"p3": True}, toml=toml)

    def need(self, stage: str) -> None:
        if stage not in self.reached:
            self.skipTest(f"an earlier stage ({stage}) failed")

    def record(self) -> dict:
        b = self.c.read(self.rec, 16)
        v = struct.unpack_from("<6H", b, 0)
        return {"edge": v[0], "held": v[1], "raw": v[3], "prev": v[4],
                "sx": struct.unpack_from("<b", b, 12)[0], "sy": struct.unpack_from("<b", b, 13)[0]}

    def test_1_record_allocated(self):
        show = self.c.mod_cmd("bbt.p3", "show")
        self.assertEqual(show["seat"], 2)
        self.rec = int(show["record"], 16)
        self.assertGreaterEqual(self.rec, 0x9F000000, "record not in mod memory")
        type(self).rec = self.rec
        self.reached.add("record")

    def test_2_two_players_with_the_multitap(self):
        p = self.p
        ok = p.boot_to_language_select() and p.language_to_title() and p.title_to_two_player_game()
        self.game.shot("2_granwich")
        self.assertTrue(ok, f"level {p.level()}: TWO PLAYERS not reachable with the multitap?")
        self.assertTrue(p.until_control(), "pad 2 (tap slot A) does not move Taz")
        self.reached.add("granwich")

    def test_3_no_third_pad_means_no_input(self):
        self.need("record")
        self.c.mod_cmd("bbt.p3", "off")
        self.c.wait_frames(6)
        r = self.record()
        self.assertEqual((r["held"], r["raw"], r["sx"], r["sy"]), (0, 0, 0, 0), r)

    def test_4_record_follows_seat_2(self):
        self.need("record")
        bad = []
        for word, held, sx, sy in CASES:
            self.c.mod_cmd("bbt.p3", f"{word:04X}")
            self.c.wait_frames(6)
            r = self.record()
            if (r["held"], r["raw"], r["prev"], r["sx"], r["sy"]) != (held, held, held, sx, sy):
                bad.append(f"{word:04X}: expected held {held:04X} stick {sx},{sy}, got {r}")
            self.c.mod_cmd("bbt.p3", "off")
            self.c.wait_frames(6)
        self.assertEqual(bad, [])
        r = self.record()
        self.assertEqual((r["held"], r["sx"], r["sy"]), (0, 0, 0))

    def test_5_polled_once_per_game_frame(self):
        self.need("record")
        before = self.c.counters().get(P + "polls", 0)
        f0 = self.c.frame()
        self.c.wait_frames(120)
        n = self.c.counters().get(P + "polls", 0) - before
        frames = self.c.frame() - f0
        self.assertGreater(n, frames // 4, f"{n} polls in {frames} frames")
        self.assertLessEqual(n, frames + 2, f"{n} polls in {frames} frames")

    def test_6_seat_2_does_not_move_players_1_and_2(self):
        self.need("granwich")
        bugs, taz = self.p.actor(1), self.p.actor(2)
        b0, t0 = self.p.pos(bugs), self.p.pos(taz)
        self.c.mod_cmd("bbt.p3", f"{pad('right'):04X}")
        self.c.wait_frames(40)
        self.c.mod_cmd("bbt.p3", "off")
        self.c.wait_frames(10)
        self.assertLess(moved(b0, self.p.pos(bugs)), 8, "seat 2 moved Bugs")
        self.assertLess(moved(t0, self.p.pos(taz)), 8, "seat 2 moved Taz")


if __name__ == "__main__":
    unittest.main()
