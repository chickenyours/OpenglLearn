# Screen Probe 第三阶段：Mesh SDF 与 Global SDF 回退

本实验接在 [HZB 屏幕追踪](SCREEN_PROBE_TRACING.md) 后：保持可靠的屏幕命中，对未命中或不能确认的射线进行近处 Mesh SDF 和远处 Global SDF 追踪。距离场由场景实际三角形构建；探针射线在近表面使用三角形 BVH 检查局部射线段，确认几何交点。命中结果保留来源和可读性，便于区分屏幕命中、Mesh 命中、Global 命中，以及几何命中后仍然无法读取光照的情况。

默认左右分屏显示 Mesh 与 Global 的三维距离场表面，直接从距离纹理追踪和计算法线，能看到采样精度及合并后的差别。低 / 中 / 高三档使用相同源三角形，仅改变 Mesh 距离体积的分辨率。按 V 可回到普通场景，观察屏幕追踪和距离场回退命中。

本阶段仍是**方向射线追踪与诊断**，尚未把方向结果积分为 GI，也没有多次反弹和 GI 去噪。距离场提供几何距离，不直接提供光照。当前辐射源仍为探针标记绘制前的本帧 HDR：距离场交点重新投影后，只有与当前可见表面吻合时才读取 HDR；屏外或被其他物体挡住的几何命中显示专用颜色。未引入 Surface Cache 的世界光照或天空替代值。

## 启动与操作

双击 [RunSdfProbeTracing.cmd](RunSdfProbeTracing.cmd)。脚本优先使用 Release 程序，默认 Vulkan、1280×800、MSAA1、90 FPS、MEDIUM 精度与三维分屏视图。默认每探针 4×4 方向，可用 F7 切为 8×8。脚本保留附加参数和程序退出码。

```text
output\Release\bin\material_lab_demo.exe --vulkan --sdf-probe-tracing --procedural --render-width 1280 --render-height 800 --msaa 1 --fps 90
```

交互入口的 [1280×800 三维分屏预览](../../build/sdf_quality_overview.png) 显示默认 MEDIUM Mesh（左）和 Global 合并结果（右），未开启体素网格；HUD 列出实际活跃精度和操作。

| 操作 | 按键 |
|---|---|
| 开关自适应布置 | F2 |
| 基础探针间距 16 / 32 / 64 像素 | F3 |
| 标记 / 候选 / 布置覆盖 | F4 |
| 开关屏幕追踪 | F5 |
| 方向辐射 / 命中来源及状态 | F6 |
| 每探针方向网格 4×4 / 8×8 | F7 |
| 回普通场景，循环无切片 / Global SDF 切片 / Mesh SDF 切片 | F8 |
| 切换 Mesh 切片对象 | F9 |
| 开关距离场回退 | F10 |
| 开关所选探针的实际射线命中点 | F11 |
| 手动 LOW / MEDIUM / HIGH；关闭自动精度 | F12 |
| 自动按距离选择精度 | L |
| 普通场景 / Mesh 三维 / Global 三维 / 左右分屏 | V |
| 三维表面体素网格 | B |
| Mesh 三维只显示选中物体 / 显示全部 | O |
| 独立开关 Mesh / Global 追踪 | M / G |
| 切片位置后退 / 前进 | `[` / `]` |
| 切片轴 | X / Y / Z |
| 选择活跃探针 | 鼠标左键 |
| 冻结 / 恢复探针布置 | Space |
| 固定布置 phase 0 / 八相位序列 | J |
| 预设视角 | 1 / 2 / 3 |
| 移动前景遮挡块 / 重置 | 左右方向键 / R |
| 转向 / 移动 | 鼠标右键，WASD，Q/E；Shift 加速，滚轮调速 |
| 帮助 / 退出 | F1 / Esc |

场景在原布置实验基础上增加 X=±5 的侧墙和 Y=5 的顶板，正面开放，相机可从外侧观察。新增几何只用于第三阶段入口；原布置和屏幕追踪入口保持各自场景。

普通场景的右下角方向网格显示所选探针的实际射线。先观察屏幕追踪，再开关 Mesh / Global，能看到原来离屏或深度不确定的方向转为世界几何命中。移动前景块时，Global 三维表面、切片和更新统计可用于观察旧位置清除、新位置写入与静态缓存复用。冻结只锁住探针布置，射线追踪继续使用当前相机和当前场景。

可直接指定初始精度与视图，例如：

