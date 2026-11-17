"""Run the built game headless for a test and drive it like a player.

A GameRun starts the executable of a build directory with the debug server
on a free localhost port, a fresh memory-card / savestate folder under the
test output, and the mod features a test asks for. The build's
mods/state.toml (the player's own mod selection) is backed up first and put
back afterwards, also after a crash of an earlier run.

Game facts used here (all verified, NOTES.md):
  0x80010000  Bouze id of the current level (0 TITLE, 2 LANGUAGE, 4 GRANNY)
  0x800100A4  mode flags, bit 0x10 = 2-player mode
  0x8008DD04 / 0x8008DD08  player 1 / player 2 actor
  0x8008DE74  level exit flag (1 + a new id in 0x80010000 = level jump)
  actor +0x08 type (Bugs 0x05000181, Taz 0x05000190), +0x84 attach node,
  +0x8C first child, +0x98 resource list, +0xFC/+0xFE/+0x100 s16 position,
  +0x158 current animation record
"""
from __future__ import annotations

import os
import shutil
import socket
import subprocess
import time
from pathlib import Path

from . import config
from .client import DebugClient, DebugError, RELEASED, pad

LEVEL_ID = 0x80010000
MODE_FLAGS = 0x800100A4
MODE_TWO_PLAYER = 0x10
P1_ACTOR = 0x8008DD04
P2_ACTOR = 0x8008DD08
LEVEL_EXIT = 0x8008DE74
ACTOR_LISTS = (0x8008DD44, 0x8008DDCC)

LEVEL_TITLE, LEVEL_LANGUAGE, LEVEL_GRANNY = 0, 2, 4
LEVEL_VICA01, LEVEL_TRCA01 = 17, 55

TYPE_BUGS = 0x05000181
TYPE_TAZ = 0x05000190

STATE_BACKUP = "state.toml.bbt-tests-backup"
ABSENT_MARKER = "# bbt-tests: there was no state.toml before the tests\n"


def in_ram(p: int) -> bool:
    return 0x80000000 <= p < 0x80200000


def in_guest(p: int) -> bool:
    """Main RAM or the mods' guest memory (Expansion 1, KSEG0 0x9F000000)."""
    return in_ram(p) or 0x9F000000 <= p < 0x9F800000


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


# ---- mod selection --------------------------------------------------------

DEFAULT_PACKAGE = "bbt.third-character"


# Features the manifests switch on by default (2026-11-08: Quick start, the
# own view, the third player; 2026-11-10: the Turkish flag). A run selects
# exactly what it names: these are written as off unless named, so every
# test keeps the selection it was written for. "_defaults": True leaves the
# manifests' defaults alone (the tests of the defaults themselves).
DEFAULT_ON = ("p3", "bbt.own-view/two-views", "bbt.quick-start/quick-start",
              "bbt.decor/turkish-flag")
# Options whose default changed with that, kept as the tests were written:
# the third player appears only with one ("auto") and comes back 5 s after
# it is out ("auto", 2026-11-16; the tests keep the game's rule, "team"),
# the own view keeps the
# shared screen on one PC ("shared"), and Quick start goes straight to the
# main menu with the players from the controllers (the tests walk the
# language screen, PRESS START and the ONE / TWO PLAYERS menu themselves).
# A test that names the option wins.
TEST_OPTIONS = {"p3": {"appear": "always", "comeback": "team"}, "bbt.own-view/two-views": {"offline": "split"},
                "bbt.quick-start/quick-start": {"language": "ask", "skip_title": "false",
                                                "auto_players": "false"}}


def state_toml(features: dict) -> str:
    """mods/state.toml selecting mod features (the format the runtime writes,
    mod_packages.cpp). A key is a feature of bbt.third-character, or
    "<package>/<feature>"; a value is True / False, or a dict
    {"enabled": bool, "values": {option: value}}. Options not given keep
    their defaults, except TEST_OPTIONS; features in DEFAULT_ON that are not
    named are written as off (unless "_defaults": True)."""
    features = dict(features)
    keep_defaults = bool(features.pop("_defaults", False))
    if not keep_defaults:
        for key in DEFAULT_ON:
            features.setdefault(key, False)
    lines = ["format_version = 2", ""]
    entries = []
    for key, sel in features.items():
        pkg, _, fid = key.rpartition("/")
        on, values = (sel.get("enabled", True), sel.get("values", {})) if isinstance(sel, dict) else (sel, {})
        if on and not keep_defaults and key in TEST_OPTIONS:
            values = {**TEST_OPTIONS[key], **values}
        entries.append((pkg or DEFAULT_PACKAGE, fid, bool(on), values))
    for pkg in dict.fromkeys(e[0] for e in entries):
        lines += ["[[package]]", f'id = "{pkg}"', ""]
    for pkg, fid, on, values in entries:
        lines += ["[[feature]]", f'package_id = "{pkg}"', f'id = "{fid}"',
                  f"enabled = {'true' if on else 'false'}"]
        if values:
            lines.append("[feature.values]")
            lines += [f'{k} = "{v}"' for k, v in values.items()]
        lines.append("")
    return "\n".join(lines)


