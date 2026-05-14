@echo off
REM Smoke test for the ijl15.dll proxy.
REM   1. wipes the old log
REM   2. launches Sacred.exe
REM   3. waits 4 seconds (enough for PE load + DllMain + initial splash)
REM   4. kills Sacred
REM   5. prints the resulting log
REM
REM Run from anywhere; this script uses its own location to find the game.

setlocal
set "GAME_DIR=%~dp0..\.."
set "LOG=%GAME_DIR%\sdk\logs\sdk_loaded.log"

echo [info] game dir : %GAME_DIR%
echo [info] log file : %LOG%

if exist "%LOG%" del "%LOG%"

pushd "%GAME_DIR%"
echo [info] launching Sacred.exe ...
start "" "Sacred.exe"
timeout /t 4 /nobreak >nul
echo [info] killing Sacred ...
taskkill /f /im Sacred.exe >nul 2>&1
taskkill /f /im Testapp.exe >nul 2>&1
popd

echo.
echo --- sdk_loaded.log ---
if exist "%LOG%" (
    type "%LOG%"
) else (
    echo [WARN] log file was not created. Possible reasons:
    echo   - proxy ijl15.dll failed to load (missing ijl15_real.dll, wrong arch, etc.)
    echo   - game crashed before DllMain executed
    echo   - log path resolution failed in DllMain
)
echo --- end ---
endlocal
