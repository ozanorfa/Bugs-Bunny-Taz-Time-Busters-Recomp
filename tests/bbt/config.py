"""Where things are: the project, the player's disc, the builds, the test
output. run_tests.py fills the BBT_* environment variables; every value has
a default so a single test module can also be run on its own."""
from __future__ import annotations

import os
import re
import sys
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[2]
TESTS = PROJECT / "tests"
EXE_SUFFIX = ".exe" if sys.platform == "win32" else ""


def _env_path(name: str, default: Path | None) -> Path | None:
    v = os.environ.get(name)
    return Path(v) if v else default


def out_dir() -> Path:
    """Test output (logs, screenshots, temporary memory cards). Under
    build-tests/, which .gitignore covers (/build-*/)."""
    d = _env_path("BBT_OUT", PROJECT / "build-tests" / "out")
    d.mkdir(parents=True, exist_ok=True)
    return d


def tests_build() -> Path:
    return _env_path("BBT_TESTS_BUILD", PROJECT / "build-tests")


def _built(name: str) -> Path:
    """A test executable: single-config generators put it in the build
    directory, multi-config ones (Visual Studio, Xcode) in a per-config one."""
    b = tests_build()
    for d in (b, b / "Release", b / "RelWithDebInfo", b / "Debug"):
        p = d / f"{name}{EXE_SUFFIX}"
        if p.is_file():
            return p
    return b / f"{name}{EXE_SUFFIX}"


def unit_exe() -> Path:
    return _env_path("BBT_UNIT_EXE", _built("bbt_unit_tests"))


def probe_exe() -> Path:
    return _env_path("BBT_PROBE", _built("bbt_disc_probe"))


def game_build() -> Path:
    """The 2-player build with debug tools (PSX_DEBUG_TOOLS=ON)."""
    return _env_path("BBT_GAME_BUILD", PROJECT / "build-dev")


def p3_build() -> Path:
    """A build with five controller seats (-DBBT_MAX_PLAYERS=5) and debug tools."""
    return _env_path("BBT_P3_BUILD", PROJECT / "build-p3")


def game_toml() -> Path:
    return _env_path("BBT_GAME_TOML", PROJECT / "game.toml")


def _toml_disc(text: str) -> str | None:
    try:
        import tomllib  # Python 3.11+
        return tomllib.loads(text).get("game", {}).get("disc")
    except ModuleNotFoundError:
        section = None
        for line in text.splitlines():
            line = line.strip()
            m = re.match(r"^\[([^\]]+)\]$", line)
            if m:
                section = m.group(1)
            elif section == "game":
                m = re.match(r'^disc\s*=\s*"([^"]*)"', line)
                if m:
                    return m.group(1)
    return None


def disc_cue() -> Path | None:
    """The player's disc as game.toml names it ([game] disc), or BBT_DISC."""
    v = os.environ.get("BBT_DISC")
    if v:
        return Path(v)
    toml = game_toml()
    if not toml.is_file():
        return None
    d = _toml_disc(toml.read_text(encoding="utf-8"))
    if not d:
        return None
    p = Path(d)
    return p if p.is_absolute() else (toml.parent / p)


def disc_image() -> Path | None:
    """The data track image: the .bin a .cue names (first FILE line), or the
    file itself. None when the disc is not there."""
    cue = disc_cue()
    if cue is None or not cue.is_file():
        return None
    if cue.suffix.lower() != ".cue":
        return cue
    for line in cue.read_text(encoding="utf-8", errors="replace").splitlines():
        m = re.match(r'\s*FILE\s+"([^"]+)"', line, re.I) or re.match(r"\s*FILE\s+(\S+)", line, re.I)
        if m:
            b = cue.parent / m.group(1)
            return b if b.is_file() else None
    return None


def exe_in(build: Path) -> Path | None:
    """The game executable of a build directory."""
    name_file = build / "psxrecomp_exe_name-psx-runtime.txt"
    names = []
    if name_file.is_file():
        names.append(name_file.read_text(encoding="utf-8").strip())
    names.append("BugsBunnyTaz_Recompiled")
    for n in names:
        p = build / f"{n}{EXE_SUFFIX}"
        if p.is_file():
            return p
    return None
