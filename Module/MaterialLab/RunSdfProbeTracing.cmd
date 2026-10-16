@echo off
setlocal
pushd "%~dp0..\.."
set "sdfProbeTracingExecutable=output\sdf-banquet\Debug\bin\material_lab_demo.exe"
if not exist "%sdfProbeTracingExecutable%" set "sdfProbeTracingExecutable=output\Release\bin\material_lab_demo.exe"
if not exist "%sdfProbeTracingExecutable%" set "sdfProbeTracingExecutable=bin\material_lab_demo.exe"
if not exist "%sdfProbeTracingExecutable%" (
    echo Build material_lab_demo before launching the mesh and global SDF experiment.
    pause
    popd
    exit /b 1
)
"%sdfProbeTracingExecutable%" --vulkan --sdf-probe-tracing --procedural --render-width 1280 --render-height 800 --msaa 1 --fps 90 %*
set "sdfProbeTracingResult=%ERRORLEVEL%"
popd
endlocal & exit /b %sdfProbeTracingResult%
