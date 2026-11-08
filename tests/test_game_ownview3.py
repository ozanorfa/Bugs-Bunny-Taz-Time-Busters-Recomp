"""Game tier: three views (bbt.own-view "two-views" with the third player,
bbt.third-character "p3", and Quick start) in the 5-seat build with debug
tools (build-p3), with a 3-player copy of game.toml (multitap on port 2).

With the third actor in a 2-player level the plugin raises the view count to
3: view 2 follows the third actor (0x80093C58[2]), every camera is a
single-player camera (record flags +0xE0: 0x80 set, 0x100 clear) looking at
its own actor, seat 2 (driven with the p3 plugin's test command) turns the
third actor to camera 2's forward direction, the game keeps running at
close to its stock rate (game.toml [timing], NOTES.md "Third player's own
view"), and the third actor's own sounds keep the master volume far from
Bugs' and Taz's cameras (NOTES.md "Sound and HUD per player"). The third
player's death follows the game's rule for Bugs and Taz: it is out (not
updated, not drawn) without touching Taz or the lives, until Bugs and Taz
are both down and the team respawns, a life lost; then it is back next to
Taz with full health (NOTES.md "The third player's death"). A death is
started by a hit put in RAM (+0x66 damage 6, +0x24 bit 0x40000000, one of
Actor_Update's hit conditions). Enemies go for the third player too: in
TRCA01 a Transylvania enemy (model 635) ignores it while the codes 4-7 rule
is switched off (debug command "ref-third off") and chases and hits it once
it is on (NOTES.md "Pick-ups and enemies for the third player"). A hit on
the third actor brings in its own HUD panel (sprites 15 / 16), at the top
centre of each 72-line band, with the health it has left (NOTES.md "The
third player's HUD panel"). The two plugins that need Actor_Update share one override
(bbt.shared/actor-update, activated implicitly). Screenshots go to
build-tests/out/ownview3/."""
from __future__ import annotations

import math
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from bbt import config  # noqa: E402
from bbt.client import pad  # noqa: E402
from bbt.game import LEVEL_GRANNY, LEVEL_TRCA01, three_player_toml  # noqa: E402
from test_game_stock import moved, start_run  # noqa: E402

V = "bbt.own-view.two-views."
A3 = "bbt.third-character.p3-actor."
VIEW_COUNT, FOLLOWED = 0x8008DE00, 0x80093C58
CAM_RECORD, CAM_RECORD_BYTES = 0x800964C4, 0x2C0
CAM_FOLLOW_ONE, CAM_FRAME_BOTH = 0x80, 0x100
VBLANKS, VIEW_PASSES = 0x8008DE20, 0x8008DE88
MAX_TURN = 30
LISTENER = 0x2C                          # camera record offset of the sound listener
SOUND_SLOTS, SOUND_SLOT_BYTES, SOUND_SLOT_COUNT = 0x80093274, 0x1C, 24
SOUND_MASTER, SOUND_RANGE = 0x8008DBD4, 0x1000
HERO_DOWN, LIVES = 0x800100A2, 0x800100A0  # 0x10 / 0x20 Bugs / Taz died, 1 / 2 out; lives
DEATH, CLIP_PASVISIBLE, FULL_HEALTH = 0x2000000, 0x20, 6
TRCA01_ENEMY_MODEL = 635


def camera(c, view: int):
    r = CAM_RECORD + view * CAM_RECORD_BYTES
    return (struct.unpack("<3i", c.read(r + 4, 12)), struct.unpack("<3i", c.read(r + 0x10, 12)),
            c.u32(r + 0xE0))


def listener(c, view: int):
    return struct.unpack("<3i", c.read(CAM_RECORD + view * CAM_RECORD_BYTES + LISTENER, 12))


def dist(a, b) -> int:
    return round(math.dist(a, b))


def sound_volumes(c, actor: int) -> set[tuple[int, int]]:
    """(left, right) of the playing sound slots (+0 flags, +4 actor, +0xC volume) of an actor."""
    raw = c.read(SOUND_SLOTS, SOUND_SLOT_COUNT * SOUND_SLOT_BYTES)
    out = set()
    for i in range(SOUND_SLOT_COUNT):
        flags, = struct.unpack_from("<H", raw, i * SOUND_SLOT_BYTES)
        who, = struct.unpack_from("<I", raw, i * SOUND_SLOT_BYTES + 4)
        vol, = struct.unpack_from("<H", raw, i * SOUND_SLOT_BYTES + 0xC)
        if flags & 0x8000 and who == actor:
            out.add((vol & 0xFF, vol >> 8))
    return out


