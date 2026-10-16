# 导入经典茶壶：近中远模型 LOD 与 Mesh / Global SDF

双击 [RunSdfBanquet.cmd](RunSdfBanquet.cmd) 现在进入后续的 [多级 Global SDF 场景](GLOBAL_SDF_CLIPMAPS.md)，默认显示相机跟随的四级真实距离场。本文保留 `--sdf-banquet` 的固定 Global 基线：使用 `Model` 模块加载真实 Utah Teapot OBJ，在三排桌面上放置 12 个共享模型实例，显示 Mesh SDF 三维表面并自动按距离选择精度；按 V 可查看普通模型、Mesh、Global 或左右分屏。

本场景把两种层级分开实现，再按同一个**已准备好的活跃等级**选择：Raster LOD 减少实际绘制三角形，Mesh SDF 质量提高或降低距离纹理采样密度。近处可观察壶嘴、壶盖和把手的细节，退远后检查三角形数量与活跃精度是否下降。

交互入口的 [1280×800 总览预览](../../build/sdf_banquet_overview.png) 显示实际距离纹理追踪出的 Mesh SDF 表面：前排蓝色 HIGH，中排青色 MEDIUM，后排暖色 LOW。默认总览视角准备完成后，活跃 LOW / MEDIUM / HIGH 为 `4 / 6 / 2`，茶壶 Raster 三角形为 `94224 / 188448`；图中表面由 MDF 三维显示 shader 生成。

## 启动与构建

脚本首先查找 `output/sdf-banquet/Debug/bin/material_lab_demo.exe`，没有时查找常规 `bin/material_lab_demo.exe`。它默认使用 Vulkan、1280×800、MSAA1、60 FPS，保留额外参数与退出码。

```text
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --sdf-banquet --render-width 1280 --render-height 800 --msaa 1 --fps 60
Module\MaterialLab\RunSdfBanquet.cmd --sdf-view split
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --sdf-banquet --sdf-quality high --sdf-view mesh
```

本机使用优化的 **Debug** 配置：C++ `/O2 /Ob2`，保留 Debug CRT，以匹配仓库现有 Assimp 5.4.3 Debug DLL。它不是 Release 构建。常规 Debug 配置在 `Model` 依赖可用时也支持该入口。

在仓库根目录重建已有配置：

```powershell
New-Item -ItemType Directory -Force -Path build/gi_detail_tmp | Out-Null
$env:TEMP = (Resolve-Path -LiteralPath build/gi_detail_tmp).Path
$env:TMP = $env:TEMP
cmake --build build/sdf-banquet --target material_lab_demo --parallel 4
```

首次配置可使用：

```powershell
cmake -S . -B build/sdf-banquet -G Ninja `
  '-DCMAKE_TOOLCHAIN_FILE=clang-msvc.cmake' `
  '-DCMAKE_BUILD_TYPE=Debug' `
  '-DCMAKE_C_FLAGS_DEBUG=/Zi /O2 /Ob2' `
  '-DCMAKE_CXX_FLAGS_DEBUG=/Zi /O2 /Ob2' `
  '-DOPENGLLEARN_STATIC_RUNTIME=OFF' `
  '-DOPENGLLEARN_LEGACY_OUTPUT_LAYOUT=OFF' `
  "-DOPENGLLEARN_OUTPUT_ROOT=$((Get-Location).Path)/output/sdf-banquet"
