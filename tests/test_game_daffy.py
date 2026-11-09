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
enemy; a deadly hit takes him out; his attack (Taz's spin, his only
attack) is Bugs' □ attack drawn on him, with no tornado, and still hits;
his moves make Bugs' sounds, or none of Taz's with sounds = "quiet"
(NOTES.md "Daffy's spin and sounds"). Screenshots go to
build-tests/out/daffy/."""
from __future__ import annotations

import struct
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