def hit(c, actor: int, damage: int = 6) -> None:
    """A hit as Actor_Update applies it: +0x66 damage, +0x24 bit 0x40000000."""
    w = c.u32(actor + 0x64)
    c.write_u32(actor + 0x64, (w & 0xFFFF) | (damage << 16))
    c.write_u32(actor + 0x24, c.u32(actor + 0x24) | 0x40000000)


def listed(c) -> list[int]:
    """View 0's update / draw list (0x800970FC[0], 0-terminated). The actor
    pass starts a list by writing 0, so a read can land on an empty one:
    read again (player 1 heads every non-empty list)."""
    for _ in range(10):
        lst, out = c.u32(0x800970FC), []
        for i in range(64):
            a = c.u32(lst + 4 * i)
            if a == 0:
                break
            out.append(a)
        if out:
            return out
        c.wait_frames(1)
    return []


def placed(c) -> list[int]:
    """The placed-actor list (0x8008DDCC, +0 next)."""
    out, a = [], c.u32(0x8008DDCC)
    while 0x80000000 <= a < 0x80200000 and len(out) < 600:
        out.append(a)
        a = c.u32(a)
    return out


def model_id(c, actor: int) -> int | None:
    """Resource id of an actor's model: the first node of its list +0x98 ({next, ?, entry}), entry +6."""
    node = c.u32(actor + 0x98)
    if not 0x80000000 <= node < 0x80200000:
        return None
    entry = c.u32(node + 8)
    return c.u16(entry + 6) if 0x80000000 <= entry < 0x80200000 else None


def teleport(c, actor: int, pos) -> None:
    x, y, z = pos
    c.write_u32(actor + 0xFC, (x & 0xFFFF) | ((y & 0xFFFF) << 16))
    w = c.u32(actor + 0x100)
    c.write_u32(actor + 0x100, (w & 0xFFFF0000) | (z & 0xFFFF))
    c.write_u32(actor + 0x70, 0)


HUD_SPRITES = 0x800929F8                  # the HUD actor's 32 sprite nodes (+6 bit 15 hidden)
H3 = "bbt.third-character.p3-hud."
P3_BAR_DX, P3_BAR_DY, P3_BAR_W = 33, 24, 80   # the panel's bar from its face; full width
P3_BAND_FACE = (-58, -104)                # three bands: top centre
P3_SCREEN_FACE = (-244, -55)              # a full screen: under Bugs' panel


def hud_sprite(c, i: int):
    """(hidden, (x0, y0), width) of HUD sprite i (a quad: xy0 +8, xy1 +0x10)."""
    r = c.read(c.u32(HUD_SPRITES + 4 * i), 0x28)
    x0, y0 = struct.unpack_from("<hh", r, 8)
    x1, = struct.unpack_from("<h", r, 0x10)
    return bool(struct.unpack_from("<H", r, 6)[0] & 0x8000), (x0, y0), x1 - x0


def check_p3_panel(t: unittest.TestCase, c, clone: int, face_xy, shot=None) -> None:
    """A real hit (2 damage) on the third actor: its panel (NOTES.md "The
    third player's HUD panel") slides in at face_xy, its bar blinks while
    the hit's invincibility runs and ends at health * 80 / 6, Bugs' panel
    stays as it was, the panel slides out again, and the invincibility
    (+0x28 0x200) ends (it never did: BUGS.md #39)."""
    hud = c.mod_cmd("bbt.p3hud", "show")
    bar, face, blk = hud["bar"], hud["face"], int(hud["block"], 16)
    t.assertTrue(c.wait_until(lambda: hud_sprite(c, face)[0] and hud_sprite(c, bar)[0], 900, 10),
                 "the third player's panel shows with nothing changing")
    bugs_face = hud_sprite(c, 1)[0]
    health = c.s16(clone + 0x3C)
    hit(c, clone, 2)
    t.assertTrue(c.wait_until(lambda: c.s16(clone + 0x3C) == health - 2, 120, 1), "the hit did not land")
    # the hit flash, as Taz's: the bar blinks while the face shows
    seen = set()
    for _ in range(40):
        if not hud_sprite(c, face)[0]:
            seen.add(hud_sprite(c, bar)[0])
        c.wait_frames(2)
    t.assertEqual(seen, {True, False}, "the bar did not blink after the hit")
    want = (health - 2) * P3_BAR_W // FULL_HEALTH
    # the bar shrinks a pixel a game frame (descriptor +2 = width shown)
    t.assertTrue(c.wait_until(lambda: c.s16(blk + 0x12) == want, 300, 2), f"bar width {c.s16(blk + 0x12)}, want {want}")
    t.assertTrue(c.wait_until(lambda: hud_sprite(c, face)[1] == tuple(face_xy) and not hud_sprite(c, face)[0]
                              and not hud_sprite(c, bar)[0], 120, 2),
                 f"face at {hud_sprite(c, face)[1]} (hidden {hud_sprite(c, face)[0]}), want {face_xy}")
    _, xy, width = hud_sprite(c, bar)
    t.assertEqual(xy, (face_xy[0] + P3_BAR_DX, face_xy[1] + P3_BAR_DY))
    t.assertEqual(width, want)
    t.assertEqual(hud_sprite(c, 1)[0], bugs_face, "Bugs' panel changed")
    if shot:
        shot()
    t.assertTrue(c.wait_until(lambda: hud_sprite(c, face)[0] and hud_sprite(c, bar)[0], 600, 5),
                 "the panel did not slide out")
    t.assertGreater(c.counters().get(H3 + "update", 0), 0)
    # the hit's invincibility (+0x28 0x200) is over before the next test hits
    t.assertTrue(c.wait_until(lambda: not c.u32(clone + 0x28) & 0x200, 900, 10), "still invincible")


