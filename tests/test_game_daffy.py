"""Game tier: Daffy Duck as the third character (feature "p3"; the only
one since 2026-11-26) in the 5-seat build with debug tools (build-p3), a
3-player copy of game.toml and Quick start.

Daffy comes from LEVEL1/RETRY.BZZ (model 19, read by resource id); his
moves are Bugs' animations retargeted onto his skeleton at Taz's frame
counts (src/mods/retarget.c, daffy_moves.c; NOTES.md "Daffy as the third
character"). Checked: the donor is prepared with his textures and the
retargeted moves, his textures at full height (BUGS #25); the third actor
spawns as him; running and jumping play
the retargeted animations (their keyframes are in the mod image) and move
him; his HUD panel and the hit / invincibility rule work; a deadly hit takes him out; his attack (Taz's spin, his only
attack) is Bugs' □ attack drawn on him, with no tornado, and still hits;
his moves make Bugs' sounds, or none of Taz's with sounds = "quiet"
(NOTES.md "Daffy's spin and sounds"); the game's package (script
action 161, func_8004E908) brings him back beside it, with an out Bugs or
on his own (BUGS.md #62); with comeback = "auto" (the default) he comes
back by himself 5 s after he is out. Screenshots go to build-tests/out/daffy/."""
from __future__ import annotations

import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from bbt import config  # noqa: E402
from bbt.client import fnv1a32_pixels, pad  # noqa: E402
from bbt.p3_vram import STRIP, TALL, inside  # noqa: E402
from bbt.game import three_player_toml  # noqa: E402
from test_game_ownview3 import (CLIP_PASVISIBLE, DEATH, HERO_DOWN, LIVES, P3_SCREEN_FACE,  # noqa: E402
                                check_p3_panel, flat, hit)
from test_game_stock import moved, start_run  # noqa: E402

A3 = "bbt.third-character.p3-actor."
RUN, JUMP, HURT, DEATH_ANIM = 0x0033, 0x0036, 0x00AA, 0x00AE
SPIN_START, SPIN, ATTACK = 0x0072, 0x004F, 0x1000   # +0x20 objetat_ATTACK
SOUND_SLOTS, SOUND_TABLE, SOUND_COUNT = 0x80093274, 0x80092AB4, 0x8008DBB8
BUGS_SOUNDS = {10, 11, 19, 23, 2, 22}      # his steps, jump, land, stop, attack


def sound_starts(c, clone, act) -> list[int]:
    """Sound ids the clone starts while `act()` runs: the sound slots
    (0x80093274, +0 flags 0x8000 in use, +4 actor, +8 table entry) from the
    per-frame RAM ring, first 18 slots (four 128-byte snapshot regions)."""
    for k in range(4):
        c.send({"cmd": "set_snapshot", "slot": k, "addr": f"0x{SOUND_SLOTS + 128 * k:08X}", "size": 128})
    f0 = c.frame()
    act()
    f1 = c.frame() - 1
    ids, prev = [], {}
    for fr in range(f0, f1):
        parts = [c.send({"cmd": "read_frame_ram", "addr": f"0x{SOUND_SLOTS + 128 * k:08X}", "len": 128,
                         "frame": fr}) for k in range(4)]
        if any("hex" not in r for r in parts):
            continue
        b = b"".join(bytes.fromhex(r["hex"]) for r in parts)
        for i in range(18):
            flags, = struct.unpack_from("<H", b, i * 0x1C)
            actor, entry = struct.unpack_from("<II", b, i * 0x1C + 4)
            cur = (flags, actor, entry) if flags & 0x8000 else None
            if cur and actor == clone and (cur[1:] != (prev.get(i) or (0, 0, 0))[1:]):
                ids.append(c.u16(entry))
            prev[i] = cur
    return ids


