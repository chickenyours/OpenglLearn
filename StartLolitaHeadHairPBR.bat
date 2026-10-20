@echo off
setlocal EnableExtensions DisableDelayedExpansion
pushd "%~dp0" 2>nul
if errorlevel 1 goto :project_unavailable
set "editorExecutable=%~dp0output\Release\bin\skeletal_asset_editor.exe"
set "headAsset=%~dp0Asset\Generated\LolitaHeadHair\Package.asset.json"
if not exist "%editorExecutable%" goto :missing_editor
if not exist "%headAsset%" goto :missing_package
echo Starting head and loose hair preview ^(PBR, Release^).
echo Only the separate head/hair package is loaded. Hair geometry stays visible.
echo Physics is disabled in this preparation view. Display limit: 30 FPS.
"%editorExecutable%" --package "%headAsset%" --pipeline pbr --mesh-regions --no-cloth --no-hair --max-fps 30 %*
set "editorExit=%ERRORLEVEL%"
if "%editorExit%"=="0" goto :done
echo Head and hair preview exited with code %editorExit%.
goto :failed
:missing_editor
echo ERROR: Release editor is missing or inaccessible: "%editorExecutable%"
echo Launcher: "%~f0"
echo See Module\Model\HEAD_HAIR_PBR.md.
set "editorExit=1"
goto :failed
:missing_package
echo ERROR: Head/hair package is missing or inaccessible: "%headAsset%"
echo Launcher: "%~f0"
echo See Module\Model\HEAD_HAIR_PBR.md for asset access and extraction details.
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