def flat(a, b) -> int:
    return round(math.hypot(a[0] - b[0], a[2] - b[2]))


def heading(dx: int, dz: int) -> float:
    return math.degrees(math.atan2(dx, dz)) % 360


def turn(a: float, b: float) -> float:
    return abs((a - b + 180) % 360 - 180)


class ThreeViews(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        toml = config.out_dir() / "game_3p_ownview3.toml"
        three_player_toml(config.game_toml(), toml)
        start_run(cls, config.p3_build(), "ownview3",
                  {"p3": True, "bbt.quick-start/quick-start": True, "bbt.own-view/two-views": True},
                  toml=toml)

    def need(self, stage: str) -> None:
        if stage not in self.reached:
            self.skipTest(f"an earlier stage ({stage}) failed")

    def clone(self) -> int:
        return self.c.u32(int(self.c.mod_cmd("bbt.p3actor", "show")["state"], 16) + 4)

    def seat2(self, word: int | None) -> None:
        self.c.mod_cmd("bbt.p3", "off" if word is None else f"{word:04X}")

    def test_1_three_views_follow_the_three_players(self):
        c, p = self.c, self.p
        self.assertTrue(c.wait_until(lambda: p.level() == 2, 3000, 2), "no language screen")
        c.wait_frames(200)
        self.assertTrue(p.language_to_title(), "no title")
        c.wait_frames(240); c.press(pad("start")); c.wait_frames(120)
        c.press(pad("cross")); c.wait_frames(60); c.release(port=2); c.wait_frames(30)
        c.press(pad("down")); c.wait_frames(30); c.press(pad("cross")); c.wait_frames(150)
        c.press(pad("cross"))
        self.assertTrue(c.wait_until(lambda: p.level() == LEVEL_GRANNY and p.players_ready(), 3000, 2))
        self.assertTrue(c.wait_until(lambda: c.counters().get(V + "views_3"), 900, 5), "no third view")
        self.assertIn("bbt.shared/actor-update activated implicitly", self.game.log_tail(400))
        for _ in range(10):                               # the opening dialogs
            c.press(pad("cross")); c.wait_frames(150)
        self.assertTrue(c.wait_until(lambda: c.u8(VIEW_COUNT) == 3, 60, 1), "the view count fell back")
        actors = [p.actor(1), p.actor(2), self.clone()]
        followed = list(struct.unpack("<3I", c.read(FOLLOWED, 12)))
        self.assertEqual(followed, actors, "views do not follow Bugs, Taz and the third actor")
        for v, own in enumerate(actors):
            _, ref, flags = camera(c, v)
            with self.subTest(view=v):
                self.assertEqual(flags & (CAM_FOLLOW_ONE | CAM_FRAME_BOTH), CAM_FOLLOW_ONE)
                self.assertLess(flat(ref, p.pos(own)), 600, f"camera {v} does not look at its actor")
        self.game.shot("1_three_views")
        self.reached.add("three views")

    def test_2_seat_2_steers_by_camera_2(self):
        self.need("three views")
        c, p = self.c, self.p
        clone = self.clone()
        self.seat2(pad("right")); c.wait_frames(40); self.seat2(None); c.wait_frames(120)
        vp, ref, _ = camera(c, 2)
        fwd = heading(ref[0] - vp[0], ref[2] - vp[2])
        x0 = p.pos(clone)
        self.seat2(pad("up")); c.wait_frames(40)
        facing = (c.u16(clone + 0xF6) & 0xFFF) * 360 / 4096
        self.seat2(None); c.wait_frames(20)
        self.assertGreater(moved(x0, p.pos(clone)), 32, "seat 2 did not move the third actor")
        self.assertLess(turn(facing, fwd), MAX_TURN, f"the third actor faces {facing:.0f}, camera 2 {fwd:.0f}")

    def test_3_three_views_keep_the_game_speed(self):
        self.need("three views")
        c = self.c
        c.hold(pad("up"), port=1); c.hold(pad("up"), port=2); self.seat2(pad("up"))
        c.wait_frames(30)
        v0, f0 = c.u32(VBLANKS), c.u32(VIEW_PASSES)
        c.wait_frames(240)
        v1, f1 = c.u32(VBLANKS), c.u32(VIEW_PASSES)
        c.release(port=1); c.release(port=2); self.seat2(None)
        rate = (f1 - f0) / 3 / max(v1 - v0, 1)
        self.assertGreaterEqual(rate, 0.44, f"{rate:.3f} frames per vblank (stock: 0.5)")
        self.assertNotIn("FATAL", self.game.log_tail(200))
        self.game.shot("3_running")

    def test_4_the_third_player_is_heard_far_from_the_others(self):
        self.need("three views")
        c, p = self.c, self.p
        clone = self.clone()

        def far() -> int:
            return min(dist(p.pos(clone), listener(c, v)) for v in (0, 1))

        for direction in ("down", "left", "up", "right", "down"):
            if far() > SOUND_RANGE:
                break
            self.seat2(pad(direction)); c.wait_frames(240); self.seat2(None); c.wait_frames(10)
        self.assertGreater(far(), SOUND_RANGE, "the third actor did not get away from cameras 0 and 1")
        self.assertLess(dist(p.pos(clone), listener(c, 2)), SOUND_RANGE, "camera 2 is not near its actor")
        heard = set()
        for _ in range(8):                                 # jumps
            self.seat2(pad("cross")); c.wait_frames(3)
            heard |= sound_volumes(c, clone)
            self.seat2(None); c.wait_frames(12)
            heard |= sound_volumes(c, clone)
        master = c.u8(SOUND_MASTER)
        self.assertTrue(heard, "no sound of the third actor seen")
        # the master byte moves by a step over time (seen: 127 / 128)
        self.assertTrue(all(l == r and abs(l - master) <= 2 for l, r in heard),
                        f"volumes {sorted(heard)}, master {master}")
        self.assertGreater(c.counters().get(V + "sound_player_3", 0), 0)

    def test_4b_the_third_player_has_a_hud_panel(self):
        self.need("three views")
        check_p3_panel(self, self.c, self.clone(), P3_BAND_FACE, lambda: self.game.shot("4b_hud_panel"))

    def test_5_the_third_player_dies_on_its_own(self):
        self.need("three views")
        c, p = self.c, self.p
        clone, taz = self.clone(), p.actor(2)
        lives = c.u8(LIVES)
        self.assertEqual(c.u8(HERO_DOWN) & 0x33, 0, "someone is already down")
        hit(c, clone)
        self.assertTrue(c.wait_until(lambda: c.u16(clone + 0x6E) & CLIP_PASVISIBLE, 600, 5),
                        "the dead third actor did not go out")
        c.wait_frames(240)                                 # long enough for more death steps
        self.assertTrue(c.u32(clone + 0x20) & DEATH)
        self.assertNotIn(clone, listed(c), "an out third actor is still updated / drawn")
        self.assertEqual(c.u8(HERO_DOWN) & 0x33, 0, "the third actor's death marked a hero down")
        self.assertEqual(c.u8(LIVES), lives, "the third actor's death cost a life")
        self.assertEqual(c.u8(0x800100A6), c.s16(taz + 0x3C), "Taz's kept health is not his")
        self.assertIn(taz, listed(c), "Taz is no longer updated")
        x0 = p.pos(taz)
        c.hold(pad("up"), port=2); c.wait_frames(40); c.release(port=2); c.wait_frames(10)
        self.assertGreater(moved(x0, p.pos(taz)), 32, "Taz does not move after the third actor's death")
        self.reached.add("third out")

    def test_6_the_team_respawn_brings_it_back(self):
        self.need("third out")
        c, p = self.c, self.p
        clone, bugs, taz = self.clone(), p.actor(1), p.actor(2)
        lives = c.u8(LIVES)
        hit(c, bugs)
        self.assertTrue(c.wait_until(lambda: c.u8(HERO_DOWN) & 0x01, 600, 5), "Bugs did not go out")
        self.assertTrue(c.u16(clone + 0x6E) & CLIP_PASVISIBLE, "the third actor came back too early")
        hit(c, taz)
        self.assertTrue(c.wait_until(lambda: c.u8(LIVES) == lives - 1, 900, 5), "no team respawn")
        self.assertTrue(c.wait_until(lambda: not c.u16(clone + 0x6E) & CLIP_PASVISIBLE, 60, 1),
                        "the third actor did not come back")
        c.wait_frames(60)
        self.assertEqual(c.s16(clone + 0x3C), FULL_HEALTH)
        self.assertFalse(c.u32(clone + 0x20) & DEATH)
        self.assertIn(clone, listed(c))
        self.assertLess(flat(p.pos(clone), p.pos(taz)), 600, "the third actor is not back next to Taz")
        x0 = p.pos(clone)
        self.seat2(pad("up")); c.wait_frames(40); self.seat2(None); c.wait_frames(10)
        self.assertGreater(moved(x0, p.pos(clone)), 32, "seat 2 does not move the third actor again")
        self.game.shot("6_back")
        self.reached.add("third back")



class EnemiesSeeTheThirdPlayer(unittest.TestCase):
    """A run of its own: TRCA01 straight after Granwich, nobody out."""

    @classmethod
    def setUpClass(cls):
        toml = config.out_dir() / "game_3p_ownview3_enemy.toml"
        three_player_toml(config.game_toml(), toml)
        start_run(cls, config.p3_build(), "ownview3_enemy",
                  {"p3": True, "bbt.quick-start/quick-start": True, "bbt.own-view/two-views": True},
                  toml=toml)

    def clone(self) -> int:
        return self.c.u32(int(self.c.mod_cmd("bbt.p3actor", "show")["state"], 16) + 4)

    def test_1_an_enemy_goes_for_the_third_player(self):
        c, p = self.c, self.p
        self.assertTrue(c.wait_until(lambda: p.level() == 2, 3000, 2), "no language screen")
        c.wait_frames(200)
        self.assertTrue(p.language_to_title(), "no title")
        c.wait_frames(240); c.press(pad("start")); c.wait_frames(120)
        c.press(pad("cross")); c.wait_frames(60); c.release(port=2); c.wait_frames(30)
        c.press(pad("down")); c.wait_frames(30); c.press(pad("cross")); c.wait_frames(150)
        c.press(pad("cross"))
        self.assertTrue(c.wait_until(lambda: p.level() == LEVEL_GRANNY and p.players_ready(), 3000, 2))
        for _ in range(10):                               # the opening dialogs
            c.press(pad("cross")); c.wait_frames(150)
        self.assertTrue(p.jump_to_level(LEVEL_TRCA01, (A3 + "spawned",)), "TRCA01 did not load")
        self.assertTrue(c.wait_until(lambda: c.u8(VIEW_COUNT) == 3, 600, 5), "no third view in TRCA01")
        for _ in range(6):
            c.press(pad("cross")); c.wait_frames(120)
        clone = self.clone()
        enemies = [a for a in placed(c) if model_id(c, a) == TRCA01_ENEMY_MODEL]
        self.assertEqual(len(enemies), 3, "the three Transylvania enemies")
        enemy = min(enemies, key=lambda a: c.u16(a + 0x34))     # identity 249, by the level's centre
        ep = p.pos(enemy)
        c.write_u32(clone + 0x3C, (c.u32(clone + 0x3C) & 0xFFFF0000) | FULL_HEALTH)
        try:
            c.mod_cmd("bbt.p3actor", "ref-third off")
            teleport(c, clone, (ep[0] + 500, ep[1] - 40, ep[2]))
            c.wait_frames(300)
            self.assertLess(dist(ep, p.pos(enemy)), 50, "the enemy moved while the rule was off")
            self.assertEqual(c.s16(clone + 0x3C), FULL_HEALTH)
            k0 = c.counters().get(A3 + "ref_third", 0)
            c.mod_cmd("bbt.p3actor", "ref-third on")
            self.assertTrue(c.wait_until(lambda: c.s16(clone + 0x3C) < FULL_HEALTH, 900, 10),
                            "the enemy did not hit the third player")
            # it closes from 500 to ~400, then lunges (measured: ~140 units moved at the first hit)
            self.assertGreater(dist(ep, p.pos(enemy)), 50, "the enemy did not come for the third player")
            self.assertGreater(c.counters().get(A3 + "ref_third", 0), k0)
        finally:
            c.mod_cmd("bbt.p3actor", "ref-third on")
        self.game.shot("1_enemy")


if __name__ == "__main__":
    unittest.main()
