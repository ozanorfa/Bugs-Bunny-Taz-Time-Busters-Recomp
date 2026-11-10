"""Game tier: the Turkish flag of bbt.decor (src/mods/decor.c).

In Granwich, with the defaults (Decorations on): the plugin rewrote the
wall texture's upload and its CLUT row, so VRAM holds the flag (a row of
the 30x20 flag mask at texel (17,32) of the 64x64 texture at VRAM
(256,448)) and the CLUT at (128,475) has the flag's red and white at
entries 10 and 13, with the game's semi-transparency bit kept. A
screenshot of the side wall goes to build-tests/out/decor/."""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from bbt import config  # noqa: E402
from bbt.client import pad  # noqa: E402
from test_game_stock import start_run  # noqa: E402

ROW_9 = "......##........####.........."   # crescent and star, flag row 9


class TurkishFlag(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        start_run(cls, config.game_build(), "decor", {"bbt.quick-start/quick-start": True})

    def test_1_the_flag_is_in_granwichs_wall_texture(self):
        c, p = self.c, self.p
        self.assertTrue(p.boot_to_language_select(), "no language screen")
        self.assertTrue(p.language_to_title(), "no title")
        self.assertTrue(p.title_to_two_player_game(), f"no Granwich (level {p.level()})")
        k = c.counters()
        self.assertEqual((k.get("bbt.decor.flag_texels"), k.get("bbt.decor.flag_clut")), (1, 1), k)
        clut = c.vram(128, 475, 16, 1)
        self.assertEqual((clut[10], clut[13]), (0x8C3C, 0xFFFF), [hex(w) for w in clut])
        tex = c.vram(256, 448, 16, 64)

        def texel(x, y):
            return (tex[y * 16 + x // 4] >> (4 * (x % 4))) & 15
        row = "".join("#" if texel(17 + x, 41) == 13 else "." if texel(17 + x, 41) == 10 else "?"
                      for x in range(30))
        self.assertEqual(row, ROW_9)
        for d in ("up", "left", "down", "right", "up"):
            c.hold(pad(d)); c.wait_frames(120); c.release(); c.wait_frames(20)
        self.game.shot("side_wall")


if __name__ == "__main__":
    unittest.main()
