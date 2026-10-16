@echo off
setlocal EnableExtensions DisableDelayedExpansion

rem Always resolve resources relative to this script, including double-click launches.
pushd "%~dp0" 2>nul
if errorlevel 1 goto :project_unavailable

set "editorExecutable=output\Release\bin\skeletal_asset_editor.exe"
set "characterAsset=Asset\Generated\MahiroCloth\Character.asset.json"
set "hairAsset=Asset\Generated\MahiroCloth\Hair\Hair_Realtime30.hair.json"
set "animationAsset=Asset\Generated\Animations\MahiroAozoraCider.anim.json"

if not exist "%editorExecutable%" goto :missing_editor
if not exist "%characterAsset%" goto :missing_character
if not exist "%hairAsset%" goto :missing_hair

echo Starting Skeletal Asset Editor ^(Release^).
echo Character: "%characterAsset%"
echo Physics: skirt and hair enabled; requested hair profile: 30 Hz.
echo Display limit: 60 FPS. Physics updates have an independent rate.
echo For animation import without physics, append --no-cloth --no-hair.
echo.
if exist "%animationAsset%" goto :launch_imported_animation
echo The optional baked Aozora Cider animation was not found.
echo Opening the character's existing sequences. Use Import VMD to add a motion.
"%editorExecutable%" --character "%characterAsset%" --hair "%hairAsset%" --max-fps 60 %*
set "editorExit=%ERRORLEVEL%"
goto :editor_finished

:launch_imported_animation
echo Animation: "%animationAsset%"
"%editorExecutable%" --character "%characterAsset%" --hair "%hairAsset%" --animation "%animationAsset%" --max-fps 60 %*
set "editorExit=%ERRORLEVEL%"

:editor_finished
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

:missing_character
echo ERROR: Character asset is missing: "%characterAsset%"
echo See Module\Model\CLOTH.md for garment_asset_import and character generation.
set "editorExit=2"
goto :failed

:missing_hair
echo ERROR: Hair physics asset is missing: "%hairAsset%"
echo See Module\Model\HAIR.md for hair_asset_import and the 30 Hz profile.
set "editorExit=3"
goto :failed

:failed
echo See Module\Model\START_EDITOR.md for launch and import instructions.
if not defined SKELETAL_EDITOR_NO_PAUSE pause

:done
popd
exit /b %editorExit%

:project_unavailable
echo ERROR: Cannot access the project directory containing this launcher.
if not defined SKELETAL_EDITOR_NO_PAUSE pause
exit /b 4
