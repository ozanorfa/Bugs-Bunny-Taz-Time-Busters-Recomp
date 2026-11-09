"""Game tier: a LAN room through the real launcher (build-net), the way
players will start a match (docs/NETPLAY.md "LAN / Direct IP").

Both peers run the launcher with its own scripting hook (recomp-ui
launcher_debug.h: LNG_SCRIPT, LNG_TEST_HIDDEN=1) and a hidden game window
(PSX_HIDDEN_WINDOW=1), each from a folder of its own (tests/bbt/net.py):
the host opens NETPLAY -> LAN / Direct IP, names itself, Host Lobby ->
Create Lobby (port 7777); the guest names itself and joins with Join
Direct (127.0.0.1:7777, the dialog's defaults); the host presses PLAY.
The launcher layout is fixed with size:1100x750 (logical pixels), so the
click positions below are that layout's (found from its screenshots).

The host selected "Taz becomes the Transylvania enemy" and Quick start; the
guest selected nothing. With [netplay] content_negotiation the room sends
the host's plan: the match runs in rollback mode with both mods active on
both peers, the host plays into Granwich, the machine digests agree, and the
guest's own selection (mods/state.toml) is left as it was. The launchers
get a copy of game.toml (the launcher writes its multitap-analog choice
into its --game file on PLAY); the repository's game.toml must stay as it
was.
Screenshots and logs go to build-tests/out/lanroom/."""
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
from bbt.game import LEVEL_GRANNY, Player, game_toml_copy, three_player_toml  # noqa: E402
from bbt.net import _peer_folder, digests, free_tcp_port  # noqa: E402

HOST_MODS = {"trca01": True, "bbt.quick-start/quick-start": True}
GUEST_MODS: dict = {}
T = "bbt.third-character.trca01."
A3 = "bbt.third-character.p3-actor."
V = "bbt.own-view.two-views."
# Phase 6: three PCs, each its own player and full-screen view (the third
# character is Daffy, the default), host's selection only. "auto": the third
# player appears because the third seat is taken.
HOST_MODS_3 = {"p3": {"enabled": True, "values": {"appear": "auto"}}, "bbt.own-view/two-views": True,
               "bbt.quick-start/quick-start": True}
# What a player gets with nothing selected (the manifests' defaults).
DEFAULTS = {"_defaults": True}
Q = "bbt.quick-start."

# Logical positions in the 1100x750 launcher (from its screenshots).
LAN_CARD = "234,282"          # NETPLAY page: "LAN / Direct IP"
NAME_FIELD = "547,435"        # first visit: Player Name dialog, name field
NAME_SAVE = "583,485"         #   ... Save
HOST_LOBBY = "118,689"        # lobby list: Host Lobby
CREATE_LOBBY = "546,545"      # Host Lobby dialog: Create Lobby
JOIN_DIRECT = "981,689"       # lobby list: Join Direct
JOIN_DIRECT_OK = "542,354"    # Join Direct dialog (127.0.0.1:7777): Join Direct
PLAY = "971,689"              # room: PLAY


def script(out: Path, name: str, steps: str) -> str:
    return (f"size:1100x750;wait:30;view:netplay_mode;wait:20;click:{LAN_CARD};wait:40;"
            f"click:{NAME_FIELD};wait:5;text:{name};wait:5;click:{NAME_SAVE};wait:20;{steps}")


class _Run:
    def __init__(self, client):
        self.client = client


