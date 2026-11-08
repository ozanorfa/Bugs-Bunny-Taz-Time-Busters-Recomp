"""Game tier: Daffy Duck as the third character (feature "p3", its
default character since 2026-11-08) in the 5-seat build with debug tools (build-p3), a
3-player copy of game.toml and Quick start.

Daffy comes from LEVEL1/RETRY.BZZ (model 19, read by resource id); his
moves are Bugs' animations retargeted onto his skeleton at Taz's frame
counts (src/mods/retarget.c, daffy_moves.c; NOTES.md "Daffy as the third
character"). Checked: the donor is prepared with his textures and the
retargeted moves; the third actor spawns as him; running and jumping play
the retargeted animations (their keyframes are in the mod image) and move
him; his HUD panel and the hit / invincibility rule work as with the
enemy; a deadly hit takes him out. Screenshots go to
build-tests/out/daffy/."""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from bbt import config  # noqa: E402
from bbt.client import pad  # noqa: E402
from bbt.game import three_player_toml  # noqa: E402
from test_game_ownview3 import CLIP_PASVISIBLE, P3_SCREEN_FACE, check_p3_panel, hit  # noqa: E402
from test_game_stock import moved, start_run  # noqa: E402

A3 = "bbt.third-character.p3-actor."
RUN, JUMP, HURT, DEATH_ANIM = 0x0033, 0x0036, 0x00AA, 0x00AE


class Daffy(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        toml = config.out_dir() / "game_3p_daffy.toml"
        three_player_toml(config.game_toml(), toml)
        start_run(cls, config.p3_build(), "daffy",
                  {"p3": True, "bbt.quick-start/quick-start": True}, toml=toml)   # Daffy is the default

    def need(self, stage: str) -> None:
        if stage not in self.reached:
            self.skipTest(f"an earlier stage ({stage}) failed")

    def counters(self) -> dict:
        return {k[len(A3):]: v for k, v in self.c.counters().items() if k.startswith(A3)}

    def show(self) -> dict:
        return self.c.mod_cmd("bbt.p3actor", "show")

    def clone(self) -> int:
        return self.c.u32(int(self.show()["state"], 16) + 4)

    def seat2(self, word: int | None) -> None:
        self.c.mod_cmd("bbt.p3", "off" if word is None else f"{word:04X}")

    def anim(self) -> int:
        return self.c.u16(self.clone() + 0x14C)

    def test_1_daffy_spawns_with_his_moves(self):
        c, p = self.c, self.p
        self.assertTrue(p.boot_to_language_select(), "no language screen")
        self.assertTrue(p.language_to_title(), "no title")
        self.assertTrue(p.title_to_two_player_game(), "no 2-player Granwich")
        self.assertTrue(c.wait_until(lambda: self.counters().get("spawned", 0) >= 1, 1200, 10), "not spawned")
        for _ in range(10):                               # the opening dialogs
            c.press(pad("cross")); c.wait_frames(150)
        k, show = self.counters(), self.show()
        self.assertEqual(show["prepared"], 1)
        self.assertEqual((k.get("hold_ready"), k.get("tex_planned"), k.get("tex_uploaded"), k.get("models")),
                         (1, 1, 1, 1), k)
        # 44 of Taz's ids that Bugs also has, without the spin family (measured)
        self.assertGreaterEqual(k.get("daffy_moves", 0), 40, k)
        self.assertEqual(k.get("daffy_failed", 0), 0, k)
        self.assertLess(show["image_size"], 0x1E000, "the image no longer fits the mod block")
        self.game.shot("1_spawned")
        self.reached.add("daffy")

    def test_2_running_plays_the_retargeted_run(self):
        self.need("daffy")
        c, p = self.c, self.p
        a = self.clone()
        img, size = int(self.show()["image"], 16), self.show()["image_size"]
        runs = [kf for _, sub, _, kf in p.anim_records(a) if sub == RUN]
        self.assertTrue(runs, "no run animation in the clone's resources")
        self.assertTrue(all(img <= kf < img + size for kf in runs), "the run is not the retargeted one")
        p0 = p.pos(a)
        self.seat2(pad("right"))
        seen = set()
        for _ in range(20):
            c.wait_frames(3)
            seen.add(self.anim())
        self.seat2(None)
        c.wait_frames(20)
        self.assertIn(RUN, seen, f"animations seen {sorted(hex(s) for s in seen)}")
        self.assertGreater(moved(p0, p.pos(a)), 200, "Daffy did not run")
        self.game.shot("2_ran")

    def test_3_jumping_plays_the_retargeted_jump_and_lands(self):
        self.need("daffy")
        c, p = self.c, self.p
        a = self.clone()
        c.wait_frames(40)                                 # settled after the run
        y0 = p.pos(a)[1]
        seen, top = set(), y0
        for _ in range(3):                                # a press can land between game frames
            self.seat2(pad("cross")); c.wait_frames(6); self.seat2(None)
            for _ in range(30):
                c.wait_frames(3)
                seen.add(self.anim())
                top = min(top, p.pos(a)[1])
            if JUMP in seen:
                break
        self.assertIn(JUMP, seen, f"animations seen {sorted(hex(s) for s in seen)}")
        self.assertLess(top, y0 - 100, "no jump")
        self.assertTrue(c.wait_until(lambda: abs(p.pos(a)[1] - y0) < 40, 300, 5), "did not land")
        self.assertFalse(c.u16(a + 0x6E) & CLIP_PASVISIBLE, "not drawn after landing")

    def test_4_his_panel_and_a_hit(self):
        self.need("daffy")
        a = self.clone()
        self.c.wait_frames(60)
        check_p3_panel(self, self.c, a, P3_SCREEN_FACE, lambda: self.game.shot("4_panel"))

    def test_5_a_deadly_hit_takes_him_out(self):
        self.need("daffy")
        c = self.c
        a = self.clone()
        hit(c, a)
        seen = set()
        for _ in range(30):
            c.wait_frames(2)
            seen.add(self.anim())
        self.assertIn(DEATH_ANIM, seen, f"animations seen {sorted(hex(s) for s in seen)}")
        self.assertTrue(c.wait_until(lambda: c.u16(a + 0x6E) & CLIP_PASVISIBLE, 600, 5), "not taken out")


if __name__ == "__main__":
    unittest.main()
