@echo off
setlocal
rem Bugs Bunny and Taz: Time Busters Recompiled on this PC. The launcher
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
if not exist "build-game\BugsBunnyTaz_Recompiled.exe" goto :missing
start "" "build-game\BugsBunnyTaz_Recompiled.exe" --game "%CD%\build-game\game_3p.toml" --disc "%CD%\disc\BugsBunnyTaz_TimeBusters_USA.cue" --memcard-dir "%CD%\saves"
exit /b 0
:missing
echo The game is not built yet: run Setup.bat first.
pause
exit /b 1