class LanRoom(unittest.TestCase):
    def setUp(self):
        self.build = config.net_build()
        exe = config.exe_in(self.build)
        if exe is None:
            self.skipTest(f"no game executable in {self.build}")
        self.cue = config.disc_cue()
        if self.cue is None or not self.cue.is_file():
            self.skipTest("disc not found (game.toml [game] disc or BBT_DISC)")
        self.exe = exe
        self.out = config.out_dir() / "lanroom"
        if self.out.exists():
            shutil.rmtree(self.out)
        self.repo_toml = config.game_toml().read_bytes()
        self.toml = game_toml_copy(config.game_toml(), self.out / "game.toml")
        self.procs = []
        # macOS: the launchers' hidden windows stop advancing while the
        # display sleeps (seen: the script never ran, no screenshot). -u wakes
        # a sleeping display (user activity), -d keeps it on for the test.
        self.awake = None
        if sys.platform == "darwin" and shutil.which("caffeinate"):
            self.awake = subprocess.Popen(["caffeinate", "-u", "-d", "-i", "-w", str(os.getpid())])
            time.sleep(2)

    def tearDown(self):
        if getattr(self, "awake", None) and self.awake.poll() is None:
            self.awake.terminate()
            self.awake = None
        for p, c, log in self.procs:
            try:
                if p.poll() is None:
                    try:
                        c.quit()
                    except Exception:  # noqa: BLE001  (still in the launcher)
                        pass
                    try:
                        p.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        p.kill()
                        p.wait(timeout=10)
            finally:
                log.close()

    def start(self, name: str, mods: dict, steps: str, toml: Path | None = None):
        folder = self.out / name
        link = _peer_folder(self.build, self.exe, folder, mods)
        port = free_tcp_port()
        log = open(folder / "game.log", "w", encoding="utf-8")
        env = dict(os.environ, LNG_TEST_HIDDEN="1", PSX_HIDDEN_WINDOW="1", PSX_DEBUG_SERVER="1",
                   LNG_SCRIPT=script(self.out, name.capitalize(), steps))
        p = subprocess.Popen([str(link), "--game", str((toml or self.toml).resolve()), "--disc",
                              str(self.cue), "--memcard-dir", str((folder / "cards").resolve()),
                              "--debug-port", str(port)],
                             cwd=str(folder), stdout=log, stderr=subprocess.STDOUT, env=env)
        self.procs.append((p, DebugClient(port), log))
        return folder

    def test_host_plan_reaches_the_guest(self):
        o = self.out
        self.start("host", HOST_MODS,
                   f"click:{HOST_LOBBY};wait:40;click:{CREATE_LOBBY};wait:900;"
                   f"shot:{o}/1_host_room.png;click:{PLAY};wait:20000")
        time.sleep(4)
        guest_dir = self.start("guest", GUEST_MODS,
                               f"click:{JOIN_DIRECT};wait:30;click:{JOIN_DIRECT_OK};wait:300;"
                               f"shot:{o}/2_guest_room.png;wait:20000")
        guest_state = (guest_dir / "mods" / "state.toml").read_text(encoding="utf-8")

        deadline, running = time.monotonic() + 150, False
        while time.monotonic() < deadline and not running:
            for p, _, _ in self.procs:
                self.assertIsNone(p.poll(), "a launcher exited before the match")
            try:
                st = [c.send({"cmd": "netplay_status"}) for _, c, _ in self.procs]
                running = all(s.get("active") == 1 and s.get("tick", 0) > 0 for s in st)
            except OSError:                   # no debug server yet: still in the launcher
                pass
            time.sleep(1)
        self.assertTrue(running, "the match did not start (see build-tests/out/lanroom/*.png)")
        for _, c, _ in self.procs:
            s = c.send({"cmd": "netplay_status"})
            self.assertEqual((s["rollback"], s["players"], s["desync"]), (1, 2, 0), str(s))

        c = self.procs[0][1]
        p = Player(_Run(c))
        self.assertTrue(c.wait_until(lambda: p.level() == 2, 3000, 2), "no language screen")
        c.wait_frames(200)
        self.assertTrue(p.language_to_title(), "no title")
        c.wait_frames(240); c.press(pad("start")); c.wait_frames(120)
        c.press(pad("cross")); c.wait_frames(60)          # NEW GAME
        c.press(pad("down")); c.wait_frames(30)
        c.press(pad("cross")); c.wait_frames(90)          # TWO PLAYERS
        c.press(pad("cross"))                             # START GAME
        self.assertTrue(c.wait_until(lambda: p.level() == LEVEL_GRANNY and p.players_ready(), 3000, 2),
                        "Granwich did not load")
        loaded_at = c.frame()
        c.wait_frames(300)
        for i, (_, cc, _) in enumerate(self.procs):
            cs = cc.counters()
            with self.subTest(peer=i):
                self.assertEqual(cs.get(T + "models_patched"), 1, "Taz is not the enemy")
                self.assertEqual(cs.get(Q + "intro_skipped"), 1, "Quick start not active")
        self.tearDown()
        self.procs = []
        a, b = digests(o / "host" / "game.log"), digests(o / "guest" / "game.log")
        common = sorted(set(a) & set(b))
        print(f"\n    {len(common)} ticks compared up to {common[-1] if common else None}, "
              f"Granwich at ~{loaded_at}", file=sys.stderr)
        self.assertEqual([t for t in common if a[t] != b[t]], [], "the peers' digests differ")
        self.assertGreater(common[-1] if common else 0, loaded_at + 100)
        self.assertEqual((guest_dir / "mods" / "state.toml").read_text(encoding="utf-8"), guest_state,
                         "the room changed the guest's own mod selection")
        self.assertEqual(config.game_toml().read_bytes(), self.repo_toml,
                         "the repository's game.toml was written")

    def test_three_pcs_in_one_room(self):
        """Three launchers, one LAN room (a 3-player game.toml: players 3,
        multitap on port 2), two guests joining by Direct IP: all three play
        in the host's plan, the third PC drives the third character (Daffy),
        each PC draws its own player's view full screen, the digests agree,
        and each guest keeps a copy of the match's memory card (the host's,
        sent at the start: docs/NETPLAY.md "Bring your own memory card")."""
        o = self.out
        toml3 = three_player_toml(config.game_toml(), o / "game_3p.toml")
        self.start("host", HOST_MODS_3,
                   f"click:{HOST_LOBBY};wait:40;click:{CREATE_LOBBY};wait:2000;"
                   f"shot:{o}/3p_1_host_room.png;click:{PLAY};wait:20000", toml3)
        time.sleep(4)
        g1 = self.start("guest", GUEST_MODS,
                        f"click:{JOIN_DIRECT};wait:30;click:{JOIN_DIRECT_OK};wait:300;"
                        f"shot:{o}/3p_2_guest_room.png;wait:20000", toml3)
        time.sleep(4)
        # a Join Direct that got no answer leaves the dialog open: ask again
        g2 = self.start("guest2", GUEST_MODS,
                        f"click:{JOIN_DIRECT};wait:30;click:{JOIN_DIRECT_OK};wait:300;"
                        f"shot:{o}/3p_3a_guest2.png;click:{JOIN_DIRECT_OK};wait:300;"
                        f"shot:{o}/3p_3_guest2_room.png;wait:20000", toml3)
        # the hidden launchers draw slower under load (the full suite): the
        # host's 2000-frame wait before PLAY took over 3 minutes there
        deadline, running = time.monotonic() + 400, False
        while time.monotonic() < deadline and not running:
            for p, _, _ in self.procs:
                self.assertIsNone(p.poll(), "a launcher exited before the match")
            try:
                st = [c.send({"cmd": "netplay_status"}) for _, c, _ in self.procs]
                running = all(s.get("active") == 1 and s.get("tick", 0) > 0 for s in st)
            except OSError:
                pass
            time.sleep(1)
        self.assertTrue(running, "the match did not start (see build-tests/out/lanroom/3p_*.png)")
        for _, c, _ in self.procs:
            s = c.send({"cmd": "netplay_status"})
            self.assertEqual((s["rollback"], s["players"], s["desync"]), (1, 3, 0), str(s))
        c = self.procs[0][1]
        p = Player(_Run(c))
        self.assertTrue(c.wait_until(lambda: p.level() == 2, 3000, 2), "no language screen")
        c.wait_frames(200)
        self.assertTrue(p.language_to_title(), "no title")
        c.wait_frames(240); c.press(pad("start")); c.wait_frames(120)
        c.press(pad("cross")); c.wait_frames(60)          # NEW GAME
        c.press(pad("down")); c.wait_frames(30)
        c.press(pad("cross")); c.wait_frames(90)          # TWO PLAYERS
        c.press(pad("cross"))                             # START GAME
        self.assertTrue(c.wait_until(lambda: p.level() == LEVEL_GRANNY and p.players_ready(), 3000, 2),
                        "Granwich did not load")
        loaded_at = c.frame()
        self.assertTrue(c.wait_until(lambda: all(cc.counters().get(A3 + "spawned") for _, cc, _ in self.procs),
                                     900, 5), "no third character on every PC")
        for _ in range(8):                                # the opening dialogs
            c.press(pad("cross")); c.wait_frames(150)
        # the third PC's own pad drives the third character
        third = self.procs[2][1]
        clone = lambda cc: cc.u32(int(cc.mod_cmd("bbt.p3actor", "show")["state"], 16) + 4)
        a = clone(c)
        start = Player(_Run(c)).pos(a)
        third.hold(pad("right")); c.wait_frames(90); third.release(); c.wait_frames(60)
        moved = abs(Player(_Run(c)).pos(a)[0] - start[0]) + abs(Player(_Run(c)).pos(a)[2] - start[2])
        self.assertGreater(moved, 200, "the third PC did not move the third character")
        for i, (_, cc, _) in enumerate(self.procs):
            cs = cc.counters()
            with self.subTest(peer=i):
                self.assertGreaterEqual(cs.get(A3 + "daffy_moves", 0), 40, "not Daffy")
                self.assertGreater(cs.get(V + f"local_view_{i}", 0), 100, "own view not drawn")
                self.assertEqual([cs.get(V + f"local_view_{v}", 0) for v in range(3) if v != i], [0, 0],
                                 "drew another player's view")
        self.tearDown()
        self.procs = []
        logs = [digests(o / n / "game.log") for n in ("host", "guest", "guest2")]
        common = sorted(set(logs[0]) & set(logs[1]) & set(logs[2]))
        print(f"\n    {len(common)} ticks compared up to {common[-1] if common else None}, "
              f"Granwich at ~{loaded_at}", file=sys.stderr)
        self.assertEqual([t for t in common if not logs[0][t] == logs[1][t] == logs[2][t]], [],
                         "the peers' digests differ")
        self.assertGreater(common[-1] if common else 0, loaded_at + 100)
        # each guest keeps the match's card 1 (the host's) in its netplay sandbox
        host_card = (o / "host" / "cards" / "card1.mcd")
        for g in (g1, g2):
            copy = g / "cards" / "netplay" / "card1.mcd"
            self.assertTrue(copy.is_file(), f"no match card kept on {g.name}")
            if host_card.is_file():
                self.assertEqual(copy.read_bytes(), host_card.read_bytes(), f"{g.name}'s copy differs")
        self.assertEqual(config.game_toml().read_bytes(), self.repo_toml,
                         "the repository's game.toml was written")

    def test_two_pcs_in_a_three_seat_room(self):
        """The shipped setup with two players: the 3-player config (a room
        of three seats) and nothing selected in Mods. The host starts the
        match with two of three seated; with the defaults every PC shows its
        own player, Quick start is on, and the third player does not appear
        (nobody in seat 3)."""
        o = self.out
        toml3 = three_player_toml(config.game_toml(), o / "game_3p_two.toml")
        self.start("host2", DEFAULTS,
                   f"click:{HOST_LOBBY};wait:40;click:{CREATE_LOBBY};wait:1500;"
                   f"shot:{o}/2of3_1_host_room.png;click:{PLAY};wait:20000", toml3)
        time.sleep(4)
        self.start("guest2of3", DEFAULTS,
                   f"click:{JOIN_DIRECT};wait:30;click:{JOIN_DIRECT_OK};wait:300;"
                   f"shot:{o}/2of3_2a_guest.png;click:{JOIN_DIRECT_OK};wait:300;"
                   f"shot:{o}/2of3_2_guest_room.png;wait:20000", toml3)
        deadline, running = time.monotonic() + 300, False
        while time.monotonic() < deadline and not running:
            for p, _, _ in self.procs:
                self.assertIsNone(p.poll(), "a launcher exited before the match")
            try:
                st = [c.send({"cmd": "netplay_status"}) for _, c, _ in self.procs]
                running = all(s.get("active") == 1 and s.get("tick", 0) > 0 for s in st)
            except OSError:
                pass
            time.sleep(1)
        self.assertTrue(running, "the match did not start with two of three seats (see 2of3_*.png)")
        for _, c, _ in self.procs:
            s = c.send({"cmd": "netplay_status"})
            self.assertEqual((s["rollback"], s["players"], s["desync"]), (1, 2, 0), str(s))
        c = self.procs[0][1]
        p = Player(_Run(c))
        # Quick start's menus: the main menu at once, NEW GAME -> START GAME
        # with two players (the second seat), START GAME -> Granwich
        self.assertTrue(c.wait_until(lambda: p.level() == 0 and p.menu_page() == Player.PAGE_MAIN, 3000, 2),
                        "no main menu")
        c.wait_frames(60)
        c.press(pad("cross"))                             # NEW GAME
        self.assertTrue(c.wait_until(lambda: p.menu_page() == Player.PAGE_START, 300, 1), "no START GAME page")
        self.assertTrue(p.two_player_mode(), "not two players")
        c.wait_frames(30)
        c.press(pad("cross"))                             # START GAME
        self.assertTrue(c.wait_until(lambda: p.level() == LEVEL_GRANNY and p.players_ready(), 3000, 2),
                        "Granwich did not load")
        self.assertTrue(p.two_player_mode(), "Granwich not in 2-player mode")
        loaded_at = c.frame()
        for _ in range(8):                                # the opening dialogs
            c.press(pad("cross")); c.wait_frames(150)
        for i, (_, cc, _) in enumerate(self.procs):
            cs = cc.counters()
            with self.subTest(peer=i):
                self.assertEqual([cs.get(Q + n) for n in ("logos_skipped", "language_skipped", "title_skipped",
                                                          "players_two")], [1, 1, 1, 1], "Quick start's menus")
                self.assertGreaterEqual(cs.get(A3 + "no_third_player", 0), 1, "the third player's rule did not run")
                self.assertEqual(cs.get(A3 + "spawned", 0), 0, "a third player appeared with two PCs")
                self.assertGreater(cs.get(V + f"local_view_{i}", 0), 100, "own view not drawn")
                self.assertEqual(cs.get(V + f"local_view_{1 - i}", 0), 0, "drew the other player's view")
        self.tearDown()
        self.procs = []
        logs = [digests(o / n / "game.log") for n in ("host2", "guest2of3")]
        common = sorted(set(logs[0]) & set(logs[1]))
        print(f"\n    {len(common)} ticks compared up to {common[-1] if common else None}, "
              f"Granwich at ~{loaded_at}", file=sys.stderr)
        self.assertEqual([t for t in common if logs[0][t] != logs[1][t]], [], "the peers' digests differ")
        self.assertGreater(common[-1] if common else 0, loaded_at + 100)


if __name__ == "__main__":
    unittest.main()
