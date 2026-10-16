# Lumen 屏幕探针布置实验

这个专用场景用于从 UE 的 `UniformPlacement → AdaptivePlacementMark → AdaptivePlacementSpawn` 开始阅读源码，观察探针如何落到当前可见物体表面，以及粗网格覆盖不足时怎样补点。范围限于 **probe placement**：读取可见表面的深度与几何法线，计算覆盖权重，生成位置并绘制世界空间标记。尚未实现这些探针的半球方向辐射追踪、GI 积分或多次反弹。

探针位置由当前相机和屏幕采样决定。它们落在物体表面，但相机移动后会重新布置；`Space` 冻结位置后可绕行观察这一点。Render 原有 Surface Cache 的 surfels 是另一套世界三角面材质/光照缓存，本实验的蓝色、橙色点表示 Screen Probe Gather 的布置结果。

## 启动与操作

双击 [RunProbePlacement.cmd](RunProbePlacement.cmd)，默认使用 Vulkan、1280×800 窗口、MSAA1 和 90 FPS 上限。脚本优先选择 `output/Release/bin/material_lab_demo.exe`，否则使用 `bin/material_lab_demo.exe`，并把附加参数传给程序。

```text
output\Release\bin\material_lab_demo.exe --vulkan --probe-placement --procedural --render-width 1280 --render-height 800 --msaa 1 --fps 90
```

| 操作 | 按键 |
|---|---|
| 开关自适应补点 | F2 |
| 基础屏幕间距 16 / 32 / 64 像素 | F3 |
| 标记 / 候选 / 覆盖诊断；冻结时仅标记 / 候选 | F4 |
| 冻结 / 恢复探针世界位置更新 | Space |
| 固定 phase 0 / UE 八相位时间序列 | J |
| 预设视角 | 1 / 2 / 3 |
| 左右移动前景遮挡块 | 左 / 右方向键 |
| 重置 | R |
| 转向 / 移动 | 按住鼠标右键，WASD，Q/E 升降 |
| 帮助 / 退出 | F1 / Esc |

基础探针用蓝色小球表示，自适应探针用橙色小球表示，默认显示半径为 4 像素。小球与接收表面相切，接受真实深度遮挡；它们只用于显示，atlas 中保存的探针位置仍是原来的真实表面位置。候选视图用于观察每个基础 tile 内尝试的位置；覆盖视图显示粗探针及实际生成的自适应探针对表面的最终支持：蓝色表示基础探针已覆盖，橙色表示由自适应探针补足，红色表示仍不足。

默认固定 phase 0，便于稳定观察候选和去重结果。按 J 开启 UE 的八相位序列后，网格 jitter 和候选随相位变化；再次按 J 返回 phase 0。Space 保存探针世界坐标，随后相机仍可移动：固定相位只固定采样序列，冻结位置则锁住已布置的点。

冻结时 F4 只切换标记和候选。覆盖图的屏幕单元索引属于布置时的相机，绕行后不能把旧 atlas 当作新视角的覆盖结果；因此冻结模式不显示覆盖染色，解除冻结并更新布置后再查看覆盖。

场景包含平墙、地面、台阶、细柱、球体、斜板和可移动遮挡块。建议先用 32 或 64 像素基础间距观察，再开关 F2：

1. 看平墙上的基础探针及其覆盖，理解均匀布置。
2. 看细柱、台阶和遮挡块的轮廓，比较候选、被标记的覆盖缺口及实际补点。
3. 看球面与斜板，观察表面平面距离如何影响覆盖权重。
4. 冻结探针后移动相机，观察已保存的世界位置；解除冻结后重新观察当前视角的布置。
5. 移动遮挡块，观察新显露表面和深度断层对布置的影响。

F3 改变的是基础网格间距。自适应阶段通过候选补充探针，不采用 `16 → 8 → 4` 的逐层网格细分。

## 本机 UE 源码阅读顺序

已核对的源码为本机 **UE 5.7.1，Changelist 48512491**，根目录 `D:/UE5/engine/UE_5.7`，版本记录见 [Build.version](<D:/UE5/engine/UE_5.7/Engine/Build/Build.version:1>)。以下行号对应这份源码。

