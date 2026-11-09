# Playing together

Up to three players, each on their own PC, over a LAN or Radmin VPN: Bugs
(seat 1), Taz (seat 2) and Daffy Duck (seat 3). Every PC shows its own
player full screen with its own camera.

## What everyone needs

- The players' ZIP (`BugsBunnyTaz3P_v<version>_<commit>.zip`): the very
  same version on every PC. Everyone in a match needs the same version; a
  different one is refused, or starts and then drifts apart during play.
  The ZIP holds no game data and no game code: each PC builds the game
  once from its own disc (the same route as the Fighting Force ZIP).
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

1. Right-click the ZIP -> **Extract All...** -> Extract. Do not run
   anything from inside the ZIP. Any folder will do.
2. Double-click `Setup.bat` in the extracted folder. If Windows says
   "Windows protected your PC", click **More info**, then **Run anyway**
   (the scripts are not signed).
3. When it asks for the disc, drag your `.cue` file into the window and
   press Enter. It is asked once and remembered for updates.
4. Wait: the first time the compiler is downloaded once and the whole game
   is built for this PC (minutes, more on a slow PC). It installs into
   `C:\RecompDev\BugsBunnyTazRecomp` (the build keeps its own copy of the
   disc) and puts **Bugs Bunny and Taz** on the desktop. If it stops while
   downloading (the site is busy, for example "504"), run Setup.bat again
   later. If it stops otherwise: select all the text in its window, copy
   it and send it along.
5. Start **Bugs Bunny and Taz** from the desktop (its icon is the game's
   logo, which Setup takes from your disc by running the game hidden for a
   minute). Your controller: on the first screen, card **PLAYER 1**,
   choose it below the controller picture (scroll the card down;
   **Configure** changes the buttons).
   - **Two or three players on one PC:** choose a controller on the
     **PLAYER 2** and **PLAYER 3** tabs too. Nothing is chosen for them by
     itself, so with only Player 1 set up the game starts for one player
     (Taz on the computer, no Daffy). A keyboard player needs their own
     keys (Configure), or the keys move two players (BUGS #46). Then NEW
     GAME -> TWO PLAYERS -> START GAME; Player 3 presses any button before
     START GAME and Daffy comes too. Online this is not needed: every PC
     uses its own PLAYER 1.
6. For a match: the first time you host or join, Windows Defender Firewall
   asks about the game. Tick both **Private** and **Public** networks (a
   Radmin VPN network often counts as Public), then **Allow access**. If
   you clicked Cancel by mistake: Windows Settings -> Windows Security ->
   Firewall -> "Allow an app through firewall" -> Change settings -> tick
   BugsBunnyTaz_Recompiled (Private and Public).

## Mods: nothing to set

What is on out of the box (Mods page, all on by default):
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
  (a controller chosen on the PLAYER 3 card) presses any button. He then comes
  at the next level start, so press before START GAME to have him in the
  first level. With two players he stays away. Options:
  Character (Daffy, or the Transylvania enemy), Daffy's sounds (Bugs' steps,
  jumps and attack; Taz's; or none of Taz's).

In a LAN room only the host's Mods count: the room plays the host's
selection on every PC, and the guests' own selections are left as they were.

## Starting a match

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
   **PLAY**.
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

## The rules of the three-player game

- Daffy is a third hero: □ is his attack (a kick; it hits like Taz's
  spin), enemies go for him too, he has his own health panel (under
  Bugs'), and he follows the game's rule for Bugs and Taz:
  when he loses all his health he is out until Bugs and Taz have both gone
  down and the team comes back (a life lost).
- A player who is out watches a teammate who is still playing.

## Updating

Extract the new ZIP and run its `Setup.bat`. It updates
`C:\RecompDev\BugsBunnyTazRecomp`, keeps the saves, settings and the disc
path, and builds again (the game code is generated again only when the
recompiler's inputs changed). Everyone in a match needs the same version.

## If something goes wrong

| What you see | What to do |
|---|---|
| Setup stops | Select all the text in its window, copy it and send it along. |
| Setup stops while downloading ("504", no internet) | Run Setup.bat again later. |
| "No .bin file next to that .cue" | Put the dump's `.cue` and `.bin` in one folder. |
| Setup says the disc digests do not match | It is not the original USA disc (or a damaged dump). |
| "Content differs between peers" or a version message in the room | Not everyone has the same version: everyone runs the newest ZIP's Setup.bat. |
| "No response" on Join Direct | Press Join Direct again; then check the host's firewall (step 6 above). |
| Something else in a match (lag, choppy sound, a freeze) | Everyone starts the game with `C:\RecompDev\BugsBunnyTazRecomp\windows\Play_with_log.bat` instead of the desktop shortcut, plays, and sends `logs\last_run.log` afterwards (it holds the netplay delay, rollbacks and errors). |
