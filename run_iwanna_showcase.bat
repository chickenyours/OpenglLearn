@echo off
setlocal
if not exist "%~dp0bin\iwanna_showcase.exe" (
    echo Build first using build.bat
    exit /b 1
)
"%~dp0bin\iwanna_showcase.exe" --assets "%~dp0Asset\IWanna" --config "%~dp0Asset\IWanna\gameplay.json" --world "%~dp0Asset\IWanna\Workshop\world.json" %*
