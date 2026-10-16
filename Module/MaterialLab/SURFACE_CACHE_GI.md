# Surface Cache 与基础软件追踪 GI

本阶段在独立的 Screen Trace / Mesh SDF / Global SDF 实验之后，加入相机无关的表面光照缓存和单次漫反射反弹。它是用于检查数据和光传输的基础版本，不是完整 UE Lumen。

## 开始前的性能判断

现有路径可以继续承载基础 GI 的开发和验证，但不能视为所有视图均达到稳定 60 FPS。RTX 4060 Laptop、1280×800、优化后的 Debug 构建，本次开始前重新测得：

| 路径 | GPU ms | 含提交和等待的整帧 ms |
| --- | ---: | ---: |
| 模型与探针追踪 | 6.19 | 16.34 |
| Global SDF 总览 | 8.35 | 12.70 |
| Global SDF 与 HUD | 8.12 | 22.31 |
| Mesh SDF 可视化 | 31.53 | 35.91 |
| 移动 Clipmap | 5.02 | 51.73 |

移动时 Clipmap CPU 更新约 10.32 ms。原始记录在 `build/surface_cache_preflight_perf.log`；每个静态项目 6 帧，移动 4 帧，不含呈现、限帧或图像回读。整帧时间受提交、系统调度和 HUD 更新影响明显，不能把 GPU 时间的倒数作为交互帧率。此前的对照记录见 `SDF_CLIPMAP_PERFORMANCE.md`。这里的“可以继续”表示数据结构和正确性验证的基础可用，并非完整 GI 性能验收。

## 参考的本机 UE 源码

参考工程：`D:/UE5/engine/UE_5.7`，UE 5.7.1。

- `Engine/Source/Runtime/Renderer/Private/Lumen/LumenSceneData.h`：`FLumenSurfaceMipMap`、`FLumenCard`、`FLumenPageTableEntry`，分离卡片投影、页分配和实际图集。
- `Engine/Shaders/Private/Lumen/SurfaceCache/LumenSurfaceCacheSampling.ush`：`SampleLumenCard` 的投影、深度有效性和朝向校验；`EvaluateRayHitFromSurfaceCache` 读取命中表面的缓存光照。缺失覆盖的调试视图使用独立颜色。
- `Engine/Shaders/Private/Lumen/LumenSoftwareRayTracing.ush`：`ConeTraceMeshSDFsAndInterpolateFromCards` 将 Mesh SDF 命中转换为 MeshCards 查询；`EvaluateGlobalDistanceFieldHit` 通过 Global SDF 页中的候选对象寻找卡片。
- `Engine/Shaders/Private/Lumen/LumenScreenProbeGather.usf`：`CalculateUpsampleInterpolationWeights` 的四角点重建与自适应探针替换。这里额外约束双向表面距离和法线，自适应支持范围采用紧支撑，避免借用其他表面的探针。
- `Engine/Shaders/Private/Lumen/LumenScreenProbeTracingCommon.ush`：二维方向单元抖动。本版针对单次漫反射直接采样余弦半球并保存 `E/PI`，没有照搬 UE 的完整球面方向表示、SH、重要性采样与过滤管线。
- `Engine/Source/Runtime/Renderer/Private/Lumen/LumenScreenProbeGather.cpp:149–238,1759–1950`：独立于 TAA 的 Final Gather 历史、最大累积帧数、深度/法线拒绝和快速更新参数。UE 的 `MaxFramesAccumulated`、`DistanceThreshold`、`NormalThreshold`、`MaxRayDirections` 默认值分别为 10、0.01、45°、8。
- `Engine/Shaders/Private/Lumen/LumenScreenProbeGatherTemporal.usf:374–670`：`ScreenProbeTemporalReprojectionCS`，重投影历史的四个候选像素，先验证表面再合并历史，根据累计帧数控制新样本权重；快速更新区域可额外限制历史。

此实现采用相同的职责拆分。UE 的自适应离线卡片生成、虚拟页分配与反馈、GPU 更新调度和多次反弹不在本版中。

## 数据与更新

每个对象保留六个正交方向的单层深度卡片，固定常驻页，默认每页 32×32。对象共享同一网格的局部捕获结果，切换相机或 MDF 精度不重新捕获。每张卡片保存逆变换、投影范围、页地址、表面误差容差、对象身份和材质。图集分为位置/有效性、法线/卡片身份、线性漫反射率三个平面；直接出射辐射率另存。

