@echo off
setlocal EnableExtensions DisableDelayedExpansion

rem Resolve the project and cached assets from this file, even when double-clicked.
pushd "%~dp0" 2>nul
if errorlevel 1 goto :project_unavailable

set "editorExecutable=output\Release\bin\skeletal_asset_editor.exe"
set "modelAsset=Asset\Generated\AliceMaid\Package.asset.json"

if not exist "%editorExecutable%" goto :missing_editor
if not exist "%modelAsset%" goto :missing_package

echo Starting Alice Maid in Skeletal Asset Editor ^(Release^).
echo Cached package: "%modelAsset%"
echo Default preview: ShoesFit wearing correction, paused.
echo Use Source pose or Shoes fit to compare in the timeline.
echo Display limit: 60 FPS.
echo Clothing classification is available in the mesh inspector.
echo.
"%editorExecutable%" --package "%modelAsset%" --mesh-regions --max-fps 60 %*
set "editorExit=%ERRORLEVEL%"
if "%editorExit%"=="0" goto :done
echo.
echo Skeletal Asset Editor exited with code %editorExit%.
goto :failed

:missing_editor
echo ERROR: Release editor is missing: "%editorExecutable%"
echo Run these commands in the project directory:
echo   cmake --preset windows-release
echo   cmake --build --preset windows-release --target skeletal_asset_editor -j2
set "editorExit=1"
goto :failed

:missing_package
echo ERROR: Cached Alice package is missing: "%modelAsset%"
echo Import the supplied Alice^(1^).blend using skeletal_asset_import.exe.
echo Use --source "path-to\Alice(1).blend" --output "Asset\Generated\AliceMaid"
echo   --fit-profile "Asset\ImportProfiles\AliceMaid.shoesfit.json".
echo See Module\Model\BLEND_IMPORT.md for the complete source path and import command.
set "editorExit=2"
goto :failed

:failed
echo See Module\Model\BLEND_IMPORT.md for setup and diagnostics.
if not defined SKELETAL_EDITOR_NO_PAUSE pause

:done
popd
exit /b %editorExit%

:project_unavailable
echo ERROR: Cannot access the project directory containing this launcher.
if not defined SKELETAL_EDITOR_NO_PAUSE pause
exit /b 3
