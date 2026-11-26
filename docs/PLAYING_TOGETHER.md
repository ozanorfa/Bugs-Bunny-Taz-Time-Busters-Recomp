# Playing together

This guide is for *Bugs Bunny & Taz: Time Busters Recompiled*, a fan port
of the 2000 PlayStation game to Windows and macOS with Daffy Duck added as a
third player. What the original game is and what this project adds: the
[README](../README.md).

Up to three players, each on their own PC, over a LAN or Radmin VPN: Bugs
(seat 1), Taz (seat 2) and Daffy Duck (seat 3). Every PC shows its own
player full screen with its own camera.

## What everyone needs

- The players' ZIP (`BugsBunnyTaz3P_v<version>_<commit>.zip`): the very
  same version on every PC. Everyone in a match needs the same version; a
  different one is refused, or starts and then drifts apart during play.
  The ZIP holds no game data and no game code: each PC builds the game
  once from its own disc. (A ready-built package, with no build step on
  the players' PCs, is planned; until it exists this ZIP is the way.)
- Windows 10 (version 1903 or newer) or 11, 64-bit; internet for the first
  setup; about 1.5 GB free on drive C:.
- Their own copy of the game disc image (Bugs Bunny & Taz: Time Busters,
  USA, `.cue` + `.bin` in one folder). Setup checks it is the original
  disc. For a match every PC's dump must be the same one: the room compares
  the discs, and a different dump of the game is refused.
- A gamepad or the keyboard. On your PC, your controller is your player.
- For play over the internet: Radmin VPN (or any VPN that puts the PCs on
  one network). Use the host's Radmin IP below. On a home LAN use the
  host's LAN IP.

## The first start on Windows

1. Put the ZIP and the folder with your disc (the `.cue` and its `.bin`)
   **side by side in one folder**, then right-click the ZIP -> **Extract
   All...** -> Extract. Do not run anything from inside the ZIP.
2. Double-click `Setup.bat` in the extracted folder. If Windows says
   "Windows protected your PC", click **More info**, then **Run anyway**
   (the scripts are not signed).
3. It **finds your disc by itself**: it looks in its own folder, the one
   above and the one above that, and their subfolders, for a `.cue` whose
   own `.bin` (same name) is the game's dump (the one the room expects,
   678,069,840 bytes, so another game's disc is never taken, even in a
   folder of many games). If it does not find it, it asks: drag your
   `.cue` file into the window and press Enter. The disc path is
   remembered after a successful build, for updates.
4. Wait: the first time the compiler is downloaded once and the whole game
   is built for this PC (minutes, more on a slow PC). It installs into
   `C:\RecompDev\BugsBunnyTazRecomp` (the build keeps its own copy of the
   disc) and puts **Bugs Bunny and Taz** on the desktop. If it stops while
   downloading (the site is busy, for example "504"), run Setup.bat again
   later. If it stops otherwise: select all the text in its window, copy
   it and send it along.
5. Start **Bugs Bunny and Taz** from the desktop (its icon is the game's
   logo, which Setup takes from your disc by running the game hidden for a
   minute). A small black window opens behind the game: leave it open, it
   keeps the game's log (`windows\Play_with_log.bat`). Your controller: on the first screen, card **PLAYER 1**,
   choose it below the controller picture (scroll the card down;
   **Configure** changes the buttons).
   - **Two or three players on one PC:** choose a controller on the
     **PLAYER 2** and **PLAYER 3** tabs too. Nothing is chosen for them by
     itself, so with only Player 1 set up the game starts for one player
     (Taz on the computer, no Daffy). A keyboard player needs their own
     keys (Configure), or the keys move two players. Then NEW
     GAME -> TWO PLAYERS -> START GAME; Player 3 presses any button, at any
     time, and Daffy comes next to Taz. Online this is not needed: every PC
     uses its own PLAYER 1.
