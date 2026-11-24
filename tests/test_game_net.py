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
  at tick 0 with "LAN content differs between peers");
- both peers also on Quick start (defaults: no logos, no intro, short
  loading screens, 2x CD speed) and the Turkish flag (bbt.decor: both
  wall textures and the 6-pane window rewritten once on each peer): they
  act on both peers alike and the digests still agree.
- both peers on Quick start and "Two views" (bbt.own-view), each in a
  hidden OpenGL window: in Granwich every peer draws its own player's view
  full screen (psx_mod_render_local_view: the host Bugs' view 0, the guest
  Taz's view 1), no own-view draw is refused, the authoritative frame is
  still the shared split screen while the presented image has no split, and
  the digests agree. The GL scale is capped at 2x with the framework's test
  variables (PSX_GL_MAX_DIM, PSX_GL_HIRES_WINDOW=0): these peers otherwise
  start at 8x, where each own-view draw costs ~80 ms (BUGS.md #35).
- three peers (host as relay, PSX_NET_SLOTS=3) with the third character
  ("p3") and Quick start, a 3-player copy of game.toml (multitap on port
  2): the third actor spawns once on every peer, peer 2's own pad is seat
  2 (the plugin's input record shows its Right on every peer) and moves the
  third actor the same way on every peer, the digests agree.
- three peers on the third character, Quick start and "Two views", in
  hidden OpenGL windows: the view count becomes 3 on every peer and each
  peer draws its own seat's view full screen (local_view_0 / 1 / 2), no
  own-view draw hits the watchdog, the presented images have no split, the
  digests agree.
- two peers with "Two views" after Bugs is down (a hit put in RAM): the
  game makes Taz player 1, the guest keeps drawing Taz (now view 0) and the
  host, whose Bugs is out, draws his teammate Taz's view.
Replays of our hooks inside a level do not happen: the level music is an XA
stream, during which the framework runs lockstep and refuses every rollback
(NOTES.md "N3: rollback and three peers"). On localhost the
peers rarely mispredict, and the framework's forced-rollback self-test
(PSX_RB_DET_SELFTEST) slows the menus' CD loads so much that the menu
presses below miss (NOTES.md "LAN netplay with our mods").
Logs go to build-tests/out/net-*/peer<slot>/game.log."""
from __future__ import annotations

import hashlib
import random
import statistics
import struct
import sys
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from bbt import config  # noqa: E402
from bbt.client import pad  # noqa: E402
from bbt.game import LEVEL_GRANNY, TYPE_TAZ, Player, three_player_toml  # noqa: E402
from bbt.net import CONTENT_DIFFERS, NetMatch  # noqa: E402
from bbt.png import dark_rows, row_jumps, row_means  # noqa: E402

T = "bbt.third-character.trca01."
Q = "bbt.quick-start."
TRCA01 = {"trca01": True}
QUICK = {"trca01": True, "bbt.quick-start/quick-start": True}
QUICK_FLAG = {**QUICK, "bbt.decor/turkish-flag": True}
DECOR = ("flag_texels", "flag_clut", "flag_far_texels", "flag_far_clut", "panes_texels")
V = "bbt.own-view.two-views."
# The enemy, not the default Daffy: these tests check what was verified with it.
ENEMY = {"enabled": True, "values": {"character": "enemy"}}
A3 = "bbt.third-character.p3-actor."
OWN = {"bbt.quick-start/quick-start": True, "bbt.own-view/two-views": True}
SMALL_GL = {"PSX_GL_MAX_DIM": "2048", "PSX_GL_HIRES_WINDOW": "0", "SDL_AUDIODRIVER": "dummy"}


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


    def test_3_quick_start_in_a_match(self):
        build = need_build()
        with NetMatch(build, "net-quick-start", [QUICK_FLAG, QUICK_FLAG]) as m:
            host, guest = m.peers
            c, p = host.client, Player(_Run(host.client))
            self.assertTrue(c.wait_until(lambda: p.level() == 2, 3000, 2), "no language screen")
            c.wait_frames(200)
            self.assertTrue(p.language_to_title(), "no title")
            c.wait_frames(240); c.press(pad("start")); c.wait_frames(120)
            c.press(pad("cross")); c.wait_frames(60)          # NEW GAME
            c.press(pad("down")); c.wait_frames(30)
            c.press(pad("cross")); c.wait_frames(90)          # TWO PLAYERS
            self.assertTrue(p.two_player_mode(), "TWO PLAYERS not selected")
            c.press(pad("cross"))                             # START GAME
            self.assertTrue(c.wait_until(lambda: p.level() == LEVEL_GRANNY and p.players_ready(),
                                         3000, 2), "Granwich did not load")
            loaded_at = c.frame()
            c.wait_frames(300)
            m.check_alive()
            for peer in m.peers:
                cs = peer.client.counters()
                got = {k: cs.get(Q + k) for k in ("logos_skipped", "intro_skipped", "cd_speed")}
                self.assertEqual(got, {"logos_skipped": 1, "intro_skipped": 1, "cd_speed": 2},
                                 f"peer {peer.slot}")
                self.assertGreaterEqual(cs.get(Q + "loading_shortened", 0), 1, f"peer {peer.slot}")
                flag = {k: cs.get("bbt.decor." + k) for k in DECOR}
                self.assertEqual(set(flag.values()), {1}, f"peer {peer.slot}: {flag}")
                self.assertEqual(peer.status()["desync"], 0, f"peer {peer.slot}")
        compared, bad = m.compare_digests()
        last = max(set(host.digests()) & set(guest.digests()), default=0)
        print(f"\n    {compared} ticks compared up to {last}, Granwich at ~{loaded_at}", file=sys.stderr)
        self.assertEqual(bad, [], "the peers' machine digests differ")
        self.assertGreater(last, loaded_at + 100, "no digests after the level load")

    def test_4_each_peer_shows_its_own_view(self):
        build = need_build()
        with NetMatch(build, "net-own-view", [OWN, OWN], window=True, env=SMALL_GL) as m:
            host, guest = m.peers
            c, p = host.client, Player(_Run(host.client))
            self.assertTrue(c.wait_until(lambda: p.level() == 2, 3000, 2), "no language screen")
            c.wait_frames(200)
            self.assertTrue(p.language_to_title(), "no title")
            c.wait_frames(240); c.press(pad("start")); c.wait_frames(120)
            c.press(pad("cross")); c.wait_frames(60)          # NEW GAME
            c.press(pad("down")); c.wait_frames(30)
            c.press(pad("cross")); c.wait_frames(90)          # TWO PLAYERS
            self.assertTrue(p.two_player_mode(), "TWO PLAYERS not selected")
            c.press(pad("cross"))                             # START GAME
            self.assertTrue(c.wait_until(lambda: p.level() == LEVEL_GRANNY and p.players_ready(),
                                         3000, 2), "Granwich did not load")
            loaded_at = c.frame()
            for _ in range(6):                                # the opening dialogs
                c.press(pad("cross")); c.wait_frames(60)
            host.client.hold(pad("up")); guest.client.hold(pad("up")); c.wait_frames(120)
            host.client.release(); guest.client.release(); c.wait_frames(60)
            m.check_alive()

            def own(peer) -> dict:
                return {k[len(V):]: v for k, v in peer.client.counters().items() if k.startswith(V)}

            for peer, mine, other in ((host, "local_view_0", "local_view_1"),
                                      (guest, "local_view_1", "local_view_0")):
                k = own(peer)
                with self.subTest(peer=peer.slot):
                    self.assertGreater(k.get(mine, 0), 60, f"own views drawn: {k}")
                    self.assertEqual(k.get(other, 0), 0, f"drew the other player's view: {k}")
                    self.assertEqual(k.get("local_refused", 0), 0, f"own-view draws refused: {k}")
                    st = peer.client.send({"cmd": "render_pass_stats"})
                    self.assertEqual(st.get("watchdog"), 0, "an own-view draw ran into the watchdog")
                    self.assertEqual(peer.status()["desync"], 0)
            out = m.dir
            for peer in m.peers:
                shared = out / f"shared{peer.slot}.png"
                shown = out / f"shown{peer.slot}.png"
                peer.client.screenshot(shared.resolve())        # authoritative VRAM: the split
                peer.client.send({"cmd": "screenshot_hires", "path": str(shown.resolve())}, timeout=60.0)
                split = dark_rows(row_means(shared), 0.4, 0.6)
                with self.subTest(peer=peer.slot):
                    self.assertGreaterEqual(len(split), 2, "the shared frame has no split-screen divider")
                    self.assertEqual(dark_rows(row_means(shown), 0.4, 0.6), [],
                                     "the presented image is still split")
        compared, bad = m.compare_digests()
        last = max(set(host.digests()) & set(guest.digests()), default=0)
        print(f"\n    {compared} ticks compared up to {last}, Granwich at ~{loaded_at}", file=sys.stderr)
        self.assertEqual(bad, [], "the peers' machine digests differ")
        self.assertGreater(last, loaded_at + 100, "no digests after the level load")

    def test_5_three_peers_with_the_third_character(self):
        self.three_peers("net-three-peers", {"p3": ENEMY, "bbt.quick-start/quick-start": True})

    def test_5b_three_peers_with_daffy(self):
        """Daffy's moves are built on each peer from the disc (integer math,
        retarget.c): the same image everywhere, and the digests agree."""
        def same_daffy(m):
            images = []
            for pe in m.peers:
                show = pe.client.mod_cmd("bbt.p3actor", "show")
                cs = pe.client.counters()
                self.assertGreaterEqual(cs.get(A3 + "daffy_moves", 0), 40, f"peer {pe.slot}")
                img, size = int(show["image"], 16), show["image_size"]
                data = b"".join(pe.client.read(img + o, min(0x4000, size - o)) for o in range(0, size, 0x4000))
                images.append(hashlib.sha256(data).hexdigest())
            self.assertEqual(len(set(images)), 1, f"the peers built different Daffy images {images}")
        # "auto" (the default): he appears because the third seat is taken
        self.three_peers("net-three-daffy",
                         {"p3": {"enabled": True, "values": {"character": "daffy", "appear": "auto"}},
                          "bbt.quick-start/quick-start": True}, same_daffy)

    def three_peers(self, name: str, three: dict, check=None):
        build = need_build()
        toml = three_player_toml(config.game_toml(), config.out_dir() / "game_3p_net.toml")
        with NetMatch(build, name, [three, three, three], toml=toml) as m:
            host, _, third = m.peers
            c, p = host.client, Player(_Run(host.client))
            self.assertTrue(c.wait_until(lambda: p.level() == 2, 6000, 2), "no language screen")
            c.wait_frames(200)
            self.assertTrue(p.language_to_title(), "no title")
            c.wait_frames(240); c.press(pad("start")); c.wait_frames(120)
            c.press(pad("cross")); c.wait_frames(60)          # NEW GAME
            c.press(pad("down")); c.wait_frames(30)
            c.press(pad("cross")); c.wait_frames(90)          # TWO PLAYERS
            self.assertTrue(p.two_player_mode(), "TWO PLAYERS not selected")
            c.press(pad("cross"))                             # START GAME
            self.assertTrue(c.wait_until(lambda: p.level() == LEVEL_GRANNY and p.players_ready(),
                                         6000, 2), "Granwich did not load")
            loaded_at = c.frame()
            self.assertTrue(c.wait_until(lambda: all(pe.client.counters().get(A3 + "spawned")
                                                     for pe in m.peers), 900, 5),
                            "no third actor on every peer")
            for _ in range(10):                               # the opening dialogs
                c.press(pad("cross")); c.wait_frames(150)
            record = int(third.client.mod_cmd("bbt.p3", "show")["record"], 16)
            # Each peer sees the press once its input has arrived: D frames
            # plus the transport, more when a peer runs behind (a loaded
            # Mac; on 2026-11-14 the host still read its prediction, 0,
            # 10 frames after the press). Until then a rollback peer shows
            # the predicted input, so wait for it on every peer.
            third.client.hold(pad("right")); c.wait_frames(10)

            def seat2_input(pe):
                return struct.unpack_from("<H", pe.client.read(record, 4), 2)[0]
            held = []
            for pe in m.peers:
                pe.client.wait_until(lambda pe=pe: seat2_input(pe) == 0x2000, 120, 2)
                held.append(seat2_input(pe))
            third.client.release(); c.wait_frames(20)
            self.assertEqual(held, [0x2000] * 3, "seat 2's Right is not in the third input record")
            start = [clone_pos(pe) for pe in m.peers]
            third.client.hold(pad("right")); c.wait_frames(90); third.client.release()
            c.wait_frames(120)
            end = [clone_pos(pe) for pe in m.peers]
            # read a few ticks apart on each peer: equal machines are what
            # the digests below prove; here the positions must just agree
            self.assertTrue(all(moved(e, end[0]) <= 32 for e in end),
                            f"the peers see the third actor at {end}")
            self.assertGreater(moved(start[0], end[0]), 200, "peer 2 did not move the third actor")
            m.check_alive()
            for pe in m.peers:
                cs = pe.client.counters()
                with self.subTest(peer=pe.slot):
                    self.assertEqual(pe.status()["players"], 3)
                    self.assertEqual(pe.status()["desync"], 0)
                    self.assertEqual(cs.get(A3 + "spawned"), 1)
            if check:
                check(m)
        compared, bad = m.compare_digests()
        last = max(set.intersection(*(set(pe.digests()) for pe in m.peers)), default=0)
        print(f"\n    {compared} ticks compared up to {last}, Granwich at ~{loaded_at}", file=sys.stderr)
        self.assertEqual(bad, [], "the peers' machine digests differ")
        self.assertGreater(last, loaded_at + 100, "no digests after the level load")

    def test_6_three_peers_each_see_their_own_view(self):
        build = need_build()
        feats = {"p3": ENEMY, "bbt.quick-start/quick-start": True, "bbt.own-view/two-views": True}
        toml = three_player_toml(config.game_toml(), config.out_dir() / "game_3p_net_own.toml")
        with NetMatch(build, "net-three-own-views", [feats] * 3, toml=toml, window=True,
                      env=SMALL_GL) as m:
            host = m.peers[0]
            c, p = host.client, Player(_Run(host.client))
            self.assertTrue(c.wait_until(lambda: p.level() == 2, 6000, 2), "no language screen")
            c.wait_frames(200)
            self.assertTrue(p.language_to_title(), "no title")
            c.wait_frames(240); c.press(pad("start")); c.wait_frames(120)
            c.press(pad("cross")); c.wait_frames(60)          # NEW GAME
            c.press(pad("down")); c.wait_frames(30)
            c.press(pad("cross")); c.wait_frames(90)          # TWO PLAYERS
            c.press(pad("cross"))                             # START GAME
            self.assertTrue(c.wait_until(lambda: p.level() == LEVEL_GRANNY and p.players_ready(),
                                         6000, 2), "Granwich did not load")
            loaded_at = c.frame()
            self.assertTrue(c.wait_until(lambda: all(pe.client.counters().get(V + "views_3")
                                                     for pe in m.peers), 900, 5),
                            "no third view on every peer")
            for _ in range(10):                               # the opening dialogs
                c.press(pad("cross")); c.wait_frames(150)
            for pe in m.peers:
                pe.client.hold(pad("up"))
            presents, tick = c.counters().get(V + "main_presents", 0), host.status()["tick"]
            c.wait_frames(120)
            for pe in m.peers:
                pe.client.release()
            c.wait_frames(60)
            pace = (c.counters().get(V + "main_presents", 0) - presents) / max(1, host.status()["tick"] - tick)
            self.assertGreater(pace, 0.45, f"the game makes {pace:.2f} frames per vblank with three views (0.5 = full speed)")
            m.check_alive()
            for pe in m.peers:
                k = {kk[len(V):]: v for kk, v in pe.client.counters().items() if kk.startswith(V)}
                mine = f"local_view_{pe.slot}"
                others = [f"local_view_{v}" for v in range(3) if v != pe.slot]
                shared, shown = m.dir / f"shared{pe.slot}.png", m.dir / f"shown{pe.slot}.png"
                pe.client.screenshot(shared.resolve())
                pe.client.send({"cmd": "screenshot_hires", "path": str(shown.resolve())}, timeout=60.0)
                with self.subTest(peer=pe.slot):
                    self.assertGreater(k.get(mine, 0), 300, f"own views drawn: {k}")
                    self.assertEqual([k.get(o, 0) for o in others], [0, 0], f"drew another view: {k}")
                    self.assertEqual(pe.client.send({"cmd": "render_pass_stats"}).get("watchdog"), 0,
                                     "an own-view draw ran into the watchdog")
                    self.assertEqual(pe.status()["desync"], 0)
                    # In a match the frame the game builds (three 72-line bands)
                    # is not shown, so its models are not drawn (own_view.c
                    # model_emit_wrap): the shared picture has no band edges
                    # at rows 88 and 160 any more, and the game still runs
                    # its 30 frames a second (one frame per two vblanks; it
                    # was 0.21-0.38 per vblank while the models were drawn).
                    self.assertGreater(k.get("shared_emits_skipped", 0), 1000, f"the shared frame's models: {k}")
                    jumps = row_jumps(shared)
                    self.assertLess(max(jumps[round(b * len(jumps) / 240)] for b in (88, 160)), 8,
                                    "the shared frame still shows the bands' pictures")
                    self.assertEqual(dark_rows(row_means(shown), 0.2, 0.8), [],
                                     "the presented image is still split")
        compared, bad = m.compare_digests()
        last = max(set.intersection(*(set(pe.digests()) for pe in m.peers)), default=0)
        print(f"\n    {compared} ticks compared up to {last}, Granwich at ~{loaded_at}", file=sys.stderr)
        self.assertEqual(bad, [], "the peers' machine digests differ")
        self.assertGreater(last, loaded_at + 100, "no digests after the level load")

    def test_7_after_bugs_is_down_each_peer_shows_a_hero_in_play(self):
        """Bugs' death makes Taz player 1 (views 0 / 1 follow players 1 / 2):
        the guest still shows Taz (now view 0), the host, whose Bugs is out,
        shows his teammate Taz. The hit is put in RAM on both peers (a debug
        write lands at a different tick on each), so digests are not compared
        after it."""
        build = need_build()
        with NetMatch(build, "net-own-view-down", [OWN, OWN], window=True, env=SMALL_GL) as m:
            host, guest = m.peers
            c, p = host.client, Player(_Run(host.client))
            self.assertTrue(c.wait_until(lambda: p.level() == 2, 3000, 2), "no language screen")
            c.wait_frames(200)
            self.assertTrue(p.language_to_title(), "no title")
            c.wait_frames(240); c.press(pad("start")); c.wait_frames(120)
            c.press(pad("cross")); c.wait_frames(60)          # NEW GAME
            c.press(pad("down")); c.wait_frames(30)
            c.press(pad("cross")); c.wait_frames(90)          # TWO PLAYERS
            c.press(pad("cross"))                             # START GAME
            self.assertTrue(c.wait_until(lambda: p.level() == LEVEL_GRANNY and p.players_ready(),
                                         3000, 2), "Granwich did not load")
            for _ in range(10):                               # the opening dialogs
                c.press(pad("cross")); c.wait_frames(150)
            bugs, taz = p.actor(1), p.actor(2)

            def own(peer) -> dict:
                return {k[len(V):]: v for k, v in peer.client.counters().items() if k.startswith(V)}

            before = [own(pe) for pe in m.peers]
            for pe in m.peers:
                w = pe.client.u32(bugs + 0x64)
                pe.client.write_u32(bugs + 0x64, (w & 0xFFFF) | (6 << 16))
                pe.client.write_u32(bugs + 0x24, pe.client.u32(bugs + 0x24) | 0x40000000)
            self.assertTrue(c.wait_until(lambda: all(pe.client.u8(0x800100A2) & 1 for pe in m.peers),
                                         600, 5), "Bugs did not go out")
            c.wait_frames(180)
            m.check_alive()
            for pe in m.peers:
                with self.subTest(peer=pe.slot):
                    self.assertEqual(pe.client.u32(0x8008DD04), taz, "Taz is not player 1")
                    self.assertEqual(pe.client.u32(0x80093C58), taz, "view 0 does not follow Taz")
                    k = own(pe)
                    d = {x: k.get(x, 0) - before[pe.slot].get(x, 0)
                         for x in ("local_view_0", "local_view_1", "local_view_teammate", "local_refused")}
                    self.assertGreater(d["local_view_0"], 60, f"Taz's view not drawn: {d}")
                    self.assertLess(d["local_view_1"], 40, f"still drawing the out Bugs' view: {d}")
                    self.assertEqual(d["local_refused"], 0, f"own-view draws refused: {d}")
                    if pe is host:
                        self.assertGreater(d["local_view_teammate"], 60, f"no teammate view: {d}")
                    else:
                        self.assertEqual(d["local_view_teammate"], 0, f"Taz's own peer spectates: {d}")
                    pe.client.send({"cmd": "screenshot_hires",
                                    "path": str((m.dir / f"shown{pe.slot}.png").resolve())}, timeout=60.0)


def clone_pos(peer) -> tuple:
    """Position of the third actor (bbt.p3actor state + 4) on one peer."""
    state = int(peer.client.mod_cmd("bbt.p3actor", "show")["state"], 16)
    a = peer.client.u32(state + 4)
    return struct.unpack("<3h", peer.client.read(a + 0xFC, 6))


def moved(a, b) -> int:
    return abs(a[0] - b[0]) + abs(a[2] - b[2])


if __name__ == "__main__":
    unittest.main()
