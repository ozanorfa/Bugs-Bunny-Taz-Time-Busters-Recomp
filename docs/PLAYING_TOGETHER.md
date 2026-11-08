# Playing together

Up to three players, each on their own PC, over a LAN or Radmin VPN: Bugs
(seat 1), Taz (seat 2) and Daffy Duck (seat 3). Every PC shows its own
player full screen with its own camera.

## What everyone needs

- This package, the same version on every PC (the launcher refuses a match
  between different versions or different mods).
- Their own copy of the game disc image (Bugs Bunny & Taz: Time Busters,
  USA, `.cue` + `.bin`). The launcher checks it is the original disc.
- A gamepad or the keyboard (Settings -> Input). On your PC, your controller
  is your player.
- For play over the internet: Radmin VPN (or any VPN that puts the PCs on
  one network). Use the host's Radmin IP below. On a home LAN use the
  host's LAN IP.

## Starting a match

1. **Host, once:** in Mods turn on
   - **Third player: Daffy Duck** (bbt.third-character "p3"; its
     Character option is Daffy by default, the Transylvania enemy is the
     other choice),
   - **Two views** (bbt.own-view: each PC shows its own player),
   - **Quick start** (no logos or long intro, short loading screens).
   Guests do not need to change anything: the room plays the host's mods.
2. **Host:** NETPLAY -> LAN / Direct IP -> Host Lobby -> Create Lobby
   (port 7777). Allow the game through the firewall if asked (UDP 7777).
3. **Guests:** NETPLAY -> LAN / Direct IP -> Join Direct: the host's IP and
   port 7777. If it says "No response", press Join Direct again.
4. When everyone is seated (the room shows "3 / 3 seated"), the host
   presses **PLAY**.
5. In the game: NEW GAME -> **TWO PLAYERS** -> START GAME. Daffy joins
   next to Taz in every level that has Taz.

For two players, the host turns "Third player" off (otherwise Daffy stands
still next to Taz with nobody to steer him).

## Saves

- A match is played on the **host's** memory card: saving in the game
  saves the campaign on the host's PC.
- Each guest's PC keeps a copy of that card as `saves/netplay/card1.mcd`
  (guests never write their own cards during a match).
- To continue the same campaign with someone else as host: that player
  picks `saves/netplay/card1.mcd` (or a copy of it) as memory card 1 in the
  launcher's Memory Card panel (Browse) before hosting.

## The rules of the three-player game

- Daffy is a third hero: enemies go for him too, he has his own health
  panel (under Bugs'), and he follows the game's rule for Bugs and Taz:
  when he loses all his health he is out until Bugs and Taz have both gone
  down and the team comes back (a life lost).
- A player who is out watches a teammate who is still playing.

## If something goes wrong

- "Content differs between peers" or a version mismatch: the PCs do not have
  the same package or the same mods; update everyone to the same package.
- Send the host's and guests' logs: start the game with its error output
  saved to a file (PowerShell: `Start-Process .\BugsBunnyTaz_Recompiled.exe
  -Wait -RedirectStandardError "$HOME\Desktop\peer.log"`).
