# Bugs Bunny & Taz: Time Busters — Recompiled

An unofficial fan port of the **PlayStation (PS1) game** *Bugs Bunny & Taz:
Time Busters* (2000). It runs as a native program on Windows and macOS and
adds **Daffy Duck as a third playable character**. Up to three players can
share one PC, or each play on their own PC over a LAN or Radmin VPN, each
with a full-screen view of their own player.

![Bugs, Taz and Daffy in Granwich: Daffy kicks](docs/images/three_players.jpg)

**This repository holds no game data and no game code.** You need your own
copy of the original PlayStation game (the USA disc, SLUS-01144). The game is
built from that copy on your own machine.

- New here? [The original game](#the-original-game) and
  [What this project is](#what-this-project-is) explain both.
- Players: the guide with every step for a match is
  [docs/PLAYING_TOGETHER.md](docs/PLAYING_TOGETHER.md).
- What changed in each version: [CHANGELOG.md](CHANGELOG.md).

## The original game

*Bugs Bunny & Taz: Time Busters* is a 3D platform game with two heroes. It
was made by Artificial Mind and Movement and published by Infogrames under
license from Warner Bros. It came out in 2000 on the PlayStation and on
Windows PCs, as an indirect sequel to *Bugs Bunny: Lost in Time* (1999).
This project starts from the PlayStation version only.

**The story.** Daffy Duck, working as a pest exterminator, breaks Granny's
time machine, the Time Regulator. He is thrown back in time with its core,
the Time Gem, and the machine's gears scatter across history. Granny sends
Bugs Bunny and her pet Taz after them: collect the gears, find the gem and
bring Daffy back.

**The world.** Granwich, Granny's town, is the hub. From there the heroes
travel to four eras: Aztec, Viking, Arabian and Transylvanian. The gears
they collect open the next era, and each era ends with a boss.

**How it plays.**
- Bugs and Taz move and fight differently, and some places and enemies need
  both of them. For example, Bugs can jump on Taz's head to reach higher.
- **One player:** you control one hero and the computer plays the other.
  R1 swaps which hero you control.
- **Two players:** controller 1 is Bugs and controller 2 is Taz, on one
  shared screen with one camera. There is no split screen.
- Daffy is part of the story but is never playable.
- The game is in English, French or Spanish, chosen at the start.

| | Original game | This project |
|---|---|---|
| Runs on | PlayStation (a separate Windows release exists too) | Windows and macOS, as a native program built from the PlayStation disc |
| Players | 1-2: Bugs and Taz | 1-3: Bugs, Taz and Daffy |
| Screen | one shared screen | in a match, each PC shows its own player full screen; on one PC, the shared screen or a split screen |
| Play over a network | none | LAN or Radmin VPN, up to three PCs |
| Disc | USA, SLUS-01144 | the same disc: your own copy |

## What this project is

*Recompiled* means that the game's program is not interpreted from the disc,
as an emulator does. The [psxrecomp](https://github.com/RetroPortingToolKit/psxrecomp)
tools read the program from your disc, translate it into C code and compile
that into a native program for your computer (static recompilation). Most of
the game then runs as native code.
- psxrecomp's runtime recreates the PlayStation's hardware: the graphics and
  sound chips, the CD drive, the controllers and the memory cards.
- Its launcher, [recomp-ui](https://github.com/RetroPortingToolKit/recomp-ui),
  adds controller set-up, picture options, mods and netplay.
- The mods in this repository change what the game does: Daffy as a third
  player, a camera for every player and a quicker start. See
  [What the mods add](#what-the-mods-add).

What is here, and what is not:
- **Here:** the project's psxrecomp setup (`game.toml`, the symbol map), the
  mods' code and packages, the build and setup tools, the Windows scripts,
  the tests and these documents.
- **Not here:** the disc, the BIOS and the game's code. The recompiled code
  (`generated/`) is made from your own disc on your own machine and is never
  committed. psxrecomp's bundled, MIT-licensed OpenBIOS stands in for Sony's
  BIOS.
- **Which disc:** the project is made for the USA PlayStation disc
  (SLUS-01144), as a `.cue` and `.bin` dump. Setup checks that it is that
  disc.

## What the mods add

All of them except the decorations are on by default; nothing has to be
set. Each one can be switched on or off, and its options changed, on the
launcher's **Mods** page (see [The launcher](#the-launcher)). In a LAN room
the host's Mods count for every PC.

### Third player: Daffy Duck

- **Who:** Daffy joins as a third hero next to Taz in every level that has
  Taz.
  - He has his own health panel (under Bugs').
  - Enemies go for him too.
  - □ is his attack, a kick that hits like Taz's spin.
  - His model is read from your own disc, and Bugs' animations are fitted to
    his skeleton.
- **When he comes:** only when there is a third player.
  - **Online:** three players seated in the room; seat 3 is Daffy.
  - **One PC:** choose a controller on the launcher's **PLAYER 3** card;
    Daffy comes next to Taz as soon as Player 3 presses any button, also in
    the middle of a level.
  - With two players he stays away.
- **When he is out:** he comes back next to Taz after 5 seconds (next to Bugs
  if Taz is out), with full health and no life lost. A package that brings a
  player back brings him back too.
- **Options** (Mods -> Third player: Daffy Duck):
  - **Character:** Daffy Duck, or the hunched enemy from the Transylvania level.
  - **Daffy's sounds:** Bugs' steps, jumps and attack; Taz's; or none of Taz's.
  - **When Daffy is out:** after 5 seconds; or the game's rule for Bugs and Taz,
    which is when the team comes back (a life lost) or a package.

### Own view for every player

In the original game two players share one screen and one camera. With this
mod every player gets their own camera:
- **In a match:** every PC shows its own player full screen and follows them
  with its own camera.
- **On one PC:** the game keeps its shared screen by default. Option
  **On one PC** -> *Split screen* gives each player a band of their own,
  player 1 on top.

![Three PCs in one match, each showing its own player: Bugs, Taz and Daffy](docs/images/own_view_three_pcs.jpg)

*One match on three PCs: PC 1 shows Bugs, PC 2 Taz, PC 3 Daffy.*

![Split screen on one PC, a band per player](docs/images/split_screen.jpg)

*Split screen on one PC, one band per player.*

**Speed.** Every PC draws the scene a second time every frame for its own
view:
- The start files cap the picture at 2x so that this extra drawing fits in a
  frame.
- Up to version 0.5.0 a match with own view ran far too slowly (in the first
  Windows tests the host turned it off). Since 0.6.0 a match no longer draws
  the picture nobody sees, and on our test machine three players run at full
  speed with own view on. Not yet tried on Windows PCs.
- If a match is still slow or choppy, switch it off on the host (Mods -> Own
  view for every player). Everyone then plays on the game's shared camera.
- Split screen on one PC draws three views, and a few heavy scenes run below
  full speed there: the Viking boss level and a few others.

### Quick start

- Straight to the game's main menu: no logos, no language screen, no PRESS
  START.
- No long new-game intro, and shorter loading screens.
- The emulated CD reads at 2x.
- In a LAN room the number of players comes from the seats, so the ONE /
  TWO PLAYERS menu is skipped.
- **Options:**
  - the game's language: English, Français, Español, or "Ask";
  - each skip on its own;
  - the CD speed, 1x to 4x.

### The picture

The game draws in true colour, without the PlayStation's dither pattern of
fine dots. To bring the dots back: Mods -> Image Filtering, turn it on, then
Dithering = Native (PS1). The framework's look options (Image Filtering,
Internal Resolution, CRT Filter, ...) are each PC's own choice, also in a
match.

### Decorations

Off by default. Mods -> Decorations hangs a Turkish flag from the windows of
Granny's house in Granwich.

## The launcher

The game starts in its launcher:
- **PLAYER 1 / 2 / 3 cards:** choose each player's controller. Online, every
  PC sets only its own PLAYER 1; PLAYER 2 and 3 are for players on one PC.
  A keyboard player needs keys of their own (Configure).
- **Mods:** every feature above, with its options on the right.
- **NETPLAY -> LAN / Direct IP:** host or join a room.
- **PLAY:** starts the game.

![The launcher: disc check, PLAYER cards, memory cards, NETPLAY and PLAY](docs/images/launcher.png)

![The Mods page: Third player: Daffy Duck and its options](docs/images/launcher_mods.png)

## Known limits

- **Matches ending:** over Radmin VPN, matches ended on every PC after a few
  minutes with no PC crashed. The start files now wait 5 s, not 1.5 s,
  before a silent PC ends the match; that is not yet tested in a real match.
  Before every match, everyone starts the game fresh.
- **Level change:** in three-view split screen on one PC, a rare stop at one
  level change has been seen.
- **Mac and Windows together:** not promised by the framework and not tried
  yet. Every player uses the same Windows build.

## Playing on Windows

Today players use a players' ZIP. It holds no game data and no game code.
Each PC builds the game once from its own disc:
1. Extract the ZIP.
2. Double-click `Setup.bat`.
3. Choose the disc once.
4. Play from the desktop shortcut.

The ZIP is made with `tools/make_player_zip.py`. See
[windows/START_HERE.txt](windows/START_HERE.txt) and
[docs/WINDOWS_AND_LAN.md](docs/WINDOWS_AND_LAN.md), section 8.

A ready-built package (extract, point it at the disc, play) is planned to
replace this. It will be shared privately with players, because it holds the
compiled game.

On a Mac there is no players' package: build the game as in
[Building (developers)](#building-developers).

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

### Notes in code comments

Comments in the code and tests refer to `NOTES.md` sections and `BUGS #<n>`
entries. Those are the maintainer's working notes and bug list, kept next to
the repository and not published. The comments say what matters in place.
Comments that mention the Fighting Force project mean a sibling PSXRecomp
port by the same maintainer; the two projects share findings.

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
- The screenshots in `docs/images/` show the original game's characters and
  artwork, which belong to their owners.

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
