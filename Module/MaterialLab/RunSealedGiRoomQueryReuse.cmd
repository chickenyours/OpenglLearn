@echo off
setlocal
rem Opt-in exact single-traversal probe profile and geometry addressing.
rem The Round8 model-update launcher remains the original comparison path.
if not exist "%~dp0..\..\output\gi-dynamic\Release\bin\material_lab_demo.exe" (
    echo Build the Release material_lab_demo before running this optimization.
    exit /b 1
)
call "%~dp0RunSealedGiRoomModelUpdate.cmd" --surface-cache-direct-software-refinement --surface-cache-fast-geometry-addressing %*
exit /b %ERRORLEVEL%
