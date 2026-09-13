@echo off
rem ======================================================================
rem  SacredSDK uninstaller: puts the game's own ijl15.dll back.
rem
rem  Your mods (custom\) and the framework (sdk\) are left alone -- delete
rem  them by hand if you want them gone.
rem ======================================================================
setlocal
cd /d "%~dp0"

echo.
echo   SacredSDK uninstaller
echo   ---------------------
echo.

if not exist "ijl15_real.dll" (
    echo   - ijl15_real.dll is not here, so the SDK is not installed.
    echo     Nothing to do.
    echo.
    pause
    exit /b 0
)

if exist "ijl15.dll" del "ijl15.dll"
if exist "ijl15.dll" (
    echo   [!] Could not remove the SDK proxy. Is the game running?
    echo.
    pause
    exit /b 1
)

ren "ijl15_real.dll" "ijl15.dll"
if errorlevel 1 (
    echo   [!] Could not restore ijl15.dll. Rename ijl15_real.dll back by hand.
    echo.
    pause
    exit /b 1
)

echo   - the game's original ijl15.dll is back in place
echo.
echo   Sacred now runs vanilla. Your custom\ and sdk\ folders were kept;
echo   delete them if you want the install completely clean.
echo.
pause
