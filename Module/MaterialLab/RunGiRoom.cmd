@echo off
setlocal
pushd "%~dp0..\.."
set "giRoomExecutable=bin\material_lab_demo.exe"
if exist "output\Release\bin\material_lab_demo.exe" set "giRoomExecutable=output\Release\bin\material_lab_demo.exe"
if not exist "%giRoomExecutable%" (
    echo Build material_lab_demo before launching the sealed GI room.
    pause
    popd
    exit /b 1
)
"%giRoomExecutable%" --vulkan --gi-room --procedural --fullscreen --render-width 2560 --render-height 1440 --msaa 1 --fps 0 %*
set "giRoomResult=%ERRORLEVEL%"
popd
endlocal & exit /b %giRoomResult%
