@echo off
setlocal

set BUILD_SHADER=call buildshaders.bat

set SOURCE_DIR="..\..\"

rem Change me to your mod's name!
set GAME_DIR="..\..\..\game\zombie_master_reborn"

%BUILD_SHADER% zmrstdshaders_dx9_20b -game %GAME_DIR% -source %SOURCE_DIR%
rem %BUILD_SHADER% zmrstdshaders_dx9_30  -game %GAME_DIR% -source %SOURCE_DIR% -force30