6. For a match: the first time you host or join, Windows Defender Firewall
   asks about the game. Tick both **Private** and **Public** networks (a
   Radmin VPN network often counts as Public), then **Allow access**. If
   you clicked Cancel by mistake: Windows Settings -> Windows Security ->
   Firewall -> "Allow an app through firewall" -> Change settings -> tick
   BugsBunnyTaz_Recompiled (Private and Public).

## Mods: nothing to set

The Mods page out of the box (everything on except Decorations):
- **Quick start:** straight to the game's main menu (no logos, no language
  screen, no PRESS START); in a LAN room no ONE / TWO PLAYERS menu (the
  players come from the room; on one PC the game asks only when a second
  controller is set up); no long intro, short loading screens.
  Options: the game's language (English by default), and each skip.
- **Own view for every player:** in a match every PC shows its own player
  full screen with its own camera. On one PC the game keeps its shared
  screen (option "On one PC": split screen instead).
- **Third player: Daffy Duck:** he joins next to Taz only when there is a
  third player: three seated in a LAN room, or, on one PC, once Player 3
  (a controller chosen on the PLAYER 3 card) presses any button: he comes at
  once, next to Taz, also in the middle of a level. With two players he
  stays away. Options:
  Character (Daffy, or the Transylvania enemy), Daffy's sounds (Bugs' steps,
  jumps and attack; Taz's; or none of Taz's), and when Daffy is out (back
  next to Taz after 5 seconds, the default; or the game's rule for Bugs and
  Taz).
- **Decorations** (off by default): a Turkish flag hangs from the windows
  of Granny's house in Granwich. Turn it on in Mods -> Decorations.

In a LAN room only the host's Mods count: the room plays the host's
selection on every PC, and the guests' own selections are left as they were.

The picture: the game draws in true colour, without the PlayStation's
dither pattern of fine dots. To get the dots back: Mods -> Image Filtering,
turn the feature on (it is off by default), then Dithering = Native (PS1).
This and the framework's other look options (Image Filtering, Internal
Resolution, CRT Filter, ...) are each PC's own choice, also in a match.

## Starting a match

**Before every match, everyone starts the game fresh** from the desktop
shortcut: close it first if you played offline or anyone changed Mods
since it was opened, and when the host changes Mods between matches
(own view off, for example), everyone closes and reopens it. The mods
place their data by what was played since the game was opened, so PCs
with a different history can fall out of sync.

0. **Over the internet:** everyone installs Radmin VPN; the host creates a
   network there (Network -> Create network, a name and a password), the
   others join it (Network -> Join network). Each PC gets a Radmin address
   `26.x.x.x`, shown in Radmin VPN. On a home LAN skip this and use the
   host's LAN address.
1. **Host:** NETPLAY -> LAN / Direct IP -> Host Lobby: Advertised IP = the
   host's Radmin address, port 7777 (if another program uses it, pick
   another and tell the others) -> Create Lobby. Allow the game through
   the firewall if asked. The room uses UDP 7777 on the host, 7778 and up
   on the guests, and 48777 to find rooms on a LAN.
2. **Guests:** NETPLAY -> LAN / Direct IP -> Join Direct: the host's Radmin
   address and port 7777. If it says "No response", press Join Direct
   again; if it stays that way, the host's firewall is blocking the game
   (see "The first start on Windows", step 6).
3. When everyone is seated (two or three players), the host presses
   **PLAY**. The seats taken must be 1, 2 and 3 with no gap: if a player
   left and the room shows an empty seat between players, the host
   creates the room again (a player in seat 4 with seat 3 empty gets no
   Daffy).
4. In the game, the host: **NEW GAME -> START GAME**. Two or three seated
   players play together; with three, Daffy joins next to Taz in every
   level that has Taz. (△ on START GAME goes back to the main menu.)

## Saves

- A match is played on the **host's** memory card: saving in the game
  saves the campaign on the host's PC.
- The cards are in `C:\RecompDev\BugsBunnyTazRecomp\saves`.
- Each guest's PC keeps a copy of that card as `saves/netplay/card1.mcd`
  (guests never write their own cards during a match).