卡片捕获使用网格已有的三角形 BVH。世界 BVH 排序后的三角形到对象的映射用于精确选择卡片，避免只凭重叠包围盒借用另一个对象的光照。图集查询检查朝向、捕获有效性、深度和距离，只选一个符合条件的样本，不进行卡片光照平滑。

对象或材质变化更新对应卡片；光源变化重新计算直接光照。移动遮挡物也会使缓存光照失效，以更新其他物体上的阴影。静止时复用已经确定的直接光照，不把新旧帧混合。光照缓存的复用属于更新调度，不是时域滤波。

单层六方向捕获无法覆盖深腔、内壳和完全被外层遮挡的表面。未覆盖表面在 Coverage / Cache 视图中标为洋红色，参与 GI 时贡献零能量；有效黑色与缺失覆盖分开。后续可增加自适应卡片和多层捕获，不能靠放宽深度容差掩盖缺失。

## 光传输边界

默认基础 GI 复用之前研究的屏幕自适应表面探针：从 GBuffer 放置基础探针，对缺少几何覆盖的深度边缘、薄物体和曲面补充探针；光线由探针发射，像素从兼容的表面探针重建间接光。逐像素软件 BVH 路径保留为可切换的参考。

缓存存储 `L_direct = emission + rho * E_direct / PI`。探针用余弦半球 PDF 积分，存储 `E_indirect / PI = mean(L_hit)`，不提前乘探针落点的材质反射率。最终像素重建该入射光场，再乘自己的 `rho_receiver`。这样红墙上的探针不会把红墙反射率再次带到白色接收面。可读取黑色、离开场景的射线仍保留在总样本数中；缺失卡片不贡献能量，也不通过缩小分母补亮。

探针射线经过屏幕 HZB 追踪，再用 Mesh / Global SDF 补充屏外命中，并从 Surface Cache 读取单次反弹光照。探针 GI 不从上一帧最终 GI 画面中取光；屏幕命中也转换为缓存查询，避免将曝光、镜面或显示用颜色反弹到场景。

重建采用四个基础探针角点的屏幕权重、双向表面平面距离与法线权重；缺少兼容角点时在相应单元选择自适应探针。自适应样本使用局部屏幕支持范围，防止一根薄柱的探针扩散到整行墙面。这属于从探针到接收面的光场重建，不是对已经产生的 GI 图像做降噪。

默认启用下文的 GI 时域累积；可用 T 或 `--surface-cache-no-temporal` 关闭，恢复固定 seed 的独立单帧结果。放置相位仍固定为 0，时域模式只循环改变射线方向 seed。原始探针积分与像素重建结果仍单独保留，不从历史画面向场景反弹光照。天空、镜面反射、透明材质传输、多次反弹以及空间图像降噪仍不属于此单次漫反射版本。

## Temporal Accumulation

时域环节位于“自适应探针积分 → 像素重建”之后、“间接光与直射合成”之前，累积的是线性 HDR 间接漫反射辐射率。它与最终图像 TAA 完全分开；直射、UI、曝光后的显示结果不进入 GI 历史。

当前像素的世界位置投影到上一帧，检查相邻四个历史像素的世界位置误差、法线和有效性；只在同一表面上做双线性重投影。遮挡后重新露出的像素、越界、无效几何从当前帧重新开始。每像素保存有效累计帧数，达到上限之前采用逐步平均，达到上限后继续以 `1 / MaxFrames` 的权重加入当前样本，并非永久冻结结果。默认每 8 帧循环一次方向采样，给累积器提供不同的蒙特卡洛样本。

本版用相机重投影后的像素移动量判定快速更新区域，并将其有效历史缩短。可选的 3×3 几何兼容邻域限制只裁剪快速更新区域的旧历史，不用邻域平均替换当前像素，也不在静止区域施加该限制。这样保留静止探针在不同方向相位上的收敛能力。UE 还使用实际物体运动、命中光照运动比例、速度和更多材质信息；本版暂未加入物体速度缓冲，物体变换、材质或光源变化会保守地清除整幅 GI 历史，不宣称是完整 UE 去噪器。

