@echo off
rem Reference camera + chapter-linked LC01 candidate. This is a source-input study,
rem not a recovered screenshot room or complete native probe field.
call "%~dp0StartDianaPreview.bat" --diana-face --camera-reference game-shot --camera-reference-profile "%~dp0Asset\Diana\game-reference-camera.json" --lighting reference-neutral --hair-lighting-profile "%~dp0Asset\Diana\Environment\st30-101-lc01.json" --hair-environment source --hair-ibl-incident spatial --hair-quality high --hair-shading segment --hair-geometry curve --hair-volume-resolution 128 --hair-shadow-cache stable --hair-ibl-paths voxel --sync-cloth %*
