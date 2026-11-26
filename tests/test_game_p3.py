"""Game tier: Phase 4, the third player (feature "p3"), in a 5-seat build
(build-p3, -DBBT_MAX_PLAYERS=5) with a 3-player copy of game.toml (an
SCPH-1070 multitap on port 2, written to the test output).

Input (P4-M0): the multitap does not break the 2-player game (TWO PLAYERS
is selectable and pad 2 still moves Taz through tap slot A), and the
plugin's third input record follows seat 2 in the game's bit order, every
game frame, without leaking into players 1 and 2. Seat 2 is driven with the
plugin's test command (the debug server only drives ports 1 and 2).

Third actor (P4-M1 spawn, P4-M2 control): after the 2-player level load a
copy of Taz drawn as Daffy Duck stands next to him; players 1 and 2 stay
Bugs and Taz with their own model and animations; his textures are in free
VRAM at full height (the strip below the frame buffer, and an area below
x = 512 for blocks taller than it: BUGS #25) and he is drawn with them;
seat 2 drives him and only him; his moves land and keep him drawn; a
level jump removes him and spawns a new one; a savestate brings him back;
random play on all three seats keeps the game running. Phase 5: a hit on
the third actor brings in its own HUD panel under Bugs' (one full-screen
view).
Daffy's own moves, attack, sounds and comeback: test_game_daffy.py.
(Until 2026-11-26 this ran with the Transylvania enemy as the third
character; that choice was removed.)
Screenshots go to build-tests/out/p3/."""
from __future__ import annotations

import random
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from bbt import config  # noqa: E402
from bbt.client import fnv1a32_pixels, pad, prim_cluts  # noqa: E402
from bbt.game import LEVEL_GRANNY, LEVEL_VICA01, TYPE_BUGS, TYPE_TAZ, in_ram, three_player_toml  # noqa: E402
from test_game_ownview3 import P3_SCREEN_FACE, check_p3_panel  # noqa: E402
from bbt.p3_vram import STRIP, TALL, inside  # noqa: E402
from test_game_stock import moved, start_run  # noqa: E402

moved_by = moved

P = "bbt.third-character.p3."
A = "bbt.third-character.p3-actor."
ACTOR_ERRORS = ("spawn_pool_full", "spawn_bad_list", "tex_upload_failed", "tex_unavailable",
                "donor_unusable", "model_unusable")
PLACED = 0x8008DDCC
MOD_MEMORY = 0x9F000000
RUN = 0x0033                 # Taz's run animation id (NOTES.md "Phase 3 findings")
SCALE = 0x1000               # Daffy's own size (third_actor.c P3_SCALE)
# Drawn with his textures: primitives using his CLUTs in a frame. Only 5 of
# his model's records are textured (the rest is flat-shaded); 2-5 of them
# are drawn per frame (measured 2026-11-26; the enemy drew ~130).
MIN_DRAWN = 1
CLIP_PASVISIBLE = 0x20       # +0x6E: not drawn this frame
DRAW_NODES = 39              # what his set-up builds (his model's 39 nodes; measured 2026-11-26)

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


