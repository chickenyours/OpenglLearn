@echo off
setlocal
for %%I in ("%~dp0..\..") do set "GRAVEYARD_PROJECT=%%~fI"
set "GRAVEYARD_EXE=%GRAVEYARD_PROJECT%\bin\graveyard_scene_lab.exe"
set "TEMP=%GRAVEYARD_PROJECT%\build\graveyard-temp"
set "TMP=%TEMP%"
if not exist "%TEMP%" mkdir "%TEMP%"
if /I "%~1"=="--build" goto build_and_run
if exist "%GRAVEYARD_EXE%" goto launch
call :build
if errorlevel 1 exit /b 1
:launch
pushd "%~dp0"
"%GRAVEYARD_EXE%" %*
set "GRAVEYARD_EXIT=%ERRORLEVEL%"
popd
exit /b %GRAVEYARD_EXIT%
:build_and_run
call :build
if errorlevel 1 exit /b 1
pushd "%~dp0"
"%GRAVEYARD_EXE%"
set "GRAVEYARD_EXIT=%ERRORLEVEL%"
popd
exit /b %GRAVEYARD_EXIT%
:build
where cmake >nul 2>&1
if errorlevel 1 (
 echo CMake is required to build Graveyard Scene Lab.
 exit /b 1
)
if not exist "%GRAVEYARD_PROJECT%\build\CMakeCache.txt" (
 cmake -S "%GRAVEYARD_PROJECT%" -B "%GRAVEYARD_PROJECT%\build"
 if errorlevel 1 exit /b 1
)
cmake --build "%GRAVEYARD_PROJECT%\build" --target graveyard_scene_lab --parallel 4
if errorlevel 1 exit /b 1
if not exist "%GRAVEYARD_EXE%" (
 echo Expected legacy build output was not found: %GRAVEYARD_EXE%
 exit /b 1
)
exit /b 0