```text
Module\MaterialLab\RunSdfProbeTracing.cmd --sdf-quality high --sdf-view split --sdf-voxel-grid
Module\MaterialLab\RunSdfProbeTracing.cmd --sdf-view mesh --sdf-isolate --sdf-object 2
Module\MaterialLab\RunSdfProbeTracing.cmd --sdf-auto --sdf-view scene
```

`--sdf-quality` 接受 `low|medium|high`，`--sdf-view` 接受 `scene|mesh|global|split`。`--sdf-object N` 使用场景对象索引；O 的单物体限制只影响 Mesh 显示，Global 始终包含完整场景的合并结果。

## 三维距离场与精度切换

Mesh 三维视图通过实例变换读取物体局部距离纹理；Global 三维视图读取合并后的世界距离纹理。两者都从当前相机发射视线，逐步追踪距离样本，并从距离梯度计算表面法线。它们不调用三角形求交，不使用 BVH 修正显示表面，也不把原始网格重新绘制后假装成距离场。

分屏左右使用相同相机位置与朝向，各用半幅画面的宽高比构建投影，分别显示 Mesh 和 Global。B 显示对应体素采样网格；O 与 F9 可单独检查某个 Mesh。Mesh 三维表面用暖橙 / 青 / 蓝表示 LOW / MEDIUM / HIGH 精度，Global 三维表面按最近物体区分颜色；这些颜色与普通场景 F6 的射线命中状态图例分开。M / G 只控制探针射线回退，三维体积显示独立读取所选距离场。F11 显示所选探针实际射线的命中点，属于另一项诊断，不能与三维视线的距离表面求交混为一项验证。

| 精度 | 最长轴目标分辨率 | 源几何 |
|---|---:|---|
| LOW | 24 | 相同三角形 |
| MEDIUM | 40 | 相同三角形 |
| HIGH | 64 | 相同三角形 |

尺寸包含构建时的边界扩张；每轴实际体素数由物体局部范围决定。提高精度减少 Mesh 采样误差，但 Global 间距仍为 `.125`，Global 显示受它自身的采样限制。该切换不改变渲染网格，不增加源三角形。

手动 F12 请求全场景同一档精度。请求待处理时继续显示完整旧场；后台准备全部需要的 Mesh 资产及完整 Global 合并后，再一次发布新场景，避免新旧精度混合成半更新画面。已在缓存中的资产不重复烘焙。请求改变后，过期任务完成只填充缓存，不能覆盖当前选择。

L 开启逐实例自动精度，距离为相机到实例世界包围盒的最近距离，近阈值 3、远阈值 7，滞回宽度 `.6`。例如 MEDIUM 进入 HIGH 需要距离小于 2.4，而 HIGH 退回 MEDIUM 需要大于 3.6；LOW 与 MEDIUM 在 6.4 / 7.6 间同样保留当前档，远距离跳变允许直接跨档。自动模式可使同一场景的不同实例使用不同精度。共享几何的同档资产复用同一不可变距离体积。

[probe_sdf_lod.h](../Render/Public/Pipeline/probe_sdf_lod.h) 中的 `ProbeSdfLodCache` 用一个后台工作线程按需构建 Mesh 精度资产，并合并重复的未完成请求。完整 Global 场景组合另在后台准备，缓存最多保留 24 个组合。Mesh 纹理按不可变资产列表复用，切回已上传组合可避免再次上传 Mesh atlas；Global 回切仍可能需要上传，且必须同步当前动态物体位置。主线程不在精度切换帧执行 Mesh 烘焙或完整 Global 合并。

首次启动仍同步构建 MEDIUM 资产和第一个完整 Global 场景，用它们提供可显示的初始距离场，因此初始化可能有加载停顿。完成初始资产后，后台主动预热 LOW 和 HIGH，其余请求复用已准备结果或排入后台队列。首次初始化、后台精度准备和缓存内切换是不同的成本，不能把缓存切换的表现当作首次加载耗时。

`MaterialLabModule::ProbeSdfQualityStatistics()` 提供后台请求、缓存命中、重复请求合并、已准备精度、实际活跃实例分布和场景待处理状态。`ReadbackProbeSdfVolume()` 返回独立三维视图的原始 RGBA：RGB 为距离表面显示，alpha 为视线命中世界距离，未命中为 0，用于独立验证几何位置。

## 状态与取色

