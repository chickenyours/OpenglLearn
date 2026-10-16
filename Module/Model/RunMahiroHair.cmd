@echo off
setlocal
pushd "%~dp0..\.."
set "hairEditor=output\Release\bin\skeletal_asset_editor.exe"
if not exist "%hairEditor%" set "hairEditor=bin\skeletal_asset_editor.exe"
if not exist "%hairEditor%" (
    echo Build skeletal_asset_editor first. See Module\Model\HAIR.md.
    popd
    exit /b 1
)
if not exist "Asset\Generated\MahiroCloth\Hair\Hair_Realtime30.hair.json" (
    echo Generate the hair physics asset first. See Module\Model\HAIR.md.
    popd
    exit /b 1
)
echo Running "%hairEditor%"
"%hairEditor%" --character "Asset/Generated/MahiroCloth/Character.asset.json" --hair "Asset/Generated/MahiroCloth/Hair/Hair_Realtime30.hair.json" --no-cloth %*
set "hairEditorResult=%ERRORLEVEL%"
popd
exit /b %hairEditorResult%