def taz_bank(c) -> set[int]:
    """The ids of Taz's sound bank (VAB program 4, table entry +2 low byte)."""
    n = c.u16(SOUND_COUNT)
    raw = c.read(SOUND_TABLE, 12 * n)
    return {struct.unpack_from("<H", raw, 12 * i)[0] for i in range(n) if raw[12 * i + 2] == 4}


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

    def test_1b_his_textures_at_full_height(self):
        """BUGS #25: his three blocks keep every row (the taller two in the
        tall area below x = 512, the other in the strip), the CLUTs are in
        the strip, and VRAM holds what the plan says."""
        self.need("daffy")
        bad = []
        groups = [self.c.mod_cmd("bbt.p3actor", f"group {g}") for g in range(self.show()["groups"])]
        self.assertEqual(len(groups), 3)
        for g in groups:
            x, y, w, h = g["block"]
            cx, cy = g["clut"]
            self.assertEqual(g["vshift"], 0, g)
            self.assertTrue(inside(x, y, w, h, STRIP) or inside(x, y, w, h, TALL), g)
            self.assertTrue(inside(cx, cy, 16, 1, STRIP), g)
            if f"{fnv1a32_pixels(self.c.vram(x, y, w, h)):08X}" != g["block_fnv"]:
                bad.append(("block", g["group"]))
            if f"{fnv1a32_pixels(self.c.vram(cx, cy, 16, 1)):08X}" != g["clut_fnv"]:
                bad.append(("clut", g["group"]))
        self.assertEqual(bad, [])
        self.assertEqual(sum(inside(*g["block"], TALL) for g in groups), 2, groups)

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

    def test_3b_his_attack_is_bugs_attack_drawn_and_it_hits(self):
        """□ starts Taz's spin (his only attack: ATTACK only in spin start
        0x0072 and the spin 0x004F). On Daffy both play Bugs' attack from
        the mod image, no tornado is attached (+0x8C stays 0), the draw pass
        draws him in the spin (spin_drawn), and the attack state is set."""
        self.need("daffy")
        c, p = self.c, self.p
        a = self.clone()
        self.assertEqual(self.counters().get("daffy_attack"), 3, self.counters())
        img, size = int(self.show()["image"], 16), self.show()["image_size"]
        recs = {sub: kf for _, sub, _, kf in p.anim_records(a) if sub in (SPIN_START, SPIN)}
        self.assertEqual(set(recs), {SPIN_START, SPIN})
        self.assertTrue(all(img <= kf < img + size for kf in recs.values()), "not his own attack")
        c.wait_frames(40)
        k0 = self.counters()
        attack = child = 0
        shot = False
        for _ in range(3):                                # a press can land between game frames
            self.seat2(pad("square")); c.wait_frames(6); self.seat2(None)
            for i in range(25):
                c.wait_frames(2)
                attack |= c.u32(a + 0x20) & ATTACK
                child |= c.u32(a + 0x8C)
                if attack and not shot and i >= 6:
                    self.game.shot("3b_attack")
                    shot = True
            if attack:
                break
        c.wait_frames(40)
        k1 = self.counters()
        self.assertTrue(attack, "no attack state")
        self.assertEqual(child, 0, "a tornado was attached")
        self.assertGreater(k1.get("spin_drawn", 0) - k0.get("spin_drawn", 0), 5, "not drawn in the spin")
        self.assertEqual(k1.get("attack_sound", 0) - k0.get("attack_sound", 0), 1, "no attack sound")
        self.assertFalse(c.u16(a + 0x6E) & CLIP_PASVISIBLE, "not drawn after the attack")

    def test_3c_his_moves_make_bugs_sounds(self):
        self.need("daffy")
        c = self.c
        a = self.clone()
        taz = taz_bank(c)
        self.assertTrue({63, 71, 72, 74, 75, 82} <= taz, f"Taz's bank {sorted(taz)}")

        def moves():
            for b in ("cross", "square"):
                self.seat2(pad(b)); c.wait_frames(6); self.seat2(None); c.wait_frames(70)
            self.seat2(pad("right")); c.wait_frames(40); self.seat2(None); c.wait_frames(30)
        ids = sound_starts(c, a, moves)
        self.assertTrue(ids, "no sounds")
        self.assertEqual([i for i in ids if i in taz], [], f"Taz's sounds played: {ids}")
        self.assertTrue({19, 22} <= set(ids) <= BUGS_SOUNDS, f"sounds {ids}")

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
        self.reached.add("daffy out")

    def back_by_package(self, a: int, at: int) -> None:
        """The game's package action for actor `at` (debug command, run at
        the next actor pass); Daffy is then back beside it and moves again."""
        c, p = self.c, self.p
        before = self.counters().get("back_package", 0)
        c.mod_cmd("bbt.p3actor", f"package {at:08X}")
        self.assertTrue(c.wait_until(lambda: not c.u16(a + 0x6E) & CLIP_PASVISIBLE, 60, 1),
                        "the package did not bring Daffy back")
        c.wait_frames(30)
        self.assertEqual(self.counters().get("back_package", 0), before + 1)
        self.assertEqual(c.s16(a + 0x3C), 6)
        self.assertFalse(c.u32(a + 0x20) & DEATH)
        self.assertLess(flat(p.pos(a), p.pos(at)), 400, "Daffy is not beside the package")
        x0 = p.pos(a)
        self.seat2(pad("up")); c.wait_frames(40); self.seat2(None); c.wait_frames(10)
        self.assertGreater(moved(x0, p.pos(a)), 32, "seat 2 does not move Daffy again")

    def test_6_a_package_brings_him_back_with_bugs(self):
        """Daffy out (test 5), then Bugs: the package brings Bugs back (the
        game's own step: player 2 after the swap, at the package, nobody out,
        no life lost) and Daffy beside it."""
        self.need("daffy out")
        c, p = self.c, self.p
        a, bugs, taz = self.clone(), p.actor(1), p.actor(2)
        lives = c.u8(LIVES)
        hit(c, bugs)
        self.assertTrue(c.wait_until(lambda: c.u8(HERO_DOWN) & 0x01, 600, 5), "Bugs did not go out")
        self.assertTrue(c.u16(a + 0x6E) & CLIP_PASVISIBLE, "Daffy came back too early")
        self.back_by_package(a, taz)
        self.assertEqual(c.u8(HERO_DOWN) & 0x33, 0, "Bugs is still out")
        self.assertEqual(c.s16(bugs + 0x3C), 6)
        self.assertEqual(c.u8(LIVES), lives, "the package cost a life")
        self.game.shot("6_package")
        self.reached.add("package")

    def test_7_a_package_with_only_daffy_out(self):
        """Only Daffy out: the package brings him back; Bugs and Taz and the
        lives are as they were."""
        self.need("package")
        c, p = self.c, self.p
        a, bugs, taz = self.clone(), p.actor(1), p.actor(2)
        c.wait_until(lambda: not c.u32(a + 0x28) & 0x200, 900, 10)   # the last hit's invincibility
        lives, hb, ht = c.u8(LIVES), c.s16(bugs + 0x3C), c.s16(taz + 0x3C)
        hit(c, a)
        self.assertTrue(c.wait_until(lambda: c.u16(a + 0x6E) & CLIP_PASVISIBLE, 600, 5), "not taken out")
        self.back_by_package(a, taz)
        self.assertEqual(c.u8(HERO_DOWN) & 0x33, 0)
        self.assertEqual((c.s16(bugs + 0x3C), c.s16(taz + 0x3C), c.u8(LIVES)), (hb, ht, lives))


