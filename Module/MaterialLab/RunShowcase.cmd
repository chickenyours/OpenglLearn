@echo off
setlocal
pushd "%~dp0..\.."
set "showcaseExecutable=bin\material_lab_demo.exe"
if exist "output\Release\bin\material_lab_demo.exe" set "showcaseExecutable=output\Release\bin\material_lab_demo.exe"
if not exist "%showcaseExecutable%" (
    echo Build material_lab_demo before launching the lighting showcase.
    pause
    popd
    exit /b 1
)
set "showcaseAssets="
if exist "bin\materials\tite\square_tiles_03_diff_1k.png" set "showcaseAssets=--legacy-textures bin\materials\tite"
"%showcaseExecutable%" --vulkan --showcase --fullscreen --render-width 2560 --render-height 1440 --msaa 2 --fps 90 %showcaseAssets% %*
popd
endlocal
