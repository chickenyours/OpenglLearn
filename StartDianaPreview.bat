@echo off
setlocal
cd /d "%~dp0"
if not exist "output\Release\bin\skeletal_asset_editor.exe" (
    echo Build first: cmake --preset windows-release
    echo cmake --build --preset windows-release --target skeletal_asset_editor -j 1
    pause
    exit /b 1
)
"output\Release\bin\skeletal_asset_editor.exe" --diana --max-fps 60 %*
if errorlevel 1 pause