| 顺序 | 入口 | 观察内容 |
|---|---|---|
| 1 | [RenderLumenScreenProbeGather](<D:/UE5/engine/UE_5.7/Engine/Source/Runtime/Renderer/Private/Lumen/LumenScreenProbeGather.cpp:2183>) | 主流程、屏幕探针参数和图集资源 |
| 2 | [UniformPlacement 调度](<D:/UE5/engine/UE_5.7/Engine/Source/Runtime/Renderer/Private/Lumen/LumenScreenProbeGather.cpp:2306>)、[ScreenProbeDownsampleDepthUniformCS](<D:/UE5/engine/UE_5.7/Engine/Shaders/Private/Lumen/LumenScreenProbeGather.usf:119>) | 基础探针的屏幕位置、可见表面深度和法线 |
| 3 | [CalculateUniformUpsampleInterpolationWeights](<D:/UE5/engine/UE_5.7/Engine/Shaders/Private/Lumen/LumenScreenProbeGather.usf:142>) | 四个粗探针的双线性和平面距离权重 |
| 4 | [GetAdaptiveSampleCoord](<D:/UE5/engine/UE_5.7/Engine/Shaders/Private/Lumen/LumenScreenProbeGather.usf:1695>)、[GetScreenTileJitter](<D:/UE5/engine/UE_5.7/Engine/Shaders/Private/Lumen/LumenScreenProbeCommon.ush:72>) | PCG/Hammersley 候选及时间索引 |
| 5 | [ScreenProbeAdaptivePlacementMarkCS](<D:/UE5/engine/UE_5.7/Engine/Shaders/Private/Lumen/LumenScreenProbeGather.usf:1717>) | 仅标记覆盖权重不足的位置 |
| 6 | [ScreenProbeAdaptivePlacementSpawnCS](<D:/UE5/engine/UE_5.7/Engine/Shaders/Private/Lumen/LumenScreenProbeGather.usf:1798>)、[GetAdaptiveProbeInterpolationWeight](<D:/UE5/engine/UE_5.7/Engine/Shaders/Private/Lumen/LumenScreenProbeGather.usf:213>) | 较小候选索引的覆盖去重、分配和写出 |
| 7 | [UE 探针布置可视化](<D:/UE5/engine/UE_5.7/Engine/Shaders/Private/Lumen/LumenScreenProbeDebug.usf:49>) | 屏幕点、世界十字和法线的调试绘制 |

两个自适应 pass 的 C++ 调度在 [LumenScreenProbeGather.cpp:2345](<D:/UE5/engine/UE_5.7/Engine/Source/Runtime/Renderer/Private/Lumen/LumenScreenProbeGather.cpp:2345>)。UE 默认每个基础探针尝试 8 个候选，见 [NumAdaptiveProbes](<D:/UE5/engine/UE_5.7/Engine/Source/Runtime/Renderer/Private/Lumen/LumenScreenProbeGather.cpp:46>)；候选数的 shader 变体量化见 [GetNumSamplesPerUniformProbe2D](<D:/UE5/engine/UE_5.7/Engine/Source/Runtime/Renderer/Private/Lumen/LumenScreenProbeGather.cpp:547>)。

## 覆盖与补点规则

先从当前屏幕深度重建查询表面的位置 `P`、法线 `N` 和视空间深度 `z`。四个粗探针的位置为 `Q_i`，双线性系数为 `B_i`。普通不透明表面的默认权重可写成：

```text
relativePlaneDistance_i = abs(dot(Q_i - P, N)) / z
w_i = B_i * exp2(-200 * relativePlaneDistance_i)
```

深度无效的粗探针权重为 0。UE 的双线性权重还使用 1 像素的 `BilinearExpand`；`-200` 来自默认深度权重系数，植被另有参数。这里判断的是探针是否与查询位置所在表面相容，而不只是两个像素的深度是否接近。

每个基础 tile 的 8 个候选使用以下布局，`k` 为候选索引：

```text
seed = Rand3DPCG16(int3(tile, screenTemporalIndex)).xy
candidatePixel = tile * spacing + tileJitter
               + clamp(Hammersley16(k, 8, seed) * spacing, 0, spacing - 1)
```

UE 的 `tileJitter` 来自 `Hammersley16(screenTemporalIndex, 8, 0) * spacing`。读取候选像素的真实表面，再计算四个粗探针的覆盖；只有 `sum(w_i) < .01` 的有效候选才进入 placement mask。

