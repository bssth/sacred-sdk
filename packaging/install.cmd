@echo off
rem ======================================================================
rem  SacredSDK installer.
rem
rem  Put the whole contents of this archive into your Sacred Gold folder
rem  (the one with Sacred.exe) and double-click this file.
rem
rem  What it does:
rem    1. renames the game's own ijl15.dll to ijl15_real.dll  (once)
rem    2. copies the SDK proxy in its place
rem
rem  Nothing else is touched. The game's files stay where they are, and
rem  uninstall.cmd puts the original back.
rem ======================================================================
setlocal
cd /d "%~dp0"

echo.
echo   SacredSDK installer
echo   -------------------
echo.

if not exist "Sacred.exe" (
    echo   [!] Sacred.exe is not next to this script.
    echo.
    echo       Copy everything from this archive into your Sacred Gold
    echo       folder first, then run install.cmd from there.
    echo.
    pause
    exit /b 1
)

if not exist "sdk\ijl15.dll" (
    echo   [!] sdk\ijl15.dll is missing - the archive was not extracted whole.
    echo.
    pause
    exit /b 1
)

if exist "ijl15_real.dll" (
    echo   - ijl15_real.dll already exists, so the game's original is already
    echo     safe. Refreshing the SDK proxy only.
) else (
    if not exist "ijl15.dll" (
        echo   [!] Neither ijl15.dll nor ijl15_real.dll is here.
        echo       This does not look like a Sacred Gold install.
        echo.
        pause
        exit /b 1
    )
    echo   - renaming the game's ijl15.dll to ijl15_real.dll
    ren "ijl15.dll" "ijl15_real.dll"
    if errorlevel 1 (
        echo   [!] The rename failed. Close the game and any file browser
        echo       sitting in this folder, then run install.cmd again.
        echo.
        pause
        exit /b 1
    )
)

echo   - installing the SDK proxy as ijl15.dll
copy /y "sdk\ijl15.dll" "ijl15.dll" >nul
if errorlevel 1 (
    echo   [!] Could not write ijl15.dll. Is the game running?
    echo.
    pause
    exit /b 1
)

echo.
echo   Done. Start Sacred as usual.
echo.
echo   Your own mods go in    custom\lua\        (create it when you need it)
echo   The framework lives in sdk\custom\lua\    (leave it alone)
echo   The log is at          sdk\logs\sdk_loaded.log
echo.
echo   Optional: rename sdk.ini.example to sdk.ini for borderless / HD.
echo.
pause