原屏幕状态 0–8 保留，见 [屏幕状态表](SCREEN_PROBE_TRACING.md#射线与真实取色)。第三阶段增加下列状态；颜色用于诊断，不能当作返回辐射。

| 值 / 状态 | 含义 | 状态显示 |
|---|---|---|
| 1 屏幕命中 | HZB 命中并读取可见 HDR | 绿 |
| 9 Mesh 可读命中 | Mesh SDF 确认几何命中，重投影可读取 HDR | 蓝 |
| 10 Global 可读命中 | Global SDF 确认几何命中，重投影可读取 HDR | 青绿 |
| 11 Mesh 不可读命中 | Mesh 几何命中，但屏幕来源不可读 | 橙 |
| 12 Global 不可读命中 | Global 几何命中，但屏幕来源不可读 | 赭黄 |
| 13 SDF 未命中 | 有效距离范围内仍未确认几何命中 | 紫红 |
| 14 超出 Global 范围 | 有限 Global 体积不能覆盖所需追踪区间 | 红 |
| 15 SDF 步数耗尽 | 距离场追踪达到预算 | 紫 |
| 16 SDF 起点无效 | 不能得到有效世界起点或追踪来源 | 粉 |

辐射视图中，可读的屏幕、Mesh、Global 命中显示实际 HDR RGB；不可读与失败结果显示表中的替代色。真实 RGB 为零的可读命中仍然有效。原始屏幕结果与距离场回退结果分开处理，可靠屏幕命中不被距离场覆盖。

## Mesh SDF：三角形加速构建与实例复用

公共资产接口为 [probe_sdf_scene.h](../Render/Public/Pipeline/probe_sdf_scene.h) 中的 `ProbeMeshSdf`、`BakeProbeMeshSdf` 和 `ProbeSdfInstance`。每个 Mesh SDF 在物体局部空间构建，最近三角形距离通过三角形 BVH 查询，按点到节点包围盒的距离剪枝。闭合选项启用后先检查网格边的闭合性，再使用确定性射线奇偶规则判内外；射线碰到三角形边时增加另外两个方向投票。开放表面保留无符号距离。

资产保存实际采样点距离、局部范围、采样间距与插值误差界。相同网格的实例共享不可变资产，刚体移动只改变实例变换和世界几何加速结构，避免每帧重新烘焙网格。旋转和非均匀缩放可用，距离转换使用最小轴尺度；采样误差也参与安全步长约束。剪切变换需要额外的奇异值界，当前接口明确拒绝。

运行时射线先裁剪到物体体积，使用距离样本推进。接近潜在表面时，仅对相应局部射线段进行精确三角形 BVH 检查；BVH 不替代整个射线的距离场遍历。此检查用于保留薄表面，并排除距离采样或插值引起的假命中。

## Global SDF：合并与动态更新

`ProbeSdfScene` 管理有限世界体积：原点 `(-6,-.5,-6)`、间距 `.125`、尺寸 `97×49×97`，采样覆盖 X/Z `[-6,6]`、Y `[-.5,5.5]`。默认距离窄带截断为 `1`，按 `4×4×4` 体素块记录脏区，边缘块保留实际剩余尺寸。CPU 数据打包为宽 1024 的 RGBA32F 二维图集，以兼容现有纹理接口。

Global 样本对各实例变换后的 Mesh SDF 距离取最小值，分别缓存静态和可移动对象，再合并两层。静态对象不变时，移动物体只更新动态脏块。物体修改或删除会标记旧范围，新位置也会标记新范围，并按影响窄带和采样误差扩大，确保附近受影响的距离重新计算。

每个脏块重新计算全部当前动态对象的最小距离，再与静态缓存合并。这样既清除物体旧位置，也保留与其重叠的其他对象；不能只把新距离覆盖到原结果。受影响的图集行通过现有 `async_UpdateTexture` 子区域上传，避免整张 Global 图集重新上传。未发生变化的实例更新保留 generation，不执行体素重建。

统计包含资产数、实例数、烘焙体素及三角形查询量、Global 更新体素/块数、静态/动态更新量和耗时。更新量与整个体积的总量分开记录，便于核对动态更新是否局部化。具体帧耗时应实际测量，不能根据脏块比例直接推导 FPS。

## UE 5.7.1 源码对应

阅读本机 `D:/UE5/engine/UE_5.7`，UE 5.7.1、Changelist 48512491。下列行号对应这份源码。

| 源码入口 | 参考行为 |
|---|---|
| [Mesh 最近三角形点查询](<D:/UE5/engine/UE_5.7/Engine/Source/Developer/MeshUtilities/Private/MeshDistanceFieldUtilities.cpp:192>) | Embree `rtcPointQuery` 得到无符号最近距离 |
| [符号判定](<D:/UE5/engine/UE_5.7/Engine/Source/Developer/MeshUtilities/Private/MeshDistanceFieldUtilities.cpp:217>)、[符号阈值](<D:/UE5/engine/UE_5.7/Engine/Source/Developer/MeshUtilities/Private/MeshDistanceFieldUtilities.cpp:280>) | 采样射线首次命中的背面比例判内外 |
| [采样方向](<D:/UE5/engine/UE_5.7/Engine/Source/Developer/MeshUtilities/Private/MeshDistanceFieldUtilities.cpp:369>) | 当前启用点查询，两半球合计 98 条符号射线 |
| [Mesh 砖块和 mip 常量](<D:/UE5/engine/UE_5.7/Engine/Source/Runtime/Engine/Public/DistanceFieldAtlas.h:38>)、[稀疏砖块筛选](<D:/UE5/engine/UE_5.7/Engine/Source/Developer/MeshUtilities/Private/MeshDistanceFieldUtilities.cpp:512>) | 8³ 砖块、7³ 独立数据、3 mip、8 位量化、窄带稀疏资产 |
| [分辨率缩放](<D:/UE5/engine/UE_5.7/Engine/Source/Developer/MeshUtilities/Private/MeshDistanceFieldUtilities.cpp:385>)、[三 mip 构建](<D:/UE5/engine/UE_5.7/Engine/Source/Developer/MeshUtilities/Private/MeshDistanceFieldUtilities.cpp:432>)、[常驻与流送数据](<D:/UE5/engine/UE_5.7/Engine/Source/Developer/MeshUtilities/Private/MeshDistanceFieldUtilities.cpp:539>) | 默认体素密度乘资产 `DistanceFieldResolutionScale`；最高精度资产生成三 mip，最粗级始终加载 |
| [GPU mip 需求](<D:/UE5/engine/UE_5.7/Engine/Shaders/Private/DistanceFieldStreaming.usf:147>)、[请求范围](<D:/UE5/engine/UE_5.7/Engine/Source/Runtime/Renderer/Private/DistanceFieldStreaming.cpp:1242>)、[异步请求处理](<D:/UE5/engine/UE_5.7/Engine/Source/Runtime/Renderer/Private/DistanceFieldStreaming.cpp:791>) | 根据物体与相机附近 Global clipmap 范围的相交关系请求 1–3 个级别；共享资产取所有实例最高需求 |
| [运行时 mip 编号](<D:/UE5/engine/UE_5.7/Engine/Shaders/Private/DistanceFieldLightingShared.ush:123>)、[Global 合并查询](<D:/UE5/engine/UE_5.7/Engine/Shaders/Private/DistanceFieldLightingShared.ush:452>) | 反向编号 0 是最粗常驻级；Global 距离查询从粗到细，仅近表面读取更高精度 |
| [Mesh 世界距离查询](<D:/UE5/engine/UE_5.7/Engine/Shaders/Private/MeshDistanceFieldCommon.ush:9>) | 变换到局部体积，边界夹取与体积外距离处理 |
| [Mesh 合并](<D:/UE5/engine/UE_5.7/Engine/Shaders/Private/DistanceField/GlobalDistanceFieldCompositeObjects.usf:46>)、[静态父层合并](<D:/UE5/engine/UE_5.7/Engine/Shaders/Private/DistanceField/GlobalDistanceFieldCompositeObjects.usf:193>) | 距离最小值合并，复用静态层 |
| [移动前后包围盒](<D:/UE5/engine/UE_5.7/Engine/Source/Runtime/Renderer/Private/DistanceFieldObjectManagement.cpp:439>) | 记录新增、旧位置和新位置的修改范围 |
| [静态缓存](<D:/UE5/engine/UE_5.7/Engine/Source/Runtime/Renderer/Private/GlobalDistanceField.cpp:64>)、[脏区积累](<D:/UE5/engine/UE_5.7/Engine/Source/Runtime/Renderer/Private/GlobalDistanceField.cpp:1094>)、[局部更新](<D:/UE5/engine/UE_5.7/Engine/Source/Runtime/Renderer/Private/GlobalDistanceField.cpp:1143>) | 静态/动态分层，修改积累到 clipmap 调度 |
| [脏区影响半径](<D:/UE5/engine/UE_5.7/Engine/Shaders/Private/DistanceField/GlobalDistanceField.usf:271>) | 按影响半径筛选受修改包围盒影响的页面 |
| [相机页面对齐](<D:/UE5/engine/UE_5.7/Engine/Source/Runtime/Renderer/Private/GlobalDistanceField.cpp:1123>)、[滚动偏移](<D:/UE5/engine/UE_5.7/Engine/Source/Runtime/Renderer/Private/GlobalDistanceField.cpp:1266>) | 摄像机移动时环形存储，只更新进入体积的条带 |
| [Mesh SDF 追踪](<D:/UE5/engine/UE_5.7/Engine/Shaders/Private/Lumen/LumenSoftwareRayTracing.ush:124>)、[Mesh 光照读取](<D:/UE5/engine/UE_5.7/Engine/Shaders/Private/Lumen/LumenSoftwareRayTracing.ush:632>) | 局部体积步进、表面膨胀、Surface Cache 辐射 |
| [Global SDF 追踪](<D:/UE5/engine/UE_5.7/Engine/Shaders/Private/DistanceField/GlobalDistanceFieldUtils.ush:90>)、[Global 光照读取](<D:/UE5/engine/UE_5.7/Engine/Shaders/Private/Lumen/LumenSoftwareRayTracing.ush:649>) | clipmap/mip/page 步进，页面物体列表与 Mesh Cards 光照 |
| [近远分段](<D:/UE5/engine/UE_5.7/Engine/Shaders/Private/Lumen/LumenSoftwareRayTracing.ush:811>)、[近处调度](<D:/UE5/engine/UE_5.7/Engine/Source/Runtime/Renderer/Private/Lumen/LumenScreenProbeTracing.cpp:795>)、[剩余射线调度](<D:/UE5/engine/UE_5.7/Engine/Source/Runtime/Renderer/Private/Lumen/LumenScreenProbeTracing.cpp:848>) | 屏幕后 Mesh，剩余射线 Global |

### UE 的分辨率与 mip 驻留

`DistanceFieldResolutionScale` 是资产构建时的体素密度倍率。UE 为该资产生成三 mip，尺寸按最高级逐轴减半并向上取整，各级都重新查询源三角形。最粗级保存在 `AlwaysLoadedMip`，另外两级可异步流送。它们是同一 Mesh Distance Field 的采样层级，不是渲染网格 LOD，也不是相机移动时重新烘焙的三套资产。

GPU 请求的级数依赖物体包围盒与相机附近 Global clipmap 范围是否相交，多个实例共享同一资产时取最高需求。请求异步回读后，先安排中等精度，再安排最高精度；实际驻留还受砖块图集预算与 I/O 完成情况限制。代码中的异步延迟不等于显式距离滞回阈值。

Lumen Mesh 射线默认从**最高已驻留级**追踪，见 [LumenSoftwareRayTracing.ush:166](<D:/UE5/engine/UE_5.7/Engine/Shaders/Private/Lumen/LumenSoftwareRayTracing.ush:166>)；层级步进分支存在，但 `SDF_TRACING_TRAVERSE_MIPS` 默认设为 0。Global 合并查询则从最粗级开始，只有距表面小于该级编码最大距离的四分之一时继续读取更细一级。实验的低 / 中 / 高精度资产、自动精度选择或异步烘焙应与这些 UE 资产内 mip 明确区分。

## 复现范围与验证入口

此实现复现三角形距离构建、局部资产复用、Global 最小距离合并、静态缓存及动态脏区更新，以及屏幕 → Mesh → Global 的追踪顺序。UE 使用 Embree 点查询和多方向背面符号估计；本实验使用项目 BVH 和闭合网格奇偶符号，数值与内外判定不要求逐体素等同。

本实验为 CPU 构建和 CPU Global 合并、二维 float 图集、有限固定体积。已实现精度资产和场景组合缓存、后台准备及 GPU Mesh 纹理复用；这些不是 UE 的稀疏页面驻留或磁盘 mip 流送。未实现 UE 的稀疏 GPU 页面分配、多级滚动 clipmap、远处距离 mip 传播、8 位窄带量化、植被覆盖和抖动透明。当前相机移动不滚动 Global 体积，超出覆盖时会返回专用状态。

后续独立茶壶入口已增加相机跟随的 dense Global clipmap，见 [多级 Global SDF](GLOBAL_SDF_CLIPMAPS.md)。它保留本入口的固定体积作为基线；新增 CPU 环形体积与 GPU 局部上传也不等同于 UE 的稀疏页面系统。

UE 的运行时 SDF 追踪依赖距离场表面膨胀，不以原始三角形 BVH 精细求交。本实验探针射线的局部 BVH 检查是额外的薄表面保护，三维距离表面显示没有这一检查。UE 命中后从 Surface Cache / Mesh Cards 获取世界辐射；本实验读取可见本帧 HDR，所以世界几何命中可能依然不可读。上述差异均保留在结果状态中，不宣称为完整 UE Lumen 软件追踪。低 / 中 / 高独立资产、按包围盒距离的滞回选择，以及后台烘焙是实验设计补充，不声称与 UE 原始 mip 请求策略完全一致。

公共追踪设置位于 [probe_sdf_tracing.h](../Render/Public/Pipeline/probe_sdf_tracing.h)，由 `screenProbePlacement.trace.sdf` 使用。默认近处 Mesh 距离为 2，SDF 最大步数为 192，几何命中 epsilon 为 `.003`；Mesh / Global 可独立关闭，用于比较两阶段作用。

```text
output\Release\bin\material_lab_demo.exe --vulkan --validation --sdf-probe-trace-test --procedural --msaa 1 --screenshot build\sdf_probe_trace.png
```

精度与三维显示的回归入口为：

```text
output\Release\bin\material_lab_demo.exe --vulkan --validation --sdf-quality-test --procedural --msaa 1 --screenshot build\sdf_quality.png
```

2026-10-14，Mesh / Global 及精度缓存两项 CPU 回归通过。独立八面体几何参考使用相同 8 个三角形，LOW / MEDIUM / HIGH 平均距离误差分别为 `.00445383 / .0019912 / .00108103`，HIGH 比 LOW 减少约 75.7%；缓存复用、未完成请求去重和距离滞回检查通过。此结果是体积距离采样精度，不能当作所有场景的三维表面误差上限。

上一轮的程序场景完成了 Release 和 Debug 构建，原 SDF 射线回归与三维精度回归的 Vulkan validation、OpenGL 均通过。历史射线日志为 [sdf_quality_regression_vulkan.log](../../build/sdf_quality_regression_vulkan.log) 与 [sdf_quality_regression_opengl.log](../../build/sdf_quality_regression_opengl.log)，历史三维精度日志为 [sdf_quality_final_vulkan.log](../../build/sdf_quality_final_vulkan.log) 与 [sdf_quality_final_opengl.log](../../build/sdf_quality_final_opengl.log)。Vulkan 使用 RTX 4060 Laptop，OpenGL 日志报告 4.5 驱动；这里比较数值验证，不据此比较后端性能。更早的 `build/probe_sdf_ready_opengl.log` 记录的是窗口创建失败，不能改称成功。这些既有程序场景的构建记录不代表新导入模型入口已完成 Release 构建；其构建与验证见 [茶壶场景说明](SDF_MODEL_BANQUET.md)。

GPU 三维视图使用 RGBA32F 原始目标，误差从 alpha 中的实际纹理追踪世界距离计算，不靠表面颜色判断。球面视图为 320×240，以相同源球面三角形的独立首次求交作参考，每档检查相同的 150 个非掠射方向：

| Mesh 精度 | Vulkan 平均视线距离误差 | OpenGL 平均视线距离误差 |
|---|---:|---:|
| LOW 24 | .00300577 | .00300570 |
| MEDIUM 40 | .000893024 | .000892963 |
| HIGH 64 | .000304693 | .000304618 |

HIGH 相比 LOW 在这一球面测试中降低约 89.9% 误差。该指标与前述 CPU 八面体的体积距离采样误差分别计算；排除了轮廓掠射方向，不代表整个图像、其他物体或任意场景的误差保证。LOW / HIGH 共有命中像素中，有 10862 个像素的世界命中距离确实改变，确认切换改变了距离表面采样，而不只是更换颜色标签。

历史 640×400 分屏每三像素抽样得到 14338 对共同命中，Mesh / Global 平均距离差为 Vulkan `.0146633`、OpenGL `.0146634`。这组数据早于下述 Global 严格零面显示修正；最新同场景差值为 `.0193567`。两侧视线对齐；这项差值衡量两个采样体积的差异，不把 Mesh 视图当作 Global 的独立几何真值。

| 行为验证 | 结果 |
|---|---|
| 重复 LOW / HIGH / MEDIUM 缓存内切换 | 未增加 Mesh 烘焙或完整场景构建；Mesh GPU 上传量为 0，cache hit 为 1 |
| HIGH 后台准备时移动遮挡块至 offset `.35` | 完成后新场采用当前姿态；CPU / GPU Global 与另行完整重建结果一致 |
| 切回缓存 MEDIUM 时遮挡块 offset `-.25` | 同步当前姿态，旧缓存不恢复过期位置；CPU / GPU / 完整重建核对通过 |
| 自动精度：概览 → 近处 | 活跃 LOW / MEDIUM / HIGH 实例由 `2 / 10 / 0` 变为 `3 / 7 / 2` |
| 小幅相机扰动 | 精度分布稳定，未新增场景构建 |

保留上一轮 [Mesh / Global 分屏](../../build/sdf_quality_final_Split_Global.png)、[LOW 球面](../../build/sdf_quality_final_FieldLow.png)、[MEDIUM 球面](../../build/sdf_quality_final_FieldMid.png)、[HIGH 球面](../../build/sdf_quality_final_FieldHigh.png) 和 [自动精度场景](../../build/sdf_quality_final_Automatic.png)。这些是历史无 HUD 回归截图；分屏图为 640×400，球面图为 320×240，交互入口默认 1280×800。

验证分别检查 Mesh 最近距离与独立三角形参考、闭合内部符号、Global 最小值合并、移动后的旧位置清除与静态缓存复用、局部更新量、两阶段命中和可读性，以及关闭两个回退后保留原屏幕结果。三维显示另外通过 raw alpha 世界命中距离检查纹理表面精度，独立于探针射线的 BVH 精细求交。

### Global 三维显示的严格零面修正

2026-10-14，本轮修正 Global 三维显示对非均匀缩放和无符号薄面的命中判定。实例距离乘最小轴尺度后，可作为保守推进距离，但沿较长轴的场梯度可能远小于 1。原先只用 `abs(sample) <= .006` 判断命中，会把这个原始场值容差放大为明显的世界轮廓偏移；例如最小轴 `.035`、长轴 `1.15` 时，对应梯度约 `.0304`，`.006` 可能相当于约 `.197` 的世界误差。

现在 Global 三维视线在候选区间读取实际场梯度，要求 `abs(sample) / length(gradient) <= .0001`，或样本本身确实为零；有符号交叉在场中二分。无符号薄面的零面可能是尖点，因此小区间内搜索实际采样场的极小值，并使用单侧梯度判断。极小值仍为正、不能满足零面条件的射线标记为 unresolved，不补造表面。显示表面仍完全来自 Global 距离纹理，未用原始网格、解析几何、深度或 BVH 代替缺失的场零面。

严格判定去除了宽容差造成的轮廓膨胀；它不会恢复 `.125` 世界网格没有采到的薄面。既有 12 物体场景的薄柱厚度 `.07` 小于一个 Global 体素，部分视线仍没有可表示的真实零面，这是固定世界采样不足。提高局部 Mesh 到 HIGH 64、走近物体或减小步长，都不会改变 Global 的世界间距。本场景继续使用零表面偏移，与导入茶壶入口的显式实验膨胀分开验证。

最新 Vulkan validation 与 OpenGL 三维回归均通过，日志为 [sdf_banquet_legacy_quality_vulkan.log](../../build/sdf_banquet_legacy_quality_vulkan.log) 和 [sdf_banquet_legacy_quality_opengl.log](../../build/sdf_banquet_legacy_quality_opengl.log)。非均匀薄面检查验证 288 个实际场零面命中；薄柱参考视线中 36 条可由场表示，48 条仍是 unresolved 的正值极小点。640×400 分屏保持 14338 对共同命中，两后端 Mesh / Global 平均世界距离差均为 `.0193567`；球面三档误差、缓存切换和当前姿态同步检查继续通过。原探针射线的 Vulkan 回归也完整通过，见 [sdf_banquet_legacy_trace_vulkan.log](../../build/sdf_banquet_legacy_trace_vulkan.log)。这轮回归使用茶壶场景说明中的优化 Debug 程序，验证的是原程序场景及其零偏移距离场，没有改变旧场景为模型膨胀场。
