# Balatro Shader Lab

这是一个在本项目中独立运行的着色器实验室，使用项目自带的 **ApplicationWindow** 和 **Render** 模块。它读取指定本地 Balatro 安装包中提取的 GLSL 和图集，让你暂停时间、切换纸底与卡片版本、拆开渲染步骤，观察背景漩涡、卡面闪光和 CRT 如何组合起来。

双击本目录 **Run.cmd** 启动 `../../bin/balatro_shader_lab.exe`。图文报告入口是 **[Report/index.html](Report/index.html)**，可直接用浏览器打开；其中区分了原游戏代码、移植适配与实验工具的设计。

## 运行与构建

已有可执行文件时，Run.cmd 会直接启动；缺少时会使用仓库现有 `build/` 配置，只构建 `balatro_shader_lab`。修改 C++ 后可运行：

```bat
Module\BalatroShaderLab\Run.cmd --build
```

启动器将 TEMP 和 TMP 指向项目内 `build/balatro-temp`，避免本机 LLVM 资源编译工具在受限系统临时目录中重命名文件失败。它不改系统环境变量。若手动构建，请在当前 PowerShell 会话中设置同样路径：

```powershell
New-Item -ItemType Directory -Force .\build\balatro-temp | Out-Null
$env:TEMP = (Resolve-Path .\build\balatro-temp).Path
$env:TMP = $env:TEMP
cmake --build build --target balatro_shader_lab --parallel 4
```

可执行文件使用编译时记录的模块目录读取素材；`--assets` 可以指定另一份完整模块目录。启动器按当前仓库的 `build/` 和 Debug `bin/` 布局查找程序。采用隔离 Release 预设时，请运行该预设自己的输出程序。

## 复用项目模块的方式

窗口由 `ApplicationWindow::Window` 创建，`Activate()` 使用现有窗口模块提供的 OpenGL 4.5 核心上下文；通过 `GetRenderContextAsOpengl()` 将上下文交给 Render 后端。GLSL 入口和版本声明由本模块的 LÖVE 适配器补齐，原始 `.fs` 文件保持提取字节不变。

场景采用同步的 `Render::OpenglBackend` 与 `Render::RHIResourcePool` 路径。资源创建、网格更新、纹理绑定、uniform、渲染目标切换和绘制都调用现有 Render API；背景、卡片与 CRT 经过 RHI 的离屏目标后得到预览纹理。它没有建立另一套场景 OpenGL 资源管理器，也没有为了本实验修改项目窗口或 Render 核心模块。

| 项目接口 | 本模块中的用途 |
|---|---|
| `ApplicationWindow/public/window.h` | `WindowSpec`、开窗、事件轮询、关闭和上下文回调 |
| `Render/Private/Backend/Opengl/backend.h` | `OpenglBackend` 同步执行资源与绘制操作 |
| `Render/Private/rhi_resource_pool.h` | 保存纹理、渲染目标、着色器、管线与顶点资源句柄 |
| `Render/Public/RHICommand/` | 背景、卡片、CRT 各次绘制的状态与命令类型 |
| `Render/Public/RHIResourceType/` | 图集纹理、网格、着色器程序和管线的资源描述 |

这里使用单线程同步调用后端，没有引入 `RHIDevice` 异步队列。ImGui 的 GLFW 与 OpenGL3 后端仅负责工具面板和预览纹理的显示；这些 UI 操作与截图诊断可以接触原生 OpenGL，场景本身经 Render 接口绘制。关闭时先清理实验器 RHI 资源与后端，再释放窗口上下文。

## 怎么观察效果

左侧选择场景，右侧是 GPU 实时预览。先看 Overview，再分别观察 Background、Card editions、Enhancements、Card layers、Hover tilt、Dissolve、Flame 和 CRT comparison；Single card 可放大单张卡，Other shaders 用来隔离观察其余原始着色器。

时间轴可以暂停、拖动、重置或改变速度。Phase 控制光泽相位，Dissolve 控制消散进度，Hover 控制鼠标透视幅度；Background 切换主菜单、普通盲注和 Boss 配色。原游戏的版本光泽也会受卡片转动与鼠标倾斜影响，实验室把相关参数拆开，便于看清各部分的作用。

