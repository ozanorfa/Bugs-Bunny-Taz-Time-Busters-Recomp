#!/usr/bin/env python3
"""Build the players' game from this repository and your own disc.

Standard library only; the same steps on Windows and macOS. Run from the
repository root (the players' ZIP has the framework in place; a git clone
needs `git submodule update --init --recursive` first):

    python tools/bbt_build.py --disc "PATH/TO/Bugs Bunny & Taz - Time Busters (USA).cue"

1. Windows only: makes sure the framework's compiler pack (cmake-clang-v1)
   is there (`psxrecomp_cli.py ensure-toolchain`; downloaded once into the
   shared cache unless PSXRECOMP_TOOLCHAIN_DIR points at an extracted pack).
2. Generates the game's C from the disc (`psxrecomp_cli.py generate`): checks
   the dump's digests against game.toml [prepare_disc], copies it into disc/
   (your files are not touched) and recompiles SLUS_011.44 into generated/.
   Skipped when generated/ is already there (--regenerate forces it).
3. Builds the game (`psxrecomp_cli.py rebuild`) into build-game: 5
   controller seats (the third player is a multitap seat) and LAN / Direct
   IP netplay without online ICE; it opens the 3-player config by itself.
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CLI = ROOT / "psxrecomp" / "psxrecomp_cli.py"
MARKER = ROOT / "generated" / "SLUS_011.44_dispatch.c"
BUILD_DIR = "build-game"
EXE_NAME = "BugsBunnyTaz_Recompiled.exe" if os.name == "nt" else "BugsBunnyTaz_Recompiled"
CMAKE_EXTRA = ["-DBBT_MAX_PLAYERS=5", "-DPSX_NETPLAY=ON", "-DRNET_ENABLE_ICE=OFF"]


def run(cmd: list[str]) -> None:
    print("\n>> " + " ".join(f'"{c}"' if " " in c else c for c in cmd), flush=True)
    rc = subprocess.call(cmd, cwd=ROOT)
    if rc != 0:
        sys.exit(f"step failed (exit {rc}); the output above says why")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--disc", default="", help="your .cue (its .bin beside it); default: the copy in disc/")
    ap.add_argument("--build-dir", default=BUILD_DIR)
    ap.add_argument("--regenerate", action="store_true", help="generate the game C again")
    args = ap.parse_args()

    missing = [p for p in (CLI, ROOT / "psxrecomp" / "lib" / "recomp-net" / "CMakeLists.txt",
                           ROOT / "recomp-ui" / "CMakeLists.txt") if not p.exists()]
    if missing:
        sys.exit("framework checkout incomplete (missing " + ", ".join(str(m) for m in missing) +
                 "). In a git clone run: git submodule update --init --recursive")

    staged = ROOT / "disc" / "BugsBunnyTaz_TimeBusters_USA.cue"
    disc = Path(args.disc).expanduser().resolve() if args.disc else staged
    py = sys.executable
    common = ["--config", "game.toml", "--project-root", "."]

    if os.name == "nt":
        run([py, str(CLI), "ensure-toolchain", "--project-root", "."])

    if args.regenerate or not MARKER.exists():
        if not disc.is_file():
            sys.exit(f"no disc at {disc}: pass --disc with the path of your .cue")
        run([py, str(CLI), "generate", *common, "--disc", str(disc)])
    else:
        print("generated/ is there; not generating again (--regenerate to force)")

    run([py, str(CLI), "rebuild", *common, "--build-dir", args.build_dir, "--no-pgo",
         *[f"--cmake-extra={e}" for e in CMAKE_EXTRA]])

    exe = ROOT / args.build_dir / EXE_NAME
    if not exe.is_file():
        sys.exit(f"build finished but {exe} is missing")
    print(f"\nbuilt {exe}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