- To continue the same campaign with someone else as host: that player
  first **copies** `saves/netplay/card1.mcd` to a new name (for example
  `saves/campaign.mcd`), then picks the copy as memory card 1 in the
  launcher's Memory Card panel (Browse) before hosting. Do not pick
  `saves/netplay/card1.mcd` itself: the next match you join as a guest
  writes the host's card into that file.

## Picture sharpness and speed

The game's start files (`windows\Play_with_log.bat`, which the desktop
shortcut starts, and `windows\Play.bat`) cap the GPU surface the game draws
on at 2x.
In a match every PC draws its own player's view a second time every frame.
At the Internal Resolution mod's default of 4x that second draw took more
than a whole frame, so matches ran far too slowly (choppy sound, about 2 s
of input lag). The cap holds whatever is picked in Mods (Internal
Resolution is each PC's own choice). With a strong PC you can try a sharper
picture: start the game
from a Command Prompt in `C:\RecompDev\BugsBunnyTazRecomp` with
`set PSX_GL_MAX_DIM=4096` and then `windows\Play.bat`.

The start files also turn off the runtime compiler for game code
(`PSX_OVERLAY_AUTOCOMPILE_OFF=1`). Setup.bat compiles the game code once,
our tests all run without that compiler, and while you play it would only
cost CPU. And they let a PC go silent for 5 s before a match ends
(`PSX_NET_LIVENESS_MS=5000`; the framework's default is 1.5 s, and over
Radmin VPN matches ended after a few minutes with no PC crashed).

## The rules of the three-player game

- Daffy is a third hero: □ is his attack (a kick; it hits like Taz's
  spin), enemies go for him too, and he has his own health panel (under
  Bugs'). When he loses all his health he is out for 5 seconds and then
  comes back next to Taz (next to Bugs if Taz is out), with full health
  and no life lost. A package that brings a player back brings him back
  too. With the option "When Daffy is out" set to the game's rule, he
  waits like Bugs and Taz instead: until both of them have gone down and
  the team comes back (a life lost), or a package.
- A player who is out watches a teammate who is still playing.

## Updating

Extract the new ZIP and run its `Setup.bat`. It updates
`C:\RecompDev\BugsBunnyTazRecomp`, keeps the saves, settings and the disc
path, and builds again (the game code is generated again only when the
recompiler's inputs changed). Everyone in a match needs the same version.

## If something goes wrong

| What you see | What to do |
|---|---|
| The game closes by itself, or in a match everyone is sent back to the room with "The match ended: ..." | One PC's problem (a crash, a stall, a pause of its connection longer than the start files' 5 s) ends the match on every PC, so note the message each PC shows. Every PC closes the game before the next match. When the game closes, a folder opens with one file selected, `reports\BugsBunnyTaz_<PC name>_<date>-<time>.zip` in `C:\RecompDev\BugsBunnyTazRecomp`: every PC sends that file (it holds `logs\last_run.log`, `psx_last_run_report.json`, the installed version `SOURCE_VERSION` and, only if the game stopped on an error, `psx_crash.txt`). The newest 20 are kept. A rare stop at a level change is known. |
| Setup stops | Select all the text in its window, copy it and send it along. |
| Setup stops while downloading ("504", no internet) | Run Setup.bat again later. |
| "No .bin file next to that .cue" | Put the dump's `.cue` and `.bin` in one folder. |
| Setup says the disc digests do not match | It is not the original USA disc (or a damaged dump). |
| "Content differs between peers" or a version message in the room | Not everyone has the same version: everyone runs the newest ZIP's Setup.bat. |
| "No response" on Join Direct | Press Join Direct again; then check the host's firewall (step 6 above). |
| Something else in a match (lag, choppy sound, a freeze) | Play on, close the game, and everyone sends the report file that opens (the log in it holds the netplay delay, rollbacks and errors). |
