"""Game tier: the "trca01" feature (Taz drawn as the Transylvania enemy),
end to end in the running game.

From a cold boot to 2-player Granwich, then: the donor is loaded and every
load path reports success; Taz plays only the enemy's animations; the
enemy's textures are in VRAM where the plugin put them (pixel hashes) and
Taz is drawn with them; every action button (✕ ○ □ △ on pad 2) lands Taz
back on the ground with his body drawn again (the floating / invisible
reports); the spin attaches the tornado; level jumps to VICA01 (textures
relocated again) and to TRCA01 (the enemy's own level: textures as they
are). Screenshots go to build-tests/out/trca01/."""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from bbt import config  # noqa: E402
from bbt.client import fnv1a32_pixels, pad, prim_cluts  # noqa: E402
from bbt.game import LEVEL_GRANNY, LEVEL_TRCA01, LEVEL_VICA01, in_ram  # noqa: E402
from test_game_stock import start_run  # noqa: E402

P = "bbt.third-character.trca01."
# One of these is counted at the end of every patched level load
# (relocate_textures in third_character.c).
LOAD_DONE = tuple(P + k for k in ("tex_relocated", "tex_native_level", "tex_no_space", "tex_no_pool",
                                  "tex_upload_failed"))
ERROR_COUNTERS = ("unexpected_state", "stream_rejected", "record_write_failed", "tex_upload_failed",
                  "tex_no_space", "tex_no_pool", "donor_unavailable", "spin_unavailable",
                  "tex_unavailable", "call.other")


