@echo off
setlocal
rem Opt-in budget architecture. The original Round9 launcher stays available.
if not exist "%~dp0..\..\output\gi-dynamic\Release\bin\material_lab_demo.exe" (
    echo Build the Release material_lab_demo before running this profile.
    exit /b 1
)
call "%~dp0RunSealedGiRoomQueryReuse.cmd" --gi90-profile 2 --fps 90 %*
exit /b %ERRORLEVEL%
