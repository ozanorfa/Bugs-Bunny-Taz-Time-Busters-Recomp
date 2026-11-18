# Bugs Bunny & Taz: Time Busters — Recompiled

A native PC version of *Bugs Bunny & Taz: Time Busters* (PS1, USA), made by
static recompilation with [psxrecomp](https://github.com/RetroPortingToolKit/psxrecomp)
and its launcher [recomp-ui](https://github.com/RetroPortingToolKit/recomp-ui).
It runs on macOS and Windows.

It adds **Daffy Duck as a third playable character**, for up to three
players. They can share one PC, or each play on their own PC over a LAN or
Radmin VPN, each with a full-screen view of their own player. The mods are on
by default:
- Quick start;
- each player's own view;
- the third player;
- a Turkish flag at the windows of Granny's house.

Players' guide: [docs/PLAYING_TOGETHER.md](docs/PLAYING_TOGETHER.md).

**This repository holds no game data and no game code.** You need your own
copy of the game (the USA disc, SLUS-01144). The game is built from that copy
on your own machine.

| | |
|---|---|
| Players | 1-3 (the third player is Daffy) |
| Region | USA (SLUS-01144) |
| Original developer | Artificial Mind and Movement |
| Original publisher | Infogrames |
| Year | 2000 |

## Playing on Windows

Players use a players' ZIP. It holds no game data and no game code. Each PC
builds the game once from its own disc:
1. Extract the ZIP.
2. Double-click `Setup.bat`.
3. Choose the disc once.
4. Play from the desktop shortcut.

The ZIP is made with `tools/make_player_zip.py`. See
[windows/START_HERE.txt](windows/START_HERE.txt) and
[docs/WINDOWS_AND_LAN.md](docs/WINDOWS_AND_LAN.md), section 8.

## Building (developers)

[docs/WINDOWS_AND_LAN.md](docs/WINDOWS_AND_LAN.md) covers three topics:
- building on a Windows PC;
- the players' ZIP;
- a LAN or Radmin VPN match between two or three machines.

The disc copy sits next to the repository (`../disc/`, as `game.toml` says).

```bash
git clone --recurse-submodules https://github.com/ozanorfa/Bugs-Bunny-Taz-Time-Busters-Recomp.git BugsBunnyTazRecomp
cd BugsBunnyTazRecomp
./psxrecomp/tools/ci/build_emitters.sh
python3 psxrecomp/psxrecomp_cli.py generate \
  --config game.toml --project-root . --disc ../disc/<your>.cue
# generated/ stays out of git (it is derived from the disc).
# rebuild configures, builds and stages overlay_toolchain/ beside the runtime,
# so streamed overlays can be compiled to native code (a bare cmake build
# skips that step and leaves every overlay gap interpreted).
python3 psxrecomp/psxrecomp_cli.py rebuild \
  --config game.toml --project-root . --build-dir build-release --no-pgo
```

Three players need `game_3p.toml`, which `tools/make_3p_toml.py` writes from
`game.toml`. The mods' code is in [src/mods/](src/mods), and their packages
are in [mods/preloaded/packages/](mods/preloaded/packages). The tests are in
[tests/](tests).

The framework's prebuilt packager (`scripts/package_release.sh`) ships code
recompiled from the game's program. It is for developers only, not for
sharing; see `psxrecomp/docs/RELEASING_A_TITLE.md`.

### Symbols

`symbols.toml` is the progressive symbol map. `python3 tools/sync_symbols.py`
turns it into `psx_symbols.h` (`PSX_FN_*`). See `psxrecomp/docs/SYMBOLS.md`.

### Framework pins

The submodule gitlinks are authoritative: `psxrecomp`, `recomp-ui` and the
nested `recomp-net`. `framework_pins.txt` records the same commits. Bump the
submodules deliberately, one tested step at a time.

## Legal

- You must own the original game. Disc images, BIOS dumps, the recompiled
  game code (`generated/`) and anything else taken from the disc are
  gitignored and are never committed. Releases run on the bundled,
  MIT-licensed OpenBIOS.
- This is an unofficial fan project. It is not affiliated with or endorsed
  by Warner Bros., Infogrames / Atari, Artificial Mind and Movement or Sony.
- Bugs Bunny, Taz, Daffy Duck and Looney Tunes are trademarks of Warner Bros.
  Entertainment Inc.

## License

The code and documents written for this project are under the
[MIT License](LICENSE): use, change and share them as you like. See
[NOTICE.md](NOTICE.md) for the parts that keep other terms:

| Part | License |
|---|---|
| [psxrecomp](https://github.com/RetroPortingToolKit/psxrecomp) (submodule) | PolyForm Noncommercial 1.0.0 |
| [recomp-ui](https://github.com/RetroPortingToolKit/recomp-ui) (submodule) | MIT |
| Files started from psxrecomp's project templates | psxrecomp's license for the template material |

A built game includes the psxrecomp runtime, so you can use and share a build
for noncommercial purposes only.

The original game and everything on its disc belong to their owners. No
license here covers them.
