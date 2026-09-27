@echo off
setlocal
pushd "%~dp0"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=clang-msvc.cmake
if errorlevel 1 goto failed
cmake --build build --config Debug
if errorlevel 1 goto failed
popd
exit /b 0
:failed
popd
exit /b 1
