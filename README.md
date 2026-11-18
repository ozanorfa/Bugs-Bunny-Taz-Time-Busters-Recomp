# BugsBunnyTaz Recompiled

Static recompilation of **BugsBunnyTaz** built on
[psxrecomp](https://github.com/mstan/psxrecomp) and
[recomp-ui](https://github.com/RetroPortingToolKit/recomp-ui).

A native PC version of *Bugs Bunny & Taz: Time Busters* (PS1, USA) with
**Daffy Duck as a third playable character** and up to three players, each
on their own PC over LAN or Radmin VPN, each with their own full-screen
view. The mods (Quick start, the own view, the third player) are on by
default; players' guide: [docs/PLAYING_TOGETHER.md](docs/PLAYING_TOGETHER.md).

This repository holds no game data and no game code. You need your own
copy of the game (the USA disc), and the game is built from it on your own
machine (see "Legal" and "Quick start" below).

| | |
|---|---|
| Players | 2 (3 with Daffy: the 3-player config `game_3p.toml`) |
| Region | USA |
| Publisher | — |
| Year | — |

Scaffolded with the New Project Layout. See
`psxrecomp/docs/GAME_PROJECT_SETUP.md` for the full flow.

## Legal

You must own the original game. Disc images under `disc/` are gitignored and
must never be committed. Retail BIOS dumps are not redistributed and no C
derived from one may be committed; releases run on the bundled MIT OpenBIOS.

`generated/` (the recompiled game C) is **not** committed in this project:
it is derived from the disc, so every machine generates its own from its
own disc copy (`psxrecomp_cli.py generate`, below). So does every player's
PC from the players' ZIP (see "Releases" below).

Default app icon: `assets/psxrecomp.ico` (and `.png` / `.svg`) — Retro-themed controller mark from `psxrecomp/assets/`. Windows builds embed it via `APP_ICON`.

Optional box art under `launcher_assets/img/` may come from
[libretro-thumbnails](https://github.com/libretro-thumbnails/libretro-thumbnails)
(`Named_Boxarts`); see `BOXART_SOURCE.txt` when present.

## Quick start (dev)

Building on a Windows PC, the players' ZIP, and a LAN / Radmin VPN match
between two or three machines: see [docs/WINDOWS_AND_LAN.md](docs/WINDOWS_AND_LAN.md).
The disc copy sits next to the repository (`../disc/`, as `game.toml` says).

```bash
git submodule update --init --recursive
./psxrecomp/tools/ci/build_emitters.sh
python3 psxrecomp/psxrecomp_cli.py generate \
  --config game.toml --project-root . --disc ../disc/<your>.cue
# generated/ stays out of git (derived from the disc).
# Configures, builds, and stages overlay_toolchain/ beside the runtime so
# streamed overlays can be compiled to native shards (a bare cmake build
# skips that stage and leaves every overlay gap interpreted).
python3 psxrecomp/psxrecomp_cli.py rebuild \
  --config game.toml --project-root . --build-dir build-release --no-pgo
```

### Releases

The players' ZIP holds no game data and no game code: each player's PC
builds the game once from their own disc (`tools/make_player_zip.py`,
`windows/Setup.bat`; docs/WINDOWS_AND_LAN.md section 8, the same route as
the Fighting Force project). The framework's prebuilt packager below ships
code recompiled from the game's program, so it is for developers only, not
for sharing:

```bash
scripts/package_release.sh --channel public
# Use --channel playtest for a non-public package; --platform selects Windows/Linux.
```

On Windows PowerShell, use
`psxrecomp/tools/package_title_release.ps1 --channel public`. The generated
`packaging/release.toml` and player instructions define the title payload.
The shared builder owns regeneration, qualified build defaults, fresh private
PGO boot training on supported native toolchains, staging and verification.
Boot training is a build step, not gameplay qualification or a performance
claim. See `psxrecomp/docs/RELEASING_A_TITLE.md`.

The scaffold's *Release builds* CI workflow (tag `vX.Y.Z` or dispatch)
builds committed `generated/` C without a disc. This project does not
commit `generated/`, so that workflow does not apply here. The common
builder does not add macOS support.

Studio's current Export action still uses the old prebuilt argument contract;
use the shared command above for optimized releases until Studio adopts the
recipe/channel interface. Its packager detection and `packaging/` warning also
need that adoption; the generated recipe is maintained configuration, not a
forked packager.

## Symbols

Progressive map: `symbols.toml` → `python3 tools/sync_symbols.py` →
`psx_symbols.h` (`PSX_FN_*`). See `psxrecomp/docs/SYMBOLS.md`.

## Framework pins

Submodule gitlinks (`psxrecomp`, optional `recomp-ui`, nested `recomp-net`)
are authoritative. `framework_pins.txt` is an optional scaffold snapshot;
release CI logs SHAs with `record_pins.sh` but builds whatever the gitlinks
resolve to. Bump submodules deliberately — do not float on `main`/`master`
in release CI.

<!-- retcomm-readme-raid -->
---

<p align="center">
  <sub><b>R.A.I.D. — Retro AI Development</b> · a Discord for AI-assisted retro reverse-engineering, decomp &amp; recomp</sub>
</p>

<p align="center">
  <a href="https://discord.gg/Ad9BwSzctP"><img src=".github/raid-discord.png" alt="Join the Retro AI Development (R.A.I.D.) Discord" width="200"></a>
</p>
<!-- /retcomm-readme-raid -->
