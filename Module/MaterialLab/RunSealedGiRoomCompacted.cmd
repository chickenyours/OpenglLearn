@echo off
setlocal
rem Optional measured compaction / AS storage reuse comparison, Release only.
if not exist "%~dp0..\..\output\gi-dynamic\Release\bin\material_lab_demo.exe" (
    echo Build the Release material_lab_demo before running this comparison.
    exit /b 1
)
call "%~dp0RunSealedGiRoom.cmd" --surface-cache-compact-sdf-rays --surface-cache-reuse-acceleration %*
exit /b %ERRORLEVEL%
