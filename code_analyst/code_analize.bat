@echo off
setlocal EnableExtensions

rem ==== 项目根目录（固定） ====
set "PROJECT_ROOT=C:\Users\16620\Desktop\projects\OpenglLearn"
set "AST_EXE=%PROJECT_ROOT%\code_analyst\ast_test.exe"

rem ==== 外部传入参数 ====
rem %1 = 源文件路径
rem %2 = 类名
rem %3 = 输出文件路径

if "%~1"=="" (
    echo 用法: %~nx0 [源文件路径] [类名] [输出文件路径]
    echo 示例: %~nx0 "%PROJECT_ROOT%\code\Resource\Scripts\xuanzhuan.cpp" XuanZhuang "%PROJECT_ROOT%\code_analyst\hh.json"
    exit /b 1
)

set "SRC_FILE=%~1"
set "CLASS_NAME=%~2"
set "OUTPUT_FILE=%~3"

rem ==== 执行 ====
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
endlocal
