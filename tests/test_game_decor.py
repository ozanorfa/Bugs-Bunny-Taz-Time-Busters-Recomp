"""Game tier: the Turkish flag of bbt.decor (src/mods/decor.c).

In Granwich, with the defaults (Decorations on): the plugin rewrote the
wall texture's upload and its CLUT row, so VRAM holds the flag (a row of
the 30x20 flag mask at texel (17,32) of the 64x64 texture at VRAM
(256,448)) and the CLUT at (128,475) has the flag's red and white at
entries 10 and 13, with the game's semi-transparency bit kept. The same
holds for the wall's half-size copy, which the game draws on a window
farther away (32x32 at VRAM (368,288), CLUT (224,470), the 15x10 flag at
texel (9,16); BUGS #65), and the 6-pane window texture that is also
drawn with the wall's CLUT (376,256) uses neither colour. A screenshot of
the side wall goes to build-tests/out/decor/."""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from bbt import config  # noqa: E402
from bbt.client import pad  # noqa: E402
from test_game_stock import start_run  # noqa: E402

ROW_9 = "......##........####.........."   # crescent and star, flag row 9
FAR_ROW_5 = "...#....###...."                   # the half-size flag, row 5
RED, WHITE = 0x8C3C, 0xFFFF


def texels(words, w):
    """4-bit texel reader over VRAM words, w words per row."""
    return lambda x, y: (words[y * w + x // 4] >> (4 * (x % 4))) & 15


def mask(texel, x0, y, n):
    return "".join("#" if texel(x0 + x, y) == 13 else "." if texel(x0 + x, y) == 10 else "?"
                   for x in range(n))


class TurkishFlag(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        start_run(cls, config.game_build(), "decor",
                  {"bbt.quick-start/quick-start": True, "bbt.decor/turkish-flag": True})

    def test_1_the_flag_is_in_granwichs_wall_texture(self):
        c, p = self.c, self.p
        self.assertTrue(p.boot_to_language_select(), "no language screen")
        self.assertTrue(p.language_to_title(), "no title")
        self.assertTrue(p.title_to_two_player_game(), f"no Granwich (level {p.level()})")
        k = c.counters()
        got = {n: k.get("bbt.decor." + n) for n in
               ("flag_texels", "flag_clut", "flag_far_texels", "flag_far_clut", "panes_texels")}
        self.assertEqual(set(got.values()), {1}, got)
        for cx, cy in ((128, 475), (224, 470)):
            clut = c.vram(cx, cy, 16, 1)
            self.assertEqual((clut[10], clut[13]), (RED, WHITE), (cx, cy, [hex(w) for w in clut]))
        self.assertEqual(mask(texels(c.vram(256, 448, 16, 64), 16), 17, 41, 30), ROW_9)
        far = texels(c.vram(368, 288, 8, 32), 8)
        self.assertEqual(mask(far, 9, 21, 15), FAR_ROW_5)
        outside = [far(x, y) for y in range(32) for x in range(32)
                   if not (9 <= x < 24 and 16 <= y < 26)]
        self.assertFalse({10, 13} & set(outside), "flag colours outside the half-size flag")
        panes = texels(c.vram(376, 256, 8, 32), 8)
        self.assertFalse({10, 13} & {panes(x, y) for y in range(32) for x in range(32)},
                         "flag colours in the 6-pane window")
        for d in ("up", "left", "down", "right", "up"):
            c.hold(pad(d)); c.wait_frames(120); c.release(); c.wait_frames(20)
        self.game.shot("side_wall")


if __name__ == "__main__":
    unittest.main()