Spawn 随后检查四个邻接粗单元中、**候选索引更小的已标记候选**，而非已经完成紧凑分配的 active 列表。邻居候选的权重为平面距离权重乘屏幕距离权重：

```text
adaptiveWeight = exp2(-200 * relativePlaneDistance)
               * (1 - saturate(min(abs(deltaPixel.x), abs(deltaPixel.y)) / spacing))
```

每个邻接单元取最大权重，再求四个单元的权重和；达到 `.01` 就跳过当前候选，避免在已有候选能覆盖的位置重复补点。剩余候选才生成探针。

F4 的覆盖视图按 UE 最终上采样的 corner 回退规则选探针，见 [CalculateUpsampleInterpolationWeights](<D:/UE5/engine/UE_5.7/Engine/Shaders/Private/Lumen/LumenScreenProbeGather.usf:237>)：先计算四个粗 corner 权重，只在某个 corner 的权重低于 `.01` 时，从该单元实际生成的自适应探针中择优替换该 corner，再统计最终支持。Mark 阶段则只看四个粗探针的权重和是否低于 `.01`，因此候选标记与最终覆盖是两个不同观察量。

## 复现范围与回归入口

本实验复现上述基础布置、8 候选、覆盖缺口标记及低索引去重规则，用现有 RHI 的 raster pass 和固定 atlas 槽位保存结果。UE 原实现使用 compute、原子计数、紧凑列表及预算截断：默认额外探针上限为基础数量的 `.5`，见 [图集与预算设置](<D:/UE5/engine/UE_5.7/Engine/Source/Runtime/Renderer/Private/Lumen/LumenScreenProbeGather.cpp:2238>)。本实验暂未复现这套全局紧凑分配和 `.5N` 上限，因此最终数量不保证与 UE 逐点一致。

世界标记和冻结用于学习 placement 的几何含义。后续可从这些位置继续接入半球方向采样、辐射追踪、过滤与积分；当前入口仅覆盖布置阶段，不消费原有 Surface Cache 光照。

```text
output\Release\bin\material_lab_demo.exe --vulkan --validation --probe-placement-test --procedural --msaa 1 --screenshot build\probe_placement.png
```

当前 Release/Debug 均已构建，最终 Vulkan validation 和 OpenGL 专用回归均通过，日志为 `build/probe_placement_ready_vulkan.log` 与 `build/probe_placement_ready_opengl.log`。独立 CPU 可见几何检查验证探针世界位置和法线；回归另覆盖 Mark/Spawn 归属、像素去重、平坦区域补点预算、phase 0/1 的确定性与真实几何、冻结后移动相机、解除冻结、移动几何、637×401 奇数尺寸以及 1280×800 展示输出。

| 640×400、固定 phase 0 测试项 | 仅基础探针 | 开启自适应 |
|---|---:|---:|
| 有效基础探针 | 178 | 178 |
| 粗覆盖不足的 marked 候选 | 9 | 9 |
| 实际生成的自适应探针 | 0 | 4 |
| 平墙 ROI 的基础 / 自适应探针 | 4 / 0 | 4 / 0 |
| 细柱 132 个 CPU 可见采样点的同物体半径覆盖 | 0 | 60 |
| 同接收物体的最近探针平均屏幕距离 | 15.3094 px | 13.4518 px |

同一 640×400 回归的 phase 1 得到 162 个有效基础探针、31 个 marked 候选和 11 个实际补点。最终 [1280×800 展示图](../../build/probe_placement_ready_overview.png) 使用 phase 0 和默认 4 像素小球，得到 684 个有效基础探针、28 个 marked 候选和 9 个实际补点，已进行视觉检查。

表中的细柱覆盖使用独立 CPU 可见性和 **16 像素同物体距离半径**，用于检查探针是否实际补到了细物体附近。这项几何参照不是 placement 算法的平面权重覆盖；F4 覆盖视图和 Mark 判据仍按前述权重规则计算。上述结果限定于此固定场景和视角，本实验尚无 GI 或性能结论。

现有基于深度/法线 tile 细分的 `--lumen-adaptive` 及 `ProbeDiffuse` 对照仍见 [原自适应屏幕探针说明](README.md#自适应屏幕探针)。本实验独立观察 UE 的候选布置过程。
