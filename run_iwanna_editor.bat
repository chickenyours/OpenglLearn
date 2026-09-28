@echo off
setlocal
pushd "%~dp0"
if not exist bin\iwanna_editor.exe call build.bat
if errorlevel 1 exit /b 1
bin\iwanna_editor.exe --assets "%~dp0Asset\IWanna" --world "%~dp0Asset\IWanna\Workshop\world.json" %*
popd