UE 默认关闭 `RejectBasedOnNormal` 和 `FastUpdateModeUseNeighborhoodClamp`，45° 是可启用法线检查的角度默认值。本版为减少跨表面拖影始终检查法线，并默认开启仅作用于快速更新区域的历史裁剪；这两项与 UE 的默认开关有所区别。历史使用未经曝光的线性辐射率，显示曝光变化无需进行历史预曝光补偿。

相机切换、手动重置、开关 GI / 时域、切入卡片检查视图、分辨率、基础 seed、探针间距、射线数和时域参数变化都重建历史；正常相机移动通过逐像素检查复用兼容表面。总光、间接光与 History length 三个视图共享同一份历史。

按 F1 打开面板，在 GI 组调节下列参数：

| 参数 | 默认 / 面板范围 | 含义与调节建议 |
| --- | --- | --- |
| Temporal accumulation | 开启 | T 切换；关闭时查看固定 seed 的原始单帧误差，逐像素参考路径不使用此历史。 |
| Max accumulated frames | 10 / 1～64 | 越大静止噪声越少、响应越慢。1 等同当前帧；先用 8～16，稳定观察可用 24～32。 |
| Temporal direction phases | 8 / 1～64 | 独立射线方向相位的循环长度。1 重复同一批射线，无法靠时域减少其采样误差；通常设为 8～16，增大历史时可同步增大。 |
| History distance / view depth | 0.01 / 0.001～0.05 | 接收表面与历史位置的相对误差基准，另有 0.002 世界单位下限和有上限的像素足迹余量；表面法线方向的误差仍受基准约束。减小可减少跨几何拖影，但会拒绝更多历史、增加闪烁。 |
| History normal threshold | 45° / 5～90° | 可接受的最大法线夹角。薄物体或曲面串光可调到 15～30°；太严会使曲面频繁重新累积。 |
| Moving history frames | 2 / 1～最大累计帧数 | 重投影像素移动超过阈值时的历史上限。1～2 响应快；3～4 移动时更稳但可能增加拖尾。 |
| Motion threshold (pixels) | 1 / 0.1～32 | 将像素判为快速更新区域的屏幕位移。越小越容易缩短历史；单位随实际 GI 渲染分辨率变化。 |
| History neighborhood clamp | 开启 | 只在快速更新区域限制旧历史，减少陈旧亮度拖影；静止区域不裁剪。 |
| History clamp sigma | 3 / 1～8 | 兼容邻域统计区间的宽度。越小裁剪越强；太小会增加采样偏差，建议 2～4。 |
| Reset GI history | 按钮 / G | 清除旧历史，下帧从一帧开始；用于观察收敛过程。 |

V 切换到 History length：蓝色表示短历史，逐渐转绿，再到达到上限的红色；黑色为无效表面。新露出表面从一帧开始，静止表面逐渐达到上限。通过 T 开关比较光斑的时间稳定性；时域只能降低变化中的采样方差，不能修复 SDF / 卡片覆盖错误、几何串光或方向分布偏差。

```text
Module\MaterialLab\RunSurfaceCacheGi.cmd --surface-cache-view indirect --surface-cache-history-frames 16 --surface-cache-history-directions 16
Module\MaterialLab\RunSurfaceCacheGi.cmd --surface-cache-view history
Module\MaterialLab\RunSurfaceCacheGi.cmd --surface-cache-no-temporal --surface-cache-view indirect
```

## 探针射线与条带光斑

旧逐像素参考在 1 条射线时使用固定半径的确定性积分点，倾角恒为 45°，只改变方位角。这覆盖一个圆锥而非完整余弦半球，会形成与遮挡形状相关的亮暗条带；仅旋转 seed 或添加蓝噪声不能消除这种方向分布偏差。

探针路径用二维分层采样：默认 8×8 个角度单元，每个单元内部两个维度都独立抖动，再通过 `r=sqrt(u)`、`phi=2*PI*v` 映射到法线朝向的余弦半球。不同探针和不同采样单元具有独立散列扰动，避免相邻探针重复同一条固定圆锥。可切换 4×4 / 8×8 / 16×16，即每探针 16 / 64 / 256 条射线。

