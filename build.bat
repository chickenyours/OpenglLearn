@echo off

set PROJDIR=%~dp0

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=clang-msvc.cmake %PROJDIR%

cmake --build build --config Debug