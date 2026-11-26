#!/usr/bin/env python3
"""Soak: which VRAM below x = 512 does any level use during play? (BUGS #25)

Not part of run_tests.py (it takes most of an hour). The third player's
textures live in the 16-row strip x 512..1023, y 240..255 that no level
writes, so blocks taller than 16 rows lose every second row. This finds
VRAM a full-height texture could use instead: for every level id with both
heroes (the level tour's route: build-p3, three players, Daffy, Two views,
Quick start), after the load it samples VRAM x 0..511, y 0..511 every 120
frames through ~30 s of random play on all three seats, once in the pause
menu, and marks every pixel that is non-zero or changes in any sample.

A pixel the game only ever writes with zero is not seen; the full-height
placement is therefore checked afterwards end to end (the third actor's
pixel hashes after play in every level: soak_level_tour.py).

Result 2026-11-26 (32 levels): x 488..511, y 256..510 was the one tall area
unused in all of them (trca01_profile.h P3_TALL_*, TRCA01_SPARE_*).

Output: build-tests/out/soak_vram/used_<level>.bin (512x512 bits, row-major,
bit set = used), report.json, and union.bin over all played levels.
Usage (repository root):  python3 tests/soak_vram_free.py [first last]
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
from bbt.client import pad  # noqa: E402
from bbt.game import GameRun, Player, three_player_toml  # noqa: E402
from soak_level_tour import FEATS, SKIP, BUTTONS, boot_to_granwich  # noqa: E402

W, H, TILE = 512, 512, 128
SAMPLE_EVERY, PLAY_FRAMES = 120, 1800


def read_vram(c) -> list[int]:
    px = [0] * (W * H)
    for ty in range(0, H, TILE):
        for tx in range(0, W, TILE):
            r = c.send({"cmd": "vram_peek", "x": tx, "y": ty, "w": TILE, "h": TILE})
            hx = r["hex"]
            for row in range(TILE):
                base = (ty + row) * W + tx
                off = row * TILE * 4
                for col in range(TILE):
                    px[base + col] = int(hx[off + col * 4: off + col * 4 + 4], 16)
    return px


def mark(used: bytearray, first: list[int], now: list[int]) -> None:
    for i, v in enumerate(now):
        if v or v != first[i]:
            used[i >> 3] |= 1 << (i & 7)


def play_and_sample(g, rng: random.Random, used: bytearray, first: list[int]) -> int:
    c, samples = g.client, 0
    for step in range(PLAY_FRAMES // 20):
        for port in (1, 2):
            c.hold(pad(rng.choice(BUTTONS)), port=port)
        c.mod_cmd("bbt.p3", f"{pad(rng.choice(BUTTONS)):04X}")
        c.wait_frames(20)
        if (step * 20) % SAMPLE_EVERY == 0:
            mark(used, first, read_vram(c))
            samples += 1
    c.release(port=1); c.release(port=2); c.mod_cmd("bbt.p3", "off")
    c.press(pad("start")); c.wait_frames(90)            # the pause menu
    mark(used, first, read_vram(c)); samples += 1
    c.press(pad("start")); c.wait_frames(60)
    return samples


def main(argv: list[str]) -> int:
    first_id, last_id = (int(argv[0]), int(argv[1])) if len(argv) >= 2 else (4, 70)
    tool = shutil.which("caffeinate")
    if tool:
        subprocess.Popen([tool, "-u", "-d", "-i", "-w", str(os.getpid())])
    out = config.out_dir() / "soak_vram"
    out.mkdir(parents=True, exist_ok=True)
    toml = three_player_toml(config.game_toml(), out / "game_3p.toml")
    rng = random.Random(20261126)
    union = bytearray(W * H // 8)
    report, level = [], first_id
    while level <= last_id:
        with GameRun(config.p3_build(), f"soak_vram/run_{level}", FEATS, toml=toml) as g:
            c, p = g.client, Player(g)
            if not boot_to_granwich(g):
                report.append({"level": level, "result": "boot failed"})
                level += 1
                continue
            while level <= last_id:
                if level in SKIP:
                    level += 1
                    continue
                if not p.two_player_mode():
                    break
                entry = {"level": level}
                t0 = time.monotonic()
                try:
                    ok = p.jump_to_level(level)
                except Exception as e:  # noqa: BLE001
                    entry.update(result=f"no answer: {e}")
                    report.append(entry)
                    level += 1
                    break
                if not ok:
                    entry.update(result="did not load", now=p.level())
                    report.append(entry)
                    level += 1
                    try:
                        if not p.jump_to_level(4):
                            break
                    except Exception:  # noqa: BLE001
                        break
                    continue
                c.wait_frames(240)
                for _ in range(4):
                    c.press(pad("cross")); c.wait_frames(60)
                used = bytearray(W * H // 8)
                first = read_vram(c)
                mark(used, first, first)
                samples = 1 + play_and_sample(g, rng, used, first)
                (out / f"used_{level}.bin").write_bytes(bytes(used))
                for i, b in enumerate(used):
                    union[i] |= b
                entry.update(result="played", samples=samples,
                             used_pixels=sum(bin(b).count("1") for b in used),
                             spawned=bool(c.mod_cmd("bbt.p3actor", "show").get("state")),
                             seconds=round(time.monotonic() - t0, 1))
                report.append(entry)
                print(json.dumps(entry), flush=True)
                level += 1
        (out / "report.json").write_text(json.dumps(report, indent=1), encoding="utf-8")
        (out / "union.bin").write_bytes(bytes(union))
    (out / "report.json").write_text(json.dumps(report, indent=1), encoding="utf-8")
    (out / "union.bin").write_bytes(bytes(union))
    played = [r for r in report if r["result"] == "played"]
    print(f"\n{len(report)} ids: {len(played)} played; union "
          f"{sum(bin(b).count('1') for b in union)} of {W * H} pixels used", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