class DaffyComesBack(unittest.TestCase):
    """comeback = "auto" (the default since 2026-11-16): an out Daffy comes
    back 5 s later (150 game frames, counted once per frame) next to a hero
    in play, with no life lost. Three views (split screen), so the actor
    pass runs three times a frame and only view 0's pass may count."""

    @classmethod
    def setUpClass(cls):
        toml = config.out_dir() / "game_3p_daffy_back.toml"
        three_player_toml(config.game_toml(), toml)
        start_run(cls, config.p3_build(), "daffy_comeback",
                  {"p3": {"enabled": True, "values": {"comeback": "auto"}},
                   "bbt.quick-start/quick-start": True, "bbt.own-view/two-views": True}, toml=toml)

    def need(self, stage: str) -> None:
        if stage not in self.reached:
            self.skipTest(f"an earlier stage ({stage}) failed")

    def counters(self) -> dict:
        return {k[len(A3):]: v for k, v in self.c.counters().items() if k.startswith(A3)}

    def clone(self) -> int:
        return self.c.u32(int(self.c.mod_cmd("bbt.p3actor", "show")["state"], 16) + 4)

    def seat2(self, word: int | None) -> None:
        self.c.mod_cmd("bbt.p3", "off" if word is None else f"{word:04X}")

    def out_and_back(self, partner: int) -> int:
        """Daffy dies; returns the frames (vblanks) from out to back, and
        checks he is back next to `partner`, whole and moving."""
        c, p = self.c, self.p
        a = self.clone()
        before = self.counters().get("back_timer", 0)
        lives = c.u8(LIVES)
        hit(c, a)
        self.assertTrue(c.wait_until(lambda: c.u16(a + 0x6E) & CLIP_PASVISIBLE, 600, 1), "not taken out")
        f0 = c.frame()
        self.assertTrue(c.wait_until(lambda: not c.u16(a + 0x6E) & CLIP_PASVISIBLE, 900, 1),
                        "Daffy did not come back by himself")
        took = c.frame() - f0
        c.wait_frames(10)
        self.assertEqual(self.counters().get("back_timer", 0), before + 1)
        self.assertEqual(c.s16(a + 0x3C), 6)
        self.assertFalse(c.u32(a + 0x20) & DEATH)
        self.assertEqual(c.u8(LIVES), lives, "his comeback cost a life")
        self.assertLess(flat(p.pos(a), p.pos(partner)), 400, "not back next to the hero in play")
        x0 = p.pos(a)
        self.seat2(pad("up")); c.wait_frames(40); self.seat2(None); c.wait_frames(10)
        self.assertGreater(moved(x0, p.pos(a)), 32, "seat 2 does not move Daffy again")
        return took

    def test_1_he_comes_back_after_five_seconds(self):
        c, p = self.c, self.p
        self.assertTrue(p.boot_to_language_select(), "no language screen")
        self.assertTrue(p.language_to_title(), "no title")
        self.assertTrue(p.title_to_two_player_game(), "no 2-player Granwich")
        self.assertTrue(c.wait_until(lambda: self.counters().get("spawned", 0) >= 1, 1200, 10), "not spawned")
        for _ in range(10):                               # the opening dialogs
            c.press(pad("cross")); c.wait_frames(150)
        self.assertEqual(c.u8(0x8008DE00), 3, "not three views")
        took = self.out_and_back(p.actor(2))
        print(f"\n    back after {took} vblanks", flush=True)
        # 150 game frames at 0.44-0.5 a vblank (three views; test_game_ownview3 test 3)
        self.assertGreaterEqual(took, 280, "back too soon: counted more than once a frame?")
        self.assertLessEqual(took, 400, "back too late")
        self.reached.add("back")

    def test_2_next_to_bugs_when_taz_is_out(self):
        self.need("back")
        c, p = self.c, self.p
        bugs, taz = p.actor(1), p.actor(2)
        c.wait_until(lambda: not c.u32(self.clone() + 0x28) & 0x200, 900, 10)   # the last hit's invincibility
        hit(c, taz)
        self.assertTrue(c.wait_until(lambda: c.u8(HERO_DOWN) & 0x02, 600, 5), "Taz did not go out")
        self.out_and_back(bugs)
        self.assertEqual(c.u8(HERO_DOWN) & 0x02, 0x02, "Taz came back with Daffy")


