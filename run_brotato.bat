@echo off
setlocal
pushd "%~dp0"
if not exist "bin\brotato_game.exe" (
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=clang-msvc.cmake
    if errorlevel 1 goto failed
    cmake --build build --target brotato_game
    if errorlevel 1 goto failed
)
"bin\brotato_game.exe" %*
set "brotato_result=%errorlevel%"
popd
exit /b %brotato_result%
:failed
popd
exit /b 1