这修正方向分布，但不承诺无过滤的有限采样完全没有光斑：16 条射线仍可能把积分方差重建成较宽的亮暗块；几何采样不足、卡片缺失和 32×32 卡片的光照离散也可能产生残余变化。观察 `indirect` 视图，先提高每探针射线数，再减小探针间距，分别检查角度积分误差和空间重建误差；不要把真实的漫反射亮度变化一并抹平。

## 启动和观察

双击 `Module/MaterialLab/RunSurfaceCacheGi.cmd`，默认以 Vulkan、1280×800、间距 32 像素的自适应探针和每探针 64 条射线运行红绿墙测试房间，启用 10 帧 GI 时域累积与 8 个方向相位，限制 60 FPS。启动器显式关闭原生光追；60 是上限，不是性能承诺。主光沿敞开的房间正面照入，使用 CSM 计算可见表面的太阳直射阴影，卡片直接光照使用软件 BVH 遮挡。关闭天空和环境补光。房间、卡片与实际渲染使用相同的固定线性漫反射材质；茶壶的三个 raster LOD 在此模式均为相同灰色，不将精度调试色注入 GI。

| 操作 | 按键 |
| --- | --- |
| 总光 / 卡片光照 / 间接光 / 覆盖 / 图集 / 累计帧数 | V，或调节面板的 GI inspection |
| 时域累积开关 / 清除历史 | T / G |
| 开关基础 GI | F5 |
| 16 / 64 / 256 条射线 / 探针 | F7；逐像素参考模式另调 1 / 4 / 8 / 16 |
| 自适应探针 / 逐像素参考 | F6，或面板 Gather from surface probes |
| 自适应补充 / 间距 16、32、64 像素 | F2 / F3 |
| 探针与追踪命中叠加显示 | P |
| 更换独立射线 seed / 冻结探针位置 | J / Space |
| 预设视角 / 重置场景 | 1 / 2 / 3，R |
| 移动物体 | 左右方向键 |
| 相机移动 / 转向 | WASD、Q/E、按住右键转向，Shift 加速 |
| 调节面板 / 退出 | F1 / Esc |

```text
Module\MaterialLab\RunSurfaceCacheGi.cmd --surface-cache-view indirect --surface-cache-probe-rays 16 --surface-cache-probe-spacing 16
Module\MaterialLab\RunSurfaceCacheGi.cmd --surface-cache-reference --surface-cache-view indirect --surface-cache-rays 16
Module\MaterialLab\RunSurfaceCacheGi.cmd --sdf-clipmaps
```

第三条复用 12 个导入茶壶、三张桌子和四级 Clipmap。它需要启用 Model / Assimp 的宴会构建。Surface Cache 不随相机或茶壶 raster/MDF 档位重捕获；移动选中的近处茶壶只更新其卡片，再使直接光照失效以重新计算遮挡。`--surface-cache-probe-rays` 的参数是方阵边长，不是总射线数。

## 验证入口

```text
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --surface-cache-test --screenshot build\surface_cache_quality.png
output\sdf-banquet\Debug\bin\material_lab_demo.exe --surface-cache-test
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --surface-cache-probe-test --screenshot build\surface_cache_probes.png
output\sdf-banquet\Debug\bin\material_lab_demo.exe --surface-cache-probe-test
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --surface-cache-temporal-test --screenshot build\surface_cache_temporal.png
output\sdf-banquet\Debug\bin\material_lab_demo.exe --surface-cache-temporal-test
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --surface-cache-performance-test --render-width 1280 --render-height 800 --msaa 1
```

原有卡片与探针质量入口显式关闭 GI 时域，保持原来的独立单帧基线。质量入口使用独立小房间，检查无光全黑且黑色卡片仍有效、关闭屏幕追踪后的缓存反弹、光强加倍时原始 HDR 反弹加倍、A→B→A 相机与光源变化后结果一致、静态帧不更新卡片光照、移动单物体只更新六张卡片，以及故意提供不相关卡片时显示洋红缺失覆盖但不注入 GI 能量。卡片太阳光另以独立双精度逐三角形遮挡测试和 `rho * sunlight * cosine / PI` 核对，覆盖受光、被遮挡和屏外地板样本，不复用 GPU BVH 或卡片查询。

