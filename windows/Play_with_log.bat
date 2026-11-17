@echo off
setlocal
rem Bugs Bunny and Taz Recompiled with a log. The desktop shortcut starts this
rem file (in a normal window: a minimized one could pass its state on to the
rem game's window). The same game as Play.bat, but everything the
rem game writes (stdout and stderr: netplay delay, rollbacks, errors) goes to
rem logs\last_run.log in this folder. When the game closes, the log, the
rem game's last-run report, the installed version (SOURCE_VERSION) and, if
rem this run stopped on an error, its crash note are put into ONE file, reports\BugsBunnyTaz_<PC name>_<date>-<time>.zip,
rem and that folder opens with the file selected: send that one file. The
rem newest 20 reports are kept. Keep this window open while you play.
rem (The Fighting Force project has the same helper.)
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
rem Started again while the game is open: the open one keeps its log.
tasklist /fi "imagename eq BugsBunnyTaz_Recompiled.exe" 2>nul | find /i "BugsBunnyTaz_Recompiled.exe" >nul
if not errorlevel 1 goto :running
if not exist "logs" mkdir "logs"
if not exist "reports" mkdir "reports"
rem A crash note left by an earlier run is moved aside, so a crash note found
rem after this run is this run's (the game writes psx_crash.txt only when it
rem stops on an error).
if exist "psx_crash.txt" move /y "psx_crash.txt" "logs\previous_crash.txt" >nul
echo.
echo   Playing with a log: "%CD%\logs\last_run.log"
echo   Keep this window open; when the game closes it collects one file
echo   for you to send.
echo.
"build-game\BugsBunnyTaz_Recompiled.exe" --game "%CD%\build-game\game_3p.toml" --disc "%CD%\disc\BugsBunnyTaz_TimeBusters_USA.cue" --memcard-dir "%CD%\saves" > "logs\last_run.log" 2>&1
set "RC=%ERRORLEVEL%"
>>"logs\last_run.log" echo [Play_with_log.bat] the game ended with exit code %RC%
set "STAMP=run"
for /f %%t in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd-HHmmss"') do set "STAMP=%%t"
set "REPORT=%CD%\reports\BugsBunnyTaz_%COMPUTERNAME%_%STAMP%.zip"
powershell -NoProfile -Command "$f = @('logs\last_run.log','psx_last_run_report.json','psx_crash.txt','SOURCE_VERSION') | Where-Object { Test-Path $_ }; if ($f) { Compress-Archive -Path $f -DestinationPath '%REPORT:'=''%' -Force }; Get-ChildItem 'reports\*.zip' | Sort-Object LastWriteTime -Descending | Select-Object -Skip 20 | Remove-Item" >nul 2>nul
echo   The game has ended.
if not exist "%REPORT%" goto :no_report
echo   Send this one file:
echo     "%REPORT%"
start "" explorer /select,"%REPORT%"
goto :done
:no_report
echo   Send this file:
echo     "%CD%\logs\last_run.log"
start "" explorer /select,"%CD%\logs\last_run.log"
:done
echo.
pause
exit /b 0
:running
echo   The game is already open (look for it in the taskbar).
pause
exit /b 0
:missing
echo The game is not built yet: run Setup.bat first.
pause
exit /b 1
