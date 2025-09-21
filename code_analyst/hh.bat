@echo off
setlocal

set "PROJECT_ROOT=C:\Users\16620\Desktop\projects\OpenglLearn"
set "AST_EXE=%PROJECT_ROOT%\code_analyst\ast_test.exe"
set "SRC_FILE=%PROJECT_ROOT%\code\Resource\Scripts\xuanzhuan.cpp"
set "CLASS_NAME=XuanZhuang"
set "OUTPUT_FILE=%PROJECT_ROOT%\code_analyst\hh.json"

"%AST_EXE%" "%SRC_FILE%" -class="%CLASS_NAME%" -o="%OUTPUT_FILE%" -- ^
  -isystem "C:\Mydata\clang+llvm-20.1.8-x86_64-pc-windows-msvc\clang+llvm-20.1.8-x86_64-pc-windows-msvc\lib\clang\20\include" ^
  -isystem "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\include" ^
  -isystem "C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\ucrt" ^
  -isystem "C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\um" ^
  -isystem "C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\shared" ^
  -isystem "%PROJECT_ROOT%\include" ^
  -isystem "%PROJECT_ROOT%" ^
  -isystem "%PROJECT_ROOT%\code"

echo ExitCode=%ERRORLEVEL%
pause
endlocal
