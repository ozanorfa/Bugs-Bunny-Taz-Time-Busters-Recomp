@echo off
setlocal
rem Bugs Bunny and Taz: Time Busters Recompiled on this PC. The launcher
rem opens first: Settings (controls) are there; PLAY starts the game, and
rem NETPLAY - LAN / Direct IP is the room for a match with friends.
rem The memory cards are in the saves folder next to this repository's
rem files (they stay when you update with a newer Setup.bat).
cd /d "%~dp0.."
if not exist "build-game\BugsBunnyTaz_Recompiled.exe" goto :missing
start "" "build-game\BugsBunnyTaz_Recompiled.exe" --game "%CD%\build-game\game_3p.toml" --disc "%CD%\disc\BugsBunnyTaz_TimeBusters_USA.cue" --memcard-dir "%CD%\saves"
exit /b 0
:missing
echo The game is not built yet: run Setup.bat first.
pause
exit /b 1