分阶段追踪分别运行 Mesh-only 与 Global-only 两组：先关闭 Surface Cache 记录原本不可读取的几何命中，再开启缓存，要求原来的 11 / 12 状态变为 9 / 10，保留相同世界命中位置，且源像素坐标为 -1（从卡片读取）。测试还要求实际屏外命中得到补充，并保持未命中、覆盖缺失、步数耗尽和非法起点等状态的分类。

探针质量入口读取实际 GPU 射线方向，以独立解析积分核对余弦分布的均值、二阶与四阶矩、球冠概率和切向对称性；固定 45° 圆锥会直接失败。还核对 `E/PI` 与实际射线辐射率的积分、已知黑色分母、自适应探针对最终图像的实际贡献、光强线性、固定 seed 的 A→B→A 无历史依赖、角度分辨率与空间间距对应的射线调度，以及缓存视图不追踪无用 GI 射线。

对条带与宽光斑另做量化收敛检查：固定相机、表面探针位置和光照，用 8 个独立 seed、每探针 256 条射线的结果在测试端形成参考，再用另外 4 个 seed 分别测量 16 / 64 / 256 条射线。检测平面上的探针入射光误差、最终 HDR 像素亮度误差，以及分表面的 16×16 区块平均误差，要求随着射线增加而下降。区块统计只用于离线误差分析，不写回渲染器；参考消除了真实几何亮度渐变对“光斑”的混淆，但无法证明所有档位共同存在的 SDF、卡片或重建偏差已经消除。

时域质量入口读取实际 raw GI、累积 GI 和每像素累计帧数，检查方向相位逐帧改变、静止方差降低、帧数上限、单帧模式、相机切换与重新露出表面的拒绝、正常平移的历史复用，以及光照、几何、参数、分辨率和模式切换后的历史失效。比较在最终曝光和色调映射之前进行，主对照检查关闭最终图像 TAA；另有两帧显式打开 TAA，验证其相机抖动不会清除独立的 GI 历史。

首次 Vulkan 回归（256×192 小房间、每探针 16 条射线、8 帧历史 / 8 个方向相位）中，48,456 个稳定接收像素的逐帧 RGB 方差由 `0.000361598` 降为 `0.000004925`，下降约 98.64%。这是同一表面上随方向相位变化的时间方差，不代表相对于真实光传输的绝对误差下降同样比例。相机平移 0.12 世界单位后，47,617 个像素复用了通过几何检查的历史，817 个像素从一帧重新开始。原有关闭时域的探针 / 卡片质量测试继续作为单帧对照。

Vulkan 与 OpenGL 的完整时域回归均通过，稳定区域的平均能量相对同批原始方向相位均值偏差约 0.09%。测试另覆盖聚光灯位置与方向改变、接收面材质改变时清空历史，以及 TAA 相机抖动下独立 GI 历史的复用；关闭时域后的累计帧数视图不调度无用探针射线。记录见 `build/surface_cache_temporal_vulkan.log`、`build/surface_cache_temporal_opengl.log`，单帧回归见 `build/surface_cache_temporal_probe_regression.log`、`build/surface_cache_temporal_reference_regression.log`。

同一优化 Debug 构建、RTX 4060 Laptop、1280×800、Vulkan validation、显式软件追踪，间距 32 像素、每探针 64 条射线，各 12 个无呈现/不限帧/无图像回读的计时帧：

| 场景 | 关闭时域 GPU / 整帧 ms | 开启时域 GPU / 整帧 ms |
| --- | ---: | ---: |
| 红绿墙房间 | 15.52 / 20.74 | 17.49 / 23.52 |
| 茶壶宴会 + Clipmap | 30.22 / 38.16 | 31.23 / 40.24 |

这是整条管线的开关对照，时域模式也会更换射线相位，差值不是完全隔离的时域着色器耗时。新增四个全屏 pass，不增加每帧探针射线预算。历史采用双缓冲颜色/计数、世界位置、法线/深度及一个可读覆盖输出，共七张 RGBA32F 纹理；1280×800 时约 109 MiB，调整最大累计帧数不会按帧数增加纹理数量。整帧结果仍未达到稳定 60 FPS，物体动态更新仍有现有 CPU 几何/SDF 更新成本。性能记录为 `build/surface_cache_temporal_perf_room.log`、`build/surface_cache_temporal_perf_banquet.log`，默认模式截图为 `build/surface_cache_temporal_{room,banquet}_final.png`。

