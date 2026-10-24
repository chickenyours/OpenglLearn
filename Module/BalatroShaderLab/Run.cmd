@echo off
setlocal
for %%I in ("%~dp0..\..") do set "BALATRO_PROJECT=%%~fI"
set "BALATRO_MODULE=%~dp0"
set "BALATRO_EXE=%BALATRO_PROJECT%\bin\balatro_shader_lab.exe"
set "TEMP=%BALATRO_PROJECT%\build\balatro-temp"
set "TMP=%TEMP%"
if not exist "%TEMP%" mkdir "%TEMP%"
if /I "%~1"=="--build" goto build_and_run
if exist "%BALATRO_EXE%" goto launch
call :build
if errorlevel 1 exit /b 1
:launch
pushd "%BALATRO_MODULE%"
"%BALATRO_EXE%" %*
set "BALATRO_EXIT=%ERRORLEVEL%"
popd
exit /b %BALATRO_EXIT%

:build_and_run
call :build
if errorlevel 1 exit /b 1
pushd "%BALATRO_MODULE%"
"%BALATRO_EXE%"
set "BALATRO_EXIT=%ERRORLEVEL%"
popd
exit /b %BALATRO_EXIT%

:build
where cmake >nul 2>&1
if errorlevel 1 (
  echo CMake is required to build Balatro Shader Lab. Install or add it to PATH.
  exit /b 1
)
if not exist "%BALATRO_PROJECT%\build\CMakeCache.txt" (
  cmake -S "%BALATRO_PROJECT%" -B "%BALATRO_PROJECT%\build"
  if errorlevel 1 exit /b 1
)
cmake --build "%BALATRO_PROJECT%\build" --target balatro_shader_lab --parallel 4
if errorlevel 1 exit /b 1
if not exist "%BALATRO_EXE%" (
  echo Build completed, but the legacy Debug executable was not found:
  echo %BALATRO_EXE%
  echo This launcher uses the repository's existing build/ and bin/ layout.
  exit /b 1
)
exit /b 0