def restore_state(build: Path) -> None:
    """Put back a state.toml saved by an earlier, interrupted test run."""
    mods = build / "mods"
    backup = mods / STATE_BACKUP
    if not backup.is_file():
        return
    text = backup.read_text(encoding="utf-8")
    state = mods / "state.toml"
    if text == ABSENT_MARKER:
        state.unlink(missing_ok=True)
    else:
        shutil.copy2(backup, state)
    backup.unlink()


def protected_build(build: Path) -> bool:
    """build-release holds the player's own mod selection; tests never run
    there unless BBT_ALLOW_RELEASE=1."""
    return build.resolve().name == "build-release" and os.environ.get("BBT_ALLOW_RELEASE") != "1"


# ---- game config ----------------------------------------------------------

def three_player_toml(src: Path, dst: Path, multitap_port: int = 2) -> Path:
    """A copy of game.toml for three players: [game] players = 3 and an
    SCPH-1070 multitap on `multitap_port` (needs a 5-seat build). Relative
    paths are made absolute so the copy works from the test output."""
    out, section, has_controller, tap_done = [], None, False, False
    for line in src.read_text(encoding="utf-8").splitlines():
        s = line.strip()
        if s.startswith("[") and s.endswith("]"):
            if section == "controller" and not tap_done:
                out.append(f"multitap_port = {multitap_port}")
                tap_done = True
            section = s[1:-1]
            has_controller |= section == "controller"
        elif section == "game" and s.startswith("players"):
            line = "players = 3"
        elif section == "game" and (s.startswith("exe ") or s.startswith("exe=") or s.startswith("disc")):
            key, _, val = line.partition("=")
            v = val.strip().strip('"')
            if v and not Path(v).is_absolute():
                line = f'{key.strip()} = "{(src.parent / v).resolve().as_posix()}"'
        elif section == "controller" and s.startswith("multitap_port"):
            line, tap_done = f"multitap_port = {multitap_port}", True
        out.append(line)
    if section == "controller" and not tap_done:
        out.append(f"multitap_port = {multitap_port}")
    elif not has_controller:
        out += ["", "[controller]", f"multitap_port = {multitap_port}"]
    dst.write_text("\n".join(out) + "\n", encoding="utf-8")
    return dst


def game_toml_copy(src: Path, dst: Path) -> Path:
    """A copy of game.toml with [game] exe / disc made absolute, for runs
    that may write to their game config: the launcher stores the player's
    multitap-analog choice into the --game file on PLAY (runtime main.cpp,
    upsert_game_toml_controller_bool), which must never be the repository's
    game.toml."""
    out, section = [], None
    for line in src.read_text(encoding="utf-8").splitlines():
        s = line.strip()
        if s.startswith("[") and s.endswith("]"):
            section = s[1:-1]
        elif section == "game" and (s.startswith("exe ") or s.startswith("exe=") or s.startswith("disc")):
            key, _, val = line.partition("=")
            v = val.strip().strip('"')
            if v and not Path(v).is_absolute():
                line = f'{key.strip()} = "{(src.parent / v).resolve().as_posix()}"'
        out.append(line)
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text("\n".join(out) + "\n", encoding="utf-8")
    return dst


# ---- the run --------------------------------------------------------------

