@echo off
echo Diagnosing launcher issue...
echo.

set STEAM_PATH=%ProgramFiles(x86)%\Steam
echo Steam path: %STEAM_PATH%

if exist "%STEAM_PATH%" (
    echo Steam directory found
) else (
    echo Steam directory NOT found
    pause
    exit /b 1
)

set SDK_PATH=%STEAM_PATH%\steamapps\common\Source SDK Base 2013 Multiplayer
echo SDK path: %SDK_PATH%

if exist "%SDK_PATH%" (
    echo Source SDK Base 2013 Multiplayer directory found
) else (
    echo Source SDK Base 2013 Multiplayer directory NOT found
    pause
    exit /b 1
)

set HL2_EXE=%SDK_PATH%\hl2.exe
echo hl2.exe path: %HL2_EXE%

if exist "%HL2_EXE%" (
    echo hl2.exe found
) else (
    echo hl2.exe NOT found
    dir "%SDK_PATH%\*.exe"
    pause
    exit /b 1
)

set GAME_DIR=%~dp0mp\game\zombie_master_reborn
echo Game directory: %GAME_DIR%

if exist "%GAME_DIR%" (
    echo Game directory found
) else (
    echo Game directory NOT found
    pause
    exit /b 1
)

echo.
echo All paths found! Testing launch command...
echo Would run: "%HL2_EXE%" -game "%GAME_DIR%" -steam
echo.
echo Press Enter to test launch...
pause

start "" "%HL2_EXE%" -game "%GAME_DIR%" -steam
