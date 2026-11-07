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
from bbt.game import LEVEL_GRANNY, Player, game_toml_copy  # noqa: E402
from bbt.net import _peer_folder, digests, free_tcp_port  # noqa: E402

HOST_MODS = {"trca01": True, "bbt.quick-start/quick-start": True}
GUEST_MODS: dict = {}
T = "bbt.third-character.trca01."
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

    def tearDown(self):
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

    def start(self, name: str, mods: dict, steps: str):
        folder = self.out / name
        link = _peer_folder(self.build, self.exe, folder, mods)
        port = free_tcp_port()
        log = open(folder / "game.log", "w", encoding="utf-8")
        env = dict(os.environ, LNG_TEST_HIDDEN="1", PSX_HIDDEN_WINDOW="1", PSX_DEBUG_SERVER="1",
                   LNG_SCRIPT=script(self.out, name.capitalize(), steps))
        p = subprocess.Popen([str(link), "--game", str(self.toml.resolve()), "--disc",
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


if __name__ == "__main__":
    unittest.main()