class GameRun:
    def __init__(self, build: Path, name: str, features: dict[str, bool] | None = None,
                 toml: Path | None = None):
        self.build = Path(build)
        self.name = name
        self.features = features or {}
        self.toml = Path(toml) if toml else config.game_toml()
        self.dir = config.out_dir() / name
        self.proc: subprocess.Popen | None = None
        self.client: DebugClient | None = None
        self._log = None

    def shot(self, tag: str) -> Path:
        p = self.dir / f"{tag}.png"
        self.client.screenshot(p.resolve())
        return p

    def log_tail(self, n: int = 40) -> str:
        p = self.dir / "game.log"
        if not p.is_file():
            return ""
        return "\n".join(p.read_text(encoding="utf-8", errors="replace").splitlines()[-n:])

    def __enter__(self) -> "GameRun":
        exe = config.exe_in(self.build)
        if exe is None:
            raise FileNotFoundError(f"no game executable in {self.build}")
        if protected_build(self.build):
            raise PermissionError(f"{self.build} holds the player's own mod selection")
        cue = config.disc_cue()
        if cue is None or not cue.is_file():
            raise FileNotFoundError("disc not found (game.toml [game] disc or BBT_DISC)")
        if self.dir.exists():
            shutil.rmtree(self.dir)
        cards = self.dir / "cards"
        cards.mkdir(parents=True)

        mods = self.build / "mods"
        mods.mkdir(exist_ok=True)
        restore_state(self.build)
        state = mods / "state.toml"
        backup = mods / STATE_BACKUP
        if state.is_file():
            shutil.copy2(state, backup)
        else:
            backup.write_text(ABSENT_MARKER, encoding="utf-8")
        state.write_text(state_toml(self.features), encoding="utf-8")

        port = free_port()
        self._log = open(self.dir / "game.log", "w", encoding="utf-8")
        env = dict(os.environ, PSX_DEBUG_SERVER="1")
        cmd = [str(exe), "--game", str(self.toml), "--disc", str(cue), "--headless",
               "--debug-port", str(port), "--memcard-dir", str(cards.resolve())]
        self._log.write("$ " + " ".join(cmd) + "\n")
        self._log.flush()
        try:
            self.proc = subprocess.Popen(cmd, cwd=str(config.PROJECT), stdout=self._log,
                                         stderr=subprocess.STDOUT, env=env)
            self.client = DebugClient(port)
            deadline = time.monotonic() + 120.0
            while not self.client.ping():
                if self.proc.poll() is not None:
                    raise RuntimeError(f"the game exited ({self.proc.returncode}):\n{self.log_tail()}")
                if time.monotonic() > deadline:
                    raise RuntimeError(f"the debug server did not answer:\n{self.log_tail()}")
                time.sleep(0.3)
        except BaseException:
            self.__exit__(None, None, None)
            raise
        return self

    def __exit__(self, *exc) -> None:
        try:
            if self.proc and self.proc.poll() is None:
                if self.client:
                    self.client.quit()
                try:
                    self.proc.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    self.proc.kill()
                    self.proc.wait(timeout=10)
        finally:
            restore_state(self.build)
            if self._log:
                self._log.close()
                self._log = None


# ---- playing --------------------------------------------------------------

