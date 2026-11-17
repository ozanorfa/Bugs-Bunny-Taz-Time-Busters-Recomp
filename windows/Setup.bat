@echo off
setlocal EnableExtensions DisableDelayedExpansion
title Bugs Bunny and Taz Recompiled - setup
rem ------------------------------------------------------------------------
rem Double-click this file (or drop your disc's .cue file on it).
rem
rem Players' ZIP: this file sits next to the folders BugsBunnyTazRecomp and
rem python. It copies them to C:\RecompDev\BugsBunnyTazRecomp and builds the
rem game there with YOUR disc. Nothing has to be installed: Python comes with
rem the ZIP, the compiler downloads itself once.
rem Bundle (developers): this file sits next to BugsBunnyTazRecomp.bundle;
rem it clones / updates the folder with Git (Git and Python needed).
rem
rem A newer ZIP or bundle: run its Setup.bat again. It updates the folder,
rem keeps your saves, settings and disc path, and builds again.
rem Another folder: set BBT_DIR=D:\Somewhere\BugsBunnyTazRecomp first.
rem (The pattern comes from the Fighting Force project's Setup.bat.)
rem ------------------------------------------------------------------------

set "HERE=%~dp0"
set "BUNDLE=%HERE%BugsBunnyTazRecomp.bundle"
set "SRC=%HERE%BugsBunnyTazRecomp"
set "DEST=C:\RecompDev\BugsBunnyTazRecomp"
if defined BBT_DIR set "DEST=%BBT_DIR%"
set "CUE=%~1"

echo.
echo   Bugs Bunny and Taz: Time Busters Recompiled - setup
echo   ===================================================
echo   Install folder: %DEST%
echo.

rem An open game holds its .exe: the build could not replace it.
tasklist /fi "imagename eq BugsBunnyTaz_Recompiled.exe" 2>nul | find /i "BugsBunnyTaz_Recompiled.exe" >nul
if not errorlevel 1 goto :game_running

set "MODE="
if exist "%SRC%\tools\bbt_setup.py" set "MODE=zip"
if not defined MODE if exist "%BUNDLE%" set "MODE=bundle"
if not defined MODE goto :no_source

rem ---- 1. Python (and Git for the bundle) ----------------------------------
set "PYCMD="
if "%MODE%"=="zip" goto :py_zip
where git >nul 2>nul
if errorlevel 1 goto :no_git
py -3 --version >nul 2>nul
if not errorlevel 1 set "PYCMD=py -3"
if defined PYCMD goto :py_ok
python --version >nul 2>nul
if not errorlevel 1 set "PYCMD=python"
if not defined PYCMD goto :no_python
goto :py_ok
:py_zip
if not exist "%HERE%python\python.exe" goto :no_zip_python
:py_ok

rem ---- 2. the disc (found or asked once, remembered for updates) ----------
rem A .cue dropped on this file wins, then the remembered path, then a search
rem of this folder, the one above and the one above that (and their
rem subfolders) for the game's dump, then the drag-and-drop prompt. (The
rem Fighting Force project's Setup.bat searches the same way.)
if defined CUE goto :check_cue
if exist "%DEST%\disc_source.txt" set /p CUE=<"%DEST%\disc_source.txt"
if not defined CUE goto :find_disc
if exist "%CUE%" goto :check_cue
:find_disc
call :find_cue
if not defined CUE goto :not_found
echo   Found your disc:
echo     "%CUE%"
goto :check_cue
:not_found
echo   Your disc was not found next to this folder.
echo.
:ask_cue
set "CUE="
echo   Drag your Bugs Bunny and Taz: Time Busters (USA) .cue file into this
echo   window and press Enter. (Its .bin file must be in the same folder.)
set /p "CUE=  Disc .cue: "
if not defined CUE goto :ask_cue
set "CUE=%CUE:"=%"
:check_cue
if not exist "%CUE%" goto :cue_missing
for %%F in ("%CUE%") do set "CUEDIR=%%~dpF"
set NBIN=0
for %%B in ("%CUEDIR%*.bin") do set /a NBIN+=1
if %NBIN% LSS 1 goto :no_bin
echo   Disc: "%CUE%"
echo.

rem ---- 3. the code ---------------------------------------------------------
if "%MODE%"=="bundle" goto :git_code
echo   Copying the game's code and Python ...
if /i "%SRC%"=="%DEST%" goto :copied
robocopy "%SRC%" "%DEST%" /E /NFL /NDL /NJH /NJS /NP /R:1 /W:1 >nul
if errorlevel 8 goto :failed
robocopy "%HERE%python" "%DEST%\python" /E /NFL /NDL /NJH /NJS /NP /R:1 /W:1 >nul
if errorlevel 8 goto :failed
:copied
set PYCMD="%DEST%\python\python.exe"
set "PATH=%DEST%\python;%PATH%"
goto :build
:git_code
if exist "%DEST%\.git" goto :update
echo   Copying the code ...
git -c core.longpaths=true clone "%BUNDLE%" "%DEST%"
if errorlevel 1 goto :failed
git -C "%DEST%" config core.longpaths true
goto :build
:update
echo   Updating the code from the bundle ...
git -C "%DEST%" pull --ff-only "%BUNDLE%" HEAD
if errorlevel 1 goto :failed

rem ---- 4. framework + build (tools\bbt_setup.py) ----------------------------
:build
cd /d "%DEST%"
echo   Building the game. The first time takes a while: the compiler is
echo   downloaded once and the whole game is compiled for this PC.
echo.
%PYCMD% tools\bbt_setup.py --disc "%CUE%"
if errorlevel 1 goto :failed
if not exist "%DEST%\build-game\BugsBunnyTaz_Recompiled.exe" goto :failed

rem ---- 5. a desktop shortcut (the launcher: play, settings, netplay room) ---
rem It starts windows\Play_with_log.bat in a normal window (WindowStyle 1, set
rem explicitly: CreateShortcut keeps an existing .lnk's properties): every run
rem keeps a log, and when the game closes the files to send are in one report
rem file. A minimized window could start the game minimized too.
set "ICON=%DEST%\build-game\BugsBunnyTaz_Recompiled.exe,0"
if exist "%DEST%\BugsBunnyTaz.ico" set "ICON=%DEST%\BugsBunnyTaz.ico"
powershell -NoProfile -Command "$w=New-Object -ComObject WScript.Shell; $l=$w.CreateShortcut([Environment]::GetFolderPath('Desktop')+'\Bugs Bunny and Taz.lnk'); $l.TargetPath='%DEST:'=''%\windows\Play_with_log.bat'; $l.WorkingDirectory='%DEST:'=''%'; $l.WindowStyle=1; $l.IconLocation='%ICON:'=''%'; $l.Save()" >nul 2>nul

echo.
echo   Done. Start the game with "Bugs Bunny and Taz" on your desktop
echo   (or %DEST%\windows\Play.bat). With friends: NETPLAY - LAN / Direct IP.
echo.
pause
exit /b 0

rem ---- messages ------------------------------------------------------------
:no_source
echo   Nothing to install next to this file: it needs the folder
echo   BugsBunnyTazRecomp (players' ZIP: extract the whole ZIP first) or
echo   BugsBunnyTazRecomp.bundle, in:
echo     "%HERE%"
goto :stop
:game_running
echo   The game is still open. Close it (and its launcher window), then run
echo   Setup.bat again.
goto :stop
:no_zip_python
echo   The python folder is missing next to this file: extract the whole ZIP
echo   (all folders), then run Setup.bat again.
goto :stop
:no_git
echo   Git for Windows is not installed. Install it with the default options:
echo     https://git-scm.com/downloads/win
echo   then run this file again.
goto :stop
:no_python
echo   Python 3 is not installed (or only the Microsoft Store shortcut is there).
echo   Install it from https://www.python.org/downloads/windows/ and tick
echo   "Add python.exe to PATH", then run this file again.
goto :stop
:cue_missing
echo   File not found: "%CUE%"
echo.
goto :ask_cue
:no_bin
echo   No .bin file next to that .cue: the dump's .bin must be in the same
echo   folder.
echo.
goto :ask_cue
:failed
echo.
echo   Setup stopped at the step above. Select all the text in this window,
echo   copy it and send it along: it says what went wrong.
:stop
echo.
pause
exit /b 1

rem ---- the disc search (called; single-line statements, no blocks) ------
rem The first .cue whose own .bin (same name) is the game's dump: one .bin
rem of 678069840 bytes (game.toml [prepare_disc] known_sizes), so another
rem game's .cue is never taken, even in a folder holding many games. The
rem install's own checked copy comes first (an update after the original
rem dump was moved), then this folder, the one above and the one above that.
:find_cue
set "CUE="
call :try_cue "%DEST%\disc\BugsBunnyTaz_TimeBusters_USA.cue"
if not defined CUE call :scan_root "%HERE%."
if not defined CUE call :scan_root "%HERE%.."
if not defined CUE call :scan_root "%HERE%..\.."
exit /b 0
:scan_root
if defined CUE exit /b 0
for %%C in ("%~f1\*.cue") do call :try_cue "%%~fC"
if defined CUE exit /b 0
for /d %%D in ("%~f1\*") do call :scan_dir "%%~fD"
exit /b 0
:scan_dir
if defined CUE exit /b 0
for %%C in ("%~f1\*.cue") do call :try_cue "%%~fC"
exit /b 0
:try_cue
if defined CUE exit /b 0
if not exist "%~dpn1.bin" exit /b 0
for %%B in ("%~dpn1.bin") do if "%%~zB"=="678069840" set "CUE=%~f1"
exit /b 0
