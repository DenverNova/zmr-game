@echo off
echo Testing Steam path detection...
echo.

echo Checking C: drive paths:
if exist "%ProgramFiles(x86)%\Steam" echo [FOUND] %ProgramFiles(x86)%\Steam
if exist "%ProgramFiles%\Steam" echo [FOUND] %ProgramFiles%\Steam
if exist "%LOCALAPPDATA%\Steam" echo [FOUND] %LOCALAPPDATA%\Steam

echo.
echo Checking D: drive paths:
if exist "D:\Program Files (x86)\Steam" echo [FOUND] D:\Program Files (x86)\Steam
if exist "D:\Program Files\Steam" echo [FOUND] D:\Program Files\Steam
if exist "D:\Steam" echo [FOUND] D:\Steam

echo.
echo Checking for Source SDK Base 2013 Multiplayer:
if exist "%ProgramFiles(x86)%\Steam\steamapps\common\Source SDK Base 2013 Multiplayer" echo [FOUND] %ProgramFiles(x86)%\Steam\steamapps\common\Source SDK Base 2013 Multiplayer
if exist "%ProgramFiles%\Steam\steamapps\common\Source SDK Base 2013 Multiplayer" echo [FOUND] %ProgramFiles%\Steam\steamapps\common\Source SDK Base 2013 Multiplayer
if exist "D:\Program Files (x86)\Steam\steamapps\common\Source SDK Base 2013 Multiplayer" echo [FOUND] D:\Program Files (x86)\Steam\steamapps\common\Source SDK Base 2013 Multiplayer
if exist "D:\Program Files\Steam\steamapps\common\Source SDK Base 2013 Multiplayer" echo [FOUND] D:\Program Files\Steam\steamapps\common\Source SDK Base 2013 Multiplayer
if exist "D:\Steam\steamapps\common\Source SDK Base 2013 Multiplayer" echo [FOUND] D:\Steam\steamapps\common\Source SDK Base 2013 Multiplayer

echo.
echo If you see [FOUND] above but the launcher still fails, please tell me which paths were found.
pause
