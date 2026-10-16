@echo off
setlocal
pushd "%~dp0..\.."
set "clothEditor=output\Release\bin\skeletal_asset_editor.exe"
if not exist "%clothEditor%" set "clothEditor=bin\skeletal_asset_editor.exe"
if not exist "%clothEditor%" (
    echo Build skeletal_asset_editor first.
    popd
    exit /b 1
)
if not exist "Asset\Generated\MahiroCloth\Character.asset.json" (
    echo Generate the modular outfit with garment_asset_import first. See Module\Model\CLOTH.md.
    popd
    exit /b 1
)
echo Running "%clothEditor%"
"%clothEditor%" --character "Asset/Generated/MahiroCloth/Character.asset.json" %*
set "clothEditorResult=%ERRORLEVEL%"
popd
exit /b %clothEditorResult%
