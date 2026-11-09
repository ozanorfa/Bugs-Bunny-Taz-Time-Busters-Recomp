"""Netplay matches between headless peers on this machine, for tests.

A NetMatch starts two peers from a netplay build (-DPSX_NETPLAY=ON, debug
tools) on 127.0.0.1, each told the other's address: slot 0 is the session
host (command-line LAN, docs/NETPLAY.md "LAN / Direct IP"). Each
peer runs from a folder of its own under the test output, holding a link
to the build's executable and a mods/ folder of its own (the runtime reads
<exe dir>/mods), so peers can select different mods and the build's own
mods/state.toml is never touched. Memory cards and logs are per peer too.

Every peer logs a digest of the whole machine every 32 simulation ticks
("rb live dig ... sim=<tick> core=... ram=... mod=... av=...", the
framework's own breadcrumb in psx_netplay.c). Two peers that agree on
every logged tick ran the same game.
"""
from __future__ import annotations

import os
import re
import shutil
import socket
import subprocess
import time
from pathlib import Path

from . import config
from .client import DebugClient
from .game import state_toml

DIGEST = re.compile(r"rb live dig \S+ sim=(\d+) core=(\w+) cpu=\w+ clk=\w+ tim=\w+ "
                    r"ram=(\w+) dirty=\w+ mod=(\w+) av=(\w+)")
# A rollback (the runtime's "rb begin epoch=.. load=N" / "rb follow epoch=..
# load=N", psx_netplay_rb.c; REFUSED, NACK and COOLDOWN lines replay
# nothing) replays from tick N: digests a
# peer logged for ticks >= N came from predicted input, so they are dropped
# unless the replay logs them again (the Fighting Force session's finding:
# kept, they show as false mismatches under lag).
ROLLBACK = re.compile(r"rb (?:begin|follow) epoch=\d+ .*?load=(\d+)")
CONTENT_DIFFERS = "LAN content differs between peers"


def free_udp_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def free_tcp_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


SECTIONS = re.compile(r"rb live dig \S+ sim=(\d+) (.*)")


def section_digests(log: Path) -> dict[int, dict[str, str]]:
    """sim tick -> every section of its "rb live dig" line (core, cpu, clk,
    tim, ram, dirty, mod, av, cd, spu, mdec, aux), same rollback rule as
    digests()."""
    out: dict[int, dict[str, str]] = {}
    if log.is_file():
        for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
            m = SECTIONS.search(line)
            if m:
                out[int(m.group(1))] = dict(kv.split("=", 1) for kv in m.group(2).split() if "=" in kv)
                continue
            r = ROLLBACK.search(line)
            if r:
                load = int(r.group(1))
                for tick in [t for t in out if t >= load]:
                    del out[tick]
    return out


def section_table(logs: list[Path], ticks: list[int]) -> dict:
    """For the given ticks: how often each section differs, and how often
    each peer is the odd one out (its core differs from all the others)."""
    per = [section_digests(lg) for lg in logs]
    sections: dict[str, int] = {}
    odd: dict[int, int] = {}
    for t in ticks:
        rows = [d.get(t, {}) for d in per]
        for k in rows[0]:
            if len({r.get(k) for r in rows}) > 1:
                sections[k] = sections.get(k, 0) + 1
        cores = [r.get("core") for r in rows]
        for i, v in enumerate(cores):
            if cores.count(v) == 1:
                odd[i] = odd.get(i, 0) + 1
    return {"sections": sections, "odd_peer": odd}


def digests(log: Path) -> dict[int, tuple[str, str, str, str]]:
    """sim tick -> (core, ram, mod, av) digests a peer logged for ticks that
    stayed final (see ROLLBACK)."""
    out = {}
    if log.is_file():
        for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
            m = DIGEST.search(line)
            if m:
                out[int(m.group(1))] = m.groups()[1:]
                continue
            r = ROLLBACK.search(line)
            if r:
                load = int(r.group(1))
                for tick in [t for t in out if t >= load]:
                    del out[tick]
    return out


class Peer:
    def __init__(self, slot: int, folder: Path):
        self.slot = slot
        self.dir = folder
        self.proc: subprocess.Popen | None = None
        self.client: DebugClient | None = None
        self.log_path = folder / "game.log"
        self._log = None

    def status(self) -> dict:
        return self.client.send({"cmd": "netplay_status"})

    def log_text(self) -> str:
        return self.log_path.read_text(encoding="utf-8", errors="replace") if self.log_path.is_file() else ""

    def digests(self) -> dict[int, tuple[str, str, str, str]]:
        return digests(self.log_path)


def _link(src: Path, dst: Path) -> None:
    try:
        dst.symlink_to(src.resolve(), target_is_directory=src.is_dir())
    except OSError:                         # no symlinks (Windows without the right): copy
        if src.is_dir():
            shutil.copytree(src, dst)
        else:
            shutil.copy2(src, dst)


# What the runtime reads next to its executable, besides mods/.
RUNTIME_FILES = ("bios", "assets", "game_options.toml")


