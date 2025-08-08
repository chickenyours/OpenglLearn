@echo off
setlocal

rem 设置项目根目录（统一前缀）
set PROJECT_ROOT=C:\Users\16620\Desktop\projects\OpenglLearn

rem 设置可执行文件路径
set AST_EXE=%PROJECT_ROOT%\code_analyst\ast_test.exe

rem 设置源文件、类名、输出文件
set SRC_FILE=%PROJECT_ROOT%\code\Resource\Scripts\xuanzhuan.cpp
set CLASS_NAME=XuanZhuang
set OUTPUT_FILE=%PROJECT_ROOT%\code_analyst\hh.json

rem 设置 include 路径（使用项目根路径）
set INC_DIRS=-I%PROJECT_ROOT%\include ^
             -I%PROJECT_ROOT% ^
             -I%PROJECT_ROOT%\code

rem 设置 system include 路径（GCC 标准库路径）
set STL_DIR=-isystem C:\Mydata\x86_64-14.2.0-release-win32-seh-msvcrt-rt_v12-rev0\mingw64\lib\gcc\x86_64-w64-mingw32\14.2.0\include

rem 设置宏定义（移除 _WINDOWS_，保留 lean and mean）
set DEFINES=-DWIN32_LEAN_AND_MEAN -D__NO_INLINE__ -D__GXX_EXPERIMENTAL_CXX0X__ -D__clang__ -D__NO_BUILTIN

rem 执行分析程序
%AST_EXE% %SRC_FILE% -class=%CLASS_NAME% -o=%OUTPUT_FILE% -- %INC_DIRS% %STL_DIR% %DEFINES%

pause

