@echo off
setlocal
if not exist "%~dp0bin\iwanna_game.exe" (
    echo Build first using build.bat
    exit /b 1
)
"%~dp0bin\iwanna_game.exe" --config "%~dp0Asset\IWanna\gameplay.json" %*
