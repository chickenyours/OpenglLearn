@echo off
setlocal
pushd "%~dp0..\.."
set "modelExecutable=bin\model_viewer.exe"
if exist "output\Release\bin\model_viewer.exe" set "modelExecutable=output\Release\bin\model_viewer.exe"
if not exist "%modelExecutable%" (
    echo Build model_viewer before launching the Rikka model viewer.
    popd
    exit /b 1
)
"%modelExecutable%" --model "%CD%\Asset\Model\Rikka_Uniform" %*
set "modelResult=%ERRORLEVEL%"
popd
exit /b %modelResult%
