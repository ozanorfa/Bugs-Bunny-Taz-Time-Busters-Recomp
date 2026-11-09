# Playing together

Up to three players, each on their own PC, over a LAN or Radmin VPN: Bugs
(seat 1), Taz (seat 2) and Daffy Duck (seat 3). Every PC shows its own
player full screen with its own camera.

## What everyone needs

- This package: the very same ZIP on every PC. A different package may be
  refused, or start and then drift apart during play.
- Their own copy of the game disc image (Bugs Bunny & Taz: Time Busters,
  USA, `.cue` + `.bin`). The launcher checks it is the original disc. For a
  match every PC's dump must be the same one: the room compares the discs,
  and a different dump of the game is refused.
- A gamepad or the keyboard (Settings -> Input). On your PC, your controller
  is your player.
- For play over the internet: Radmin VPN (or any VPN that puts the PCs on
  one network). Use the host's Radmin IP below. On a home LAN use the
  host's LAN IP.

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
  third player: three seated in a LAN room, or, on one PC, a controller for
  Player 3 in Settings -> Input. With two players he stays away. Options:
  Character (Daffy, or the Transylvania enemy), Daffy's sounds (Bugs' steps,
  jumps and attack; Taz's; or none of Taz's).

In a LAN room only the host's Mods count: the room plays the host's
selection on every PC, and the guests' own selections are left as they were.

## Starting a match

1. **Host:** NETPLAY -> LAN / Direct IP -> Host Lobby -> Create Lobby
   (port 7777). Allow the game through the firewall if asked (UDP 7777).
2. **Guests:** NETPLAY -> LAN / Direct IP -> Join Direct: the host's IP and
   port 7777. If it says "No response", press Join Direct again.
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
  picks `saves/netplay/card1.mcd` (or a copy of it) as memory card 1 in the
  launcher's Memory Card panel (Browse) before hosting.

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
