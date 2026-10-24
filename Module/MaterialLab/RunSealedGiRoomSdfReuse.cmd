@echo off
setlocal
rem Opt-in trace sample reuse on top of the Release SDF/shadow configuration.
rem Trailing options can disable reuse for an interactive comparison.
if not exist "%~dp0..\..\output\gi-dynamic\Release\bin\material_lab_demo.exe" (
    echo Build the Release material_lab_demo before running this optimization.
    exit /b 1
)
call "%~dp0RunSealedGiRoomSdfOptimized.cmd" --surface-cache-reuse-sdf-samples %*
exit /b %ERRORLEVEL%
