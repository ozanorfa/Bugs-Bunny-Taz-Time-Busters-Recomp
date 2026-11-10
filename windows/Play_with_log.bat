@echo off
setlocal
rem Bugs Bunny and Taz Recompiled with a log, for a test match. The same game
rem as Play.bat, but everything the game writes (stdout and stderr: netplay
rem delay, rollbacks, errors) goes to logs\last_run.log in this folder. Keep
rem this window open while you play; send logs\last_run.log afterwards.
rem (The Fighting Force project has the same helper.)
cd /d "%~dp0.."
rem The GPU surface the game draws on is capped at 2x (BUGS #51): in an online
rem match each PC draws its own view a second time every frame, and the
rem framework sets that surface up at 8x whatever the resolution chosen in
rem Mods, which made the match far too slow (2 s input lag, choppy sound).
rem Measured on a Mac: 13-14 ms instead of 55-135 ms per frame. To try a
rem sharper picture, set PSX_GL_MAX_DIM=4096 before starting this file.
if not defined PSX_GL_MAX_DIM set "PSX_GL_MAX_DIM=2048"
if not defined PSX_GL_HIRES_WINDOW set "PSX_GL_HIRES_WINDOW=0"
if not exist "build-game\BugsBunnyTaz_Recompiled.exe" goto :missing
if not exist "logs" mkdir "logs"
echo.
echo   Playing with a log: %CD%\logs\last_run.log
echo   Keep this window open; it closes the log when the game ends.
echo.
"build-game\BugsBunnyTaz_Recompiled.exe" --game "%CD%\build-game\game_3p.toml" --disc "%CD%\disc\BugsBunnyTaz_TimeBusters_USA.cue" --memcard-dir "%CD%\saves" > "logs\last_run.log" 2>&1
echo   The game has ended. Send this file: %CD%\logs\last_run.log
echo.
start "" explorer /select,"%CD%\logs\last_run.log"
pause
exit /b 0
:missing
echo The game is not built yet: run Setup.bat first.
pause
exit /b 1
