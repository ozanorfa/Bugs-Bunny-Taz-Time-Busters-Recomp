#!/usr/bin/env python3
"""Setup helper run by windows/Setup.bat (works on macOS too), after it put
this folder in place: a copy from the players' ZIP (no Git; the ZIP lists
its files in .bbt_files) or a git clone / pull from a bundle.

1. With Git: fetches the framework and the launcher at the pinned commits
   (git submodules; long paths for this repository only). In the ZIP they
   are already in the folder.
2. Decides whether the game's C must be generated again: on the first build,
   and whenever the files the recompiler reads changed since the last
   generation (framework, game.toml, symbols, seeds, ...). That is a SHA-256
   over those files, kept in generated/.bbt_gen_key. Changes to our mods,
   tools, docs, launcher files or the 3-player config only need a rebuild.
3. Builds build-game with tools/bbt_build.py and your disc.
4. Remembers the disc path in disc_source.txt (ignored by Git) for the next
   update.
5. Makes the desktop icon from the game's own title screen
   (tools/bbt_icon.py: BugsBunnyTaz.ico, once; a failure keeps the
   default icon).

    python tools/bbt_setup.py --disc "PATH/your.cue" [--dry-run]

Standard library only. The pattern comes from the Fighting Force session's
tools/ff_setup.py (2026-11-09).
"""
from __future__ import annotations

import argparse
import hashlib
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MARKER = ROOT / "generated" / "SLUS_011.44_dispatch.c"
KEY_FILE = ROOT / "generated" / ".bbt_gen_key"
NOTE = ROOT / "disc_source.txt"
ICON = ROOT / "BugsBunnyTaz.ico"    # made on this PC from the game's title screen (ignored by Git)
EXE = ROOT / "build-game" / ("BugsBunnyTaz_Recompiled.exe" if os.name == "nt" else "BugsBunnyTaz_Recompiled")
FILE_LIST = ROOT / ".bbt_files"     # the players' ZIP: every file it holds, one per line
# Paths a change to which needs no new game C. Everything else in the source
# (psxrecomp, game.toml, symbols, seeds, codegen_setup.*, ...) counts.
BUILD_ONLY = ("src/", "tools/", "docs/", "windows/", "mods/", "assets/", "packaging/", "scripts/",
              "tests/", "launcher_assets/", ".github/", "recomp-ui/", "README.md", "VERSION",
              "CMakeLists.txt", "game_3p.toml", "game_options.toml", "framework_pins.txt",
              ".gitignore", ".gitattributes", ".bbt_files", "SOURCE_VERSION")


def git(*args: str, capture: bool = False) -> str:
    cmd = ["git", "-c", "core.longpaths=true", *args]
    if capture:
        return subprocess.run(cmd, cwd=ROOT, check=True, capture_output=True, text=True).stdout
    print(">> git " + " ".join(args), flush=True)
    if subprocess.call(cmd, cwd=ROOT) != 0:
        sys.exit(f"git {' '.join(args)} failed; the output above says why")
    return ""


def source_files() -> list[str]:
    if FILE_LIST.is_file():
        return [l for l in FILE_LIST.read_text(encoding="utf-8").splitlines() if l]
    if (ROOT / ".git").exists():
        return [l for l in git("ls-files", "--recurse-submodules", capture=True).splitlines() if l]
    sys.exit("no .bbt_files and no .git: this folder is neither the players' ZIP nor a clone")


def generation_key() -> str:
    h = hashlib.sha256()
    for rel in sorted(source_files()):
        if rel.startswith(BUILD_ONLY):
            continue
        p = ROOT / rel
        if not p.is_file():
            continue
        h.update(rel.encode("utf-8") + b"\0")
        h.update(hashlib.sha256(p.read_bytes()).digest())
    return h.hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--disc", required=True, help="your .cue (its .bin beside it)")
    ap.add_argument("--dry-run", action="store_true", help="say what would be done, build nothing")
    args = ap.parse_args()
    disc = Path(args.disc).expanduser()
    if not disc.is_file():
        sys.exit(f"no disc at {disc}")

    if (ROOT / ".git").exists() and not FILE_LIST.is_file() and not args.dry_run:
        git("submodule", "sync", "--recursive")
        git("submodule", "update", "--init", "--recursive")
    key = generation_key()
    last = KEY_FILE.read_text().strip() if KEY_FILE.is_file() else ""
    if not MARKER.exists():
        regen, why = True, "first build"
    elif key != last:
        regen, why = True, "the files the recompiler reads changed" if last else "no record of the last generation"
    else:
        regen, why = False, "the recompiler's inputs are unchanged"
    print(f"\ngenerate the game C: {'yes' if regen else 'no'} ({why})", flush=True)
    cmd = [sys.executable, str(ROOT / "tools" / "bbt_build.py"), "--disc", str(disc)]
    if regen and MARKER.exists():
        cmd.append("--regenerate")
    if args.dry_run:
        print("would run:", " ".join(cmd))
        return 0
    # Setup.bat reads this back with `set /p`, which decodes the console's
    # code page (OEM, e.g. 857 on a Turkish Windows), not UTF-8. A path that
    # code page cannot hold is not remembered: Setup.bat then asks again.
    try:
        NOTE.write_text(str(disc) + "\n", encoding="oem" if os.name == "nt" else "utf-8")
    except (OSError, UnicodeError, LookupError):
        NOTE.unlink(missing_ok=True)
    print(">> " + " ".join(f'"{c}"' if " " in c else c for c in cmd), flush=True)
    rc = subprocess.call(cmd, cwd=ROOT)
    if rc != 0:
        sys.exit(f"build failed (exit {rc}); the output above says why")
    if MARKER.exists():
        KEY_FILE.write_text(key + "\n")
    if not ICON.is_file() and EXE.is_file():
        print("\nmaking the desktop icon from the game's title screen (the game runs hidden "
              "for a minute) ...", flush=True)
        subprocess.call([sys.executable, str(ROOT / "tools" / "bbt_icon.py"), "--exe", str(EXE),
                         "--game", str(ROOT / "build-game" / "game_3p.toml"),
                         "--disc", str(ROOT / "disc" / "BugsBunnyTaz_TimeBusters_USA.cue"),
                         "--out", str(ICON)], cwd=ROOT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
