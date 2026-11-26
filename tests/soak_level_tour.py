#!/usr/bin/env python3
"""Soak: a tour of every level id with the full three-player setup (build-p3,
3-player config, p3 = Daffy, Two views, Quick start), offline.

Not part of run_tests.py (it takes most of an hour). For each level id in
the range: the verified level jump (NOTES.md "Level jump"), then, if the
level loaded with both heroes, ~20 s of random play on all three seats; it
records whether the third character spawned, the mod's error counters, the
frame rate, whether his textures are still in VRAM as uploaded (BUGS #25:
the tall area below x = 512 must stay unwritten), and a screenshot. A level that does not load in time, or a game
that stops answering, is recorded and the game is started again for the
next id. Results: build-tests/out/soak_tour/report.json and the screenshots.

Usage (repository root):  python3 tests/soak_level_tour.py [first last]
"""
from __future__ import annotations

import json
import os
import random
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from bbt import config  # noqa: E402
from bbt.client import fnv1a32_pixels, pad  # noqa: E402
from bbt.game import GameRun, Player, three_player_toml  # noqa: E402

FEATS = {"p3": True, "bbt.own-view/two-views": True, "bbt.quick-start/quick-start": True}
A3 = "bbt.third-character.p3-actor."
H3 = "bbt.third-character.p3-hud."
V = "bbt.own-view.two-views."
# not levels to play: 0 title, 2 language screen (they leave 2-player mode),
# 1 / 3 / 5 menus or screens without the heroes
SKIP = {0, 1, 2, 3, 5}
ERRORS = ("spawn_bad_list", "model_unusable", "donor_unusable", "daffy_failed", "hold_unavailable")
BUTTONS = ["up", "down", "left", "right", "cross", "square", "circle", "triangle"]
VBLANKS, VIEW_PASSES = 0x8008DE20, 0x8008DE88   # vblanks; view passes (views per frame)


def boot_to_granwich(g) -> bool:
    c, p = g.client, Player(g)
    if not c.wait_until(lambda: p.level() == 2, 3000, 2):
        return False
    c.wait_frames(200)
    if not p.language_to_title():
        return False
    c.wait_frames(240); c.press(pad("start")); c.wait_frames(120)
    c.press(pad("cross")); c.wait_frames(60); c.release(port=2); c.wait_frames(30)
    c.press(pad("down")); c.wait_frames(30); c.press(pad("cross")); c.wait_frames(150)
    c.press(pad("cross"))
    if not c.wait_until(lambda: p.level() == 4 and p.players_ready(), 3000, 2):
        return False
    for _ in range(8):
        c.press(pad("cross")); c.wait_frames(150)
    return True


def random_play(g, frames: int, rng: random.Random) -> None:
    c = g.client
    for _ in range(frames // 20):
        for port in (1, 2):
            c.hold(pad(rng.choice(BUTTONS)), port=port)
        c.mod_cmd("bbt.p3", f"{pad(rng.choice(BUTTONS)):04X}")
        c.wait_frames(20)
    c.release(port=1); c.release(port=2); c.mod_cmd("bbt.p3", "off")


def textures_changed(c) -> list[str]:
    """The third character's blocks and CLUTs whose VRAM no longer holds
    what the plugin uploaded (empty when he has none)."""
    bad = []
    for gi in range(c.mod_cmd("bbt.p3actor", "show").get("groups", 0)):
        g = c.mod_cmd("bbt.p3actor", f"group {gi}")
        if not g.get("ok"):
            continue
        x, y, w, h = g["block"]
        if f"{fnv1a32_pixels(c.vram(x, y, w, h)):08X}" != g["block_fnv"]:
            bad.append(f"block {gi} at ({x},{y})")
        cx, cy = g["clut"]
        if f"{fnv1a32_pixels(c.vram(cx, cy, 16, 1)):08X}" != g["clut_fnv"]:
            bad.append(f"clut {gi} at ({cx},{cy})")
    return bad


def main(argv: list[str]) -> int:
    first, last = (int(argv[0]), int(argv[1])) if len(argv) >= 2 else (4, 70)
    # macOS: no sleep during the tour (a sleeping Mac stalls the run)
    tool = shutil.which("caffeinate")
    if tool:
        subprocess.Popen([tool, "-u", "-d", "-i", "-w", str(os.getpid())])
    out = config.out_dir() / "soak_tour"
    out.mkdir(parents=True, exist_ok=True)
    toml = three_player_toml(config.game_toml(), out / "game_3p.toml")
    rng = random.Random(20261108)
    report, level = [], first
    while level <= last:
        with GameRun(config.p3_build(), f"soak_tour/run_{level}", FEATS, toml=toml) as g:
            c, p = g.client, Player(g)
            if not boot_to_granwich(g):
                report.append({"level": level, "result": "boot failed", "log": g.log_tail(20)})
                level += 1
                continue
            while level <= last:
                if level in SKIP:
                    level += 1
                    continue
                if not p.two_player_mode():
                    break                     # 2-player mode lost: boot again
                t0 = time.monotonic()
                k0 = c.counters()
                entry = {"level": level}
                try:
                    ok = p.jump_to_level(level)
                except Exception as e:  # noqa: BLE001  (the game stopped answering)
                    entry.update(result=f"no answer: {e}")
                    report.append(entry)
                    level += 1
                    break
                if not ok:
                    entry.update(result="did not load", now=p.level())
                    report.append(entry)
                    level += 1
                    # back to a known state for the next id
                    try:
                        if not p.jump_to_level(4):
                            break
                    except Exception:  # noqa: BLE001
                        break
                    continue
                c.wait_frames(240)
                for _ in range(4):
                    c.press(pad("cross")); c.wait_frames(60)
                v0, f0 = c.u32(VBLANKS), c.u32(VIEW_PASSES)
                random_play(g, 1200, rng)
                v1, f1 = c.u32(VBLANKS), c.u32(VIEW_PASSES)
                views = max(1, c.u8(0x8008DE00))
                k = c.counters()
                d = {key: k.get(key, 0) - k0.get(key, 0) for key in k if k.get(key, 0) != k0.get(key, 0)}
                entry.update(
                    result="played",
                    views=c.u8(0x8008DE00),
                    spawned=d.get(A3 + "spawned", 0),
                    clone=c.mod_cmd("bbt.p3actor", "show").get("state"),
                    hud_updates=d.get(H3 + "update", 0),
                    errors={e: d[A3 + e] for e in ERRORS if d.get(A3 + e)},
                    textures_changed=textures_changed(c) if d.get(A3 + "spawned") else [],
                    frames_per_vblank=round((f1 - f0) / views / max(1, v1 - v0), 3),
                    lives=c.u8(0x800100A0),
                    seconds=round(time.monotonic() - t0, 1),
                )
                g.shot(f"level_{level}")
                report.append(entry)
                print(json.dumps(entry), flush=True)
                level += 1
        (out / "report.json").write_text(json.dumps(report, indent=1), encoding="utf-8")
    (out / "report.json").write_text(json.dumps(report, indent=1), encoding="utf-8")
    played = [r for r in report if r["result"] == "played"]
    bad = [r for r in played if r.get("errors") or r.get("textures_changed")]
    print(f"\n{len(report)} ids: {len(played)} played, {len(bad)} with errors; "
          f"{sum(1 for r in played if r.get('spawned'))} with the third character", flush=True)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
