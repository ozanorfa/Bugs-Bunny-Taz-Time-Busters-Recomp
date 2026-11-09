#!/usr/bin/env python3
"""Make the players' ZIP: everything a Windows player needs to build and play
Bugs Bunny & Taz: Time Busters Recompiled with THEIR OWN disc, without
installing anything.

    python3 tools/make_player_zip.py --python-zip PATH/python-3.12.10-embed-amd64.zip [--out DIR]

The ZIP holds no game data and no game code:
  BugsBunnyTaz3P/
    Setup.bat             double-click: copies to C:\\RecompDev, builds with the player's disc
    START_HERE.txt        the steps (windows/START_HERE.txt, the same headings as the
                          Fighting Force ZIP's START_HERE.txt)
    BugsBunnyTazRecomp/   this repository and its submodules at HEAD (git archive:
                          committed files only; no .git, no disc, no generated code,
                          no builds, no saves), plus .bbt_files (the file list)
    python/               python.org's Windows embeddable package, with three
                          search paths added (our tools, the framework's tools)

Built from the committed state: the working tree must be clean. A content
scan refuses disc images, memory cards, savestates, generated code and
retail BIOS names before the ZIP is written. Licences ride along (the
framework: PolyForm Noncommercial 1.0.0 with its PERMISSIONS.md where
present; recomp-ui, recomp-net, retcomm-rbengine: MIT; Python: its
LICENSE.txt). The same route as the Fighting Force project
(its tools/make_player_zip.py, 2026-11-09), so a player who installed one
game knows the other.
"""
from __future__ import annotations

import argparse
import fnmatch
import io
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOP = "BugsBunnyTaz3P"
PTH_EXTRA = ["../tools", "../psxrecomp/tools", "../psxrecomp"]
# Our files: nothing from the disc and no player data. The framework's public
# submodules are shipped as published (they hold their own tools, OpenBIOS and
# a test memory card); there only disc images and retail BIOS dumps refuse.
DISC_AND_BIOS = ["*.cue", "*.iso", "*.chd", "*.img", "*.ecm", "*.ccd", "*.sub", "*.pst",
                 "*scph*.bin", "*SCPH*.BIN", "*scph*.BIN", "*SCPH*.bin"]
FORBIDDEN = DISC_AND_BIOS + ["*.mcd", "*.mcr", "*.bin", "generated/*", "build-*/*", "disc/*",
                             "prepared_disc/*", "saves/*", "*overlay_captures.json*", "SLUS_*"]
SUBMODULES = ("psxrecomp/", "recomp-ui/")


def git(*args: str, cwd: Path = ROOT) -> str:
    return subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True, text=True).stdout


def archive_into(repo: Path, commit: str, dest: Path) -> int:
    data = subprocess.run(["git", "archive", "--format=tar", commit], cwd=repo, check=True,
                          capture_output=True).stdout
    n = 0
    with tarfile.open(fileobj=io.BytesIO(data)) as t:
        for m in t.getmembers():
            if m.isfile():
                target = dest / m.name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(t.extractfile(m).read())
                n += 1
    return n


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--python-zip", required=True, help="python.org python-3.x-embed-amd64.zip")
    ap.add_argument("--out", default=str(ROOT.parent / "dist"))
    ap.add_argument("--stage", default="", help="also leave the unpacked tree here (for a test build)")
    args = ap.parse_args()
    if git("status", "--porcelain", "--untracked-files=no").strip():
        sys.exit("the working tree has uncommitted changes: commit first (the ZIP is built from HEAD)")
    head = git("rev-parse", "--short", "HEAD").strip()
    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    stage = Path(tempfile.mkdtemp(prefix="bbtzip-"))
    src = stage / TOP / "BugsBunnyTazRecomp"
    try:
        n = archive_into(ROOT, "HEAD", src)
        print(f"repository at {head}: {n} files")
        for line in git("submodule", "status", "--recursive").splitlines():
            sha, path = line[1:].split()[:2]
            k = archive_into(ROOT / path, sha, src / path)
            print(f"  {path} at {sha[:8]}: {k} files")
        files = sorted(p.relative_to(src).as_posix() for p in src.rglob("*") if p.is_file())

        def refused(f: str) -> bool:
            pats = DISC_AND_BIOS if f.startswith(SUBMODULES) else FORBIDDEN
            return any(fnmatch.fnmatch(f, pat) or fnmatch.fnmatch(Path(f).name, pat) for pat in pats)
        bad = [f for f in files if refused(f)]
        if bad:
            sys.exit(f"refused: {len(bad)} files look like game data: {bad[:10]}")
        (src / ".bbt_files").write_text("\n".join(files + [".bbt_files", "SOURCE_VERSION"]) + "\n",
                                        encoding="utf-8")
        (src / "SOURCE_VERSION").write_text(f"{version} {head}\n", encoding="utf-8")
        py = stage / TOP / "python"
        with zipfile.ZipFile(args.python_zip) as z:
            z.extractall(py)
        pth = next(py.glob("python3*._pth"))
        lines = pth.read_text().splitlines()
        pth.write_text("\r\n".join(lines[:2] + PTH_EXTRA + lines[2:]) + "\r\n")
        print(f"python: {pth.name} search paths {lines[:2] + PTH_EXTRA}")
        shutil.copy2(src / "windows" / "Setup.bat", stage / TOP / "Setup.bat")
        start = (src / "windows" / "START_HERE.txt").read_text(encoding="utf-8").replace("@VERSION@", version)
        (stage / TOP / "START_HERE.txt").write_bytes(start.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8"))
        for bat in (stage / TOP / "Setup.bat", src / "windows" / "Play.bat"):
            if bat.read_bytes().replace(b"\r\n", b"").count(b"\n"):
                sys.exit(f"{bat.name} has LF line endings in the archive (.gitattributes *.bat eol=crlf)")
        out = Path(args.out)
        out.mkdir(parents=True, exist_ok=True)
        zpath = out / f"{TOP}_v{version}_{head}.zip"
        with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            for p in sorted((stage / TOP).rglob("*")):
                if p.is_file():
                    z.write(p, p.relative_to(stage).as_posix())
        total = sum(1 for p in (stage / TOP).rglob("*") if p.is_file())
        print(f"wrote {zpath} ({zpath.stat().st_size / 1e6:.1f} MB, {total} files)")
        if args.stage:
            keep = Path(args.stage)
            if keep.exists():
                shutil.rmtree(keep)
            shutil.copytree(stage / TOP, keep)
            print(f"unpacked tree kept at {keep}")
        return 0
    finally:
        shutil.rmtree(stage, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
