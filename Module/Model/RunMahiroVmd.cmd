@echo off
setlocal
pushd "%~dp0..\.."
set "vmdEditor=bin\skeletal_asset_editor.exe"
if not exist "%vmdEditor%" set "vmdEditor=output\Release\bin\skeletal_asset_editor.exe"
if not exist "%vmdEditor%" (
    echo Build skeletal_asset_editor first.
    popd
    exit /b 1
)
if not exist "Asset\Generated\Animations\MahiroTaitui.anim.json" (
    echo Import the example with skeletal_animation_import first. See Module\Model\SKELETAL_ASSETS.md.
    popd
    exit /b 1
)
"%vmdEditor%" --package "Asset/Generated/MahiroMaid/Package.asset.json" --animation "Asset/Generated/Animations/MahiroTaitui.anim.json" --curve-bone "J_Bip_L_UpperLeg" %*
set "vmdEditorResult=%ERRORLEVEL%"
popd
exit /b %vmdEditorResult%
