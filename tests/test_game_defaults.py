"""Game tier: the mod defaults a player gets with nothing selected
(2026-11-08), offline in the 5-seat build (build-p3) with the 3-player
config: Quick start on (logos, the language screen, PRESS START and the
new-game intro skipped; with pad 2 in on one PC the game asks ONE / TWO
PLAYERS), the own view
on but keeping the game's shared screen on one PC (one view in a 2-player
level), and the third player on but not appearing without a third player
(this test process gives seat 2 no controller), and Decorations off (no
Turkish flag; off by default since 2026-11-18). OfflineJoin: with a
controller set for Player 3 (settings.toml p3_device), Daffy still waits
until seat 3 presses a button, then comes at once, next to Taz (and at
every later level start). A LAN
room with the same defaults: test_game_lanroom.py
test_two_pcs_in_a_three_seat_room."""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from bbt import config  # noqa: E402
from bbt.client import DebugClient, pad  # noqa: E402
from bbt.game import LEVEL_VICA01, Player, three_player_toml  # noqa: E402
from bbt.net import _peer_folder, free_tcp_port  # noqa: E402
from test_game_stock import start_run  # noqa: E402

A3 = "bbt.third-character.p3-actor."
V = "bbt.own-view.two-views."
Q = "bbt.quick-start."
VIEW_COUNT = 0x8008DE00


