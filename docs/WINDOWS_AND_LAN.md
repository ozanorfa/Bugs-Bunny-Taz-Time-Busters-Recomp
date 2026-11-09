# Windows build and a LAN / Radmin VPN match (Phase 6, step N5)

Status (2026-11-07): written on the Mac from the framework's own docs
(`psxrecomp/docs/GETTING_STARTED.md`, `BUILDING.md`, `NETPLAY.md`) and its
source. **Not yet run on a Windows PC.** A Mac <-> Windows match is not
promised by the framework docs either; the first match below is the test.

The goal: one PC per player, each showing its own character full screen
(the "Own view for every player" mod, on by default), connected over LAN or
Radmin VPN.

## What has to be identical on both machines

A match only starts, and only stays in step, when the peers run the same
game. The framework checks some of it, not all:

| Must match | Checked by the framework? | How to compare |
|---|---|---|
| This repo's commit (our mod code) | Through the mod packages' version (the netplay compatibility number, raised on every simulation change; a guest without the host's version is refused) | `git log -1 --oneline` |
| Framework and launcher commits | Yes (execution identity) | `git submodule status` |
| Execution identity | Yes, before the first frame | `"identity"` in `build-release-net\BugsBunnyTaz_Recompiled.execution.json` |
| Build options | Partly | use the exact commands below |
| The disc dump | Yes (disc fingerprint) | MD5 of the `.bin`, below |
| Selected mods | Yes (plan fingerprint) | a LAN room sends the host's selection to the guests |

If our code differs between the two machines, the match starts and then
drifts apart. So always move the same commit to both.

## 1. Tools on the PC (once)

As in `psxrecomp/docs/GETTING_STARTED.md`, section 2:

- [Git for Windows](https://git-scm.com/downloads/win) and
  [Python 3](https://www.python.org/downloads/windows/), both on PATH.
- `cmake-clang-v1-windows-x64.zip` from
  [the toolchain releases](https://github.com/RetroPortingToolKit/RetroPorting-Toolchains/releases/latest),
  extracted to, for example, `C:\RecompDev\Toolchain` (the folder that
  contains `bin\cmake.exe`).

Every new PowerShell window needs:

```powershell
$env:PSXRECOMP_TOOLCHAIN_DIR = 'C:\RecompDev\Toolchain'
$env:Path = "$env:PSXRECOMP_TOOLCHAIN_DIR\bin;$env:Path"
```

Check: `git --version`, `python --version`, `cmake --version`,
`ninja --version`, `clang --version` all print a version.

## 2. Move the project to the PC

The repo has no remote, so it travels as a git bundle. On the Mac, in
`BugsBunnyTazRecomp/`:

```bash
git bundle create ../BugsBunnyTazRecomp.bundle --all
```

Copy to the PC (USB stick or network share):

- `BugsBunnyTazRecomp.bundle`
- the disc: `disc/BugsBunnyTaz_TimeBusters_USA.cue` and `.bin` from the
  workspace folder (a copy; never the OpenEmu original).

The layout on the PC (the disc sits next to the repo, as on the Mac;
`game.toml` names it `../disc/...`):

```
C:\RecompDev\
  Toolchain\
  disc\BugsBunnyTaz_TimeBusters_USA.cue
  disc\BugsBunnyTaz_TimeBusters_USA.bin
  BugsBunnyTazRecomp\        (cloned below)
```

In PowerShell:

```powershell
Set-Location C:\RecompDev
git clone -b n4-own-views BugsBunnyTazRecomp.bundle BugsBunnyTazRecomp
Set-Location BugsBunnyTazRecomp
git submodule update --init --recursive
```

The submodules (psxrecomp, recomp-ui and the nested recomp-net) come from
their public GitHub repositories at the commits this repo pins.

Check against the Mac (both must print the same):

```powershell
git log -1 --oneline
git submodule status
```

Later updates: make a new bundle on the Mac, then on the PC
`git pull ..\BugsBunnyTazRecomp.bundle n4-own-views` and
`git submodule update --init --recursive`.

## 3. Check the disc

```powershell
Get-FileHash -Algorithm MD5 ..\disc\BugsBunnyTaz_TimeBusters_USA.bin
```

It must be `03FD0DC4AF76689E571EEAC73721602C` (the Mac's dump,
`disc_probe.json`). Another dump has another netplay disc fingerprint
and the match is refused.

## 4. Generate the game code and build

`generated/` is not in git (it is derived from the disc), so the PC makes
its own from the same disc:

```powershell
python psxrecomp\psxrecomp_cli.py generate --config game.toml --project-root . --disc ..\disc\BugsBunnyTaz_TimeBusters_USA.cue
python psxrecomp\psxrecomp_cli.py rebuild --config game.toml --project-root . --build-dir build-release-net --no-pgo --cmake-extra=-DBBT_MAX_PLAYERS=5 --cmake-extra=-DPSX_NETPLAY=ON --cmake-extra=-DRNET_ENABLE_ICE=OFF
```

These are the Mac's `build-release-net` options: Release, netplay on,
LAN / Direct IP only (no online ICE), 5 controller seats.

Check the identity (must equal the Mac's
`build-release-net/BugsBunnyTaz_Recompiled.execution.json`):

```powershell
Select-String '"identity"' build-release-net\BugsBunnyTaz_Recompiled.execution.json
```

First start: run `build-release-net\BugsBunnyTaz_Recompiled.exe`. The
launcher asks for the disc: pick `C:\RecompDev\disc\BugsBunnyTaz_TimeBusters_USA.cue`.
In Settings -> Input, set Player 1 to the controller this PC's player
uses. Try a short offline game first (boot, title, Granwich).

## 5. Radmin VPN

- Install Radmin VPN on both machines; one creates a network, the other
  joins it. Each machine gets a Radmin address (`26.x.x.x`).
- Firewalls: the host must accept UDP 7777.
  - Windows: allow the game when Windows Defender Firewall asks.
  - Mac: if the macOS firewall is on, allow incoming connections for
    `BugsBunnyTaz_Recompiled`.
- On a plain home LAN the same steps work with the LAN addresses.

## 6. The match

Start each game with its console output saved to a file, so the two runs
can be compared afterwards:

- Mac (Terminal, in `BugsBunnyTazRecomp/`):

  ```bash
  build-release-net/BugsBunnyTaz_Recompiled 2> ~/Desktop/mac_peer.log
  ```

- PC (PowerShell, in `BugsBunnyTazRecomp\`):

  ```powershell
  Start-Process .\build-release-net\BugsBunnyTaz_Recompiled.exe -Wait -RedirectStandardError "$HOME\Desktop\pc_peer.log"
  ```

Then:

1. **Host** (either machine): NETPLAY -> LAN / Direct IP -> Host Lobby.
   Advertised IP = the host's Radmin address, Port 7777 -> Create Lobby.
   Mods: nothing to select. Quick start, the own view and the third
   player (Daffy, only with three seated) are on by default; the room
   sends the host's selection to the guests.
2. **Guest:** NETPLAY -> LAN / Direct IP -> Join Direct: Host IP = the
   host's Radmin address, Port 7777.
3. Host presses PLAY. Seat P1 = host (Bugs), P2 = guest (Taz).
4. The game opens on its main menu (Quick start): NEW GAME -> START GAME
   (the players come from the room), play a few minutes in Granwich:
   walk apart, jump, talk to Granny.

## 7. What to look at and send back

- Did the room connect, and did the match start? (Messages such as "LAN
  content differs between peers" or a version / identity mismatch mean the
  machines are not identical; see the table at the top.)
- Does each screen show its own character full screen?
- Speed and smoothness on each machine.
- Both log files. Search them for `DIVERGE`: the framework compares the two
  machines' state every frame and prints `rb FIRST CORE DIVERGE sim=...` at
  the first difference. None means both machines ran the same game. The
  `rb live dig ... sim=...` lines every 32 ticks are the per-tick digests.

## 8. The package for the players (one ZIP)

The other players build nothing. One Windows PC builds the package, and
every player gets the same ZIP (`psxrecomp/docs/RELEASING_A_TITLE.md`).

Tools on that PC, besides Git (section 1). The packager uses MSYS2
MinGW-w64, not the clang toolchain of section 1:

- [MSYS2](https://www.msys2.org/), installed to `C:\msys64`. In its
  "MSYS2 MINGW64" shell:

  ```sh
  pacman -S --needed mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja
  ```

  This puts `cmake.exe`, `ninja.exe`, `gcc.exe`, `g++.exe` and
  `objdump.exe` in `C:\msys64\mingw64\bin` (the packager's default
  `--toolchain-bin`). MSYS2's `bash.exe` is used if the recompiled
  OpenBIOS has to be regenerated.
- Python 3.11+ (`py` or `python` on PATH).
- Internet on the first run: SDL3 and the bundled overlay compiler are
  downloaded and checked by SHA-256.

In PowerShell, in the clone from section 2 (`C:\RecompDev\BugsBunnyTazRecomp`,
the disc at `..\disc` as in section 3):

```powershell
psxrecomp/tools/package_title_release.ps1 --channel public --platform windows
```

The packager refuses a tree with uncommitted changes or a submodule off its
pin, so build from a clean clone. It regenerates the game code from the
disc, builds a Release with this repo's `packaging/release.toml` (the
3-player config, netplay, 5 seats), then checks and verifies the result:

- `dist\BugsBunnyTazRecomp-v<VERSION>-windows-x64.zip` (`VERSION` is the
  file in the repo root, now 0.2.0); inside it one folder,
  `BugsBunnyTaz_Recompiled\` (`packaging/release.toml` `zip_folder`), with
  the exe, `START_HERE.txt` and `PLAYING_TOGETHER.md`
- a `.sha256` file next to it

**The ZIP is what every player gets.** It holds no disc, BIOS dump, saves or
settings; the packager refuses a package with any of them. Every player
needs their own disc dump with the MD5 of section 3.

What a player meets (all in `START_HERE.txt`): extract the ZIP first (an
exe started from inside a ZIP runs from a temp folder without its data);
the exe is statically linked (the packager fails if it imports a DLL that
is not part of Windows), so nothing else is installed; it is not signed
(signing only with `WINDOWS_SIGN_PFX`), so Windows SmartScreen asks once
("More info" -> "Run anyway"); the first start asks for the disc; the
host's first room makes Windows Defender Firewall ask (allow Private and
Public). Settings, keys and saves are written next to the exe, so the
folder must be writable (not Program Files). Folders with non-ASCII names
(a Turkish user name, `OneDrive\Masaüstü`) rely on the exe's UTF-8
manifest (`psxrecomp/assets/windows/psxrecomp.manifest`, Windows 10 1903
and later); not tried on Windows yet.

Every player must run the same ZIP. Our mod packages and `VERSION` carry
one netplay compatibility number (0.<N>.0): a guest whose packages are
another version is refused by the room ("... is not installed"), so two
different builds cannot start a match. Raise N in `VERSION` and every
`mods/preloaded/packages/bbt.*` manifest (and its folder) whenever a
change alters what the game does (python-unit checks they agree).

## Known limits

- Three players over three PCs: the launcher's room takes its seat count
  from the config's `players`. The release package ships the 3-player
  config (`game_3p.toml`, generated from `game.toml` by
  `tools/make_3p_toml.py`) as its `game.toml`, and a local 5-seat build
  opens its staged `game_3p.toml` without `--game`, so the room has three
  seats. Verified with three launchers on one machine
  (`tests/test_game_lanroom.py` `test_three_pcs_in_one_room`); not yet on
  three real PCs. The player-facing steps are in `docs/PLAYING_TOGETHER.md`.
- The shareable package (section 8) has not been built yet; the
  framework's packager only runs on Windows and Linux.
- A player on the Mac with Windows players: the Mac builds are other
  builds than the Windows ZIP, and a Mac <-> Windows match is not promised
  by the framework docs. Until a real match shows it works, every player
  uses the same Windows ZIP.
- Overlay code is compiled per machine at runtime
  (`overlay_captures.json` next to the executable, not in git), so the two
  machines' overlay caches differ. The digest comparison above is what
  shows whether that matters.
