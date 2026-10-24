@echo off
setlocal
rem Release SDF address specialization on top of the measured shadow bundle.
rem Trailing options can select legacy addressing for an interactive comparison.
if not exist "%~dp0..\..\output\gi-dynamic\Release\bin\material_lab_demo.exe" (
    echo Build the Release material_lab_demo before running this optimization.
    exit /b 1
)
call "%~dp0RunSealedGiRoomShadowOptimized.cmd" --surface-cache-fast-sdf-addressing %*
exit /b %ERRORLEVEL%
