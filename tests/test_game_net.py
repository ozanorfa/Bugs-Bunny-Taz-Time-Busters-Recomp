"""Game tier: netplay with our mods (Phase 6 step N1/N2), two headless peers
of the netplay build (build-net: -DPSX_NETPLAY=ON -DRNET_ENABLE_ICE=OFF) on
127.0.0.1, in rollback mode (the launcher's default).

game.toml sets [netplay] content_negotiation = true, so a match keeps the
mods each peer selected and the peers compare their plans before the game
starts (runtime main.cpp netplay_commit_mods, NOTES.md "LAN netplay with
our mods"):

- both peers on "Taz becomes the Transylvania enemy": the match plays into
  Granwich with the mod active on both, and the peers' machine digests
  agree on every tick both logged;
- one peer with the mod and one without: the match never starts (both wait
  at tick 0 with "LAN content differs between peers").
Not covered yet (Phase 6 step N3): replays of our hooks. On localhost the
peers rarely mispredict, and the framework's forced-rollback self-test
(PSX_RB_DET_SELFTEST) slows the menus' CD loads so much that the menu
presses below miss (NOTES.md "LAN netplay with our mods").
Logs go to build-tests/out/net-*/peer<slot>/game.log."""
from __future__ import annotations

import random
import sys
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from bbt import config  # noqa: E402
from bbt.client import pad  # noqa: E402
from bbt.game import LEVEL_GRANNY, TYPE_TAZ, Player  # noqa: E402
from bbt.net import CONTENT_DIFFERS, NetMatch  # noqa: E402

T = "bbt.third-character.trca01."
TRCA01 = {"trca01": True}


class _Run:
    """What Player needs from a run: its debug client."""

    def __init__(self, client):
        self.client = client


def need_build() -> Path:
    build = config.net_build()
    if config.exe_in(build) is None:
        raise unittest.SkipTest(f"no game executable in {build}")
    cue = config.disc_cue()
    if cue is None or not cue.is_file():
        raise unittest.SkipTest("disc not found (game.toml [game] disc or BBT_DISC)")
    return build


class NetplayWithMods(unittest.TestCase):
    def test_1_same_mods_play_in_step(self):
        build = need_build()
        with NetMatch(build, "net-same-mods", [TRCA01, TRCA01]) as m:
            host, guest = m.peers
            p = Player(_Run(host.client))
            self.assertTrue(p.boot_to_language_select() and p.language_to_title()
                            and p.title_to_two_player_game(),
                            "the host could not start a 2-player game in Granwich")
            m.check_alive()
            loaded_at = host.client.frame()

            # random play on both seats, each peer driving its own player
            rng = random.Random(7)
            names = ["cross", "circle", "square", "triangle", "up", "down", "left", "right"]
            for _ in range(120):
                for peer in m.peers:
                    peer.client.press(pad(rng.choice(names)), frames=6)
                host.client.wait_frames(8)
            m.check_alive()

            for peer in m.peers:
                s = peer.status()
                self.assertEqual((s["active"], s["rollback"], s["players"], s["desync"]), (1, 1, 2, 0),
                                 f"peer {peer.slot}: {s}")
                pl = Player(_Run(peer.client))
                self.assertEqual(pl.level(), LEVEL_GRANNY, f"peer {peer.slot} left Granwich")
                taz = pl.actor(2)
                self.assertTrue(taz and pl.actor_type(taz) == TYPE_TAZ, f"peer {peer.slot}: no Taz")
                cs = peer.client.counters()
                for k in ("loads_patched", "models_patched", "tex_relocated"):
                    self.assertGreaterEqual(cs.get(T + k, 0), 1, f"peer {peer.slot}: {k} (mod not active)")
            # the same model on both: Taz's resource list (stable in the level)
            lists = [Player(_Run(peer.client)).c.u32(Player(_Run(peer.client)).actor(2) + 0x98)
                     for peer in m.peers]
            self.assertEqual(lists[0], lists[1], "Taz's resources differ between the peers")

        compared, bad = m.compare_digests()
        last = max(set(host.digests()) & set(guest.digests()), default=0)
        print(f"\n    {compared} ticks compared up to {last}, level loaded at ~{loaded_at}",
              file=sys.stderr)
        self.assertEqual(bad, [], "the peers' machine digests differ")
        self.assertGreater(last, loaded_at + 200, "no digests after the level load")

    def test_2_different_mods_do_not_start(self):
        build = need_build()
        with NetMatch(build, "net-other-mods", [TRCA01, {}]) as m:
            deadline = time.monotonic() + 30
            seen = set()
            while time.monotonic() < deadline and len(seen) < 2:
                m.check_alive()
                for peer in m.peers:
                    s = peer.status()
                    self.assertEqual(s["tick"], 0, f"peer {peer.slot} started playing: {s}")
                    if s["stall"] == "content_mismatch":
                        seen.add(peer.slot)
                time.sleep(0.5)
            self.assertEqual(seen, {0, 1}, "the peers did not stop at the content check")
            time.sleep(2)
            for peer in m.peers:
                self.assertEqual(peer.status()["tick"], 0, f"peer {peer.slot} started playing")
        for peer in m.peers:
            self.assertIn(CONTENT_DIFFERS, peer.log_text(), f"peer {peer.slot} did not say why")


if __name__ == "__main__":
    unittest.main()
