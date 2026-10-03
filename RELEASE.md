# Windows Release 构建与便携发布

所有根工程子项目共用 `windows-debug`／`windows-release` CMake 配置。Release 使用 `/O2`、`NDEBUG` 和 `/MT`；GLFW、GLAD、JsonCpp、Lua 及模块库从源码使用相同配置编译，避免链接 `lib/debug` 中的旧调试库。

## 一键打包

在项目根目录执行：

```powershell
# 默认只编译并打包 PixelSandbox。
.\tools\build-release.ps1

# 指定其他子项目，或一次打包全部已登记项目。
.\tools\build-release.ps1 -Targets forest_fire_demo,material_lab_demo
.\tools\build-release.ps1 -All
```

等效 CMake 命令（无需运行 PowerShell 脚本）：

```powershell
cmake --preset windows-release
cmake --build --preset windows-release --target package_pixel_sandbox_demo
cmake --build --preset release-packages
```

`cmake --build --preset windows-release --target <任意根工程目标>` 也可单独构建其他 EXE、测试或库。无需打包时直接指定 `pixel_sandbox_demo` 等原有目标。

## 输出位置

```text
output/Release/
  bin/                         # 编译得到的程序，可能含测试工具
  lib/                         # 公共第三方静态库
  packages/
    pixel_sandbox_demo/         # 可直接运行的便携目录
    pixel_sandbox_demo-windows-x64.zip
    forest_fire_demo/ ...
  portable-verification.json   # 执行便携验证后生成
```

只需把对应 ZIP 复制到另一台 Windows 10／11 x64 电脑并完整解压，双击目录中的主 EXE。图形程序要求显卡及厂商驱动支持 OpenGL 4.5。默认静态 CRT 包无需另装 Visual Studio、CMake 或 VC++ 运行库；Windows 系统库和显卡驱动由目标系统提供。

每个包包含所需 EXE、非系统 DLL（如有）、指定资源目录、使用说明、第三方许可、PE 依赖检查结果及文件 SHA-256 清单。静态库已经链接进 EXE，不需要将 `.lib` 文件复制到使用者电脑。

打包目标会更新其生成的便携目录和 ZIP。运行产生的存档、编辑后的关卡需先另行备份；不要把唯一副本保存在生成目录中。原始 `Asset` 目录、开发目录 `bin` 和已有 Debug 程序不会被 Release 覆盖。

## 已登记项目

| 主目标 | 打包目标 | 附带资源／程序 |
| --- | --- | --- |
| pixel_sandbox_demo | package_pixel_sandbox_demo | 字体、着色器内置，附模块说明 |
| forest_fire_demo | package_forest_fire_demo | 无外部画面资源 |
| terrain_render_demo | package_terrain_render_demo | 程序化资源 |
| material_lab_demo | package_material_lab_demo | 若存在则带 `bin/materials/tite` 的贴图；否则使用程序化材质 |
| brotato_game | package_brotato_game | `Asset/Brotato` → `Brotato`，包括贴图、音频与清单 |
| iwanna_game | package_iwanna_game | `Asset/IWanna` → `IWanna`，同时带 showcase、editor、project、content、route_replay 工具 |

## 验证发布包

```powershell
cmake --build --preset windows-release --target pixel_sandbox_test pixel_sandbox_ui_test
ctest --preset windows-release -R '^pixel_sandbox_(test|ui_test)$'
.\tools\verify-portable.ps1
.\tools\verify-portable.ps1 -All
```

验证脚本将 ZIP 解压到新目录、核对每个文件校验值，在没有资源的工作目录中运行隐藏窗口自检，并把子进程 PATH 限定为 Windows 目录。打包时还会递归解析 PE 导入，缺 DLL、冲突 DLL、调试 CRT 或 `/MT` 包意外依赖动态 VC CRT 都会使任务失败。此验证使用本机驱动，不等同于已经在第二台物理电脑上测试。

## 新子项目接入

正常定义目标，链接公共 CMake target `glfw3`、`glad`、`jsonloader` 或现有模块即可；不要手动添加 `lib/debug` 搜索目录。

在 `cmake/Packages.cmake` 登记：

```cmake
opengl_add_portable_package(my_game
    ASSETS "${CMAKE_SOURCE_DIR}/Asset/MyGame|MyGame"
    DOCS "${CMAKE_SOURCE_DIR}/Module/MyGame/README.md"
    COMPANIONS my_editor)
```

`ASSETS` 左边为源目录，右边为包内相对路径；`DOCS`、`ASSETS`、`COMPANIONS` 都可以省略。程序应从 EXE 所在目录寻找资源。之后直接构建 `package_my_game`；该目标也会被 `package_portable` 自动包含。

## Debug 与工具链

新的隔离 Debug 配置：

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug --target pixel_sandbox_demo
```

输出到 `output/Debug/bin`，使用 `/MDd`。原来的 `cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=clang-msvc.cmake` 仍默认 Debug 并保留 `bin` 输出，但公共依赖也改为源码构建。打包命令明确拒绝 Debug。

构建需要 CMake ≥3.24、Ninja、clang-cl、lld-link、MSVC Build Tools 和 Windows SDK。`clang-msvc.cmake` 保留本机已有编译器／SDK 路径；在另一台开发电脑编译时需调整这些路径，或使用自己的工具链。运行发布包不依赖这些路径。

需要 `/MD` 的项目可以在独立构建目录设置 `OPENGLLEARN_STATIC_RUNTIME=OFF`；所有依赖仍会统一重编，打包会收集所需发布版 VC 运行库 DLL。`OPENGLLEARN_OUTPUT_ROOT` 可覆盖输出根目录。不要让不同运行库配置共用同一个构建目录或输出目录。
