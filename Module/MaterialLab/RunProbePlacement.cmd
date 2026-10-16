@echo off
setlocal
pushd "%~dp0..\.."
set "probePlacementExecutable=output\sdf-banquet\Debug\bin\material_lab_demo.exe"
if not exist "%probePlacementExecutable%" set "probePlacementExecutable=output\Release\bin\material_lab_demo.exe"
if not exist "%probePlacementExecutable%" set "probePlacementExecutable=bin\material_lab_demo.exe"
if not exist "%probePlacementExecutable%" (
    echo Build material_lab_demo before launching the probe placement experiment.
    pause
    popd
    exit /b 1
)
"%probePlacementExecutable%" --vulkan --probe-placement --procedural --render-width 1280 --render-height 800 --msaa 1 --fps 90 %*
set "probePlacementResult=%ERRORLEVEL%"
popd
endlocal & exit /b %probePlacementResult%
