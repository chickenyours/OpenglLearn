@echo off
setlocal
rem Explicit architecture experiment. 90 is a frame cap, not a performance guarantee.
rem Consult GI91_ARCHITECTURE.md for the measured quality and performance status.
if not exist "%~dp0..\..\output\gi-dynamic\Release\bin\material_lab_demo.exe" (
    echo Build the Release material_lab_demo before running this profile.
    exit /b 1
)
call "%~dp0RunSealedGiRoomQueryReuse.cmd" --gi90-profile 4 --no-debug-ui --fps 90 %*
exit /b %ERRORLEVEL%
