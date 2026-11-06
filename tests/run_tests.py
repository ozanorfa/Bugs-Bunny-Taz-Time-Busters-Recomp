#!/usr/bin/env python3
"""Runs the project's tests (see tests/README.md).

  python3 tests/run_tests.py                 every tier (what cannot run is skipped)
  python3 tests/run_tests.py unit disc       hermetic + disc tiers (about 15 s)
  python3 tests/run_tests.py game -k trca01  only matching game tests

Tiers:
  c-unit       hermetic C tests of the mod's host-side logic (bbt_unit_tests)
  python-unit  hermetic tests of tools/bouze and the test helpers
  unit         both of the above
  disc         offline checks on the player's own disc (needs game.toml's disc)
  game         end-to-end: the built game runs headless, driven through the
               debug server (needs build-dev and, for the 3-player test,
               build-p3; several minutes)

Exit status: 0 passed, 1 failed, 77 nothing could run (all skipped).
Unless --no-build is given, the tests' own CMake project (tests/) is
configured and built into build-tests/ first. Test output (logs,
screenshots) goes to build-tests/out/, which git ignores.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import time
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parent
TIERS = ("c-unit", "python-unit", "disc", "game")
MODULE_PREFIX = {"python-unit": "test_unit_", "disc": "test_disc_", "game": "test_game_"}
GAME_ORDER = ("test_game_stock", "test_game_trca01", "test_game_p3")


def build_tests(build_dir: Path) -> None:
    if not (build_dir / "CMakeCache.txt").is_file():
        cmd = ["cmake", "-S", str(HERE), "-B", str(build_dir), "-DCMAKE_BUILD_TYPE=Release"]
        if shutil.which("ninja"):
            cmd += ["-G", "Ninja"]
        subprocess.run(cmd, check=True)
    subprocess.run(["cmake", "--build", str(build_dir), "--config", "Release"], check=True)


def run_c_unit(exe: Path, pattern: str | None) -> tuple[int, int]:
    """(tests run, failures). The binary prints one line per test."""
    if not exe.is_file():
        print(f"c-unit: skipped ({exe} not built)")
        return 0, 0
    r = subprocess.run([str(exe)] + ([pattern] if pattern else []), capture_output=True, text=True)
    sys.stdout.write(r.stdout)
    sys.stderr.write(r.stderr)
    lines = r.stdout.splitlines()
    ran = sum(1 for line in lines if line.startswith(("ok ", "FAIL ")))
    if r.returncode != 0 and ran == 0 and pattern:
        return 0, 0                                     # no test matches the pattern
    return ran, (0 if r.returncode == 0 else max(1, sum(1 for line in lines if line.startswith("FAIL"))))


def load_modules(tier: str, pattern: str | None) -> unittest.TestSuite:
    loader = unittest.TestLoader()
    if pattern:
        loader.testNamePatterns = [f"*{pattern}*"]
    names = sorted(p.stem for p in HERE.glob(f"{MODULE_PREFIX[tier]}*.py"))
    if tier == "game":
        names = [n for n in GAME_ORDER if n in names] + [n for n in names if n not in GAME_ORDER]
    suite = unittest.TestSuite()
    for n in names:
        suite.addTests(loader.loadTestsFromName(n))
    return suite


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("tiers", nargs="*", default=["all"],
                    help="c-unit, python-unit, unit, disc, game or all (default)")
    ap.add_argument("-k", dest="pattern", help="only tests whose name contains this")
    ap.add_argument("--no-build", action="store_true", help="do not build tests/ first")
    ap.add_argument("--tests-build", type=Path, default=PROJECT / "build-tests")
    ap.add_argument("--unit-exe", type=Path)
    ap.add_argument("--probe", type=Path)
    ap.add_argument("--game-build", type=Path, help="2-player build with debug tools (build-dev)")
    ap.add_argument("--p3-build", type=Path, help="5-seat build with debug tools (build-p3)")
    ap.add_argument("--disc", type=Path, help="disc .cue or image (default: game.toml [game] disc)")
    ap.add_argument("--out", type=Path, help="test output directory (default build-tests/out)")
    ap.add_argument("-q", "--quiet", action="store_true")
    a = ap.parse_args(argv)

    tiers: list[str] = []
    for t in a.tiers:
        expand = {"all": list(TIERS), "unit": ["c-unit", "python-unit"]}.get(t, [t])
        for x in expand:
            if x not in TIERS:
                ap.error(f"unknown tier {t!r}")
            if x not in tiers:
                tiers.append(x)

    env = {"BBT_TESTS_BUILD": a.tests_build, "BBT_UNIT_EXE": a.unit_exe, "BBT_PROBE": a.probe,
           "BBT_GAME_BUILD": a.game_build, "BBT_P3_BUILD": a.p3_build, "BBT_DISC": a.disc,
           "BBT_OUT": a.out}
    for k, v in env.items():
        if v is not None:
            os.environ[k] = str(Path(v).resolve())
    sys.path.insert(0, str(HERE))
    from bbt import config  # noqa: E402  (after the environment is set)

    if not a.no_build and any(t in tiers for t in ("c-unit", "python-unit", "disc")):
        build_tests(config.tests_build())

    started = time.monotonic()
    ran = failed = skipped = 0
    for tier in tiers:
        print(f"\n===== {tier} =====", flush=True)
        if tier == "c-unit":
            n, f = run_c_unit(config.unit_exe(), a.pattern)
            ran, failed = ran + n, failed + f
            continue
        suite = load_modules(tier, a.pattern)
        if suite.countTestCases() == 0:
            continue
        res = unittest.TextTestRunner(verbosity=1 if a.quiet else 2).run(suite)
        ran += res.testsRun
        skipped += len(res.skipped)
        failed += len(res.failures) + len(res.errors) + len(res.unexpectedSuccesses)

    print(f"\n{ran} tests, {failed} failed, {skipped} skipped, "
          f"{time.monotonic() - started:.0f} s; output in {config.out_dir()}")
    if failed:
        return 1
    return 77 if ran == skipped else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
