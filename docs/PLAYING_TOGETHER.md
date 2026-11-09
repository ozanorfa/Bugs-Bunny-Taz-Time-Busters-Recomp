# Playing together

Up to three players, each on their own PC, over a LAN or Radmin VPN: Bugs
(seat 1), Taz (seat 2) and Daffy Duck (seat 3). Every PC shows its own
player full screen with its own camera.

## What everyone needs

- This package: the very same ZIP on every PC. A different package may be
  refused, or start and then drift apart during play.
- Windows 10 or 11 (64-bit).
- Their own copy of the game disc image (Bugs Bunny & Taz: Time Busters,
  USA, `.cue` + `.bin`). The launcher checks it is the original disc. For a
  match every PC's dump must be the same one: the room compares the discs,
  and a different dump of the game is refused. A `.chd` image starts on
  its own, but a match needs the `.cue` + `.bin`.
- A gamepad or the keyboard (Settings -> Input). On your PC, your controller
  is your player.
- For play over the internet: Radmin VPN (or any VPN that puts the PCs on
  one network). Use the host's Radmin IP below. On a home LAN use the
  host's LAN IP.

## The first start on Windows

1. Right-click the ZIP -> **Extract All...** -> Extract. Do not start the
   game from inside the ZIP (Windows lets you, but then the game cannot
   find its files or keep settings and saves). Keep the folder somewhere
   ordinary, like Desktop or Documents, not Program Files.
2. Double-click `BugsBunnyTaz_Recompiled.exe` in the extracted folder. If
   Windows says "Windows protected your PC", click **More info**, then
   **Run anyway** (the exe is not signed with a paid certificate).
3. The launcher asks for the disc: choose your `.cue` file.
4. Settings -> Input: your gamepad or keyboard is your player.
5. For a match: the first time you host or join, Windows Defender Firewall
   asks about the game. Tick both **Private** and **Public** networks (a
   Radmin VPN network often counts as Public), then **Allow access**. If
   you clicked Cancel by mistake: Start -> "Allow an app through Windows
   Firewall" -> Change settings -> tick both boxes for
   BugsBunnyTaz_Recompiled.

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
  (a controller set in Settings -> Input) presses any button. He then comes
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
   host's Radmin address, port 7777 -> Create Lobby. Allow the game through
   the firewall if asked (UDP 7777).
2. **Guests:** NETPLAY -> LAN / Direct IP -> Join Direct: the host's Radmin
   address and port 7777. If it says "No response", press Join Direct
   again; if it stays that way, the host's firewall is blocking the game
   (see "The first start on Windows", step 5).
3. When everyone is seated (two or three players), the host presses
   **PLAY**.
4. In the game, the host: **NEW GAME -> START GAME**. Two or three seated
   players play together; with three, Daffy joins next to Taz in every
   level that has Taz. (△ on START GAME goes back to the main menu.)

## Saves

- A match is played on the **host's** memory card: saving in the game
  saves the campaign on the host's PC.
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

## If something goes wrong

- "Content differs between peers" or a version mismatch: the PCs do not have
  the same package or the same mods; update everyone to the same package.
- Send the host's and guests' logs: start the game with its error output
  saved to a file (PowerShell: `Start-Process .\BugsBunnyTaz_Recompiled.exe
  -Wait -RedirectStandardError "$HOME\Desktop\peer.log"`).
