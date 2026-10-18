@echo off
setlocal
pushd "%~dp0..\.."
set "sealedGiExecutable=output\sdf-banquet\Debug\bin\material_lab_demo.exe"
if not exist "%sealedGiExecutable%" set "sealedGiExecutable=bin\material_lab_demo.exe"
if not exist "%sealedGiExecutable%" (
    echo Build material_lab_demo before launching the sealed GI room.
    pause
    popd
    exit /b 1
)
"%sealedGiExecutable%" --vulkan --software-rays --surface-cache-sealed-room --render-width 1280 --render-height 800 --msaa 1 --fps 60 %*
set "sealedGiResult=%ERRORLEVEL%"
popd
endlocal & exit /b %sealedGiResult%
