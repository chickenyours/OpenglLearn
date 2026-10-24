@echo off
setlocal
rem Measured shadow submission bundle, Release only. Trailing options override.
if not exist "%~dp0..\..\output\gi-dynamic\Release\bin\material_lab_demo.exe" (
    echo Build the Release material_lab_demo before running this optimization.
    exit /b 1
)
call "%~dp0RunSealedGiRoom.cmd" --surface-cache-reuse-shadow-casters --surface-cache-cull-shadow-casters --surface-cache-reuse-shadow-atlas --surface-cache-reuse-uniform-snapshots %*
exit /b %ERRORLEVEL%