class Trca01Game(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        start_run(cls, config.game_build(), "trca01", {"trca01": True})
        p = cls.p
        if not (p.boot_to_language_select() and p.language_to_title() and p.title_to_two_player_game()):
            raise AssertionError(f"could not reach 2-player Granwich (level {p.level()}):\n{cls.game.log_tail()}")
        if not p.until_control():
            raise AssertionError("pad 2 never moved Taz")
        cls.game.shot("0_granwich")

    # ---- helpers ------------------------------------------------------------
    def show(self) -> dict:
        return self.c.mod_cmd("bbt.trca01", "show")

    def groups(self) -> list[dict]:
        return [self.c.mod_cmd("bbt.trca01", f"group {g}") for g in range(self.show()["groups"])]

    def enemy_cluts(self) -> set[int]:
        return {(g["clut"][1] << 6) | (g["clut"][0] >> 4) for g in self.groups()}

    def drawn_enemy_prims(self) -> int:
        cl = prim_cluts(self.c.gpu_prims())
        return sum(n for clut, n in cl.items() if clut in self.enemy_cluts())

    def check_vram(self, level: int) -> None:
        show = self.show()
        self.assertEqual(show["plan_level"], level, "the last relocation was for another level")
        bad = []
        for g in self.groups():
            x, y, w, h = g["block"]
            got = fnv1a32_pixels(self.c.vram(x, y, w, h))
            if f"{got:08X}" != g["block_fnv"]:
                bad.append(f"group {g['group']} block at ({x},{y})")
            cx, cy = g["clut"]
            if f"{fnv1a32_pixels(self.c.vram(cx, cy, 16, 1)):08X}" != g["clut_fnv"]:
                bad.append(f"group {g['group']} CLUT at ({cx},{cy})")
        self.assertEqual(bad, [], "VRAM differs from what the plugin uploaded")

    def check_donor_animations(self) -> None:
        show = self.show()
        lo = int(show["donor"], 16)
        hi = lo + show["image_size"]
        taz = self.p.anim_records(self.p.actor(2))
        self.assertGreater(len(taz), 0)
        outside = [f"{sub:04X}" for _, sub, _, kf in taz if not lo <= kf < hi]
        self.assertEqual(outside, [], "Taz animations not redirected to the enemy")
        bugs = self.p.anim_records(self.p.actor(1))
        self.assertEqual([r for r in bugs if not in_ram(r[3])], [], "Bugs was changed")

    # ---- tests ----------------------------------------------------------------
    def test_1_donor_loaded_and_load_paths_clean(self):
        show = self.show()
        self.assertEqual(show["donor_state"], 1)
        self.assertEqual(show["tex_ready"], 1)
        self.assertEqual(show["groups"], 13)
        self.assertTrue(int(show["donor"], 16) >= 0x9F000000, show)
        c = {k[len(P):]: v for k, v in self.c.counters().items() if k.startswith(P)}
        self.assertEqual(c.get("spin_attach_node"), 23)
        self.assertEqual(c.get("tex_ready"), 1)
        for k in ("loads_patched", "models_patched", "anims_mapped", "anim_records_fixed",
                  "scale_records", "tex_relocated", "tex_records_patched"):
            self.assertGreater(c.get(k, 0), 0, k)
        self.assertEqual({k: c[k] for k in ERROR_COUNTERS if c.get(k)}, {})

    def test_2_taz_plays_the_enemy_animations(self):
        self.check_donor_animations()
        self.assertEqual(len(self.p.anim_records(self.p.actor(2))), 67)

    def test_3_textures_are_in_vram_and_drawn(self):
        self.check_vram(LEVEL_GRANNY)
        self.assertGreater(self.drawn_enemy_prims(), 20, "Taz is not drawn with the relocated textures")

    def test_4_every_action_lands_and_stays_visible(self):
        c, taz = self.c, self.p.actor(2)
        report = []
        for button in ("cross", "circle", "square", "triangle"):
            ground = self.p.pos(taz)[1]
            c.press(pad(button), frames=8, port=2)
            ys = []
            for _ in range(60):
                c.wait_frames(3)
                ys.append(self.p.pos(taz)[1])
            c.wait_frames(30)
            end = self.p.pos(taz)[1]
            drawn = self.drawn_enemy_prims()
            self.game.shot(f"4_after_{button}")
            report.append(f"{button}: ground {ground} range {min(ys)}..{max(ys)} end {end} drawn {drawn}")
            with self.subTest(button=button):
                self.assertLessEqual(abs(end - ground), 8, f"{button}: Taz did not come back down")
                self.assertGreater(drawn, 20, f"{button}: Taz's body is not drawn")
        print("\n    " + "\n    ".join(report))

    def test_5_spin_attaches_the_tornado(self):
        c, taz = self.c, self.p.actor(2)
        c.hold(pad("square"), port=2)
        child = 0
        for _ in range(40):
            c.wait_frames(2)
            child = child or self.c.u32(taz + 0x8C)
            if child:
                self.game.shot("5_spin")
                break
        c.release(port=2)
        self.assertTrue(child, "no tornado attached during the spin (+0x8C stayed 0)")
        c.wait_frames(150)
        self.assertGreater(self.drawn_enemy_prims(), 20, "Taz's body is not drawn after the spin")

    def test_6_level_jump_relocates_again(self):
        before = self.c.counters()
        self.assertTrue(self.p.jump_to_level(LEVEL_VICA01, LOAD_DONE), f"level {self.p.level()}")
        after = self.c.counters()
        self.assertEqual(after.get(P + "loads_patched", 0) - before.get(P + "loads_patched", 0), 1)
        self.assertEqual(after.get(P + "tex_relocated", 0) - before.get(P + "tex_relocated", 0), 1)
        self.check_vram(LEVEL_VICA01)
        self.check_donor_animations()
        self.game.shot("6_vica01")

    def test_7_enemy_level_keeps_its_own_textures(self):
        before = self.c.counters()
        self.assertTrue(self.p.jump_to_level(LEVEL_TRCA01, LOAD_DONE), f"level {self.p.level()}")
        after = self.c.counters()
        self.assertEqual(after.get(P + "loads_patched", 0) - before.get(P + "loads_patched", 0), 1)
        self.assertEqual(after.get(P + "tex_native_level", 0) - before.get(P + "tex_native_level", 0), 1)
        self.assertEqual(after.get(P + "tex_relocated", 0), before.get(P + "tex_relocated", 0))
        self.check_donor_animations()
        self.game.shot("7_trca01")


if __name__ == "__main__":
    unittest.main()
