# Notices

The MIT License in [LICENSE](LICENSE) covers the code and documents written
for this project: the mods (`src/mods/`), their packages, the tools, the
tests, the Windows scripts and the docs. The parts below keep their own
terms.

## Framework (git submodules)

| Path | Project | License |
|---|---|---|
| `psxrecomp/` | [psxrecomp](https://github.com/RetroPortingToolKit/psxrecomp), Copyright (c) 2026 Matthew Stanley | PolyForm Noncommercial 1.0.0 |
| `recomp-ui/` | [recomp-ui](https://github.com/RetroPortingToolKit/recomp-ui), Copyright (c) 2026 Matthew Stanley | MIT |

Each submodule ships its own license file and third-party notices. A built
game links the psxrecomp runtime, so a build may be used and shared for
noncommercial purposes only, under psxrecomp's license. The bundled OpenBIOS
is MIT-licensed; see `psxrecomp/bios/OpenBIOS.LICENSE`.

## Files started from psxrecomp's project templates

These files were created by psxrecomp's New Project Layout scaffold, or
copied from psxrecomp. The template material in them stays under psxrecomp's
license; this project's changes to them are under the MIT License.
- `CMakeLists.txt`, `codegen_setup.c`, `codegen_setup.h`, `.gitignore`;
- `scripts/package_release.sh`;
- `tools/sync_symbols.py`;
- `packaging/` (`linux/AppRun`, `linux/bbt.desktop`, `release.toml`,
  `release/START_HERE.txt`);
- `assets/psxrecomp.ico`, `assets/psxrecomp.png`, `assets/psxrecomp.svg`
  (the framework's default app icon).

## The original game

*Bugs Bunny & Taz: Time Busters* and everything on its disc belong to their
owners. This repository holds none of it, and no license here covers it.
Bugs Bunny, Taz, Daffy Duck and Looney Tunes are trademarks of Warner Bros.
Entertainment Inc. This is an unofficial fan project, not affiliated with or
endorsed by Warner Bros., Infogrames / Atari, Artificial Mind and Movement or
Sony.
