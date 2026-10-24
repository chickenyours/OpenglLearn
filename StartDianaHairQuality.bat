@echo off
call "%~dp0StartDianaPreview.bat" --diana-face --lighting reference-neutral --hair-quality high --hair-shading segment --hair-geometry curve --hair-volume-resolution 128 --hair-shadow-cache stable --hair-ibl-paths voxel --sync-cloth %*