性能入口预热后分别测量无 GI、16 / 64 / 256 条探针射线、64 条射线额外启用时域、较密的 16 像素探针间距、逐像素参考的 1 / 4 / 8 条射线、缓存视图与动态对象更新。每个静态项目测量 12 帧，批次内不呈现、不限帧、不回读图像；GPU 时间和包含提交等待的整帧时间分别记录。加上 `--sdf-clipmaps` 可测量茶壶宴会，不能把房间结果代替高精度模型负载。`surfaceCacheRays` 和 `surfaceCacheProbeRayBudget` 是含非活动预留槽的配置上限；实际有效探针与射线数需通过诊断回读单独统计。

## 自适应探针的光斑收敛记录

`build/surface_cache_probe_vulkan.log` 的 320×240 房间测试中，101 个有效探针包含 21 个自适应探针，共发射 6,464 条有效射线（默认每探针 64 条）。实际 GPU 方向的 `E[cos(theta)]=0.66718`、`E[cos²(theta)]=0.500166`、`E[cos⁴(theta)]=0.333363`，分别符合余弦 PDF 的 `2/3`、`1/2`、`1/3`；不存在所有射线固定在 45° 的圆锥。积分最大误差为 `1.24e-7`。保持缓存入射光不变、将接收地板反射率减半后，26,618 个地板像素的间接光恰好减半，探针入射积分保持不变。

固定位置，针对 69 个平面探针、58,788 个墙壁/天花板/地板像素和 287 个分表面的 16×16 区块，以独立 seed 的离线高采样参考测得：

| 每探针射线 | 探针 RGB 均方误差 | 平面 HDR 亮度均方误差 | 区块平均亮度均方误差 |
| --- | ---: | ---: | ---: |
| 16 | 6.82550e-4 | 1.27535e-4 | 1.27382e-4 |
| 64 | 1.26987e-4 | 2.79871e-5 | 2.70075e-5 |
| 256 | 1.99783e-5 | 3.69185e-6 | 3.82548e-6 |

在该场景与参考下，64 条射线相对 16 条的宽区块误差下降约 79%，256 条相对 64 条再下降约 86%。这是多个 seed 的统计收敛结果，不表示每一个 seed、每一个像素都会单调改善，也不证明卡片分辨率和几何覆盖的共同误差已经消失。测试端形成的参考和区块统计不参与交互渲染，未加入时空过滤。

Vulkan（RTX 4060 Laptop，validation、显式软件追踪）和 OpenGL（Intel）均通过以上探针回归，包括冻结位置后改变相机时强制重建屏幕格子。逐像素参考、旧 Mesh / Global SDF 追踪与三项 CPU 回归也通过。记录见 `build/surface_cache_probe_{vulkan,opengl,reference_regression,legacy_sdf}.log`。

## 当前探针版本性能

同一优化 Debug 构建、RTX 4060 Laptop、1280×800、Vulkan validation、显式软件追踪；静态批次各 12 帧，未呈现、限帧或回读图像：

| 路径 | 房间 GPU / 整帧 ms | 茶壶宴会 GPU / 整帧 ms |
| --- | ---: | ---: |
| 直接光，无探针和 GI | 1.34 / 4.26 | 3.41 / 7.58 |
| 16 射线/探针，间距 32 | 10.97 / 16.72 | 11.29 / 18.07 |
| 默认 64 射线/探针，间距 32 | 16.05 / 21.28 | 25.59 / 32.61 |
| 256 射线/探针，间距 32 | 40.65 / 46.20 | 82.66 / 89.35 |
| 64 射线/探针，间距 16 | 42.70 / 48.42 | 64.77 / 71.87 |
| 逐像素参考，1 射线 | 12.75 / 17.51 | 19.36 / 25.98 |
| 逐像素参考，4 射线 | 40.85 / 45.46 | 63.84 / 70.62 |
| 逐像素参考，8 射线 | 74.38 / 78.92 | 118.99 / 125.36 |
| 卡片光照查看，无 GI 射线 | 2.29 / 6.62 | 3.47 / 9.59 |

