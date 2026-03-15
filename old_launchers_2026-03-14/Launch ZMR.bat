@echo off
title Zombie Master: Reborn - Game Launcher

echo.
echo ========================================
echo  Zombie Master: Reborn - Game Launcher
echo ========================================
echo.

REM Try to find Steam (check both C: and D: drives)
set STEAM_PATH=
set STEAM_FOUND=0

REM Check common Steam locations
if exist "%ProgramFiles(x86)%\Steam" (
    set STEAM_PATH=%ProgramFiles(x86)%\Steam
    set STEAM_FOUND=1
) else if exist "%ProgramFiles%\Steam" (
    set STEAM_PATH=%ProgramFiles%\Steam
    set STEAM_FOUND=1
) else if exist "%LOCALAPPDATA%\Steam" (
    set STEAM_PATH=%LOCALAPPDATA%\Steam
    set STEAM_FOUND=1
) else if exist "D:\Program Files (x86)\Steam" (
    set STEAM_PATH=D:\Program Files (x86)\Steam
    set STEAM_FOUND=1
) else if exist "D:\Program Files\Steam" (
    set STEAM_PATH=D:\Program Files\Steam
    set STEAM_FOUND=1
) else if exist "D:\Steam" (
    set STEAM_PATH=D:\Steam
    set STEAM_FOUND=1
)

if %STEAM_FOUND%==0 (
    echo Error: Steam not found.
    echo Please install Steam and Source SDK Base 2013 Multiplayer.
    pause
    exit /b 1
)

echo Steam found at: %STEAM_PATH%

REM Check for Source SDK Base 2013 Multiplayer
set SDK_PATH=%STEAM_PATH%\steamapps\common\Source SDK Base 2013 Multiplayer
if not exist "%SDK_PATH%" (
    echo Error: Source SDK Base 2013 Multiplayer not found.
    echo Please install it from Steam: https://store.steampowered.com/app/243750/
    pause
    exit /b 1
)

echo Source SDK Base 2013 Multiplayer found at: %SDK_PATH%

REM Set game paths
set GAME_DIR=%~dp0mp\game\zombie_master_reborn
set HL2_EXE=%SDK_PATH%\hl2.exe

if not exist "%HL2_EXE%" (
    echo Error: hl2.exe not found in Source SDK directory.
    pause
    exit /b 1
)

if not exist "%GAME_DIR%" (
    echo Error: Game directory not found at: %GAME_DIR%
    pause
    exit /b 1
)

echo.
echo Launching Zombie Master: Reborn...
echo.

REM Launch the game
start "" "%HL2_EXE%" -game "%GAME_DIR%" -steam

echo Game launched! You can close this window.
timeout /t 3 > nul
