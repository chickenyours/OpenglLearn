@echo off
setlocal
pushd "%~dp0..\.."
set "assetEditorExecutable=bin\skeletal_asset_editor.exe"
if exist "output\Release\bin\skeletal_asset_editor.exe" set "assetEditorExecutable=output\Release\bin\skeletal_asset_editor.exe"
if not exist "%assetEditorExecutable%" (
    echo Build skeletal_asset_editor before launching the editor.
    popd
    exit /b 1
)
"%assetEditorExecutable%" %*
set "assetEditorResult=%ERRORLEVEL%"
popd
exit /b %assetEditorResult%