class ThirdPlayer(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        toml = config.out_dir() / "game_3p.toml"
        three_player_toml(config.game_toml(), toml)
        start_run(cls, config.p3_build(), "p3", {"p3": True}, toml=toml)

    def need(self, *stages: str) -> None:
        for stage in stages:
            if stage not in self.reached:
                self.skipTest(f"an earlier stage ({stage}) failed")

    # ---- helpers ------------------------------------------------------------
    def record(self) -> dict:
        b = self.c.read(self.rec, 16)
        v = struct.unpack_from("<6H", b, 0)
        return {"edge": v[0], "held": v[1], "raw": v[3], "prev": v[4],
                "sx": struct.unpack_from("<b", b, 12)[0], "sy": struct.unpack_from("<b", b, 13)[0]}

    def seat2(self, word: int | None) -> None:
        self.c.mod_cmd("bbt.p3", "off" if word is None else f"{word:04X}")

    def actor_show(self) -> dict:
        return self.c.mod_cmd("bbt.p3actor", "show")

    def clone(self) -> int:
        return self.c.u32(int(self.actor_show()["state"], 16) + 4)

    def placed(self) -> list[int]:
        out, p = [], self.c.u32(PLACED)
        while in_ram(p) and len(out) < 1024:
            out.append(p)
            p = self.c.u32(p)
        return out

    def groups(self) -> list[dict]:
        return [self.c.mod_cmd("bbt.p3actor", f"group {g}") for g in range(self.actor_show()["groups"])]

    def clone_cluts(self) -> set[int]:
        return {(g["clut"][1] << 6) | (g["clut"][0] >> 4) for g in self.groups()}

    def drawn_clone_prims(self) -> int:
        cl = prim_cluts(self.c.gpu_prims())
        return sum(n for clut, n in cl.items() if clut in self.clone_cluts())

    def counters(self) -> dict[str, int]:
        return {k[len(A):]: v for k, v in self.c.counters().items() if k.startswith(A)}

    def check_actor(self, a: int, taz: int) -> None:
        c = self.c
        self.assertTrue(in_ram(a), f"no third actor: {self.counters()}")
        self.assertIn(a, self.placed(), "third actor not in the placed-actor list")
        # the plugin clears the hero flag for the length of each actor pass;
        # a read can land inside one (seen once in a full-suite run), so the
        # flag must be back within a few frames
        types = []
        for _ in range(4):
            types.append(c.u32(a + 0x08))
            if types[-1] == TYPE_TAZ:
                break
            c.wait_frames(1)
        self.assertEqual(types[-1], TYPE_TAZ, f"type word (hero flag back after the pass): {types}")
        self.assertEqual(c.u16(a + 0x34), 3, "identity: Taz's")
        self.assertEqual(c.u32(a + 0x98), int(self.actor_show()["nodes"], 16), "own resource list")
        self.assertNotEqual(a, taz)
        self.assertEqual([c.s16(a + 0xEC + 2 * k) for k in range(3)], [SCALE] * 3)
        p1, p2 = self.p.actor(1), self.p.actor(2)
        self.assertEqual((c.u32(p1 + 8), c.u32(p2 + 8)), (TYPE_BUGS, TYPE_TAZ))
        self.assertEqual(p2, taz)

    def check_vram(self) -> None:
        """Every block in the strip or the tall area at full height (BUGS
        #25), every CLUT in the strip, and VRAM holds what the plan says."""
        bad = []
        for g in self.groups():
            x, y, w, h = g["block"]
            cx, cy = g["clut"]
            self.assertTrue(inside(x, y, w, h, STRIP) or inside(x, y, w, h, TALL), g)
            self.assertEqual(g["vshift"], 0, g)
            self.assertTrue(inside(cx, cy, 16, 1, STRIP), g)
            if f"{fnv1a32_pixels(self.c.vram(x, y, w, h)):08X}" != g["block_fnv"]:
                bad.append(("block", g["group"]))
            if f"{fnv1a32_pixels(self.c.vram(cx, cy, 16, 1)):08X}" != g["clut_fnv"]:
                bad.append(("clut", g["group"]))
        self.assertEqual(bad, [])

    def until_drawn(self, tries: int = 8) -> int:
        """Past a level's opening flyover: the characters on screen."""
        n = 0
        for _ in range(tries):
            n = self.drawn_clone_prims()
            if n >= MIN_DRAWN:
                break
            self.c.press(pad("cross"))
            self.c.wait_frames(150)
        return n

    # ---- input (P4-M0) -------------------------------------------------------
    def test_01_record_allocated(self):
        show = self.c.mod_cmd("bbt.p3", "show")
        self.assertEqual(show["seat"], 2)
        self.rec = int(show["record"], 16)
        self.assertGreaterEqual(self.rec, MOD_MEMORY, "record not in mod memory")
        type(self).rec = self.rec
        self.reached.add("record")

    def test_02_two_players_with_the_multitap(self):
        p = self.p
        ok = p.boot_to_language_select() and p.language_to_title() and p.title_to_two_player_game()
        self.game.shot("02_granwich")
        self.assertTrue(ok, f"level {p.level()}: TWO PLAYERS not reachable with the multitap?")
        self.assertTrue(p.until_control(), "pad 2 (tap slot A) does not move Taz")
        self.reached.add("granwich")

    def test_03_no_third_pad_means_no_input(self):
        self.need("record")
        self.seat2(None)
        self.c.wait_frames(6)
        r = self.record()
        self.assertEqual((r["held"], r["raw"], r["sx"], r["sy"]), (0, 0, 0, 0), r)

    def test_04_record_follows_seat_2(self):
        self.need("record")
        bad = []
        for word, held, sx, sy in CASES:
            self.seat2(word)
            self.c.wait_frames(6)
            r = self.record()
            if (r["held"], r["raw"], r["prev"], r["sx"], r["sy"]) != (held, held, held, sx, sy):
                bad.append(f"{word:04X}: expected held {held:04X} stick {sx},{sy}, got {r}")
            self.seat2(None)
            self.c.wait_frames(6)
        self.assertEqual(bad, [])
        r = self.record()
        self.assertEqual((r["held"], r["sx"], r["sy"]), (0, 0, 0))

    def test_05_polled_once_per_game_frame(self):
        self.need("record")
        before = self.c.counters().get(P + "polls", 0)
        f0 = self.c.frame()
        self.c.wait_frames(120)
        n = self.c.counters().get(P + "polls", 0) - before
        frames = self.c.frame() - f0
        self.assertGreater(n, frames // 4, f"{n} polls in {frames} frames")
        self.assertLessEqual(n, frames + 2, f"{n} polls in {frames} frames")

    def test_06_seat_2_does_not_drive_players_1_and_2(self):
        """Seat 2 now moves the third actor, which may bump into the others,
        so: neither runs and neither is driven far. (Player 2's input slot
        itself cannot be sampled: the debug server reads while the game
        runs, and during the third actor's own update the slot holds seat
        2 on purpose; player 2 is updated before it, with its own.)"""
        self.need("granwich")
        c = self.c
        bugs, taz = self.p.actor(1), self.p.actor(2)
        b0, t0 = self.p.pos(bugs), self.p.pos(taz)
        # their run records, looked up before (a sample must be quick: the
        # game keeps running while the test reads)
        run = {who: {r for _, sub, r, _ in self.p.anim_records(who) if sub == RUN} for who in (bugs, taz)}
        self.assertTrue(run[bugs] and run[taz])
        runs = set()
        self.seat2(pad("right"))
        end = c.frame() + 40
        while c.frame() < end:
            for who, name in ((bugs, "bugs"), (taz, "taz")):
                if c.u32(who + 0x158) in run[who]:
                    runs.add(name)
        self.seat2(None)
        c.wait_frames(10)
        self.assertEqual(runs, set(), "seat 2 made player 1 or 2 run")
        self.assertLess(moved(b0, self.p.pos(bugs)), 120, "Bugs moved far")
        self.assertLess(moved(t0, self.p.pos(taz)), 120, "Taz moved far")

    # ---- third actor (P4-M1 / M2) ------------------------------------------
    def test_07_third_actor_spawned_next_to_taz(self):
        self.need("granwich")
        cs = self.counters()
        self.assertEqual({k: cs.get(k, 0) for k in ACTOR_ERRORS}, {k: 0 for k in ACTOR_ERRORS})
        self.assertEqual((cs.get("spawned"), cs.get("tex_uploaded"), cs.get("models")), (1, 1, 1))
        # Taz's 67 animations (NOTES.md "Phase 3 findings"), each mapped to
        # one of Daffy's (Bugs' moves fitted to him) or held on his still pose
        print(f"\n    anims mapped {cs.get('anims_mapped')} default {cs.get('anims_default')} "
              f"native {cs.get('anims_native')}")
        self.assertEqual(cs.get("anims_mapped", 0) + cs.get("anims_default", 0) + cs.get("anims_native", 0), 67)
        self.assertGreaterEqual(cs.get("anims_mapped", 0), 40)
        self.assertEqual(cs.get("hold_ready"), 1)
        show = self.actor_show()
        self.assertEqual((show["prepared"], show["tex"], show["groups"]), (1, 1, 3))
        a, taz = self.clone(), self.p.actor(2)
        self.check_actor(a, taz)
        # copied from the placed Taz untouched (no draw nodes, not initialised yet)
        src = int(show["source_copy"], 16)
        st = int(show["state"], 16)
        self.assertEqual(self.c.u32(st + 0x0C), taz)
        self.assertEqual((self.c.u32(src + 0x9C), self.c.u32(src + 0x24) & 0x8000), (0, 0))
        # placed 160 units (largest axis) from where Taz stood, away from Bugs
        sx, _, sz = struct.unpack("<3h", self.c.read(src + 0xFC, 6))
        xz = self.c.u32(st + 0x18)
        px, pz = struct.unpack("<2h", struct.pack("<I", xz))
        self.assertEqual(max(abs(px - sx), abs(pz - sz)), 160, (px, pz, sx, sz))
        # draw nodes: his set-up builds them
        n, p = 0, self.c.u32(a + 0x9C)
        while in_ram(p) and n < 100:
            n, p = n + 1, self.c.u32(p)
        self.assertEqual(n, DRAW_NODES)
        self.game.shot("07_three_characters")
        type(self).actor = a
        self.reached.add("actor")

    def test_08_everyone_keeps_their_own_resources(self):
        self.need("actor")
        c, a = self.c, self.actor
        bugs, taz = self.p.actor(1), self.p.actor(2)
        img = int(self.actor_show()["image"], 16)
        size = self.actor_show()["image_size"]
        mine = self.p.anim_records(a)
        self.assertEqual(len(mine), 67)
        self.assertTrue(all(img <= kf < img + size for _, _, _, kf in mine), "clone keyframes outside the donor image")
        self.assertTrue(img <= c.u32(a + 0x15C) < img + size, "clone model not the donor's")
        for who in (bugs, taz):
            recs = self.p.anim_records(who)
            self.assertTrue(recs)
            self.assertTrue(all(in_ram(kf) for _, _, _, kf in recs), "a player plays donor data")
            self.assertTrue(in_ram(c.u32(who + 0x15C)), "a player's model moved to mod memory")
            self.assertNotEqual(c.u32(who + 0x98), c.u32(a + 0x98))
        # Taz's ids are all there, so his script finds every animation it asks for
        self.assertEqual(sorted(s for _, s, _, _ in mine), sorted(s for _, s, _, _ in self.p.anim_records(taz)))

    def test_08c_hud_panel_under_bugs(self):
        """Phase 5: one full-screen view, the third player's panel under
        Bugs' (test_game_ownview3.check_p3_panel)."""
        self.need("actor")
        check_p3_panel(self, self.c, self.clone(), P3_SCREEN_FACE, lambda: self.game.shot("08c_hud_panel"))

    def test_09_textures_in_free_vram_and_drawn(self):
        self.need("actor")
        self.check_vram()
        n = self.until_drawn()
        self.game.shot("09_drawn")
        self.assertGreaterEqual(n, MIN_DRAWN, "the third actor is not drawn with its textures")
        self.assertFalse(self.c.u16(self.actor + 0x6E) & CLIP_PASVISIBLE, "the third actor is not drawn")

    def test_10_seat_2_drives_only_the_third_actor(self):
        self.need("actor")
        c, a = self.c, self.actor
        bugs, taz = self.p.actor(1), self.p.actor(2)
        for word, mover in ((pad("right"), "clone"), (pad("left"), "clone")):
            p0 = (self.p.pos(a), self.p.pos(taz), self.p.pos(bugs))
            self.seat2(word)
            c.wait_frames(45)
            self.seat2(None)
            c.wait_frames(20)
            d = [moved(p0[i], self.p.pos(x)) for i, x in enumerate((a, taz, bugs))]
            self.assertGreater(d[0], 300, f"seat 2 did not move the third actor: {d}")
            self.assertLess(max(d[1], d[2]), 120, f"seat 2 moved a player: {d}")
        p0 = (self.p.pos(a), self.p.pos(taz))
        c.hold(pad("right"), port=2)
        c.wait_frames(45)
        c.release(port=2)
        c.wait_frames(20)
        self.assertGreater(moved(p0[1], self.p.pos(taz)), 300, "pad 2 no longer moves Taz")
        self.assertLess(moved(p0[0], self.p.pos(a)), 120, "pad 2 moved the third actor")

    def test_11_moves_land_and_stay_visible(self):
        """✕ jump, □ attack, ○, △, ✕ then △: each ends back on the floor
        (+0x20 bit 1, PLANCHER) with the body drawn. (Daffy's attack is
        Bugs', without Taz's tornado: test_game_daffy test 3b.)"""
        self.need("actor")
        c, a = self.c, self.actor
        bad, report = [], []
        for name, seq in (("cross", [pad("cross")]), ("square", [pad("square")]),
                          ("circle", [pad("circle")]), ("triangle", [pad("triangle")]),
                          ("cross+triangle", [pad("cross"), None, pad("triangle")])):
            y0 = c.s16(a + 0xFE)
            lo = y0
            for w in seq:
                if w is None:
                    c.wait_frames(8)
                    continue
                self.seat2(w)
                c.wait_frames(6)
                self.seat2(None)
            for _ in range(60):
                lo = min(lo, c.s16(a + 0xFE))
                c.wait_frames(2)
            c.wait_frames(40)
            floor = c.u32(a + 0x20) & 0x3
            drawn = self.drawn_clone_prims()
            hidden = c.u16(a + 0x6E) & CLIP_PASVISIBLE
            report.append(f"{name}: y {y0} min {lo} floor {floor} drawn {drawn} hidden {hidden}")
            if floor != 0x2 or drawn < MIN_DRAWN or hidden:
                bad.append(name)
            if name.startswith("cross") and y0 - lo < 200:
                bad.append(name + " (no jump)")
        print("\n    " + "\n    ".join(report))
        self.assertEqual(bad, [])

    def test_12_level_jump_spawns_a_new_one(self):
        self.need("actor")
        p = self.p
        for level in (LEVEL_VICA01, LEVEL_GRANNY):
            before = self.counters()
            self.assertTrue(p.jump_to_level(level, (A + "spawned",)), f"no third actor after the jump to {level}")
            after = self.counters()
            self.assertEqual(after.get("gone", 0) - before.get("gone", 0), 1)
            self.assertEqual(after.get("spawned", 0) - before.get("spawned", 0), 1)
            self.assertEqual(after.get("tex_uploaded", 0) - before.get("tex_uploaded", 0), 1)
            self.check_actor(self.clone(), p.actor(2))
            self.check_vram()
            n = self.until_drawn()
            self.game.shot(f"12_level_{level}")
            self.assertGreaterEqual(n, MIN_DRAWN, f"third actor not drawn in level {level}")
        type(self).actor = self.clone()

    def test_13_savestate_round_trip(self):
        self.need("actor")
        c, a = self.c, self.actor
        self.assertTrue(c.send({"cmd": "savestate", "op": "save", "slot": 3}).get("ok"))
        c.wait_frames(30)
        self.assertEqual(c.send({"cmd": "savestate_status"}).get("last_ok"), 1)
        saved = self.p.pos(a)
        self.seat2(pad("left"))
        c.wait_frames(45)
        self.seat2(None)
        c.wait_frames(10)
        self.assertGreater(moved(saved, self.p.pos(a)), 100)
        self.assertTrue(c.send({"cmd": "savestate", "op": "load", "slot": 3}).get("ok"))
        c.wait_frames(30)
        self.assertEqual(c.send({"cmd": "savestate_status"}).get("last_op"), "load")
        self.assertEqual(self.clone(), a)
        self.assertLess(moved(saved, self.p.pos(a)), 40, "the loaded state did not bring it back")
        self.check_actor(a, self.p.actor(2))
        p0 = self.p.pos(a)
        self.seat2(pad("right"))
        c.wait_frames(45)
        self.seat2(None)
        c.wait_frames(10)
        self.assertGreater(moved(p0, self.p.pos(a)), 300, "no control after loading the state")

    def test_14_random_play_on_three_seats(self):
        self.need("actor")
        c = self.c
        rnd = random.Random(1107)
        moves = ["cross", "square", "circle", "triangle", "left", "right", "up", "down", None]
        logic = lambda: c.u32(0x8008DE88)   # game logic ticks (FUN_80011AF0, once per view)
        problems = []
        for i in range(40):
            w = [pad(m) if m else 0xFFFF for m in (rnd.choice(moves) for _ in range(3))]
            c.hold(w[0])
            c.hold(w[1], port=2)
            self.seat2(w[2])
            before = logic()
            c.wait_frames(rnd.randint(10, 70))
            if logic() == before:
                problems.append(f"step {i}: logic stopped")
            if self.p.actor(2) != 0 and c.u32(self.p.actor(2) + 8) != TYPE_TAZ:
                problems.append(f"step {i}: player 2 is not Taz")
        c.release()
        c.release(port=2)
        self.seat2(None)
        c.wait_frames(60)
        a = self.clone()
        self.game.shot("14_after_random_play")
        self.assertEqual(problems, [])
        self.assertEqual(a, self.actor, "the third actor was replaced or lost")
        nodes, hidden, p = 0, 0, c.u32(a + 0x9C)
        while in_ram(p) and nodes < 100:
            nodes, hidden, p = nodes + 1, hidden + bool(c.u16(p + 6) & 0x8000), c.u32(p)
        self.assertEqual((nodes, hidden), (DRAW_NODES, 0))


if __name__ == "__main__":
    unittest.main()