def _peer_folder(build: Path, exe: Path, folder: Path, features: dict[str, bool]) -> Path:
    """<folder>/<exe name> -> the build's executable (and its bios/, assets/
    and game_options.toml), <folder>/mods = the build's bundled catalog +
    this peer's own state.toml."""
    folder.mkdir(parents=True)
    link = folder / exe.name
    _link(exe, link)
    for name in RUNTIME_FILES + (exe.name + ".execution.json",):
        if (build / name).exists():
            _link(build / name, folder / name)
    mods = folder / "mods"
    mods.mkdir()
    bundled = build / "mods" / "bundled"
    if bundled.is_dir():
        shutil.copytree(bundled, mods / "bundled")
    (mods / "state.toml").write_text(state_toml(features), encoding="utf-8")
    (folder / "cards").mkdir()
    return link


class NetMatch:
    """`with NetMatch(build, "name", [features of slot 0, of slot 1, ...]) as m:`

    Two peers dial each other; with three or more, slot 0 is the hub (host
    as relay: no --net-peer) and every other slot dials it, all with
    PSX_NET_SLOTS = the peer count (psx_netplay.c: slot 0, 3+ slots, no
    peer -> rnet_session_start_lan_hub).

    mode: "rollback" (the launcher's default) or "delay" (delay-sync).
    env: extra environment for every peer (e.g. PSX_RB_DET_SELFTEST).
    window: run each peer in a hidden OpenGL window (`--hidden-window
    --renderer opengl`) instead of headless, so the presenter exists (an own
    view drawn with psx_mod_render_local_view needs it).
    settings: a settings.toml copied into every peer's folder (else the
    runtime's defaults)."""

    def __init__(self, build: Path, name: str, features: list[dict[str, bool]],
                 toml: Path | None = None, mode: str = "rollback", env: dict | None = None,
                 window: bool = False, settings: Path | None = None):
        self.build = Path(build)
        self.name = name
        self.features = features
        self.toml = Path(toml) if toml else config.game_toml()
        self.mode = mode
        self.env = env or {}
        self.window = window
        self.settings = Path(settings) if settings else None
        self.dir = config.out_dir() / name
        self.peers: list[Peer] = []

    def __enter__(self) -> "NetMatch":
        exe = config.exe_in(self.build)
        if exe is None:
            raise FileNotFoundError(f"no game executable in {self.build}")
        cue = config.disc_cue()
        if cue is None or not cue.is_file():
            raise FileNotFoundError("disc not found (game.toml [game] disc or BBT_DISC)")
        if self.dir.exists():
            shutil.rmtree(self.dir)
        n = len(self.features)
        if n < 2:
            raise ValueError("NetMatch needs two or more peers")
        udp = [free_udp_port() for _ in range(n)]
        env = dict(os.environ, PSX_DEBUG_SERVER="1", PSX_NET_MODE=self.mode, **self.env)
        if n > 2:       # host as relay: slot 0 binds and waits, every guest dials it
            env["PSX_NET_SLOTS"] = str(n)
        try:
            for slot, feats in enumerate(self.features):
                peer = Peer(slot, self.dir / f"peer{slot}")
                link = _peer_folder(self.build, exe, peer.dir, feats)
                if self.settings:
                    shutil.copy2(self.settings, peer.dir / "settings.toml")
                port = free_tcp_port()
                peer_addr = udp[1 - slot] if n == 2 else udp[0]
                dial = [] if (n > 2 and slot == 0) else ["--net-peer", f"127.0.0.1:{peer_addr}"]
                display = ["--hidden-window", "--renderer", "opengl"] if self.window else ["--headless"]
                cmd = [str(link), "--game", str(self.toml.resolve()), "--disc", str(cue), *display,
                       "--debug-port", str(port), "--memcard-dir", str((peer.dir / "cards").resolve()),
                       "--netplay", "--net-slot", str(slot),
                       "--net-bind", f"127.0.0.1:{udp[slot]}", *dial]
                peer._log = open(peer.log_path, "w", encoding="utf-8")
                peer._log.write("$ " + " ".join(cmd) + "\n")
                peer._log.flush()
                peer.proc = subprocess.Popen(cmd, cwd=str(config.PROJECT), stdout=peer._log,
                                             stderr=subprocess.STDOUT, env=env)
                peer.client = DebugClient(port)
                self.peers.append(peer)
            deadline = time.monotonic() + 120.0
            for peer in self.peers:
                while not peer.client.ping():
                    self.check_alive()
                    if time.monotonic() > deadline:
                        raise RuntimeError(f"peer {peer.slot}: the debug server did not answer")
                    time.sleep(0.3)
        except BaseException:
            self.__exit__(None, None, None)
            raise
        return self

    def check_alive(self) -> None:
        for p in self.peers:
            if p.proc and p.proc.poll() is not None:
                tail = "\n".join(p.log_text().splitlines()[-30:])
                raise RuntimeError(f"peer {p.slot} exited ({p.proc.returncode}):\n{tail}")

    def __exit__(self, *exc) -> None:
        for p in self.peers:
            try:
                if p.proc and p.proc.poll() is None:
                    try:
                        p.client.quit()
                    except Exception:  # noqa: BLE001  (a peer that stopped answering)
                        pass
                    try:
                        p.proc.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        p.proc.kill()
                        p.proc.wait(timeout=10)
            finally:
                if p._log:
                    p._log.close()
                    p._log = None

    def compare_digests(self) -> tuple[int, list[int]]:
        """(ticks every peer logged, ticks where they differ)."""
        logs = [p.digests() for p in self.peers]
        common = sorted(set.intersection(*(set(d) for d in logs))) if logs else []
        bad = [t for t in common if len({d[t] for d in logs}) > 1]
        return len(common), bad
