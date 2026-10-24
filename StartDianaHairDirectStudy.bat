@echo off
rem Candidate checkpoint52 light inputs. The screenshot location, runtime sun
rem direction and active light/shadow state have not been captured.
call "%~dp0StartDianaHairReferenceStudy.bat" --hair-direct-profile "%~dp0Asset\Diana\st30-101-direct-lighting.json" --hair-direct-input source %*
