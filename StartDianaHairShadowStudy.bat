@echo off
rem Q25 source-light calibration with Q24 native longitudinal transfer and Q23 soft projection.
rem Runtime sun sign and active CB remain unobserved.
rem Hair Poisson radius .003 and constructor ShadowPow .5 are explicit source candidates.
rem Surface radius .012 is a HOST close-view filtering adapter, not native skin law.
rem DeepOpacity keeps continuous coverage in eight HOST depth nodes; DS and hair receiving are unchanged.
rem Extinction .35 is a HOST surface-casting calibration; 1 restores the Q22 depth.
rem Source transfer maps authored roughness .5 to shader hm_v .320359975; raw restores the old roughness input.
rem Conditional static groom-center probe mix; HOST environment EV -5 and solar scale .85 are calibration.
rem The native incident consumer has no extra constant preview ambient; this profile sets hair host ambient0.
call "%~dp0StartDianaHairVolumeStudy.bat" --hair-direct-input source --hair-direct-profile "%~dp0Asset\Diana\st30-101-direct-sst-depth-candidate.json" --sun-shadow-filter source-poisson8 --sun-shadow-radius .003 --hair-opaque-shadow-power .5 --sun-surface-shadow-radius .012 --hair-shadow-coverage deep-opacity --hair-shadow-extinction-scale .35 --hair-roughness-input source --hair-lighting-profile "%~dp0Asset\Diana\Environment\st30-101-reference-lighting-calibrated.json" --direct-sun-scale .85 %*
