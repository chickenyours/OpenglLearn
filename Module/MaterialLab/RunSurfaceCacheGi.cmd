@echo off
setlocal
pushd "%~dp0..\.."
set "surfaceCacheExecutable=output\sdf-banquet\Debug\bin\material_lab_demo.exe"
if not exist "%surfaceCacheExecutable%" set "surfaceCacheExecutable=bin\material_lab_demo.exe"
if not exist "%surfaceCacheExecutable%" (
    echo Build material_lab_demo before launching Surface Cache GI.
    pause
    popd
    exit /b 1
)
"%surfaceCacheExecutable%" --vulkan --software-rays --surface-cache-gi --render-width 1280 --render-height 800 --msaa 1 --fps 60 %*
set "surfaceCacheResult=%ERRORLEVEL%"
popd
endlocal & exit /b %surfaceCacheResult%
