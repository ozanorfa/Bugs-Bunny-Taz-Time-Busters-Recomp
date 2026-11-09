@echo off
setlocal
rem Bugs Bunny and Taz Recompiled with a log, for a test match. The same game
rem as Play.bat, but everything the game writes (stdout and stderr: netplay
rem delay, rollbacks, errors) goes to logs\last_run.log in this folder. Keep
rem this window open while you play; send logs\last_run.log afterwards.
rem (The Fighting Force project has the same helper.)
cd /d "%~dp0.."
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