默认探针配置的 GPU 耗时约为 8 射线逐像素参考的 22%，但两者是不同的空间/角向近似，不能称作等质量加速；默认探针结果也仍有有限采样方差。整帧结果说明当前默认房间与茶壶宴会尚未达到稳定 60 FPS。当前四 texel 射线诊断布局会重复执行求交，固定候选槽也保留了无效容量，后续可通过紧凑调度和多输出/计算着色器降低开销。

动态单帧（64 射线/探针）房间为 17.68 / 99.57 ms，宴会为 27.21 / 305.00 ms，包含几何、SDF、上传、卡片重打光；卡片 CPU 更新分别为 0.14 / 3.70 ms，不代表稳定移动平均。静态帧卡片更新计数均为零。完整记录见 `build/surface_cache_probe_perf_room.log` 与 `build/surface_cache_probe_perf_banquet.log`，截图为 `build/surface_cache_probes_{room,banquet}_final.png`。

## 先前逐像素参考版本的验证与性能记录

五项 CPU 回归通过。Vulkan（显式软件追踪、validation）和 OpenGL 的完整新 GI 测试通过：72 张卡片、46,824 个有效捕获样本；32 个受光、32 个阴影、28 个离屏地板参考样本的最大直射光误差为 `1.52e-8`。原始 GI 光强翻倍误差和 A→B→A 历史依赖误差均为零。Mesh-only 与 Global-only 各有 241 条原本不可读取的命中从卡片读到光照，合计 372 次位于屏幕外；世界命中位置保持不变。有效黑色、缺失覆盖、动态更新也通过。

日志：`build/surface_cache_cpu_regressions.log`、`build/surface_cache_quality_vulkan.log`、`build/surface_cache_quality_opengl.log`。OpenGL 在本机 Intel 显卡上验证，性能表仅使用 NVIDIA Vulkan 测量。旧 SDF 软件补追回归与 PBR GPU 回归也通过，日志为 `build/surface_cache_legacy_sdf_vulkan.log`、`build/surface_cache_pbr_regression.log`。

同一优化 Debug 构建（`/O2 /Ob2`，Debug CRT）、RTX 4060 Laptop、1280×800、Vulkan validation、禁用原生光追：

| 路径 | 房间 GPU / 整帧 ms | 茶壶宴会 GPU / 整帧 ms |
| --- | ---: | ---: |
| 直接光，无 GI | 1.03 / 5.55 | 2.26 / 11.27 |
| 原始 GI，1 条射线/像素 | 15.59 / 22.38 | 17.49 / 34.42 |
| 原始 GI，4 条射线/像素 | 33.76 / 44.24 | 60.23 / 77.08 |
| 原始 GI，8 条射线/像素 | 65.02 / 75.06 | 117.99 / 132.80 |
| 卡片光照查看，无 GI 射线 | 1.82 / 10.34 | 2.68 / 16.97 |
| 动态更新一次，8 条射线/像素 | 64.71 / 137.71 | 109.32 / 301.85 |

日志：`build/surface_cache_perf_room_final.log`、`build/surface_cache_perf_banquet_final.log`。静态时卡片光照更新数均为 0；房间/宴会常驻卡片容量分别为 73,728 / 98,304 texels。动态项只有一帧，包含几何 refit、上传、卡片更新、全部卡片重打光和 8 条射线的积分，不代表稳定移动平均；单独卡片 CPU 更新约 1.45 / 4.27 ms。计数器 `surfaceCacheRays` 是视口像素数乘采样数的配置上限，背景像素会在着色器中提前退出。

这些先前数值说明数据结构与离屏读取已经可用，缓存查看成本较轻，但逐像素软件追踪参考路径没有达到完整实时 GI 的性能目标。当时默认采用 1 条射线方便交互；4/8 条用于质量检查。当前默认已将昂贵的光照射线调度到屏幕探针，当前性能见上节，不能沿用这份旧表。本阶段未用降低 SDF/模型精度或时空滤波掩盖此开销。

首版卡片直接光支持方向光、点光和聚光灯。天空漫反射与矩形灯尚未接入缓存，开启这些不支持的组合会给出明确校验错误。主画面点光的直射仍继承现有 PBR 路径，独立阴影尚未接入；默认太阳场景使用 CSM，缓存中的各类支持光源则使用软件阴影射线。六向单层卡片、固定页和完整 BVH refit 也是后续优化项，不等同于 UE 的虚拟分页与调度实现。
