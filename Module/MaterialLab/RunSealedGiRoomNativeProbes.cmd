@echo off
setlocal
rem Measured probe tracing and radiance filtering on the Release GI preset.
rem Trailing options can override either path for comparison.
if not exist "%~dp0..\..\output\gi-dynamic\Release\bin\material_lab_demo.exe" (
    echo Build the Release material_lab_demo before running this optimization.
    exit /b 1
)
call "%~dp0RunSealedGiRoomSdfReuse.cmd" --surface-cache-native-probes --surface-cache-radiance-filter --fps 120 %*
exit /b %ERRORLEVEL%
