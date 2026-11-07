"""Netplay rollback probe (Phase 6 step N3, NOTES.md "N3: rollback and three peers").

Two peers of the netplay build (build-net) in hidden OpenGL windows, both on
Quick start + Two views (+ the feature keys given), with the framework's
forced-rollback self-test (PSX_RB_DET_SELFTEST=K: the host rewinds 16 ticks
every K ticks) and its snapshot audit (PSX_RB_SNAP_AUDIT). The host plays
into Granwich, both seats play randomly, then the script reports the
self-test rewinds (in the level they are refused: XA lockstep), the
digests, replay vs live per seat and seat vs seat, per section, in the
level. Output in build-tests/out/n3-rb-<K>[RUN_TAG][-features]/.

    python3 tools/netplay_rb_probe.py 128 [trca01 ...]

The self-test slows the menus' CD loads, so the scripted menu presses can
miss (it then stops with "no 2P"); run it again."""
import collections, os, random, re, sys
from pathlib import Path
PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT / "tests"))
sys.path.insert(0, str(PROJECT / "psxrecomp" / "tools"))
from bbt import config
from bbt.client import pad
from bbt.game import LEVEL_GRANNY, Player
from bbt.net import NetMatch
import rb_snap_audit_diff as audit

K = sys.argv[1]
FEATS = {"bbt.quick-start/quick-start": True, "bbt.own-view/two-views": True}
for extra in sys.argv[2:]:
    FEATS[extra] = True
NAME = "n3-rb-" + K + os.environ.get("RUN_TAG", "") + ("-" + "-".join(a.split("/")[-1] for a in sys.argv[2:]) if sys.argv[2:] else "")
AUD = config.out_dir() / (NAME + "-audit")
AUD.mkdir(parents=True, exist_ok=True)
for f in AUD.glob("*"):
    f.unlink()
ENV = {"PSX_RB_DET_SELFTEST": K, "PSX_RB_SNAP_AUDIT": str(AUD), "PSX_GL_MAX_DIM": "2048",
       "PSX_GL_HIRES_WINDOW": "0", "SDL_AUDIODRIVER": "dummy"}
V = "bbt.own-view.two-views."


class _Run:
    def __init__(self, client):
        self.client = client


with NetMatch(config.net_build(), NAME, [FEATS, FEATS], window=True, env=ENV) as m:
    host, guest = m.peers
    c, p = host.client, Player(_Run(host.client))
    assert c.wait_until(lambda: p.level() == 2, 6000, 2), "no language screen"
    c.wait_frames(200)
    assert p.language_to_title(), "no title"
    c.wait_frames(240); c.press(pad("start")); c.wait_frames(120)
    c.press(pad("cross")); c.wait_frames(60)
    c.press(pad("down")); c.wait_frames(30)
    c.press(pad("cross")); c.wait_frames(90)
    assert p.two_player_mode(), "no 2P"
    c.press(pad("cross"))
    assert c.wait_until(lambda: p.level() == LEVEL_GRANNY and p.players_ready(), 6000, 2), "no Granwich"
    loaded = host.status().get("tick", 0)
    print("Granwich at sim tick", loaded, "debug frame", c.frame(), flush=True)
    for _ in range(6):
        c.press(pad("cross")); c.wait_frames(60)
    rng = random.Random(3)
    names = ["cross", "circle", "square", "up", "down", "left", "right", "up", "up"]
    for i in range(150):
        for peer in m.peers:
            peer.client.press(pad(rng.choice(names)), frames=rng.choice((4, 8, 16)))
        host.client.wait_frames(10)
    m.check_alive()
    end = host.status().get("tick", 0)
    for peer in m.peers:
        st = peer.status()
        k = {kk[len(V):]: v for kk, v in peer.client.counters().items() if kk.startswith(V)}
        print("peer", peer.slot, "desync", st.get("desync"), "rollbacks", st.get("rollback"),
              "own view", k, flush=True)
compared, bad = m.compare_digests()
print("digests compared", compared, "differ", len(bad), bad[:5])
log = host.log_text()
rew = [int(x) for x in re.findall(r"det-selftest rewind mismatch=(\d+)", log)]
print("self-test rewinds:", len(rew), "in the level:", len([t for t in rew if t > loaded]), "ticks", loaded, "-", end)
# replay vs live, in the level only
seats = {int(re.search(r"p(-?\d+)", f.name)[1]): audit.parse(str(f)) for f in AUD.glob("snap-audit-p*.txt")}
for s, (live, rep) in sorted(seats.items()):
    n_rep = sum(len(v) for t, v in rep.items() if t > loaded)
    diffs = collections.Counter()
    first = {}
    for t, rows in sorted(rep.items()):
        if t <= loaded:
            continue
        for r in rows:
            for tag, (crc, h) in r.items():
                if t in live and tag in live[t] and live[t][tag][0] != crc:
                    name = audit.NAMES.get(tag, hex(tag))
                    diffs[name] += 1
                    first.setdefault(name, (t, audit.bytes_diff(live[t][tag][1], h)))
    print(f"seat {s}: {n_rep} replayed saves in the level; replay!=live by section: {dict(diffs)}", flush=True)
    for name, (t, b) in first.items():
        print(f"    first {name} at tick {t}: {b}")
if len(seats) > 1:
    common = set.intersection(*(set(v[0]) for v in seats.values()))
    level = [t for t in common if t > loaded]
    cross = collections.Counter()
    for t in level:
        for tag in seats[min(seats)][0][t]:
            vals = {s: v[0][t].get(tag, ("", ""))[0] for s, v in seats.items()}
            if len(set(vals.values())) > 1:
                cross[audit.NAMES.get(tag, hex(tag))] += 1
    print(f"seats compared at {len(level)} level ticks; differ by section: {dict(cross)}")
