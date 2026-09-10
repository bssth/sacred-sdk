@echo off
set "JAVA_HOME=C:\Program Files\Eclipse Adoptium\jdk-21.0.11.10-hotspot"
set "PATH=%JAVA_HOME%\bin;%PATH%"
set "GHIDRA_HOME=D:\ghidra_12.1_PUBLIC"
set "GAME_DIR=E:\SteamLibrary\steamapps\common\Sacred Gold"
set "PROJ_DIR=%GAME_DIR%\sdk\ghidra"
set "SCRIPT_DIR=%GAME_DIR%\sdk\re\ghidra"
"%GHIDRA_HOME%\support\analyzeHeadless.bat" ^
    "%PROJ_DIR%" "sacred_decrypted" ^
    -process Sacred_decrypted.exe ^
    -postScript %1 %2 %3 %4 %5 %6 ^
    -scriptPath "%SCRIPT_DIR%" ^
    -noanalysis
