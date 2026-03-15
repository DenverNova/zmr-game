@echo off
title Zombie Master: Reborn - Simple Launcher

echo.
echo  Zombie Master: Reborn - Simple Launcher
echo ========================================
echo.

set STEAM_PATH=C:\Program Files (x86)\Steam
set SDK_PATH=%STEAM_PATH%\steamapps\common\Source SDK Base 2013 Multiplayer
set GAME_DIR=%~dp0mp\game\zombie_master_reborn
set HL2_EXE=%SDK_PATH%\hl2.exe

echo Steam: %STEAM_PATH%
echo SDK: %SDK_PATH%
echo Game: %GAME_DIR%
echo Executable: %HL2_EXE%
echo.

if not exist "%HL2_EXE%" (
    echo ERROR: hl2.exe not found!
    pause
    exit /b 1
)

if not exist "%GAME_DIR%" (
    echo ERROR: Game directory not found!
    pause
    exit /b 1
)

echo Launching Zombie Master: Reborn...
echo Command: "%HL2_EXE%" -game "%GAME_DIR%" -steam
echo.

REM Try launching without steam first, then with steam
start "" "%HL2_EXE%" -game "%GAME_DIR%" -steam

echo Game launched!
timeout /t 2 > nul
