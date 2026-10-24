@echo off
setlocal
rem Opt-in model-resource update preset. The Round7 launcher stays available.
rem Trailing arguments may select the retained BUILD/upload/origin paths.
if not exist "%~dp0..\..\output\gi-dynamic\Release\bin\material_lab_demo.exe" (
    echo Build the Release material_lab_demo before running this optimization.
    exit /b 1
)
call "%~dp0RunSealedGiRoomNativeProbes.cmd" --surface-cache-update-acceleration --surface-cache-prune-native-uploads %*
exit /b %ERRORLEVEL%
