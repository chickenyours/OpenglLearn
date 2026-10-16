@echo off
setlocal
pushd "%~dp0..\.."
set "screenProbeTracingExecutable=output\sdf-banquet\Debug\bin\material_lab_demo.exe"
if not exist "%screenProbeTracingExecutable%" set "screenProbeTracingExecutable=output\Release\bin\material_lab_demo.exe"
if not exist "%screenProbeTracingExecutable%" set "screenProbeTracingExecutable=bin\material_lab_demo.exe"
if not exist "%screenProbeTracingExecutable%" (
    echo Build material_lab_demo before launching the screen probe tracing experiment.
    pause
    popd
    exit /b 1
)
"%screenProbeTracingExecutable%" --vulkan --screen-probe-tracing --procedural --render-width 1280 --render-height 800 --msaa 1 --fps 90 %*
set "screenProbeTracingResult=%ERRORLEVEL%"
popd
endlocal & exit /b %screenProbeTracingResult%
