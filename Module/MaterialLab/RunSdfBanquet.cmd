@echo off
setlocal
pushd "%~dp0..\.."
set "sdfBanquetExecutable=output\sdf-banquet\Debug\bin\material_lab_demo.exe"
if not exist "%sdfBanquetExecutable%" set "sdfBanquetExecutable=bin\material_lab_demo.exe"
if not exist "%sdfBanquetExecutable%" (
    echo Build material_lab_demo with the Model module before launching the SDF banquet.
    pause
    popd
    exit /b 1
)
"%sdfBanquetExecutable%" --vulkan --sdf-clipmaps --render-width 1280 --render-height 800 --msaa 1 --fps 60 %*
set "sdfBanquetResult=%ERRORLEVEL%"
popd
endlocal & exit /b %sdfBanquetResult%
