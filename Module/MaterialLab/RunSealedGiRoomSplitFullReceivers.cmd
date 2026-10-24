@echo off
setlocal
rem Split-world-cache reference with full receiver tracing for report comparisons.
call "%~dp0RunSealedGiRoomQueryReuse.cmd" --gi90-profile 5 --no-debug-ui --fps 90 %*
exit /b %ERRORLEVEL%
