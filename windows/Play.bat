@echo off
setlocal
rem Bugs Bunny and Taz: Time Busters Recompiled on this PC, without a log (the
rem desktop shortcut starts Play_with_log.bat, which keeps one). The launcher
rem opens first: Settings (controls) are there; PLAY starts the game, and
rem NETPLAY - LAN / Direct IP is the room for a match with friends.
rem The memory cards are in the saves folder next to this repository's
rem files (they stay when you update with a newer Setup.bat).
cd /d "%~dp0.."
rem The GPU surface the game draws on is capped at 2x (BUGS #51): in an online
rem match each PC draws its own view a second time every frame, and the
rem Internal Resolution mod's default (4x) made that second draw cost
rem 28-40 ms per frame on a Mac, more than a whole frame (16.7 ms); at 2x
rem it is 12-15 ms. The cap also holds when a host picks a higher resolution
rem in Mods. To try a sharper picture, set PSX_GL_MAX_DIM=4096 before
rem starting this file.
if not defined PSX_GL_MAX_DIM set "PSX_GL_MAX_DIM=2048"
if not defined PSX_GL_HIRES_WINDOW set "PSX_GL_HIRES_WINDOW=0"
rem The runtime compiler for game code is off, as in every test of this
rem build: the game code is compiled once by Setup.bat, and the compiler
rem only costs CPU while you play (the Fighting Force project does the
rem same). Set PSX_OVERLAY_AUTOCOMPILE_OFF=0 before starting this file to
rem turn it back on.
if not defined PSX_OVERLAY_AUTOCOMPILE_OFF set "PSX_OVERLAY_AUTOCOMPILE_OFF=1"
rem A match ends on every PC when one PC hears nothing from another for this
rem long (PSX_NET_LIVENESS_MS, the framework's default 1.5 s). Over Radmin VPN
rem matches ended after a few minutes with no PC crashed (BUGS #61), so the
rem start files wait 5 s. Every PC in a match should use the same value.
if not defined PSX_NET_LIVENESS_MS set "PSX_NET_LIVENESS_MS=5000"
if not exist "build-game\BugsBunnyTaz_Recompiled.exe" goto :missing
start "" "build-game\BugsBunnyTaz_Recompiled.exe" --game "%CD%\build-game\game_3p.toml" --disc "%CD%\disc\BugsBunnyTaz_TimeBusters_USA.cue" --memcard-dir "%CD%\saves"
exit /b 0
:missing
echo The game is not built yet: run Setup.bat first.
pause
exit /b 1