```

`Model` 的 [依赖说明](../Model/README.md) 列出兼容 Assimp 包、源码构建与本机 Debug DLL 的条件。没有可用的 `model_module` 时，CMake 不接入导入场景 helper，运行 `--sdf-banquet` 会明确报告缺少 Model / Assimp；不会改用程序茶壶或静默跳过模型。共享 Assimp DLL 由该可执行 target 复制到程序目录。

首次初始化仍同步准备 MEDIUM 48、Global 合并场与模型 GPU 资源，可能出现加载停顿。LOW 24 与 HIGH 128 在一个后台线程中预热；显示期间保留上一份可用 Mesh SDF。准备完成后，模型绘制和距离场按同一活跃等级切换。

## 操作

| 操作 | 按键 |
|---|---|
| 普通模型 / Mesh 三维 / Global 三维 / 左右分屏 | V |
| 开关自动距离选择 | L |
| 手动 LOW / MEDIUM / HIGH，同时关闭自动选择 | F12 |
| 开关实际模型 Raster LOD；关闭时 12 个茶壶都绘制 HIGH | T |
| 总览 / 近处茶壶 / 远处观察 | 1 / 2 / 3 |
| Mesh 三维只显示选中实例 / 显示全部 | O |
| 选择检查实例，循环 0–15 | F9 |
| 普通场景及 Global / Mesh 二维切片 | F8 |
| 显示体素网格 | B |
| Mesh / Global 追踪开关 | M / G |
| SDF 回退 / 探针实际命中点 | F10 / F11 |
| 屏幕追踪 / 辐射与状态 / 4×4 与 8×8 方向 | F5 / F6 / F7 |
| 选择探针 | 鼠标左键 |
| 冻结探针布置 | Space |
| 移动近排第二个茶壶 / 重置 | 左右方向键 / R |
| 相机移动及转向 | WASD、Q/E、鼠标右键；Shift 加速，滚轮调速 |
| 帮助 / 退出 | F1 / Esc |

O 只隔离 Mesh 三维视图；Global 保留完整合并场。F9 的 0–11 是茶壶，12–14 是三张支撑桌，15 是地板。普通场景中 HUD 统计 12 个茶壶的实际 Raster 三角形、LOW / MEDIUM / HIGH 活跃数量、后台准备状态与局部切档耗时。

## 真实模型与两种等级

资产来自 McGuire Computer Graphics Archive 的 CC0 Utah Teapot 修复版，保留来源、许可、原始文件与几何审计。见 [资产说明](../../Asset/Model/SdfBanquet/SOURCE.md) 和 [geometry-audit.json](../../Asset/Model/SdfBanquet/geometry-audit.json)。

| 活跃等级 | Raster OBJ 三角形 / 茶壶 | Mesh SDF 目标最长轴分辨率 | SDF 源几何 |
|---|---:|---:|---|
| LOW | 3926 | 24 | 原版 HIGH，15704 个三角形 |
| MEDIUM | 7852 | 48 | 原版 HIGH，15704 个三角形 |
| HIGH | 15704 | 128 | 原版 HIGH，15704 个三角形 |

LOW / MEDIUM Raster 文件分别由原版进行真实 QEM 边折叠简化；HIGH 保留原始 OBJ。未用线性细分增加三角形数量，也没有程序形状代替导入资产。三档实际模型均检查了边界、非流形边、方向和顶点连接，支持闭合有符号距离构建。

三档 Raster 都使用从 HIGH 边界得到的同一个双精度归一化变换：底部 Y=0，高度 2.2。原始累积节点变换只应用一次，先在 double 中变换，再转为小范围局部三角形。`ModelResource::AppendTo` 的绘制放置为 `worldPlacement * normalization`，使模型与距离场使用同一世界放置。

Mesh SDF 的三个分辨率**都从完整 HIGH 三角形烘焙**，不从 3926 / 7852 的 Raster 简化网格烘焙。这样远处的渲染简化不会污染高精度距离场，也能分别检查 Raster LOD 与 MDF 采样误差。距离查询使用三角形 BVH；12 个实例共享同一组三档不可变场，不各自烘焙。

茶壶虽闭合，壶身与壶嘴仍有很薄的壁。若壁厚小于一个体素，采样点可能都落在壁外；对真正 signed-distance 样本作三线性插值后，原零面会出现缺口，仅减小射线步长不能恢复缺失的负值。本场景显式启用薄面膨胀：局部偏移为 `.8660254 × voxelSize`，即立方体素的半对角长度。原始距离纹理仍保存真实有符号样本，`SurfaceBias()` 独立保存偏移；三维显示追踪 `sample - bias = 0` 的等值面，并在场中二分求交，未用原始三角形补画表面。Global 合并也一致使用这个扩展场。

该膨胀系数是本实验用于覆盖薄壳插值误差的选择，**不是 UE 默认参数**。它会使显示表面向外扩展，并可能填平很窄的缝隙；HIGH 128 的体素更小，膨胀的世界尺度也随之缩小。原几何和 Raster 模型保持不变，探针射线的局部 BVH 检查仍使用原三角形。其他既有场景的默认膨胀为零，严格 raw-zero 距离场测试继续保留。

开启 T 的 Raster LOD 时，每个茶壶使用其当前已准备 MDF 对应的 Raster 档；请求仍在后台准备时保留旧档，避免模型与场的活跃质量不同步。关闭 T 后仅模型绘制固定为 HIGH，MDF 自动精度仍可变化。HUD 的茶壶 Raster 数量不含桌面与地板；12 个全 HIGH 茶壶共 188448 个三角形。

## 自动选择、缓存与 Global 驻留

自动选择使用相机到茶壶**世界包围盒**的最短距离，不使用屏幕面积或到物体中心的距离。HIGH / MEDIUM 边界为 4.5，MEDIUM / LOW 边界为 10，迟滞宽度为 0.6：升级 HIGH 需小于 3.9，离开 HIGH 需大于 5.1；降为 LOW 需大于 10.6，离开 LOW 需小于 9.4。边界附近的小幅走动保留已有请求档，避免反复切换。

本实验的阈值与后台烘焙是研究场景的选择策略。UE 的三层 MDF mip、常驻粗层和异步细层流送不是这三套独立 Raster LOD，也不等同于这里的固定距离阈值。

Global SDF 固定使用完整 HIGH 源几何的 **MEDIUM 48** 距离场作为常驻输入，世界三角形精化结构也保留同一参考几何。只走动相机或切 Mesh MDF 时，更新局部图集与对象参数；Global 体素和参考 BVH 不因此重建。已准备的局部图集可复用 GPU 缓存，切回 resident 档避免重传相同 Mesh 纹理。

场景有 12 个茶壶和 4 个支撑几何，共 16 个 SDF 实例。Global 体积为 `193×45×193`，世界间距 `.125`，覆盖 X/Z `[-12,12]`、Y `[-.25,5.25]`；保留静态与动态两层，按 `4³` 砖块记录脏区。移动近排茶壶会清除旧范围并更新新范围，再与静态场合并；GPU 上传受影响的图集行。Mesh、Global 与参考几何有独立 generation，避免把一次局部质量变化当作世界重建。

Global 的固定世界采样间距不会因走近或选择 HIGH 128 而变细。右侧 Global 三维视图仍可能损失细壶嘴、把手和窄缝；它用于合并场与远处几何追踪，不能作为近处高精轮廓的替代。此入口没有滚动 clipmap、稀疏页面流送或完整 Lumen GI。屏幕 → Mesh → Global 的探针方向结果与不可读替代色沿用 [SDF 追踪说明](SDF_PROBE_TRACING.md)，尚未进行 GI 积分或去噪。

## 验证

CPU 验证入口：

```powershell
cmake --build build/sdf-banquet --target material_lab_sdf_model_scene_test probe_sdf_scene_test probe_sdf_lod_test --parallel 4
ctest --test-dir build/sdf-banquet -R '^(material_lab_sdf_model_scene_test|probe_sdf_scene_test|probe_sdf_lod_test)$' --output-on-failure
```

2026-10-14，更新后的三项 CPU 测试全部通过。导入 helper 检查大坐标双精度节点变换、镜像绕序、开放与非流形处理、三档真实茶壶闭合性、共享归一化及全部 16 个实例的覆盖范围。24 / 48 / 128 缓存测试检查共享烘焙、只改 local MDF 时 Global / Geometry generation 不变，以及移动后的局部 Global 更新与完整重建一致。薄面测试检查 raw 样本完全不变、偏移随体素大小缩放、默认偏移为零及缓存 seed 的 bias 匹配；另用 2 体素膨胀的独立样本距离，验证对象分块不能漏掉扩展窄带及移动后的旧、新区域。

GPU 验证入口：

```text
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --sdf-banquet-test --screenshot build\sdf_banquet_check.png
```

GPU 测试独立加载原版三角形作为参考，检查三维距离纹理命中位置、近远活跃档与实际 Raster 三角形数、缓存切回、相机迟滞，以及仅 Mesh 切档时 Global 保持驻留。Vulkan validation 与 OpenGL 4.5 两个后端均通过，日志分别为 [Vulkan](../../build/sdf_banquet_final_vulkan.log) 和 [OpenGL](../../build/sdf_banquet_final_opengl.log)。验证使用上述优化 Debug 配置，图像为 320×240；Vulkan 设备为 RTX 4060 Laptop。

下表使用同一固定集合的 75 条原版茶壶参考射线；三档均命中 75 条。误差比较原始三角形首次交点与**扩展距离场**的纹理命中，包含薄面偏移和插值误差，不是 raw signed-distance 样本自身的误差上界。

| Mesh MDF | 显式局部偏移 | Vulkan 平均世界射线距离误差 | OpenGL 平均误差 | 对照图 |
|---|---:|---:|---:|---|
| LOW 24 | .218720 | .254938 | .254938 | [LOW](../../build/sdf_banquet_final_Mesh_Low.png) |
| MEDIUM 48 | .0906886 | .110339 | .110339 | [MEDIUM](../../build/sdf_banquet_final_Mesh_Medium.png) |
| HIGH 128 | .0307292 | .0385347 | .0385348 | [HIGH](../../build/sdf_banquet_final_Mesh_High.png) |

HIGH 的 75 条参考射线都通过几何对齐门限；LOW/HIGH 的共同画面中有 9243 个像素的真实纹理命中距离发生变化，证明切档改变了采样表面。此固定场景的误差与通过率不能推广为任意模型的精度保证。

| 自动视角路径 | 活跃 LOW / MEDIUM / HIGH 茶壶 | 绘制茶壶三角形 | 对照图 |
|---|---|---:|---|
| 初次近处 | 5 / 5 / 2 | 90298 / 188448 全 HIGH | [近处 Mesh / Global](../../build/sdf_banquet_final_Near_Split.png) |
| 退到远处 | 10 / 2 / 0 | 54964 | [远处 Mesh / Global](../../build/sdf_banquet_final_Far_Split.png) |
| 从远处回到近处 | 6 / 4 / 2 | 86372 | [缓存返回](../../build/sdf_banquet_final_Near_Return.png) |

迟滞具有路径依赖，所以回到同一相机位置不要求每个档位数量都等于初次近处；当前结果恢复了两个 HIGH 茶壶，并减少边界重复切档。上述相机切换中 Global / Geometry generation 始终为 `1 / 1`，世界参考结构保持 188486 个三角形。切回缓存 MEDIUM 时 Mesh 上传和 Global 上传均为 0。

茶壶移动 `.35` 的回归只更新 `152320 / 1676205` 个 Global 体素。合并图集行后的实际 GPU 上传为 `484352 / 1676288` 个 texel，大于真实修改体素数，因为同一上传行也包含未改变的元素；两者都小于完整体积。见 [动态更新图](../../build/sdf_banquet_final_Moved_Split.png) 与 [总览分屏](../../build/sdf_banquet_final_Overview_Split.png)。

这些数据验证几何、驻留、局部更新与实际绘制数量。测试包含读回、后台准备和 validation，不是受控 FPS 测量；三角形或上传量减少不能直接换算为帧率提升。