class Defaults(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        toml = config.out_dir() / "game_3p_defaults.toml"
        three_player_toml(config.game_toml(), toml)
        start_run(cls, config.p3_build(), "defaults", {"_defaults": True}, toml=toml)

    def test_1_nothing_selected(self):
        c, p = self.c, self.p
        self.assertTrue(p.straight_to_game(two=True), f"no 2-player Granwich (level {p.level()})")
        self.assertTrue(p.two_player_mode())
        k = c.counters()
        self.assertEqual([k.get(Q + n) for n in ("logos_skipped", "language_skipped", "title_skipped",
                                                  "players_asked")], [1, 1, 1, 1], "Quick start's defaults")
        for _ in range(8):                                 # the opening dialogs
            c.press(pad("cross")); c.wait_frames(150)
        c.wait_frames(120)
        k = c.counters()
        self.assertEqual(k.get(Q + "intro_skipped"), 1, "the intro was not skipped")
        # the third player: on (its plugins ran the level load), not spawned
        self.assertGreaterEqual(k.get(A3 + "no_third_player", 0), 1, "the rule did not run")
        self.assertEqual(k.get(A3 + "spawned", 0), 0, "Daffy appeared without a third player")
        # the own view: one PC keeps the shared screen
        self.assertEqual(c.u8(VIEW_COUNT), 1, "the screen is split on one PC")
        self.assertEqual(k.get(V + "views_set", 0) + k.get(V + "views_3", 0), 0)
        # Decorations: off, so Granwich's wall texture is uploaded unchanged
        self.assertEqual({n: k.get("bbt.decor." + n, 0) for n in ("flag_texels", "flag_far_texels")},
                         {"flag_texels": 0, "flag_far_texels": 0}, "the Turkish flag is on by default")
        self.game.shot("1_granwich")

    def test_2_no_hook_lost_its_guard(self):
        """Every function override of ours declines (and the original runs)
        when the 4 guard words at its address do not match: after a framework
        update or a regenerate that is a hook that silently stopped working.
        The `func_override` debug command counts those declines (the
        Fighting Force session's check, 2026-11-09). Our hooks are in the
        main executable, so a miss is never legitimate here."""
        overrides = self.c.send({"cmd": "func_override"}).get("overrides", [])
        ours = [o for o in overrides if o["id"].startswith("bbt.")]
        print("\n    " + "\n    ".join(f"{o['id']:<44} {o['addr']} calls {o['calls']:>7} misses {o['guard_misses']}"
                                       for o in ours), file=sys.stderr)
        self.assertGreater(len(ours), 10, f"our overrides are not armed: {[o['id'] for o in overrides]}")
        self.assertEqual([o["id"] for o in ours if o["guard_misses"]], [], "a hook's guard words no longer match")
        unreached = [o["id"] for o in ours if not o["calls"]]
        self.assertNotIn("bbt.quick-start:menus", unreached, "the menu hook was never reached")

    def test_3_the_pad_is_polled_every_frame(self):
        """BUGS #49: with psxrecomp v0.3.7's live DualShock mode the game's
        pad driver loops the DualShock detection and polls the buttons about
        once every 27 frames, so short presses are lost (game.toml
        `[controller] lock_mode = true` keeps the plain digital pad there;
        the shipped pin d9605697 presents it anyway). With the shipped
        config the driver sends button polls (0x42) every frame and never a
        config-mode command (0x43..0x4F)."""
        cmds = self.c.pad_commands(100)
        print(f"\n    pad commands in 100 frames: { {f'0x{k:02X}': v for k, v in sorted(cmds.items())} }",
              file=sys.stderr)
        self.assertEqual({k: v for k, v in cmds.items() if 0x43 <= k <= 0x4F}, {},
                         "the game runs the DualShock config handshake")
        self.assertGreaterEqual(cmds.get(0x42, 0), 95, "the buttons are not polled every frame")


class _Run:
    def __init__(self, client):
        self.client = client


class OfflineJoin(unittest.TestCase):
    """One PC, Player 3 set to the keyboard in this folder's settings.toml
    (its seat counts as connected), nothing selected in Mods."""

    @classmethod
    def setUpClass(cls):
        build = config.p3_build()
        exe = config.exe_in(build)
        cue = config.disc_cue()
        if exe is None or cue is None or not cue.is_file():
            raise unittest.SkipTest("no build or disc")
        cls.out = config.out_dir() / "defaults_join"
        shutil.rmtree(cls.out, ignore_errors=True)
        link = _peer_folder(build, exe, cls.out, {"_defaults": True})
        (cls.out / "settings.toml").write_text(
            '[controller]\np1_device = "keyboard"\np2_device = "keyboard"\np3_device = "keyboard"\n')
        toml = three_player_toml(config.game_toml(), cls.out / "game_3p.toml")
        port = free_tcp_port()
        cls.log = open(cls.out / "game.log", "w", encoding="utf-8")
        cls.proc = subprocess.Popen(
            [str(link), "--game", str(toml), "--disc", str(cue), "--headless",
             "--memcard-dir", str((cls.out / "cards").resolve()), "--debug-port", str(port)],
            cwd=str(cls.out), stdout=cls.log, stderr=subprocess.STDOUT,
            env=dict(os.environ, PSX_DEBUG_SERVER="1"))
        cls.c = DebugClient(port)
        deadline = time.monotonic() + 120
        while not cls.c.ping():
            if cls.proc.poll() is not None or time.monotonic() > deadline:
                cls.tearDownClass()
                raise RuntimeError("the game did not start")
            time.sleep(0.3)
        cls.p = Player(_Run(cls.c))

    @classmethod
    def tearDownClass(cls):
        try:
            cls.c.quit()
        except Exception:  # noqa: BLE001
            pass
        try:
            cls.proc.wait(timeout=20)
        except subprocess.TimeoutExpired:
            cls.proc.kill()
        cls.log.close()

    def k(self) -> dict:
        return {x[len(A3):]: v for x, v in self.c.counters().items() if x.startswith(A3)}

    def clone(self) -> int:
        return self.c.u32(int(self.c.mod_cmd("bbt.p3actor", "show")["state"], 16) + 4)

    def test_daffy_waits_for_seat_3(self):
        c, p = self.c, self.p
        self.assertTrue(p.straight_to_game(two=True), f"no 2-player Granwich (level {p.level()})")
        for _ in range(8):                                 # the opening dialogs
            c.press(pad("cross")); c.wait_frames(150)
        # play a little first, so Taz is no longer as the level loaded him
        c.hold(pad("right"), port=2); c.wait_frames(60); c.release(port=2); c.wait_frames(60)
        k = self.k()
        self.assertGreaterEqual(k.get("no_third_player", 0), 1)
        self.assertEqual((k.get("spawned", 0), k.get("p3_joined", 0)), (0, 0),
                         "Daffy came with a Player 3 setting but nobody at seat 3")
        self.assertEqual(self.clone(), 0)
        # seat 3 presses a button during the level: Daffy comes at once, next to Taz
        c.mod_cmd("bbt.p3", f"{pad('cross'):04X}"); c.wait_frames(10); c.mod_cmd("bbt.p3", "off")
        self.assertTrue(c.wait_until(lambda: self.k().get("spawned", 0) >= 1, 120, 5),
                        f"no Daffy in the level after seat 3's button: {self.k()}")
        k = self.k()
        self.assertEqual((k.get("p3_joined"), k.get("spawned_in_level")), (1, 1), k)
        a, taz = self.clone(), p.actor(2)
        self.assertTrue(a, "no clone address")
        c.wait_frames(90)                                  # its first updates: model bound, on the ground
        self.assertGreaterEqual(self.k().get("updates", 0), 30, "the clone is not updated")
        self.assertNotEqual(c.u32(a + 0x9C), 0, "the clone has no model (draw nodes)")
        ax, ay, az = p.pos(a); tx, ty, tz = p.pos(taz)
        self.assertLessEqual(max(abs(ax - tx), abs(az - tz)), 400, f"not next to Taz: {(ax, ay, az)} vs {(tx, ty, tz)}")
        self.assertLessEqual(abs(ay - ty), 300, f"not on Taz's ground: {ay} vs {ty}")
        try:
            c.screenshot(self.out / "joined_in_level.png")
        except Exception:  # noqa: BLE001  (headless without a picture)
            pass
        # seat 3 moves it; Taz stays
        tx0 = p.pos(taz)
        c.mod_cmd("bbt.p3", f"{pad('left'):04X}"); c.wait_frames(60); c.mod_cmd("bbt.p3", "off"); c.wait_frames(20)
        bx, _, bz = p.pos(a)
        self.assertGreater(abs(bx - ax) + abs(bz - az), 100, "seat 3 does not move the clone")
        self.assertEqual(p.pos(taz), tx0, "seat 3 moved Taz")
        self.assertEqual(c.u8(VIEW_COUNT), 1, "one PC keeps the shared screen")
        # and he is there from the start of the next level
        self.assertTrue(p.jump_to_level(LEVEL_VICA01), "the next level did not load")
        self.assertTrue(c.wait_until(lambda: self.k().get("spawned", 0) >= 2, 900, 10),
                        "no Daffy at the next level after joining")
        self.assertEqual(self.k().get("spawned_in_level"), 1)


if __name__ == "__main__":
    unittest.main()
