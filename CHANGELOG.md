# Changelog

The notable changes, newest first.

A version is the netplay compatibility number, `0.<N>.0`. It lives in
`VERSION` and in every `bbt.*` mod package, and it is raised whenever a change
alters what the game does. Every player in a match needs the same version: the
room refuses a guest whose packages are another version. A version marked
**players' ZIP** was given to players.

## 0.6.0 — not yet given to players

### Added
- **Daffy comes back on his own.** 5 seconds after he is out he returns next
  to Taz (or next to Bugs if Taz is out), with full health and no life lost.
  A new option, "When Daffy is out", keeps the game's rule for Bugs and Taz
  instead: he waits until the team comes back.
- **A package brings Daffy back too.** In the original game a package
  revives an out player 2; now it also revives an out third player.
- **One file to send after a test.** The desktop shortcut starts
  `windows\Play_with_log.bat`. When the game closes, it puts the game's log,
  its last-run report, the installed version and, after a crash, the crash
  note into one ZIP under `reports\`, then opens that folder. It keeps the
  newest 20.
- **A public repository** with an MIT license (see `NOTICE.md` for the parts
  that keep other terms), a README covering each mod with pictures, and this
  changelog.

### Changed
- **Matches tolerate short silences.** A match ends on every PC when one PC
  goes silent. The start files now wait 5 seconds before that, instead of the
  framework's 1.5 seconds, because matches over Radmin VPN ended after a few
  minutes with no PC crashed. Not yet tried in a real match.
- **True colour by default:** the picture is drawn without the PlayStation's
  dither pattern. To bring it back: Mods -> Image Filtering, turn it on, then
  Dithering = Native (PS1).
- **The Turkish flag is off by default.** To turn it on: Mods ->
  Decorations. In a LAN room the host's choice counts for every PC.
- The launcher no longer labels Own View and Third Character "in development".

### Fixed
- **Changing Mods in a LAN room should no longer split the next match.**
  The mods' memory now sits at the same place on every PC whatever was
  played before, it is cleared before every match, and switching between
  Daffy and the Transylvania enemy in the room builds the other character's
  moves. Not yet tried in a real room, so the guides still ask everyone to
  restart the game after the host changes Mods.
- Setup takes a disc path typed at its prompt also when it is relative
  (for example `..\disc\game.cue`); before, it stopped with "no disc at".
  Dropping the .cue on the window and the automatic search were not
  affected. Not yet tried on Windows.
- Checks against a damaged disc file: the game's file reader no longer
  writes past its buffer on an unusually long copy, and animation data that
  is too short or names a bone out of range is refused. The checked disc
  never had any of these; the game plays the same.
- **The Turkish flag stays at a distance.** The game draws far-away walls
  with a half-size copy of their texture. That copy now has a half-size flag,
  so the flag of a farther window no longer vanishes and pops back. The
  6-pane window that shares the wall's colours lost its red and white specks.

## 0.5.0 — 2026-11-14 — players' ZIP

The first version played on Windows: three PCs over Radmin VPN, 2026-11-16.

### Added
- **Decorations: a Turkish flag** hangs from the windows on both sides of
  Granny's house in Granwich.
- **Setup finds the disc by itself.** It searches its own folder, the two
  folders above it and their subfolders for the game's `.cue` and its own
  `.bin`, and remembers the disc after a good build.
- `windows\Play_with_log.bat`: a start with the game's whole output in
  `logs\last_run.log`.
- The guides ask players to send `psx_last_run_report.json` when the game
  closes by itself.

### Changed
- **Online matches at full speed:** the start files cap the surface the game
  draws on at 2x. At the 4x default every PC's second, own-view drawing took
  longer than a frame, and matches crawled.
- The start files turn the framework's runtime compiler off. Setup compiles
  the game code once, so the compiler would only cost CPU while playing.

## 0.4.0 — 2026-11-09 — players' ZIP

### Changed
- Daffy's kick is shown whole.
- The desktop shortcut gets the game's logo as its icon, taken from the
  player's own disc.

## 0.3.0 — 2026-11-09 — players' ZIP

### Added
- **The self-build players' ZIP.** It holds no game data and no game code:
  extract it, double-click `Setup.bat`, choose the disc once, and play from
  the desktop shortcut. Each PC builds the game from its own disc.

### Changed
- The framework stays on psxrecomp `d9605697` and recomp-ui `839a6f9`.
  PSXRecomp v0.3.7-alpha was tried and dropped: short button presses got
  lost, and rollback replays drifted mid-game.

## 0.2.0 — 2026-11-09

### Added
- **The netplay compatibility number.** Two different builds cannot start a
  match.
- **Daffy's own attack and sounds:** □ is his kick, and it hits like Taz's
  spin.
- **Quick start:** the game opens on its main menu, with no logos, language
  screen or PRESS START. In a LAN room the players come from the seats.
- **On one PC,** Player 3 joins by pressing any button, and Daffy comes at
  the next level start.

### Changed
- Quick start, the own view and the third player are on by default.

## 0.1.0 — 2026-11-06 to 2026-11-08 — development

- The game runs natively on macOS, recompiled with psxrecomp, from boot to
  gameplay for one and two players.
- **Third character**, first as the hunched Transylvania enemy drawn in Taz's
  place, then as a real third player controlled from seat 3. He has his own
  death rule, his own health panel, and enemies that go for him.
- **Daffy Duck** as the third player. His model is read from the player's own
  disc, and Bugs' animations are fitted to his skeleton.
- **Netplay:** a separate netplay build. Mods are kept in a match; the room
  sends the host's selection.
- **Own view for every player:** each player gets their own camera, with up
  to three views. Every PC in a match shows its own player full screen, and
  sound is heard from every view.
- **Three players in one LAN room,** each on their own PC.
