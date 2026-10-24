@echo off
rem Separate static SST sun-basis candidate. Runtime sun sign, character pose,
rem active light list and source-to-host EV remain unobserved in the reference.
rem The ordinary volume study retains its neutral lighting.
call "%~dp0StartDianaHairVolumeStudy.bat" --hair-direct-input source --hair-direct-profile "%~dp0Asset\Diana\st30-101-direct-sst-depth-candidate.json" %*
