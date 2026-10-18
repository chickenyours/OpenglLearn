@echo off
setlocal EnableExtensions DisableDelayedExpansion
pushd "%~dp0" 2>nul
if errorlevel 1 goto :project_unavailable
set "editorExecutable=output\Release\bin\skeletal_asset_editor.exe"
set "modelAsset=Asset\Generated\LolitaMaid\Package.asset.json"
if not exist "%editorExecutable%" goto :missing_editor
if not exist "%modelAsset%" goto :missing_package
echo Starting Lolita Maid high-resolution FBX package ^(Release^).
echo Mesh Regions: select Garment to inspect the clothing submeshes.
echo Static preview starts paused. Display limit: 60 FPS.
"%editorExecutable%" --package "%modelAsset%" --mesh-regions --frame-region body --max-fps 60 %*
set "editorExit=%ERRORLEVEL%"
if "%editorExit%"=="0" goto :done
echo Skeletal Asset Editor exited with code %editorExit%.
goto :failed
:missing_editor
echo ERROR: Release editor is missing. See Module\Model\MAX_IMPORT.md.
set "editorExit=1"
goto :failed
:missing_package
echo ERROR: Cached package is missing: "%modelAsset%"
echo Import Asset\Model\LolitaMaid\scene.fbx into a new package directory.
echo See Module\Model\MAX_IMPORT.md.
set "editorExit=2"
goto :failed
:failed
if not defined SKELETAL_EDITOR_NO_PAUSE pause
:done
popd
exit /b %editorExit%
:project_unavailable
echo ERROR: Cannot access the project directory containing this launcher.
if not defined SKELETAL_EDITOR_NO_PAUSE pause
exit /b 3