class DaffyQuiet(unittest.TestCase):
    """sounds = "quiet": none of Taz's bank, and no attack sound."""

    @classmethod
    def setUpClass(cls):
        toml = config.out_dir() / "game_3p_daffy_quiet.toml"
        three_player_toml(config.game_toml(), toml)
        start_run(cls, config.p3_build(), "daffy_quiet",
                  {"p3": {"enabled": True, "values": {"sounds": "quiet"}},
                   "bbt.quick-start/quick-start": True}, toml=toml)

    def test_1_no_taz_sounds(self):
        c, p = self.c, self.p
        self.assertTrue(p.boot_to_language_select(), "no language screen")
        self.assertTrue(p.language_to_title(), "no title")
        self.assertTrue(p.title_to_two_player_game(), "no 2-player Granwich")
        k = lambda: {x[len(A3):]: v for x, v in c.counters().items() if x.startswith(A3)}
        self.assertTrue(c.wait_until(lambda: k().get("spawned", 0) >= 1, 1200, 10), "not spawned")
        for _ in range(10):
            c.press(pad("cross")); c.wait_frames(150)
        a = c.u32(int(c.mod_cmd("bbt.p3actor", "show")["state"], 16) + 4)
        seat = lambda w: c.mod_cmd("bbt.p3", "off" if w is None else f"{w:04X}")

        def moves():
            for b in ("cross", "square", "cross"):
                seat(pad(b)); c.wait_frames(6); seat(None); c.wait_frames(70)
            seat(pad("right")); c.wait_frames(40); seat(None); c.wait_frames(30)
        k0 = k()
        ids = sound_starts(c, a, moves)
        k1 = k()
        taz = taz_bank(c)
        self.assertEqual([i for i in ids if i in taz or i in BUGS_SOUNDS], [], f"sounds {ids}")
        self.assertGreater(k1.get("sound_muted", 0) - k0.get("sound_muted", 0), 2)
        self.assertEqual(k1.get("attack_sound", 0) - k0.get("attack_sound", 0), 0)


if __name__ == "__main__":
    unittest.main()