卡片由纸底 center、点数花色 front、版本层、印章/贴纸与状态遮罩依次叠合。八种“增强牌”主要替换静态底图：玻璃跳过投影，石头跳过点数花色。Foil、Holographic、Polychrome 则重新读取原图再叠一层光泽；Negative 先画负片底，再只给 center 叠 negative_shine。金印章在原游戏使用 voucher 光泽；包内的 gold_seal.fs 作为独立算法仍可查看。

打开 **GLSL source / editor** 可阅读或编辑当前会话中的着色器。Compile edited buffer 尝试编译编辑内容；编译错误显示在窗口中，最近一次成功的程序保留。**Reload shaders / F5** 从磁盘恢复提取版本。编辑器不将修改写回原始资源。Save current PNG 保存当前 GPU 画面到 `Captures/current.png`。

## 19 份原始着色器

`Assets/shaders` 保留原始文件；LÖVE 的 `extern`、`number`、`Image`、`Texel` 与 `effect/position` 接口由 C++ 在内存中适配到 OpenGL。17 份文件提供片段算法，skew 和 vortex 为顶点算法。

| 文件 | 作用 |
|---|---|
| splash.fs | 主菜单旋涡与色彩混合 |
| background.fs | 关卡背景的旋转、扭曲与分色 |
| CRT.fs | 整屏曲面变形、扫描线、RGB 细节、噪声和辉光 |
| dissolve.fs | 普通卡面、投影、像素消散遮罩 |
| foil.fs | 蓝银色细纹闪箔 |
| holo.fs | 网格和细碎彩色镭射 |
| polychrome.fs | 宽幅流动彩虹色带 |
| negative.fs | HSL 深浅与色相反转 |
| negative_shine.fs | 负片上的冷色流动微光 |
| debuff.fs | 禁用卡片的红色 X 遮罩 |
| played.fs | 已打出卡片的灰置与透明度 |
| flame.fs | 计分火焰的程序花纹 |
| hologram.fs | Hologram 小丑浮层的青绿辉光和扫描错位 |
| gold_seal.fs | 包内独立金印章闪光算法 |
| booster.fs | 补充包与幻灵牌的包装光泽 |
| voucher.fs | 优惠券、印章与贴纸的光泽 |
| flash.fs | 画面闪光 |
| skew.fs | 鼠标驱动的顶点透视 |
| vortex.fs | 将卡片顶点卷向屏幕中心 |

## 检查与截图

从项目根目录运行：

```powershell
.\bin\balatro_shader_lab.exe --smoke-test
ctest --test-dir build -R balatro_shader_lab_smoke --output-on-failure
.\bin\balatro_shader_lab.exe --capture-dir .\Module\BalatroShaderLab\Report\images
```

`--smoke-test` 创建隐藏的 OpenGL 窗口，检查着色器编译、场景绘制、Framebuffer 和 GL 错误；它需要可用的图形驱动。编译通过与实际画面正确是两种检查，报告还提供实际 GPU 截图供核对。`--capture-dir` 按固定时间与参数导出报告画面；可附加 `--capture-frames N` 输出连续帧。截图流程不会启动或修改原游戏。

## 素材与证据

- `Assets/shaders/`：从指定本地程序提取的 GLSL。
- `Assets/textures/`：实验用原始卡片图集。
- `Evidence/`：提取清单、目标哈希、源码分析与映射；细节以清单和报告为准。
- `Private/main.cpp`：OpenGL 适配、渲染步骤、控制界面与截图入口。
- `Report/`：通俗技术报告、图集分层图和 GPU 效果图。

## 复现范围

复现范围是图形公式与主要渲染步骤；它不实现 Balatro 的完整玩法、卡片逻辑或 UI。时间、卡片种子、转动和弹跳相位被拆成可调参数，便于观察，未重建原 Lua 事件队列、完整粒子系统、音效或翻牌状态机。场景资源与绘制复用现有 Render 接口，实验器的布局和滑杆由本模块制作。

原游戏的代码与美术归其权利人所有，本模块用于本地分析和学习。图集技术图使用原 PNG 裁切，效果截图由实际运行的 shader 生成；不同纹理过滤、颜色空间、混合状态与驱动会影响最终像素，因此没有声称与原游戏逐像素一致。
