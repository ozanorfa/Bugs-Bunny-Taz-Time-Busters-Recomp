"""Game tier: Phase 4, the third player (feature "p3"), in a 5-seat build
(build-p3, -DBBT_MAX_PLAYERS=5) with a 3-player copy of game.toml (an
SCPH-1070 multitap on port 2, written to the test output).

Input (P4-M0): the multitap does not break the 2-player game (TWO PLAYERS
is selectable and pad 2 still moves Taz through tap slot A), and the
plugin's third input record follows seat 2 in the game's bit order, every
game frame, without leaking into players 1 and 2. Seat 2 is driven with the
plugin's test command (the debug server only drives ports 1 and 2).

Third actor (P4-M1 spawn, P4-M2 control): after the 2-player level load a
copy of Taz drawn as the Transylvania enemy stands next to him; players 1
and 2 stay Bugs and Taz with their own model and animations; its textures
are in the free VRAM strip and it is drawn with them; seat 2 drives it and
only it; its moves land and keep it drawn (the spin attaches a tornado); a
level jump removes it and spawns a new one; a savestate brings it back;
random play on all three seats keeps the game running. Phase 5: a hit on
the third actor brings in its own HUD panel under Bugs' (one full-screen
view).
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
from test_game_stock import moved, start_run  # noqa: E402

moved_by = moved

P = "bbt.third-character.p3."
A = "bbt.third-character.p3-actor."
ACTOR_ERRORS = ("spawn_pool_full", "spawn_bad_list", "tex_upload_failed", "tex_unavailable",
                "donor_unusable", "model_unusable")
PLACED = 0x8008DDCC
MOD_MEMORY = 0x9F000000
RUN = 0x0033                 # Taz's run animation id (NOTES.md "Phase 3 findings")
TAZ_IDLE = range(0x001F, 0x0028)   # Taz's idle chain (NOTES.md "M4")
STANCE_FRAMES = 21           # the enemy's stance 0x001C (NOTES.md "M2")
ROLE_IDS = {0x0001, 0x0033, 0x0034, 0x0036, 0x0037, 0x0039, 0x0072, 0x004F, 0x0097, 0x0073, 0x00AA, 0x00AE}
SCALE_75 = 0x0C00            # default "size" option, 75 %

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


# The enemy, not the default Daffy: these tests check what was verified with it.
ENEMY = {"enabled": True, "values": {"character": "enemy"}}
class ThirdPlayer(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        toml = config.out_dir() / "game_3p.toml"
        three_player_toml(config.game_toml(), toml)
        start_run(cls, config.p3_build(), "p3", {"p3": ENEMY}, toml=toml)

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
        self.assertEqual(c.u32(a + 0x08), TYPE_TAZ, "type word (hero flag back after the pass)")
        self.assertEqual(c.u16(a + 0x34), 3, "identity: Taz's")
        self.assertEqual(c.u32(a + 0x98), int(self.actor_show()["nodes"], 16), "own resource list")
        self.assertNotEqual(a, taz)
        self.assertEqual([c.s16(a + 0xEC + 2 * k) for k in range(3)], [SCALE_75] * 3)
        p1, p2 = self.p.actor(1), self.p.actor(2)
        self.assertEqual((c.u32(p1 + 8), c.u32(p2 + 8)), (TYPE_BUGS, TYPE_TAZ))
        self.assertEqual(p2, taz)

    def check_vram(self) -> None:
        bad = []
        for g in self.groups():
            x, y, w, h = g["block"]
            cx, cy = g["clut"]
            self.assertTrue(512 <= x and x + w <= 1024 and 240 <= y and y + h <= 256, g)
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
            if n > 20:
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
        # Taz's 67 animations (NOTES.md "Phase 3 findings"): 12 by role (hurt
        # and death since Phase 5) and his 9 idle ones on the enemy's stance (at
        # its own timing); 46 hold a pose.
        self.assertEqual((cs.get("anims_mapped"), cs.get("anims_default"), cs.get("anims_native")),
                         (21, 46, 9))
        self.assertEqual(cs.get("hold_ready"), 1)
        show = self.actor_show()
        self.assertEqual((show["prepared"], show["tex"], show["groups"]), (1, 1, 13))
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
        # draw nodes: the enemy's set-up builds 23 (NOTES.md "M2")
        n, p = 0, self.c.u32(a + 0x9C)
        while in_ram(p) and n < 100:
            n, p = n + 1, self.c.u32(p)
        self.assertEqual(n, 23)
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

    def test_08b_idle_plays_whole_loops(self):
        """The "trembling while idle" report: Taz's 2-7 frame idle loops
        played the first frames of the enemy's 21-frame stance back and
        forth. Idle now plays the whole stance at its own speed; moves
        without a role hold one still pose; idle still answers at once."""
        self.need("actor")
        c, a = self.c, self.actor
        img = int(self.actor_show()["image"], 16)
        recs = self.p.anim_records(a)
        idle = [(sub, r, kf) for _, sub, r, kf in recs if sub in TAZ_IDLE]
        self.assertEqual(len(idle), 9)
        stance = {kf for _, _, kf in idle}
        self.assertEqual(len(stance), 1, "idle ids on different donor animations")
        for sub, r, _ in idle:
            self.assertEqual((c.u16(r + 8), c.u8(r + 0x0B)), (STANCE_FRAMES, 2), f"{sub:04X}")
        held = {kf for _, sub, _, kf in recs if sub not in TAZ_IDLE and sub not in ROLE_IDS}
        self.assertEqual(len(held), 1, "moves without a role are not all on the hold")
        self.assertTrue(img <= held.pop() < img + self.actor_show()["image_size"])
        # per frame: an idle pass runs to its last keyframe before the next
        c.send({"cmd": "set_snapshot", "slot": 0, "addr": f"0x{a + 0x148:08X}", "size": 8})
        self.seat2(None)
        c.wait_frames(260)
        end = c.frame() - 2
        seen, wraps_early = [], 0
        last = None
        for fr in range(end - 200, end):
            r = c.send({"cmd": "read_frame_ram", "addr": f"0x{a + 0x148:08X}", "len": 8, "frame": fr})
            nfr, cur, aid, _ = struct.unpack("<4H", bytes.fromhex(r["hex"]))
            if aid in TAZ_IDLE:
                seen.append(aid)
                if last and last[0] == aid and cur < last[1] and last[1] != nfr:
                    wraps_early += 1
                last = (aid, cur)
        self.assertTrue(seen, "the third actor was not idle")
        self.assertEqual(wraps_early, 0, "an idle loop restarted before its last keyframe")
        # still answers at once: moving within 8 frames of a press (Taz: 4-6)
        p0 = self.p.pos(a)
        f0 = c.frame()
        self.seat2(pad("right"))
        moved = c.wait_until(lambda: moved_by(self.p.pos(a), p0) > 2, 30, 1)
        lag = c.frame() - f0
        self.seat2(None)
        c.wait_frames(30)
        self.assertTrue(moved, "no reaction from idle")
        self.assertLessEqual(lag, 12, f"{lag} frames from the press to moving")

    def test_08c_hud_panel_under_bugs(self):
        """Phase 5: one full-screen view, the third player's panel under
        Bugs' (test_game_ownview3.check_p3_panel)."""
        self.need("actor")
        check_p3_panel(self, self.c, self.clone(), P3_SCREEN_FACE, lambda: self.game.shot("08c_hud_panel"))

    def test_09_textures_in_the_strip_and_drawn(self):
        self.need("actor")
        self.check_vram()
        n = self.until_drawn()
        self.game.shot("09_drawn")
        self.assertGreater(n, 20, "the third actor is not drawn with its textures")

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
        """✕ jump, □ spin (tornado), ○, △, ✕ then △ (air spin): each ends
        back on the floor (+0x20 bit 1, PLANCHER) with the body drawn."""
        self.need("actor")
        c, a = self.c, self.actor
        bad, report = [], []
        for name, seq in (("cross", [pad("cross")]), ("square", [pad("square")]),
                          ("circle", [pad("circle")]), ("triangle", [pad("triangle")]),
                          ("cross+triangle", [pad("cross"), None, pad("triangle")])):
            y0 = c.s16(a + 0xFE)
            lo, tornado = y0, 0
            for w in seq:
                if w is None:
                    c.wait_frames(8)
                    continue
                self.seat2(w)
                c.wait_frames(6)
                self.seat2(None)
            for _ in range(60):
                lo = min(lo, c.s16(a + 0xFE))
                tornado = tornado or c.u32(a + 0x8C)
                c.wait_frames(2)
            c.wait_frames(40)
            floor = c.u32(a + 0x20) & 0x3
            drawn = self.drawn_clone_prims()
            report.append(f"{name}: y {y0} min {lo} floor {floor} tornado {tornado:08X} drawn {drawn}")
            if floor != 0x2 or drawn < 20:
                bad.append(name)
            if name.startswith("cross") and y0 - lo < 200:
                bad.append(name + " (no jump)")
            if name in ("square", "cross+triangle") and not tornado:
                bad.append(name + " (no tornado)")
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
            self.assertGreater(n, 20, f"third actor not drawn in level {level}")
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
        self.assertEqual((nodes, hidden), (23, 0))


if __name__ == "__main__":
    unittest.main()
