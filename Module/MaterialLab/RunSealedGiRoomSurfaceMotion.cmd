@echo off
setlocal
pushd "%~dp0..\.."
rem Prefer the verified local package, with normal build outputs for source checkouts.
set "sealedGiExecutable=build\gi92_trial73\material_lab_demo.exe"
if not exist "%sealedGiExecutable%" set "sealedGiExecutable=output\gi-dynamic\Release\bin\material_lab_demo.exe"
if not exist "%sealedGiExecutable%" set "sealedGiExecutable=bin\material_lab_demo.exe"
if not exist "%sealedGiExecutable%" (
    echo Build material_lab_demo in Release before running this GI preset.
    popd
    exit /b 1
)
echo F: select under cursor / arrows: move on floor / Shift: faster
echo H: toggle all six props / Shift+H: spatial filter / R: reset
"%sealedGiExecutable%" --vulkan --surface-cache-sealed-room --gi90-ue-high --render-width 1280 --render-height 800 --msaa 1 --fps 0 --procedural --no-debug-ui %*
set "sealedGiResult=%ERRORLEVEL%"
popd
endlocal & exit /b %sealedGiResult%
