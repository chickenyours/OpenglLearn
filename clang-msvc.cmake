# 设置编译器路径（必须写全路径）
set(CMAKE_C_COMPILER   "C:/Mydata/clang+llvm-20.1.8-x86_64-pc-windows-msvc/clang+llvm-20.1.8-x86_64-pc-windows-msvc/bin/clang-cl.exe")
set(CMAKE_CXX_COMPILER "C:/Mydata/clang+llvm-20.1.8-x86_64-pc-windows-msvc/clang+llvm-20.1.8-x86_64-pc-windows-msvc/bin/clang-cl.exe")

# 设置链接器路径（lld-link）
set(CMAKE_LINKER       "C:/Mydata/clang+llvm-20.1.8-x86_64-pc-windows-msvc/clang+llvm-20.1.8-x86_64-pc-windows-msvc/bin/lld-link.exe")

# 设置默认 CRT 类型（影响 msvcrtd.lib）
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDebugDLL")  # /MDd

set(CLANG_BUILTIN_INC "C:/Mydata/clang+llvm-20.1.8-x86_64-pc-windows-msvc/clang+llvm-20/include")
set(MSVC_INC          "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207/include")
set(WINSDK_UCRT_INC   "C:/Program Files (x86)/Windows Kits/10/Include/10.0.22621.0/ucrt")
set(WINSDK_UM_INC     "C:/Program Files (x86)/Windows Kits/10/Include/10.0.22621.0/um")
set(WINSDK_SHARED_INC "C:/Program Files (x86)/Windows Kits/10/Include/10.0.22621.0/shared")

set(CMAKE_C_FLAGS_INIT   "/X \"-imsvc${CLANG_BUILTIN_INC}\" \"-imsvc${MSVC_INC}\" \"-imsvc${WINSDK_UCRT_INC}\" \"-imsvc${WINSDK_SHARED_INC}\" \"-imsvc${WINSDK_UM_INC}\"")
set(CMAKE_CXX_FLAGS_INIT "/X \"-imsvc${CLANG_BUILTIN_INC}\" \"-imsvc${MSVC_INC}\" \"-imsvc${WINSDK_UCRT_INC}\" \"-imsvc${WINSDK_SHARED_INC}\" \"-imsvc${WINSDK_UM_INC}\"")

# 指定库搜索路径（避免 msvcrtd.lib 缺失）
set(CMAKE_LIBRARY_PATH
    "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207/lib/x64"
    "C:/Program Files (x86)/Windows Kits/10/Lib/10.0.22621.0/um/x64"
    "C:/Program Files (x86)/Windows Kits/10/Lib/10.0.22621.0/ucrt/x64"
)
