@echo off
setlocal
cd /d "%~dp0"
rem Use the exact validated Model snapshot, including its compiled Render/RHI.
rem The ordinary editor executable may be relinked by later workspace builds.
if not exist "output\Diana\Quality25\captured\skeletal_asset_editor.exe" (
    echo Missing Quality25 validated preview. See output\Diana\Quality25\validation-summary.md
    exit /b 1
)
"output\Diana\Quality25\captured\skeletal_asset_editor.exe" --diana-face --max-fps 60 --camera-reference game-shot --camera-reference-profile "%~dp0Asset\Diana\game-reference-camera.json" --lighting reference-neutral --hair-lighting-profile "%~dp0Asset\Diana\Environment\st30-101-lc01.json" --hair-environment source --hair-ibl-incident spatial --hair-quality high --hair-shading segment --hair-geometry curve --hair-volume-resolution 128 --hair-shadow-cache stable --hair-ibl-paths voxel --sync-cloth --hair-ds-volume native --hair-ds-quality 2 %*
