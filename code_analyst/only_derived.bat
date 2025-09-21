@echo off
setlocal EnableExtensions

REM ===== 固定环境路径（按你的机器改/确认） =====
set "PROJECT_ROOT=C:\Users\16620\Desktop\projects\OpenglLearn"
set "AST_EXE=%PROJECT_ROOT%\code_analyst\only_derived.exe"

set "CLANG_BUILTIN=C:\Mydata\clang+llvm-20.1.8-x86_64-pc-windows-msvc\clang+llvm-20.1.8-x86_64-pc-windows-msvc\lib\clang\20\include"
set "MSVC_INC=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\include"
set "SDK_UCRT=C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\ucrt"
set "SDK_UM=C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\um"
set "SDK_SHARED=C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\shared"

REM ===== 参数：源文件、类名、输出文件 =====
if "%~3"=="" (
    echo 用法: %~nx0 "源文件.cpp" 基类 "输出.json"
    echo 示例: %~nx0 "%PROJECT_ROOT%\code\Resource\Scripts\xuanzhuan.cpp" IScript "%PROJECT_ROOT%\code_analyst\out.json"
    echo 错误代码含义:
    echo   0 - 成功
    echo   1 - 解析失败
    echo   2 - 并无基类
    echo   3 - 存在多个子类
    echo   4 - 无子类
  exit /b 1
)
set "SRC=%~1"
set "BASECLASS=%~2"
set "OUTJSON=%~3"

if not exist "%AST_EXE%" (
  echo [ERR] 未找到 ast_test.exe: %AST_EXE%
  exit /b 101
)
if not exist "%SRC%" (
  echo [ERR] 源文件不存在: %SRC%
  exit /b 102
)

REM 创建输出目录（若不存在）
for %%I in ("%OUTJSON%") do set "OUTDIR=%%~dpI"
if not exist "%OUTDIR%" mkdir "%OUTDIR%" 2>nul

REM ===== 执行 =====
"%AST_EXE%" "%SRC%" -class="%BASECLASS%" -o="%OUTJSON%" -- ^
  -isystem "%CLANG_BUILTIN%" ^
  -isystem "%MSVC_INC%" ^
  -isystem "%SDK_UCRT%" ^
  -isystem "%SDK_UM%" ^
  -isystem "%SDK_SHARED%" ^
  -isystem "%PROJECT_ROOT%\include" ^
  -isystem "%PROJECT_ROOT%" ^
  -isystem "%PROJECT_ROOT%\code"

set "RC=%ERRORLEVEL%"
echo ExitCode=%RC%
endlocal & exit /b %RC%
