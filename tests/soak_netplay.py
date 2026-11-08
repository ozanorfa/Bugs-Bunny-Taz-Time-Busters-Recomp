#!/usr/bin/env python3
"""Soak: a long three-PC match (build-net, three peers on 127.0.0.1 in
rollback mode, each in a hidden OpenGL window, the 3-player config, p3 =
Daffy, Two views, Quick start), random play on every seat.

Not part of run_tests.py (it runs ~10 minutes of play). Each peer's own pad
drives its own seat. Checked every minute: every peer still answers, no
desync is reported, no own-view draw hit the watchdog, each peer draws only
its own seat's view; at the end the machine digests of all three agree
over the whole match. Results: build-tests/out/soak_net/report.json.

Usage (repository root):  python3 tests/soak_netplay.py [minutes]
"""
from __future__ import annotations

import json
import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from bbt import config  # noqa: E402
from bbt.client import pad  # noqa: E402
from bbt.game import LEVEL_GRANNY, Player, three_player_toml  # noqa: E402
from bbt.net import NetMatch  # noqa: E402

FEATS = {"p3": True, "bbt.own-view/two-views": True, "bbt.quick-start/quick-start": True}
SMALL_GL = {"PSX_GL_MAX_DIM": "2048", "PSX_GL_HIRES_WINDOW": "0", "SDL_AUDIODRIVER": "dummy"}
A3 = "bbt.third-character.p3-actor."
V = "bbt.own-view.two-views."
BUTTONS = ["up", "down", "left", "right", "cross", "square", "circle", "triangle"]


class _Run:
    def __init__(self, client):
        self.client = client


def main(argv: list[str]) -> int:
    minutes = int(argv[0]) if argv else 10
    out = config.out_dir() / "soak_net"
    out.mkdir(parents=True, exist_ok=True)
    toml = three_player_toml(config.game_toml(), out / "game_3p.toml")
    rng = random.Random(8112026)
    report = {"minutes": minutes, "checks": []}
    with NetMatch(config.net_build(), "soak_net/match", [FEATS] * 3, toml=toml, window=True,
                  env=SMALL_GL) as m:
        host = m.peers[0]
        c, p = host.client, Player(_Run(host.client))
        assert c.wait_until(lambda: p.level() == 2, 6000, 2), "no language screen"
        c.wait_frames(200)
        assert p.language_to_title(), "no title"
        c.wait_frames(240); c.press(pad("start")); c.wait_frames(120)
        c.press(pad("cross")); c.wait_frames(60)
        c.press(pad("down")); c.wait_frames(30)
        c.press(pad("cross")); c.wait_frames(90)
        c.press(pad("cross"))
        assert c.wait_until(lambda: p.level() == LEVEL_GRANNY and p.players_ready(), 6000, 2), "no Granwich"
        loaded_at = c.frame()
        assert c.wait_until(lambda: all(pe.client.counters().get(A3 + "spawned") for pe in m.peers), 900, 5)
        for _ in range(8):
            c.press(pad("cross")); c.wait_frames(150)
        for minute in range(minutes):
            for _ in range(60 * 60 // 20):                       # a minute of 20-frame steps
                for pe in m.peers:
                    pe.client.hold(pad(rng.choice(BUTTONS)))
                c.wait_frames(20)
            for pe in m.peers:
                pe.client.release()
            m.check_alive()
            row = {"minute": minute + 1, "level": p.level(), "peers": []}
            for pe in m.peers:
                k = {kk[len(V):]: v for kk, v in pe.client.counters().items() if kk.startswith(V)}
                st = pe.status()
                row["peers"].append({
                    "slot": pe.slot, "desync": st.get("desync"), "tick": st.get("tick"),
                    "own_view": k.get(f"local_view_{pe.slot}", 0),
                    "other_views": sum(k.get(f"local_view_{v}", 0) for v in range(3) if v != pe.slot),
                    "watchdog": pe.client.send({"cmd": "render_pass_stats"}).get("watchdog"),
                    "lives": pe.client.u8(0x800100A0),
                })
            report["checks"].append(row)
            print(json.dumps(row), flush=True)
            (out / "report.json").write_text(json.dumps(report, indent=1), encoding="utf-8")
    compared, bad = m.compare_digests()
    report.update(compared=compared, digest_mismatches=bad[:20], loaded_at=loaded_at)
    (out / "report.json").write_text(json.dumps(report, indent=1), encoding="utf-8")
    problems = [pr for row in report["checks"] for pr in row["peers"]
                if pr["desync"] or pr["watchdog"] or pr["other_views"]]
    print(f"\n{compared} ticks compared, {len(bad)} differ; {len(problems)} peer problems", flush=True)
    return 1 if bad or problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