class Player:
    """Reads the game state and plays through menus like a person would."""

    def __init__(self, run: GameRun):
        self.run = run
        self.c = run.client

    # state
    def level(self) -> int:
        return self.c.u32(LEVEL_ID)

    def two_player_mode(self) -> bool:
        return bool(self.c.u8(MODE_FLAGS) & MODE_TWO_PLAYER)

    def actor(self, player: int) -> int:
        a = self.c.u32(P1_ACTOR if player == 1 else P2_ACTOR)
        return a if in_ram(a) else 0

    def actor_type(self, a: int) -> int:
        return self.c.u32(a + 0x08)

    def pos(self, a: int) -> tuple[int, int, int]:
        import struct
        return struct.unpack("<3h", self.c.read(a + 0xFC, 6))

    def players_ready(self) -> bool:
        a1, a2 = self.actor(1), self.actor(2)
        return bool(a1 and a2 and self.actor_type(a1) == TYPE_BUGS and self.actor_type(a2) == TYPE_TAZ)

    def anim_records(self, a: int) -> list[tuple[int, int, int, int]]:
        """(resource id, animation id, record address, keyframe pointer) of
        every animation entry (kind 4) in the actor's resource list (main
        RAM or mod memory: the Phase 4 third actor's list is there)."""
        out, node = [], self.c.u32(a + 0x98)
        for _ in range(512):
            if not in_guest(node):
                break
            e = self.c.u32(node + 8)
            if in_guest(e) and self.c.u16(e + 8) == 4:
                rec = self.c.u32(e + 0x18)
                kf = self.c.u32(rec + 0x0C) if in_guest(rec) else 0
                out.append((self.c.u16(e + 6), self.c.u16(e + 0x0A), rec, kf))
            node = self.c.u32(node)
        return out

    # menus
    def boot_to_language_select(self) -> bool:
        """Cold boot: logos, then the language screen (level 2) from ~f1440."""
        if not self.c.wait_until(lambda: self.level() == LEVEL_LANGUAGE, 4000, 10):
            return False
        while self.c.frame() < 1500:
            self.c.wait_frames(10)
        return True

    def language_to_title(self) -> bool:
        self.c.press(pad("cross"))
        return bool(self.c.wait_until(lambda: self.level() == LEVEL_TITLE, 900, 5))

    def title_to_two_player_game(self) -> bool:
        """PRESS START -> NEW GAME -> TWO PLAYERS (needs a pad in port 2) ->
        START GAME, skip the intro, until Granwich (level 4) is loaded with
        Bugs and Taz registered as player 1 and 2."""
        c = self.c
        c.wait_frames(240)
        c.press(pad("start")); c.wait_frames(120)
        c.press(pad("cross")); c.wait_frames(60)          # NEW GAME
        c.release(port=2); c.wait_frames(30)              # plug in pad 2
        c.press(pad("down")); c.wait_frames(30)
        c.press(pad("cross")); c.wait_frames(90)          # TWO PLAYERS
        if not self.two_player_mode():
            return False
        c.press(pad("cross")); c.wait_frames(300)         # START GAME
        c.press(pad("cross")); c.wait_frames(120)
        c.press(pad("select"))                            # skip the intro
        if not c.wait_until(lambda: self.level() == LEVEL_GRANNY, 2400, 10):
            return False
        return bool(c.wait_until(self.players_ready, 1200, 10))

    MENU_IDENTITY = 225                                  # the title level's menu actor
    PAGE_MAIN, PAGE_PLAYERS, PAGE_START = 0x0102, 0x0103, 0x0104

    def menu_page(self) -> int | None:
        """The title menu's page (its behaviour state id), None outside it."""
        p, n = self.c.u32(0x8008DDCC), 0
        while 0x80000000 <= p < 0x80200000 and n < 300:
            if self.c.u16(p + 0x34) == self.MENU_IDENTITY:
                node = self.c.u32(p + 0x178)
                return self.c.u16(node + 4) if 0x80000000 <= node < 0x80200000 else None
            p = self.c.u32(p); n += 1
        return None

    def straight_to_game(self, two: bool = True) -> bool:
        """Quick start's menus (language, skip_title, auto_players): the
        main menu comes up by itself; NEW GAME goes to START GAME, or, on
        one PC with pad 2 in, to the game's ONE / TWO PLAYERS page (then
        ONE or TWO PLAYERS is chosen here); START GAME to Granwich."""
        c = self.c
        if not c.wait_until(lambda: self.level() == LEVEL_TITLE and self.menu_page() == self.PAGE_MAIN,
                            3000, 2):
            return False
        c.wait_frames(60)
        if two:
            c.release(port=2); c.wait_frames(30)         # plug in pad 2
        c.press(pad("cross"))                             # NEW GAME
        if not c.wait_until(lambda: self.menu_page() in (self.PAGE_PLAYERS, self.PAGE_START), 300, 1):
            return False
        if self.menu_page() == self.PAGE_PLAYERS:          # asked: one PC, pad 2 in
            c.wait_frames(30)
            # the page keeps its cursor (script variable 0x7F, 1 = TWO PLAYERS)
            if bool(c.u8(0x80093950 + 0x7F)) != two:
                c.press(pad("down")); c.wait_frames(30)
            c.press(pad("cross"))                         # ONE / TWO PLAYERS
            if not c.wait_until(lambda: self.menu_page() == self.PAGE_START, 300, 1):
                return False
        c.wait_frames(30)
        c.press(pad("cross"))                             # START GAME
        if not c.wait_until(lambda: self.level() == LEVEL_GRANNY, 2400, 10):
            return False
        return bool(c.wait_until(self.players_ready, 1200, 10))

    def until_control(self, presses: int = 6, attempts: int = 6) -> bool:
        """Close the intro texts / dialog boxes with ✕ until pad 2 moves
        Taz (and back a little)."""
        c = self.c
        for _ in range(presses):
            c.press(pad("cross")); c.wait_frames(150)
        for _ in range(attempts):
            taz = self.actor(2)
            if taz:
                x0 = self.pos(taz)
                c.hold(pad("right"), port=2); c.wait_frames(30); c.release(port=2)
                c.wait_frames(20)
                x1 = self.pos(taz)
                if abs(x1[0] - x0[0]) + abs(x1[2] - x0[2]) > 16:
                    c.hold(pad("left"), port=2); c.wait_frames(30); c.release(port=2)
                    c.wait_frames(30)
                    return True
            c.press(pad("cross")); c.wait_frames(150)
        return False

    def jump_to_level(self, level: int, done_counters: tuple[str, ...] = ()) -> bool:
        """The verified level jump: new id + exit flag, then wait for the
        load. `done_counters` are mod counters of which one is bumped at the
        very end of every load (the old actors stay registered until then,
        so "players ready" alone does not mean loaded); without them, a
        fixed wait."""
        c = self.c

        def done() -> int:
            cs = c.counters()
            return sum(cs.get(k, 0) for k in done_counters)

        before = done()
        c.write_u32(LEVEL_ID, level)
        c.write_u32(LEVEL_EXIT, 1)
        if done_counters:
            if not c.wait_until(lambda: done() > before, 3000, 10):
                return False
        else:
            c.wait_frames(600)
        return bool(c.wait_until(lambda: self.level() == level and self.players_ready(), 2400, 10))
