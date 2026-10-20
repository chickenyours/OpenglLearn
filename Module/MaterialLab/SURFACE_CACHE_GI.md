# Surface Cache 与基础软件追踪 GI

本阶段在独立的 Screen Trace / Mesh SDF / Global SDF 实验之后，加入相机无关的表面光照缓存和单次漫反射反弹。当前默认采用每子格两个表面代表、按实际探针位置计算的 B2 连续核、像素到探针的屏幕深度可见性检查，以及受限的一阶方向重建。它是用于检查数据和光传输的基础版本，不是完整 UE Lumen。文中带日期的性能与质量表保留对应旧版本的记录，不能代替当前版本的验收。

## 历史：加入 Surface Cache 前的性能判断

当时判断现有路径可以继续承载基础 GI 的开发和验证，但不能视为所有视图均达到稳定 60 FPS。RTX 4060 Laptop、1280×800、优化后的 Debug 构建，在加入本阶段之前测得：

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
- `Engine/Shaders/Private/Lumen/LumenScreenProbeGather.usf`：`CalculateUpsampleInterpolationWeights` 的几何兼容重建与自适应贡献，以及按接收面法线求值的方向积分。这里的实际位置 B2 核、每子格双表面代表和三点屏幕深度检查是本实验的适配，不是该函数的逐行移植。
- `Engine/Shaders/Private/Lumen/LumenScreenProbeTracingCommon.ush`：二维方向单元抖动。本版仍直接采样余弦半球，并在原 `E/PI` 之外拟合受限的一阶方向系数，没有照搬 UE 的完整球面方向表示、SH、重要性采样与过滤管线。
- `Engine/Shaders/Private/Lumen/LumenScreenProbeGather.usf::EvaluateDiffuse`、`LumenScreenSpaceBentNormal.ush`：区分探针方向光照与逐像素局部遮挡的职责。当前三点深度检查仅约束像素向探针借用光照的可见性，不等同于 UE 的 Short Range AO / GI、Bent Normal 或完整短程软件追踪。
- `Engine/Source/Runtime/Renderer/Private/Lumen/LumenScreenProbeGather.cpp:149–238,1759–1950`：独立于 TAA 的 Final Gather 历史、最大累积帧数、深度/法线拒绝和快速更新参数。UE 的 `MaxFramesAccumulated`、`DistanceThreshold`、`NormalThreshold`、`MaxRayDirections` 默认值分别为 10、0.01、45°、8。
- `Engine/Shaders/Private/Lumen/LumenScreenProbeGatherTemporal.usf:374–670`：`ScreenProbeTemporalReprojectionCS`，重投影历史的四个候选像素，先验证表面再合并历史，根据累计帧数控制新样本权重；快速更新区域可额外限制历史。
- `Engine/Source/Runtime/Renderer/Private/Lumen/LumenScreenProbeFiltering.cpp:18–45`：`SpatialFilterNumPasses=3`、`SpatialFilterHalfKernelSize=1`、`SpatialFilterMaxRadianceHitAngle=10` 度、`SpatialFilterPositionWeightScale=1000`。`Engine/Shaders/Private/Lumen/LumenScreenProbeFiltering.usf:84–105` 将抖动追踪散射到共同方向的 gather 单元；`292–454` 的 `GetFilterPositionWeight`、`GatherNeighborRadiance`、`ScreenProbeFilterGatherTracesCS` 在探针方向辐射率上过滤，通过相对深度、近处距离限制和共同锥方向的命中角度保护接触阴影。

此实现采用相同的职责拆分。UE 的自适应离线卡片生成、虚拟页分配与反馈、GPU 更新调度和多次反弹不在本版中。

## 2026-10-19：近距静止椭球的探针采样收敛

正确交互入口是 [RunSealedGiRoom.cmd](RunSealedGiRoom.cmd)，以 `--surface-cache-sealed-room` 启动方形顶灯 / 陶瓷椭球场景，默认渲染 **1280×800**。按F1，在GI时域面板切换 `Static probe convergence (64 frames)` 对比。`RunGiRoom.cmd` 的 `--gi-room` 是旧点灯房间 / lumenGi路径，不是本节场景；文中2560×1440数据是显式指定尺寸的高分辨率专项，不代表本场景默认启动尺寸。

上一轮的曲面权重与补追交接约束解决的是照明复用和颜色连续性；近距离静止观察仍可能看见每个探针的采样残差。旧路径默认每探针64条射线，probe seed按 `temporalSequence % directionPhases` 推进，默认8帧后重复同一组方向；时域默认最多累积10帧。反复平均固定八组测量，不能继续消除它们共同留下的探针相关 Monte Carlo 误差。单独延长历史也不能消除不同法线半球间近邻角度重采样产生的固定偏差，因此本轮结合长方向序列、64帧静止历史与匹配角度单元的空间复用，保留后文曲面 / 色彩修正。

`SurfaceCacheTemporalSettings.staticConvergence` 默认开启。时域路径有效且 `maxFrames>1` 时，probe seed使用基础种子加**已完成帧的 temporal sequence**，再取29bit掩码 `0x1fffffff`，不再先对默认8相位取模。该分支也用于移动期间，使停走时不会在两种种子序列间切换；它改变的是探针方向实现，不改变探针位置或每帧射线数。29bit种子空间是有限的，不能称为无限永不重复，也不保证每条实际浮点方向永不碰撞。关闭开关仍使用用户的 `directionPhases` 周期；`maxFrames=1` 同样不启用这项长序列。

64帧历史只在CPU确认以下条件时开放：历史已有效、新开关开启、用户 `maxFrames>1`、本帧没有 dynamic-lighting 标志，且当前 view / projection 与上一已完成帧**精确相同**。历史上限此时为 `max(userMaxFrames,64)`；现有设置允许的最大值为64。GI接收的是实际渲染相机，TAA投影抖动也会使这一精确比较失败，不能把抖动中的静止相机直接当作64帧静止视图。

相机移动、动态照明或几何运动的响应路径继续使用原上限与运动 / dynamic / edge caps。GPU仍逐像素验证历史位置、深度、法线、readability及有限值；不可读当前样本不会被旧历史填成有效光照。64是可用的累计权重上限，不保证所有像素都累计64帧，达到上限后仍为有界EMA更新，不是无限增加样本的算术平均。真实光照或材质变化造成历史key变化时按原规则重置，新开关本身也进入历史兼容key；丢弃或未完成提交的帧不推进已完成序列。

在上述静止判定成立且 `reconstructionDebug=0` 时，管线在 `RecordScreenProbePlacement` 完成之后给运行时 `placementFrameConstants.refinement` 添加数值16的标志（bit4），约束 `SFGeometryWeight` 的角度复用。只有法线点积 **大于 `1-1e-6`（约0.081°容差）**，且 `abs(normal.z)<0.99` 的切线轴分支相同的邻探针才能参与空间过滤，按对应的角度stratum索引复用。各探针仍有独立jitter，实际射线世界方向并不逐条相同；这个容差和索引对应也不等于消除所有过滤偏差。物理分配、实际探针位置和追踪起点已经确定，不被运行时标志改变。

不同法线的曲面邻居不再在静止模式下通过 nearest angular warp 借用射线。该近邻重映射没有严格保持角度单元的测量分布，跨三轮重复可能留下与探针相关的偏暗，即使随机噪声继续收敛也不会自动消失。新限制按法线与切线基判定，不根据RGB或亮度挑选邻居；同向平面仍保留三轮空间过滤，实际接收点的接触过滤也保留。首帧、移动或动态settling期间继续沿用原复用路径和历史保护；刚进入静止模式的历史还混有旧路径估计，需要后续帧逐步更新，不承诺立即无偏。UE使用共同世界方向gather grid，本实验仍是局部余弦半球表示，只在匹配角度单元时保守复用，不等于移植了完整UE方向过滤管线。

这项组合没有增加GPU绑定接口、持久纹理 / UBO、pass、每帧射线数或空间模糊，也不加入receiver-query偏移、额外查询路径纹理读取或Contact反照率绑定。静止运行时常量与分配时常量不同，会多一份placement UBO版本snapshot；它使用现有接口和资源管理机制，不能据此称为严格零上传成本，配对GPU测量包含这部分工作。原上采样查询位置及真实接收点 `p/n/rho`、安全起点和接触射线保持原定义；变化的是方向序列、历史上限和静止时允许参与空间过滤的角度兼容邻居。静止等待按帧计，实际秒数取决于运行帧率；该方法不能消除固定几何、缓存覆盖或方向拟合的全部系统偏差。新的方向与复用限制仍可能改变短命中分类和接触mask，因此无新增持久资源不等于无成本，整帧与接触组仍需配对测量。

F1的 `Static probe convergence (64 frames)` 控制该开关。专项CLI使用 `--surface-cache-probe-imprint-convergence` 修饰原质量或性能入口；修饰参数本身不启动专项。新旧两组都保持 `curvatureAwareWeights=true`、`coherentProbeRisk=true`，仅对比 `staticConvergence=false/true`，避免把上一轮颜色过渡修改的收益或代价混入本轮。

质量模式默认在重置后渲染128帧，检查第32/64/128帧；`--surface-cache-probe-imprint-convergence-frames 64` 可选择32/64帧检查点，此参数接受64或128。`current` 是用户实际看到的当前GPU阶段，`cumulative` 是CPU对重置后全部帧的平均，`last32` 是最近32帧的CPU块平均；三者分开保存和报告。它还比较相隔8帧的当前图、相邻32帧块平均，以及实际历史权重、活动探针数、mask覆盖和未知 / alpha约束。相隔8帧不再相同只能说明短周期被打破，不能单独证明降噪；仍需结合current图的参考误差、偏色、接触剖面和参考噪声底。参考继续使用同安全起点、共享Surface Cache的独立seed采样，沿用split-half噪声底及几何ROI，不能当作完整路径追踪真值。

默认 convergence 质量视角为正对椭球的 oval_frontal_static；显式 `--surface-cache-probe-imprint-view 1` 可测 contact_near，`2`可测 wall_corner。性能仍固定 contact_near、AB/BA/AB/BA四对，每批预热64帧、测量16帧，每组64个fresh frame / trace / filter样本，使用四个配对批均值的Student t(3)区间。初次SDF / atlas / 卡片准备不计时；禁止validation、呈现、图像回读与CPU质量oracle。原非convergence专项保留16帧预热与上一轮两项开关的对照语义，并显式关闭新的静止收敛选项。

```text
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --procedural --surface-cache-probe-imprint-test --surface-cache-probe-imprint-convergence --surface-cache-probe-imprint-view 0 --surface-cache-curved-contact-reference-seeds 32 --render-width 640 --render-height 400 --screenshot build\probe_stationary_front.png
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --procedural --surface-cache-probe-imprint-test --surface-cache-probe-imprint-convergence --surface-cache-probe-imprint-convergence-frames 64 --surface-cache-probe-imprint-view 1 --surface-cache-curved-contact-reference-seeds 32 --render-width 640 --render-height 400 --screenshot build\probe_stationary_near.png
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --software-rays --procedural --surface-cache-probe-imprint-performance --surface-cache-probe-imprint-convergence --render-width 1280 --render-height 800
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --software-rays --procedural --surface-cache-probe-imprint-performance --surface-cache-probe-imprint-convergence --render-width 2560 --render-height 1440
```

### 正面静止视图的正式结果

[正式正面日志](../../build/probe_stationary_front.log) 已完成640×400、1024-spp共享Surface Cache参考、217606个椭球外壳几何接收点的128帧对照；完整结果见 [指标JSON](../../build/probe_stationary_front_metrics.json)。以下是**当前GPU temporal图**，没有用CPU累计平均替代显示结果：

| 重置后帧数 | 旧RGB MSE | 新RGB MSE | MSE变化 | phase32 x/y残差能量和变化 | 新历史平均权重 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 32 | 4.14489e-6 | 2.63043e-6 | −36.5% | +16.6% | 32 |
| 64 | 4.17808e-6 | 2.42033e-6 | −42.1% | +10.6% | 64 |
| 128 | 4.17574e-6 | 2.30427e-6 | −44.8% | −5.6% | 64 |

旧历史平均权重始终为10。第128帧新图平均亮度偏差从−2.273e-4减为−9.7e-6，但phase32只小幅减少，前两个检查点甚至上升，因此不能将MSE降幅称为同等比例的探针瘢痕或总噪声改善。参考RGB噪声底为2.08007e-6，已接近新图MSE；高通及梯度也受参考底限制，不据扣底比例追加更大的降噪声明。CPU累计128帧平均的MSE下降38.3%，phase32反而增加约0.8%，进一步说明当前图、长期平均和格相关残差应分开解读。

第32/64/128帧新旧活动探针数均为1157；probe raw / spatial的未知几何alpha计数均为0，选中几何未知计数为0，alpha、mask零、有限值及三项参数历史失效检查通过。旧probe raw相隔8帧及相邻32帧块平均精确重复，新方向序列打破该短循环；这仅说明得到新方向测量，不能单独作为画质改善证据。

第128帧实际总图：[旧配置](../../build/probe_stationary_front_oval_frontal_static_previous_frame128_total_current_renderer_ldr.png) · [新配置](../../build/probe_stationary_front_oval_frontal_static_improved_frame128_total_current_renderer_ldr.png)；独立当前GI：[旧temporal](../../build/probe_stationary_front_oval_frontal_static_previous_frame128_current_temporal_tone4.png) · [新temporal](../../build/probe_stationary_front_oval_frontal_static_improved_frame128_current_temporal_tone4.png)。这里没有把仅长历史或查询抖动的探索版本作为交付结果。

### 接触近景的正式结果

[正式近景日志](../../build/probe_stationary_near.log) 完成640×400、1024-spp共享缓存参考的64帧对照，完整的分阶段与三种估计结果见 [指标JSON](../../build/probe_stationary_near_metrics.json) 和 [汇总](../../build/probe_stationary_near_summary.log)。第64帧**当前GPU temporal图**如下；不能用CPU累计平均替代这张实际显示结果：

| 几何区域 | 像素数 | 旧RGB MSE | 新RGB MSE | MSE变化 | phase32 x/y残差能量和变化 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 椭球外壳（>35 cm） | 137757 | 1.64935e-6 | 9.50110e-7 | −42.39% | **+17.74%** |
| 椭球过渡（3–35 cm） | 4825 | 2.43257e-5 | 2.10286e-5 | −13.55% | −10.9% |
| 方柱接触核心（0–3 cm） | 1131 | 6.28672e-7 | 4.55754e-7 | −27.51% | −11.0% |
| 方柱过渡（3–35 cm） | 33992 | 1.17257e-6 | 8.88243e-7 | −24.25% | −14.0% |

椭球外壳MSE下降，但该近景的32像素相位残差上升17.74%，因此不能宣称各视角的探针印痕都下降，或把MSE变化当作同等比例的总噪声下降。外壳参考RGB底为6.90394e-7，接近新图MSE；多数区域高通和梯度仍受参考底限制。CPU累计64帧平均的方柱核心MSE反而上升约4.8%、phase32上升1.2%，也说明组合没有在所有估计与区域上取得一致收益。

方柱核心1131个像素的平均亮度绝对误差为6.67384e-5→3.21739e-5，核心相对外壳的对比误差为1.08029e-4→6.80233e-5，原接触细节保护通过。该视角椭球0–3 cm核心ROI没有有效像素，不能把方柱核心结论扩展为椭球核心验证。新旧活动探针数均为964，mask平均覆盖7.82891%→7.82148%；raw与spatial每帧各4个未知几何像素在新旧配置中一致，选中几何未知计数为0。alpha、mask零、有限值及三项参数历史失效检查通过。

第64帧实际总图：[旧配置](../../build/probe_stationary_near_contact_near_previous_frame64_total_current_renderer_ldr.png) · [新配置](../../build/probe_stationary_near_contact_near_improved_frame64_total_current_renderer_ldr.png)；独立当前GI：[旧temporal](../../build/probe_stationary_near_contact_near_previous_frame64_current_temporal_tone4.png) · [新temporal](../../build/probe_stationary_near_contact_near_improved_frame64_current_temporal_tone4.png)。

### 静止收敛的GPU成本与回归范围

1280×800的 [正式性能日志](../../build/probe_stationary_perf_1280.log) 已完成。硬件为RTX 4060 Laptop，Debug、Vulkan软件射线、contact_near视角；每配置64个计时帧，另有每批64帧预热。两组仅改变 `temporal.staticConvergence`，不包含validation、呈现、图像回读或CPU质量oracle。各scope的帧样本分布如下，单位ms：

| scope | 配置 | 均值 | 中位数 | P10 | P90 |
| --- | --- | ---: | ---: | ---: | ---: |
| frame | 旧 | 358.082 | 352.248 | 324.245 | 403.965 |
| frame | 新 | 333.923 | 334.263 | 307.472 | 364.422 |
| trace | 旧 | 162.574 | 161.764 | 153.681 | 172.341 |
| trace | 新 | 158.606 | 158.588 | 148.507 | 167.302 |
| filter | 旧 | 14.9091 | 12.9336 | 7.45114 | 23.2818 |
| filter | 新 | 13.4312 | 12.1651 | 6.54152 | 21.0190 |

| scope | 新减旧 ms | 四对批均值的95%区间 ms |
| --- | ---: | --- |
| frame | −24.1582（−6.74656%） | [−48.2311, −0.08528] |
| trace | −3.96765 | [−9.39643, 1.46113] |
| filter | −1.47784 | [−4.24234, 1.28666] |

整帧配对区间上界仅略低于零，当前批次有变快迹象，但证据有限，不能据此普遍承诺提速。trace与filter区间均跨零，不能分别宣称其成本下降；`filter` 是接触resolve / filter组，**不是探针辐射率空间过滤的独占成本**。这些scope也不是可相加的独占分解，不能用组差之和解释整帧净变化。正式近景质量的探针数相同，但性能模式不回读图像，仍明确允许活动计数 / mask覆盖随方向变化，不能称为共同总射线预算的严格对照。

2560×1440的 [第一轮正式性能日志](../../build/probe_stationary_perf_2560.log) 使用相同协议。各scope分布与配对区间保留如下，不剔除波动较大的首对批次，单位ms：

| scope | 配置 | 均值 | 中位数 | P10 | P90 |
| --- | --- | ---: | ---: | ---: | ---: |
| frame | 旧 | 919.460 | 834.081 | 788.920 | 1238.100 |
| frame | 新 | 894.377 | 829.905 | 788.514 | 1106.700 |
| trace | 旧 | 516.977 | 502.140 | 477.805 | 574.196 |
| trace | 新 | 523.203 | 507.926 | 482.520 | 563.220 |
| filter | 旧 | 25.3955 | 13.0760 | 12.3119 | 54.0200 |
| filter | 新 | 35.0719 | 13.1917 | 12.3615 | 132.0670 |

| scope | 新减旧 ms | 四对批均值的95%区间 ms |
| --- | ---: | --- |
| frame | −25.0833（−2.72805%） | [−115.868, 65.701] |
| trace | +6.22571 | [−12.3256, 24.7770] |
| filter | +9.67642 | [−20.6726, 40.0255] |

首对旧 / 新整帧批均值为1225.20 / 1115.07 ms，后三对各配置约793–844 ms；首对filter为62.1814 / 100.464 ms，随后约12.6–14.0 ms。三scope的配对区间均跨零且较宽，不能由该轮确定净成本，更不能称为零成本或提速。该结果保留为正式第一轮。

相同协议的 [2560×1440独立复测](../../build/probe_stationary_perf_2560_repeat.log) 也已完成，每配置仍为四批、共64个计时帧。下表与第一轮独立报告，单位ms：

| scope | 配置 | 均值 | 中位数 | P10 | P90 |
| --- | --- | ---: | ---: | ---: | ---: |
| frame | 旧 | 824.312 | 824.621 | 784.068 | 856.171 |
| frame | 新 | 783.679 | 781.517 | 734.670 | 822.616 |
| trace | 旧 | 489.554 | 488.144 | 465.284 | 506.035 |
| trace | 新 | 475.543 | 474.793 | 443.991 | 503.091 |
| filter | 旧 | 14.8128 | 14.3370 | 12.0300 | 17.1922 |
| filter | 新 | 13.2237 | 12.8753 | 11.9457 | 15.0065 |

| scope | 新减旧 ms | 四对批均值的95%区间 ms |
| --- | ---: | --- |
| frame | −40.6336（−4.92939%） | [−101.346, 20.0791] |
| trace | −14.0103 | [−59.6722, 31.6515] |
| filter | −1.58914 | [−3.32623, 0.14796] |

复测三scope区间仍全部跨零；两轮都不能可靠确定小幅净增减，不能宣称零成本或普遍提速。两轮不合并、不挑轮，也不剔除首对较慢批次。`filter` 仍只是接触组，不能据其均值变化解释全部探针空间复用成本；1280结果的有限变快迹象也不能外推到2560或其他设备。

[最终构建](../../build/probe_stationary_final_build.log)、[最终CPU设置回归](../../build/probe_stationary_final_cpu.log) 与 [Vulkan时域回归](../../build/probe_stationary_temporal_vk.log) 已完成并通过：新模式生成第8帧之外的新方向，49150个像素可达到64帧权重，微小相机运动回原cap、恢复静止可再累积；认证物体运动仍受4帧cap约束，可读黑色与不可读当前样本的契约保留。这些数学 / 历史检查不能代替画质专项。

[OpenGL空间回归](../../build/probe_stationary_spatial_gl.log) 和 [OpenGL方向契约](../../build/probe_stationary_directional_gl.log) 已通过，使用的是 **Intel OpenGL 4.5（驱动32.0.101.6790）**，不是上述Vulkan性能所用的RTX 4060 Laptop。空间回归检查噪声收敛、探针密度、自适应覆盖、屏幕可见性和unknown保留，metadata / light-scale误差均为0；方向回归检查原RGB / alpha数学契约、最近几何命中元数据、可读黑色 / 不可读样本、方向重建开关和历史失效。该320×200方向专项的raw未知计数4883保持，on/off/on误差为0。这些结果支持结构与数学兼容，不证明两设备或两后端的视觉 / 性能等价。

原Final Gather时域fixture显式关闭静止收敛与接触补追，分别隔离原相位 / history cap及probe覆盖契约；probe / spatial无覆盖时，默认接触路径仍可在真实接收点合法恢复光照，因此不应把该最终结果误判为历史填洞。旧“几何变化全重置”的初始与shipped配置还显式设置 `retainHistoryOnObjectMotion=false`，隔离原reset / raw-equality契约；默认开启的运动历史保留仍由独立认证物体运动fixture以true测试。两项职责隔离不改变候选像素、断言或门限；完整edge / recovery GPU回归不在这里列为已通过。

[最终隔离连续运动回归](../../build/probe_stationary_legacy_motion.log) 已通过yaw、translation与combined三条路径，实际渲染320×200、间距8、每探针16条射线，参考为4×256条；每条路径预热24帧、运动16帧、settle 8帧。72个对齐帧的 `raw_phase_fairness_max=0`，调参候选的噪声与几何保护检查通过。该专项关闭新静止模式，只验证隔离后的既有Final Gather运动对照与输入相位公平性；它不代表已完成全部edge / recovery GPU专项，也不把运动候选之间的收益归到新静止组合。

## 2026-10-19：表面探针印痕与补追色彩过渡

**历史范围：** 本节记录曲面 / 色彩过渡修正，静止收敛选项未启用；质量和成本数字仍对应本节原配置，不能代替上方静止收敛专项。

本轮处理椭球表面随探针格排列的照明印痕，以及探针估计与真实接收点补追交接时的色彩变化。曲面上的两点本来就会偏离彼此的切平面；若将这种二阶弦误差全部当成表面不兼容，再乘较强的法线幂次权重，邻点支持会围绕每个探针集中。不过，直接放宽所有曲面支持也会在近处遮挡附近借入不同颜色的照明，所以新增支持还必须受真实几何命中视差限制。这些是本实验重建与复用策略的取舍，不表示 Lumen 无法进一步改善。

### 照明复用与物理聚类分别判断

`ScreenProbePlacementSettings.curvatureAwareWeights` 默认开启，只影响 GI 的照明兼容权重。重建和探针射线空间过滤共享中点切平面残差 `abs(dot(delta,normalize(n+pn)))`：平滑曲面的弦位移不再因端点切平面误差而过度集中在某个探针周围。重建的法线兼容在原有 0.6 硬门限至 0.9 之间平滑上升；空间过滤使用自己的原有法线硬门限。平面支持在门限前的最后25%区间平滑降到零，硬拒绝边界仍保留。平行分层的法线相同，中点平面残差仍保留实际层间距离；屏幕段 / BVH 可见性也继续独立约束复用。

表面代表的**物理聚类、实际选点和 GI 分配覆盖**使用独立的严格权重，不使用放宽后的照明模型。物理聚类保持原双端切平面判定 `SPGiClusterPlaneWeight`；分配阶段的 `SPUniformWeights` 和 `SPGiCoarseCurvatureError` 也使用原 Cluster 权重，未启用 refinement 时仍用原 `SPGiPlaneWeight`。这样不会仅因为两折面位移在平均法线方向抵消，就把它们合入同一候选槽，也不会因为新照明支持更宽而改变活动探针的分配数量或位置。曲率代表分配、实际位置 B2 核和固定完整支持窗口继续保留；没有加入按 RGB 亮度选择邻居的规则。

接收点重建进一步在严格旧权重与曲面新权重之间混合。对每个探针，使用接收点切向位移乘该探针**最近真实几何命中的逆距离**，得到命中视差近似；`SPGiCurvedHitSupport` 在视差小于 `tan(5°)` 时允许新权重，在 `tan(5°)` 到 `tan(10°)` 之间连续退回严格旧支持，之后只用旧支持。采用最近命中，是为了让少数近处障碍也能保守限制新增借光支持，避免平均命中距离掩盖它们。这个约束只决定是否允许新增曲面支持，不删除合法的旧支持；缺少可用元数据时同样退回严格权重。几何覆盖和解析重建诊断不读取该照明元数据。

重建与探针射线空间过滤还会依据两端法线旋转角限制新权重：10°以内允许完整新支持，10–20°之间平滑退回严格旧支持，20°及以上使用旧支持。这是另一项角度约束，不要与上述 **5–10°命中视差**混用。它针对本实现的有限法向半球与局部方向拟合；几何表面连续不代表大角度法线变化下的方向估计同样可靠。几何 coverage 以及解析 constant / impulse 重建诊断使用同一法线旋转支持，只有真实照明额外读取最近命中 metadata，避免诊断宣称几何可用、常量重建却因另一套支持变黑。新旧几何权重都为零时提前返回，避免继续归一化无效法线。接收点真实照明同时使用几何命中视差与法线旋转约束；它不是 UE 共同世界方向表示的完整移植。

### 内部几何元数据与补追交接

现有 directional atlas 每探针四个 RGBA32F texel，第一项仍为原 `E/PI` 与公开 readability，后三项为 RGB 方向系数。这里只复用 **x 系数 texel（part1）的 `.w`**，存储 `max(1/d)`，即最近真实几何命中的逆距离。只统计有效正距离的真实几何命中状态 `1/17/9..12`；黑色、背面或缓存未知几何命中都参与最近距离判断，miss 不参与，无真实几何命中时为0。该几何 metadata 不是调和平均，也没有按射线数求平均；原 RGB 与 readability 仍使用完整射线预算作为分母，未知射线不会被移除。RGB、part0 的公开 coverage 和 part2/3 的零 alpha 均保持原契约；关闭曲面权重时 part1.w 为零，并跳过额外的命中距离读取。

内部 metadata 可用位与 RGB 方向拟合开关分开。默认方向重建开启时复用现有积分 pass；关闭 `directionalReconstruction` 但保持曲面权重开启时，仍执行内部 directional 积分以生成 metadata，公开 `surfaceCacheDirectionalCollected` 和 directional readback 仍保持关闭。这使 RGB 方向拟合的开关不会顺带改变几何权重或 readability；两项都关闭时无需该内部 pass。

Contact mask 与探针 RGB 重建共用 `SPGiReconstructionStencil`、`SPGI_CURVED_SUPPORT` hook 和同一 directional metadata，确保几何风险按实际照明所用的支持计算。`SurfaceCacheContactSettings.coherentProbeRisk` 默认开启：除原有切向视差风险之外，以完整射线预算中的近几何命中比例提供风险下限，避免恰好位于探针源点时位移为零、接触风险反而消失。该下限随切向世界距离在 `0.15D..D` 内平滑衰减；RGB 不参与风险选择。近黑命中和缓存未知几何仍属于风险，新增选中接收点仍独立认证安全起点并发射真实射线。

同一开关将探针风险到 mask 的映射由旧 `smoothstep(0.5t,1.5t,risk)` 改为 `smoothstep(0,4t,risk)`，默认 `t=0.05`。低风险外带从0开始以更宽范围平滑过渡，避免探针估计与补追结果在较窄选区内交接；接收点屏幕几何风险仍以 `max` 合入，明确接触核心仍可保持 mask=1。这个变化会改变实际补追覆盖，低风险外带可能扩张，成本需纳入测量；它不对 RGB 施加额外模糊。

同一开关也让年轻历史恢复将连续 readability 当作置信度，而非用 `abs(alpha差)>.001` 把同表面的探针 / 补追混合区域分开。未知 tap 仍拒绝，逐像素路径、法线、平面、材质和世界距离门限继续生效；置信度仅衰减过滤权重，不提高中心 alpha，也不增加真实时域样本数。

本轮未增加每探针或每个选中接收点的射线数，默认仍分别为 **64 / 8**；未新增纹理或 UBO，默认方向重建开启时也没有额外 pass。严格分配模型独立于新照明权重，固定场景 / 相位下预期活动探针数量、位置及对应探针射线数保持一致，专项仍记录 actual counts 核对。接触风险会改变补追覆盖，metadata 读取与权重计算也有成本。因此不能称为整套方案总射线预算相同或零成本，需分别实测完整帧、接触检测 / 追踪组与过滤组。

本机 UE 5.7.1 的 [LumenScreenProbeFiltering.usf:358](D:/UE5/engine/UE_5.7/Engine/Shaders/Private/Lumen/LumenScreenProbeFiltering.usf:358) 用中心和邻点较短命中距离约束近邻视差，保护近处接触；[LumenScreenProbeGather.cpp:294](D:/UE5/engine/UE_5.7/Engine/Source/Runtime/Renderer/Private/Lumen/LumenScreenProbeGather.cpp:294) 说明全分辨率短程 AO 对低分辨率探针缺失高频接触的补充职责。这里参考的是“方向光照复用与接收点短程修正分开、近遮挡限制借光”的原则。本实现仍使用独立法向余弦半球与受限一阶拟合，并非 UE 的共同世界方向 gather grid、SH3 或完整 Short Range AO / GI 移植。

### 专项范围与复现

`--surface-cache-probe-imprint-test` 比较 previous（两项新开关均关闭）、improved（均开启）及 contact_off，分开记录 probe raw、probe spatial、contact unfiltered、contact filtered、temporal 五级 HDR。`--surface-cache-probe-imprint-view 0|1|2|3` 对应 oval_close / contact_near / wall_corner / 全部，默认全部。接收区域依据独立 CPU 主射线对象归属及几何距离确定，不由生产 mask 选择；接触核心为0–3 cm、过渡3–35 cm，外壳超过35 cm。另记录真实活动探针数量、补追覆盖、未知 / mask零 / alpha约束、接触剖面和参数历史失效。

参考复用 `--surface-cache-curved-contact-reference-seeds 32|64|128`：每 seed 32条安全接收点射线，总计1024 / 2048 / 4096 spp，参考重照明预热不计入样本。参考和生产共享 Surface Cache，因此它检查接收点估计与重建偏差，**不是完整路径追踪真值**。无可读参考的像素从质量 ROI 排除，同时保留独立未知覆盖约束。完整参考噪声底按两半参考均值差平方的四分之一估计，高通与残差梯度也使用对应算子估底。

质量指标针对预热16帧之后8帧的**平均 HDR 图**计算 RGB MSE、区域偏色、残差高通及梯度；另行记录帧间方差，不能把平均图 MSE 当作单帧颗粒方差。周期4/8/16/32的 x/y 相位指标是去掉区域平均偏差后的条件均值能量，适合观察探针格相关印痕，不能当作频谱证明，也不能仅凭相位或高通下降断言改善。应同时核对参考噪声底、RGB偏差、核心 / 外壳对比和实际图像。

```text
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --procedural --surface-cache-probe-imprint-test --surface-cache-probe-imprint-view 3 --surface-cache-curved-contact-reference-seeds 32 --render-width 640 --render-height 400 --screenshot build\probe_imprint_views.png
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --procedural --surface-cache-probe-imprint-test --surface-cache-probe-imprint-view 3 --surface-cache-curved-contact-reference-seeds 32 --render-width 1280 --render-height 800 --screenshot build\probe_imprint_verified.png
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --software-rays --procedural --surface-cache-probe-imprint-performance --render-width 1280 --render-height 800
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --software-rays --procedural --surface-cache-probe-imprint-performance --render-width 2560 --render-height 1440
```

性能专项固定 contact_near 视角，仅交错上述两个开关，保持相同每探针 / 每选中接收点射线数。四对顺序为 AB / BA / AB / BA，每批预热16帧、测量16帧，每配置各64个新鲜 frame / trace / filter 时间戳；报告均值、中位数、P10/P90和四个配对批均值的 Student t(3) 95%区间。SDF准备、初次卡照明和阴影atlas创建不计入稳态样本；禁止 validation、呈现、图像回读和 CPU oracle。`trace` 包含接触风险 / mask / 实际补追，`filter` 包含既有 resolve 与过滤；两组跨度不能简单当作整帧净增量。

F1 中 `Curved surface lighting weights` 控制曲面权重，`Coherent near-contact probe risk` 控制风险下限与年轻恢复置信度；它们分别进入历史兼容键。CLI质量与性能专项自行设置新旧模式，当前没有将这两个 F1 开关各自暴露为单独的命令行禁用参数。

### 最终1280×800质量与结构回归

[正式质量日志](../../build/probe_imprint_verified.log) 已完成 oval_close / contact_near / wall_corner 三视图及 previous / improved / contact_off 对照，使用1024-spp安全接收点参考；完整逐区域、逐阶段结果与参考底限制见 [指标JSON](../../build/probe_imprint_verified_metrics.json)。下表均为 **temporal 阶段8帧平均 HDR 图**的新减旧百分比；phase32 为去区域偏差后 x/y 条件相位残差能量之和，未将它解释为总噪声降幅。

| 视图 / 几何区域 | RGB MSE变化 | phase32残差能量变化 |
| --- | ---: | ---: |
| oval_close / 椭球外壳 >35 cm | −0.83% | −44.78% |
| contact_near / 椭球外壳 >35 cm | +0.328% | −41.97% |
| contact_near / 椭球过渡3–35 cm | +0.126% | +2.54% |
| contact_near / 方柱过渡3–35 cm | −2.88% | +15.73% |

椭球外壳 phase32 的减少高于对应参考噪声底，说明这两个视图中的32像素格相关残差减弱；RGB精度变化远小于相位指标变化。contact_near 的椭球过渡 RGB MSE 为 `1.03568e-5 → 1.03698e-5`，基本持平；过渡区的相位指标并非全面下降。方柱接触核心 RGB MSE 减少1.58%，接触核心绝对偏差、亮侧过冲及核心 / 外壳对比约束均通过。多数高通与梯度变化受参考底限制，墙角过渡变化也较弱，不能据其扣底比率宣称可靠的全面降噪幅度。JSON中“可识别”使用大于1.25倍估计参考底的启发式规则，并非统计置信区间。

严格分配的实际计数也已核对：新旧8帧的活动探针均值、最小值和最大值完全相同。oval_close 为 **4020=1000均匀+3020自适应**，contact_near 为 **3161=1000+2161**，wall_corner 为 **1341=1000+341**。探针射线数量因此在该固定场景 / 相位下保持一致；mask 选中覆盖仍有变化，三个视图的新旧全图选中比例分别为61.777%→61.838%、43.700%→45.470%、11.430%→12.401%，不能把整套方案称为总射线预算相同。

椭球视图未知几何计数为0；墙角新旧每帧平均4个未知几何样本保持原样。mask零、alpha与有限值约束、两项开关的历史失效检查均通过，没有把未知值变成有效光照。

同视角实际总图：[contact_near旧配置](../../build/probe_imprint_verified_contact_near_previous_total_renderer_ldr.png) · [contact_near新配置](../../build/probe_imprint_verified_contact_near_improved_total_renderer_ldr.png)；椭球近景：[oval_close旧配置](../../build/probe_imprint_verified_oval_close_previous_total_renderer_ldr.png) · [oval_close新配置](../../build/probe_imprint_verified_oval_close_improved_total_renderer_ldr.png)。总图采用 renderer 的显示变换，HDR质量数字仍来自上面的独立阶段和几何 ROI。

[独立几何回归](../../build/probe_imprint_ship_geometry.log) 的8种布局已通过，覆盖常量、有效黑、内部几何、跨面覆盖和微小运动。曲率激活 ramp 的独立 CPU 权重 oracle 使用新几何权重与实际像素插值，`impulse_contract_max_error=1.309e-6`，保留原 `3e-4` 门限；将部分激活错误当作满权的反事实响应差为0.161403，说明该检查能够区分激活权重错误。它验证重建契约，不能替代上述实际照明质量对照。

### 最终GPU成本与后端回归

RTX 4060 Laptop、Debug、Vulkan软件射线、固定 contact_near 视角，按上述四对批次、每配置64帧完成 [1280×800性能专项](../../build/probe_imprint_perf_1280.log) 和 [2560×1440性能专项](../../build/probe_imprint_perf_2560.log)。validation、呈现、图像回读和 CPU oracle 均关闭，初始准备与每批16帧预热不计时。下表为真实GPU时间戳，区间按四个配对批均值计算 Student t(3) 95%区间。

| 分辨率 / scope | 旧均值 ms | 新均值 ms | 新减旧 ms | 配对95%区间 ms |
| --- | ---: | ---: | ---: | --- |
| 1280×800 / frame | 253.307 | 257.823 | +4.516 | [−10.825, 19.857] |
| 1280×800 / trace | 129.856 | 135.175 | +5.319 | [−1.847, 12.485] |
| 1280×800 / filter | 6.145 | 5.705 | −0.440 | [−2.210, 1.329] |
| 2560×1440 / frame | 844.059 | 925.243 | **+81.185** | **[27.739, 134.631]** |
| 2560×1440 / trace | 464.270 | 508.567 | +44.297 | [12.028, 76.566] |
| 2560×1440 / filter | 20.883 | 24.506 | +3.623 | [1.428, 5.818] |

2560×1440高分辨率专项的整帧增加 **81.1847 ms（9.618%）**，区间没有跨零，支持本轮存在明确额外成本；默认没有新增 pass 并不等于没有成本。1280×800整帧点估计为+4.51595 ms（1.783%），三个scope的区间均跨零，本批未充分区分变化与批间波动，不能称为免费，也不能将 filter 的负点估计当成确定加速。

各scope的中位数及P10/P90如下，保留批内波动范围；这些分位数不是上述配对差值的置信区间。

| 分辨率 / scope | 旧中位数 [P10, P90] ms | 新中位数 [P10, P90] ms |
| --- | --- | --- |
| 1280×800 / frame | 254.547 [240.929, 263.987] | 257.814 [253.275, 262.902] |
| 1280×800 / trace | 129.761 [122.357, 136.418] | 135.206 [129.411, 139.080] |
| 1280×800 / filter | 6.167 [2.950, 7.866] | 6.025 [2.825, 7.428] |
| 2560×1440 / frame | 850.474 [770.665, 924.312] | 915.382 [871.356, 991.737] |
| 2560×1440 / trace | 458.799 [423.185, 530.553] | 511.107 [481.998, 524.590] |
| 2560×1440 / filter | 19.292 [15.478, 28.752] | 22.544 [17.712, 33.049] |

frame包含整帧，trace / filter是其中的组跨度，三者不是可相加的独占成本；两组差值也不足以精确解释整帧净增。补追覆盖扩大、最近命中 metadata 和受限曲面重建都增加工作，过滤组还包括原有 resolve。质量专项已核对固定视角的活动探针数一致，但性能日志的覆盖可变声明仍然适用。此结果限于当前 Debug 软件射线路径、硬件和视角，不能直接外推 Release、硬件光追或其他视图。

[Vulkan方向契约回归](../../build/probe_imprint_verified_directional.log) 和 [OpenGL方向契约回归](../../build/probe_imprint_directional_gl.log) 均完成320×200、64 rays/probe的检查：原scalar/RGB/alpha数学契约、自己的法线求值恒等式、最近几何 metadata、关闭曲面权重时 metadata 为零、方向重建开关往返、历史失效与关灯零能量均通过。Vulkan raw unknown为4，OpenGL为4883；强制未知路径各保留19584条不可读射线，有限值约束通过。该回归证明各自数据契约，不能把OpenGL结果称为与Vulkan画质等价。

[Vulkan灯框回归](../../build/probe_imprint_lamp_vk.log) 已通过480×300、3视图、13配置、256-spp共享Surface Cache参考，`improve_assertions=1`；六项参数历史失效、光强线性及关灯检查通过，后两项最大RGB误差为0。[OpenGL灯框结构回归](../../build/probe_imprint_lamp_gl.log) 已通过320×200、3视图、13配置，`reference_spp=0`、`improve_assertions=0`；四帧微小运动复用254391个pixel-frame，六项参数历史失效、光强线性误差0及关灯最大RGB为0。OpenGL直接光RGB8读取仍存在旧问题，该批仅作为结构兼容范围，不用于证明直接阴影画质或两后端视觉等价。

## 2026-10-19：墙顶静止宽条带与确定性面光源采样

**历史范围：** 本节记录上一轮仅改变面积灯源点分布的版本，尚未包含上方曲面权重与补追一致性组合；下方图像观察、改善范围和性能数值不能代替本轮最终验收。

墙顶交界仍能做得更干净、柔和。这次用户图中的一部分宽平行明暗台阶来自本地直接面光源的离散采样，并非 Lumen 无法改进的固有限制。把总光拆成直接光与间接光后，旧配置在关闭 GI 时仍保留宽台阶；再关闭直接面积阴影，台阶明显消退。直接光图在连续静止帧间几乎不变，而接触 GI 的各级图仍有独立的采样方差，因此不能把两者统称为接触 GI 白噪声。

旧版把矩形灯分为 8×8 个单元，固定采样每格中点。64 个点沿任一轴只有 8 种坐标，灯框边缘可能同时遮住整排或整列光源，使多个样本一起切换可见性。继续增加 GI 历史或平滑 GI RGB，不能移除直接照明中已经固定的阶梯。Surface Cache 卡片照明也使用这些源点，因而还可能把同类结构带入反弹信号。

本轮保持相同 64 点预算，改为**固定的二维分层采样**：每个 8×8 单元仍恰好一个点，横、纵投影各覆盖 64 个互不重复的细子层；4×4 模式相应覆盖 16 个子层。对于单元 `(x,y)`、网格边长 `g`，两个格内子层分别为 `(5y+2x+3)%g`、`(3x+4y+3)%g`，在各子层中心取点。4 和 8 两种模式均保持中心对称、完整发光面积与每点原有面积权重。这样避免整排同时遮挡，让几何定义的半影过渡具有更细的采样分布。

它不随帧改变种子，不抖动源点，也不增加空间 RGB 模糊；静止场景继续复用同一张深度图集。CPU 的 `AreaLightSampleParameters` 与 GLSL helper 使用同一公式，以下三路必须采用完全相同的源点：

- CPU 生成的每源点六面深度 atlas。
- 不透明 PBR 的矩形灯照明及其深度阴影查询。
- Surface Cache `SCDirect` 的真实三角形 BVH 可见性积分。

`AreaLightsSettings.stratifiedSamples` 默认关闭，普通材质保持原中点积分；封闭方形顶灯场景默认开启。现有 256B `AreaLightsConstants` 的 `halfAxisU.w` 用符号编码模式，绝对值仍为 4 或 8；原 `halfAxisV.w` 的 GGX 过滤强度保持原义。atlas key 显式包含模式，Surface Cache 照明 key 包含带符号的常量，TAA / GI 历史兼容键也包含模式；切换会替换同尺寸 atlas、重照明并清除不兼容历史，之后恢复缓存复用。未增加渲染 pass、纹理、UBO 或射线数量：默认 64 源点仍对应 384 个深度面，256 配置下仍为 **24 MiB atlas**，常量缓冲仍为 **256B 面积灯 + 48B 阴影常量**。预算不变不代表耗时严格相同，实际成本以配对 GPU 测量为准。

运行时关闭阴影使用独立的 `AreaLightShadowSettings.enabled`。`SurfaceCacheGiSettings.areaLightShadows` 保留为材质 / 管线 opt-in：普通 PBR 的天空反射占用纹理10、静态探针占用 UBO12，不能在普通路径无条件清空它们。已 opt-in 的场景即使临时关闭阴影，仍在不透明场景前上传 `options.x=0` 并清对应 atlas 绑定，避免前一帧 GI 共用 UBO12 后留下错误解释的常量；重新启用可复用原 atlas。F1 的 `Recessed ceiling fixture` 提供 `Stratified emitter samples` 和 `Direct area shadows enabled` 两个开关，F5 仍用于直接光 / GI 对照。

### UE 职责与本次适配范围

[Epic 的 Lumen 说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-global-illumination-and-reflections-in-unreal-engine) 将漫反射间接照明与其他阴影系统配合使用；提高 `Final Gather Quality` 可减少噪声，同时增加 GPU 成本。本机 UE 5.7.1 的 [LumenScreenProbeGather.cpp:218](D:/UE5/engine/UE_5.7/Engine/Source/Runtime/Renderer/Private/Lumen/LumenScreenProbeGather.cpp:218) 也明确描述累积帧数与噪声 / 光照响应的取舍。直接软阴影则有独立的 VSM SMRT 路径：[VirtualShadowMapSMRTCommon.ush:44](D:/UE5/engine/UE_5.7/Engine/Shaders/Private/VirtualShadowMaps/VirtualShadowMapSMRTCommon.ush:44) 生成时空样本，[VirtualShadowMapProjectionSpot.ush:366](D:/UE5/engine/UE_5.7/Engine/Shaders/Private/VirtualShadowMaps/VirtualShadowMapProjectionSpot.ush:366) 按帧索引使用它们。这里仅参考分开诊断直接阴影与间接 GI 的职责，没有移植 SMRT。

本次修正的是本地 64 源点共行 / 共列造成的结构性台阶，不能据此声称消除了全部 Lumen 随机噪声、所有遮挡方向的采样误差或几何轮廓锯齿。原接触补追、曲率过滤与成熟 GI 历史算法未扩大核或更改射线预算；接触阴影细节仍由真实几何可见性和原有 GI 检查保护。

### 验证与复现

480×300、三个固定视图的 Vulkan 分图诊断已通过：[旧中点日志](../../build/wall_transition_midpoint.log)、[新分层日志](../../build/wall_transition_stratified.log)。各配置预热24帧、观察8个静止帧；保存实际显示变换后的总光 / 直接光，以及独立的未过滤补追、过滤补追、时域 GI、probe raw GI 和风险 mask。GI HDR 预览使用另外标明的 tone4 显示，不与总光 LDR 的数值直接相减。此诊断检查有限值与分层输出，没有高采样画质改善断言；480 图用于定位和低分辨率回归，不作为最终高分辨率性能或误差结论。

同机位总图：[旧8×8中点](../../build/wall_transition_midpoint_corner_lamp_total_renderer_ldr.png) · [新固定分层](../../build/wall_transition_stratified_corner_lamp_total_renderer_ldr.png)。

[面积灯 GPU 单元日志](../../build/wall_transition_area_gpu.log) 已验证默认中点模式、正负4/8编码、实际GPU循环16/64点数、每格占用、两轴投影子层、中心对称与常量布局；两种模式对独立512×512密积分的最大相对误差为 **0.003790**，低于0.008门限，GL错误为0。它覆盖未遮挡积分与编码，不等同于遮挡 atlas 的端到端真值测试。

另有 [CPU 采样剖面数据](../../build/wall_shadow_sampling_reference.json) 和 [剖面图](../../build/wall_shadow_sampling_profiles.png)：使用场景实际盒体的精确 AABB 可见性及密集矩形光积分，隔离样本位置造成的阶梯。这是 sampling-only oracle，不读取 GPU 深度 atlas，也不是 GI 画质真值；其数值改善比例不能用作最终渲染的降噪百分比。

```text
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --procedural --surface-cache-wall-transition-test --surface-cache-wall-transition-legacy --render-width 1280 --render-height 800 --screenshot build\wall_transition_midpoint_high.png
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --procedural --surface-cache-wall-transition-test --render-width 1280 --render-height 800 --screenshot build\wall_transition_stratified_high.png
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --software-rays --procedural --surface-cache-wall-transition-performance --render-width 1280 --render-height 800
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --software-rays --procedural --surface-cache-wall-transition-performance --render-width 2560 --render-height 1440
```

性能专项仅比较采样模式，保留相同接触 GI 设置，分别测 `gi_on` 和 `direct_only`。每种模式按 AB / BA / AB / BA 四对顺序运行，每批预热16帧、计时16帧，每配置64个新鲜整帧 GPU 时间戳；禁用 validation、呈现和图像回读。报告均值、中位数、P10 / P90及四个配对批均值的95% t区间；没有增加过滤组，因此不能将旧接触过滤耗时变化归给这项采样修改。

#### 1280×800正式三视图

[新分层采样](../../build/wall_transition_final.log) 与 [旧中点配置](../../build/wall_transition_legacy_final.log) 均已完成 corner_close / corner_wide / corner_lamp 三个1280×800视图。各视图预热24帧、检查8个静止帧，保留总光、带阴影直接光、无面积阴影直接光及上述独立GI阶段。总光对照可见原先宽平行台阶明显减少，墙顶过渡更连续；这属于实际渲染图的观察，没有把空间高通能量当作相对真值误差或声称所有随机颗粒消失。

高分辨率同机位总图：[旧中点](../../build/wall_transition_legacy_final_corner_lamp_total_renderer_ldr.png) · [新分层](../../build/wall_transition_final_corner_lamp_total_renderer_ldr.png)；直接光对照：[旧阴影](../../build/wall_transition_legacy_final_corner_lamp_direct_area_shadows_ldr.png) · [新阴影](../../build/wall_transition_final_corner_lamp_direct_area_shadows_ldr.png)。

两批缓存断言全部通过：atlas字节数均为 **25,165,824（24 MiB）**；改变采样模式会重建同尺寸atlas并清除GI历史，下一帧稳定复用；运行时关闭 / 重新启用面积阴影也复用兼容atlas。诊断仍不包含独立高采样遮挡atlas真值，CPU采样oracle的改善比例不作为这些GPU图的误差下降比例。

#### 仅改变源点分布的GPU开销

RTX 4060 Laptop、Debug、Vulkan软件射线、corner_lamp视角，测量关闭validation、呈现及图像回读。下表为完整帧GPU时间；相同射线和atlas预算、相同接触GI设置，每配置64帧，按上文四对批次计算差值及95%区间：[1280×800日志](../../build/wall_transition_perf_1280.log)、[2560×1440日志](../../build/wall_transition_perf_2560.log)。静止复用atlas，切换模式后重建atlas与初次卡片重照明的成本位于预热阶段，不包含在稳态均值中。

| 分辨率 / 显示模式 | 旧中点均值 ms | 新分层均值 ms | 新减旧 ms | 配对95%区间 ms |
| --- | ---: | ---: | ---: | --- |
| 1280×800 / GI开启 | 161.170 | 162.237 | +1.067 | [-3.876, 6.010] |
| 1280×800 / 仅直接光 | 3.988 | 4.243 | +0.256 | [-0.056, 0.567] |
| 2560×1440 / GI开启 | 449.073 | 451.640 | +2.567 | [0.262, 4.873] |
| 2560×1440 / 仅直接光 | 13.642 | 14.189 | +0.547 | [-0.344, 1.438] |

2560×1440、GI开启的本批差值约 **+0.572%**，区间未跨零，支持存在小幅稳态额外成本。其余三项区间跨零，当前批次未充分区分变化与批间波动，不能据此称为“无成本”。这是源点分布修改的配对整帧开销，未增加过滤组，不能与前两轮独立接触过滤开销混算。GI开启的绝对耗时仍高，以上离屏GPU时间不代表全屏交互FPS，也不能直接外推其他视角、Release构建或硬件光追性能。

#### 当前回归范围

[Vulkan灯框回归](../../build/wall_transition_lamp_vk.log) 已通过480×300、3视图、13配置、256-spp共享Surface Cache参考，`improve_assertions=1`；六项参数历史重置、灯强线性和关灯零能量检查均通过，后两项最大RGB误差为0。此项防止源点修改破坏原有接触修正，仍不是路径追踪真值。

[OpenGL灯框结构回归](../../build/wall_transition_lamp_gl.log) 已通过320×200、3视图、13配置，`reference_spp=0`、`improve_assertions=0`；四帧小幅运动复用254,391个pixel-frame，六项参数历史重置、灯强线性和关灯检查通过。此批没有独立高采样参考，直接光RGB8读取也全为0，因此只报告结构兼容通过，不能据其宣称OpenGL直接阴影画质与Vulkan等价。上方独立面积灯GPU单元另在真实OpenGL积分中验证了两种采样模式与正负4/8编码。

## 2026-10-19：椭球 / 方柱残噪与独立 GPU 计时

**历史范围：** 本节与下一节保留前两轮接触 GI 修改的记录，使用当时的8×8中点面积灯配置；这些改善比例和绝对耗时不能直接作为上方分层源点版本的当前验收。

上一轮椭球 / 方柱的“最终空间高通残差下降 5.66%”是相对一张有限采样参考图的指标，并不表示最终图像只减少了同样比例的可见颗粒。旧 1024-spp 参考的半图差表明，6–35 cm 外带的参考噪声已经占据最终高通残差的大部分；旧逐像素参考还使用 `p+n*rayBias`，与生产补追的源三角形锚定 / 间隙限幅起点不同。继续比较这个百分比，容易把参考噪声或起点差异当成降噪瓶颈。下方保留上一轮原始记录；本轮以相同安全起点的独立多 seed 参考、参考噪声底和接触剖面共同评估。

新的默认路径保留独立 R2 接触序列和每个选中像素 8 条射线，增加以下处理：

- **补上探针遗漏的接收点接触风险。** 原探针短命中 / 视差风险之外，从每个可见接收点沿 8 个屏幕方向检查 6 环位置，半径为 1、2、4、8、16、32 像素。只考察世界距离小于 `Contact risk distance` 的有效几何，用法线变化与中点切平面残差识别不连续近面；两面同时处于对方背半空间的凸边候选跳过。距离项在风险距离 D 的 15% 以内保持满权，从 15% 至 100% 以 `1-smoothstep(0.15D,D,d)` 宽范围渐变至零；默认 D=10 cm 时，核心 1.5 cm 内距离项为 1，之后平滑衰减到 10 cm，避免探针与真实接收点修正结果在选区边缘突然切换。它仅决定额外发射真实接收点射线，不根据亮度选择或借用邻面光照。屏外和被完全遮住的邻面不能靠该屏幕补充检测发现，原探针风险和完整场景补追仍保留。
- **用同表面的软风险边缘填补小孔。** 增加一个 3×3 几何兼容 mask 扩展 pass，在已验证的同表面邻点中取 `max(center,0.5*axisNeighbors,0.25*diagonalNeighbors)`。它只提高周围弱风险，不降低接触核心；法线、反照率、中点切平面及世界距离检查阻止跨几何边界扩展。此步骤不读取或混合 RGB，每个新增接收点仍独立进行安全起点认证和真实射线追踪。原始 mask 暂存在既有 unfiltered 目标中，扩展后写入专用 mask，再由后续追踪覆盖临时内容，因此不增加纹理。
- **给曲面可用的过滤支持。** 三轮支持半径由 1 逐步增加为 2、4 像素，受 `Contact guided max radius` 限制。同一表面的弦位移以两端法线的中点切平面衡量，避免把椭球平滑曲率当成平面断裂；法线、反照率、世界距离、中心有效性和逐像素路径检查继续阻止跨边界取样。
- **扩大支持时保护近处接触。** 实际补追按完整射线预算累计几何命中距离倒数，包括黑色、背面和缓存未知命中，得到调和命中距离。新增的接收点命中距离倒数仅在 guided 模式累加，旧模式不承担这项统计开销。更远的过滤步长使用中心与邻点距离的较小值计算命中视差，默认上限 10°；远处光照不能因为距离长而压过近接触。每个方向遇到视差或几何拒绝时，只保留已经逐步验证的较近端点，至少保留合法的 1 像素支持，避免极近接触完全失去降噪。调和距离是本实验的标量适配，不是 UE 的逐方向命中图集。
- **先过滤实际测量，再混合一次。** 新增内部 measurement 图保存实际接收点 RGB，以及打包在有限浮点 alpha 中的 readable count / 调和距离。原始计数和距离在三轮中保持不变，最后一次才与探针基础信号按中心 mask 混合；公开的未过滤 / 过滤结果仍输出真实中心可读率。未知起点保持未知，已知黑色保留，完整射线归一化分母不变，mask 为零的像素严格保持基础信号。

相对上一轮三轮小核版本，增加一张全分辨率 RGBA32F measurement 图：1280×800 为 **15.625 MiB**，2560×1440 为 56.25 MiB；这不包含上一轮已经增加的 scratch 图。默认 contact 从旧版 6 个 pass 增为 **8 个**：几何风险、原始 mask、软风险 mask 扩展、接收点追踪、measurement resolve、三轮过滤。measurement resolve 纳入过滤计时，mask 扩展纳入 trace 计时。关闭过滤但保留新 mask 时执行 4 个 pass，全接收点参考模式执行 3 个 pass。每个选中像素的射线数仍为 8，但接收点风险检测及软扩展扩大选区，因此总射线数和分类开销可能增加，不能把“8 条不变”理解成整帧成本不变。

F1 的新增参数为 `Contact hit-distance / curved filter`、`Receiver screen contact detection`、`Contact guided max radius`（默认 4）和 `Contact filter hit angle (degrees)`（默认 10）。关闭前两个开关可比较上一轮三轮过滤；独立接触序列和连续覆盖率过滤仍保持开启。新参数全部进入 GI 历史兼容键；`referenceReceiver` 是质量专项使用的全接收点安全起点参考模式。

参考的本机 UE 5.7.1 实现如下，接收点风险掩码和标量曲率过滤是本实验的适配，不是逐行移植：

- [LumenScreenProbeGather.cpp:294](D:/UE5/engine/UE_5.7/Engine/Source/Runtime/Renderer/Private/Lumen/LumenScreenProbeGather.cpp:294)：明确将短程全分辨率 AO 用于补充降采样 Screen Probes 缺少的高频接触遮蔽。
- [LumenShortRangeAOHardwareRayTracing.usf:47](D:/UE5/engine/UE_5.7/Engine/Shaders/Private/Lumen/LumenShortRangeAOHardwareRayTracing.usf:47) 读取实际接收点材质坐标、深度和法线；[同文件:83](D:/UE5/engine/UE_5.7/Engine/Shaders/Private/Lumen/LumenShortRangeAOHardwareRayTracing.usf:83) 从接收点位置沿余弦半球发射局部可见性射线。这里借鉴接收点补充近处细节的职责，实际实现仍追踪完整 GI 距离并读取 Surface Cache 辐射率。
- [LumenScreenSpaceBentNormal.usf:637](D:/UE5/engine/UE_5.7/Engine/Shaders/Private/Lumen/LumenScreenSpaceBentNormal.usf:637) 是屏幕短程 AO 入口，[同文件:675](D:/UE5/engine/UE_5.7/Engine/Shaders/Private/Lumen/LumenScreenSpaceBentNormal.usf:675) 调用接收点 horizon 搜索，[同文件:693](D:/UE5/engine/UE_5.7/Engine/Shaders/Private/Lumen/LumenScreenSpaceBentNormal.usf:693) 写出可选 ShortRangeGI。
- [LumenScreenProbeFiltering.usf:358](D:/UE5/engine/UE_5.7/Engine/Shaders/Private/Lumen/LumenScreenProbeFiltering.usf:358) 将邻点命中距离限制为不超过中心命中距离，再计算视差角，防止远处光照覆盖接触阴影。[LumenScreenProbeGatherTemporal.usf:629](D:/UE5/engine/UE_5.7/Engine/Shaders/Private/Lumen/LumenScreenProbeGatherTemporal.usf:629) 分别混合短程 AO / GI 历史。本实验继续将补追后的完整 RGB 信号送入既有几何验证时域，没有移植完整 UE Short Range AO / GI 管线。

### 曲面接触质量专项（最终八 pass）

```text
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --procedural --surface-cache-curved-contact-test --surface-cache-curved-contact-view 0 --surface-cache-curved-contact-reference-seeds 128 --render-width 1280 --render-height 800 --screenshot build\curved_contact_final.png
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --procedural --surface-cache-curved-contact-test --surface-cache-curved-contact-reference-seeds 32 --render-width 640 --render-height 400 --screenshot build\curved_contact_views.png
```

`--surface-cache-curved-contact-view` 可指定 0（objects）、1（near）、2（detail），默认 3 覆盖三个固定视图。参考 seed 支持 32 / 64 / 128，对应每 seed 32 条射线、共 1024 / 2048 / 4096 spp；参考关闭空间与时域过滤，使用生产补追同一套源锚定 / 间隙限幅起点和完整距离追踪。它共享单次反弹 Surface Cache，仍不是路径追踪真值。参考完全不可用的接收点从质量 ROI 排除并单独统计；这不影响全屏未知中心、alpha 和 mask 外不变检查，部分未知射线仍保留完整分母。

每个视图分别在椭球和方柱侧按真实几何距离统计 0–1、1–3、3–6、6–15、15–35 cm 五档；椭球外部距离求解析椭球的最近点，避免按最小半径缩放球 SDF 把长轴处厘米档划错。对照为上一轮三轮过滤和新默认，两者均使用独立接触方向、连续覆盖率和相同探针相位。每种配置预热 12 帧、测量 12 帧，分别报告未过滤、当前帧过滤和最终时域结果的总 / 误亮 / 误暗 MSE、偏差、残差高通、时间方差，以及 selected / unselected 子集。两种配置选区可以不同；仅在 mask 相同的像素检查实际接收点射线结果一致，同时要求上游探针 GI 哈希完全一致。

参考平均图由两个独立的等规模 seed 集合组成。两半均值之差的平方误差乘 1/4，估计完整参考的噪声 MSE；对同一兼容邻域做高通后再乘 1/4，估计参考高通噪声底。日志同时保留 raw 和扣除该噪声底后的 corrected 指标；`corrected_*_identifiable` 仅在旧误差高于估计噪声底 1.25 倍时为 1。接近参考噪声底的 corrected 比率不稳定，标记不支持识别时不能据此宣称降噪百分比，也不能把减法后的数值当作确定的路径追踪误差。

`paired_half_reference` 另用两个独立半参考分别比较新旧结果：由完整均值加 / 减半个两半差值重建各半参考，在同一几何 ROI、同一高通算子下报告当前帧过滤和最终时域阶段的 MSE / HP MSE，以及 `mse_after_minus_before` / `hp_after_minus_before` 配对差值。每个配对使用相同参考，其共同噪声底在期望差值中抵消；这组日志用于检查回退方向及其对半参考的敏感性。不能仅因参考噪声底高就推断配对差不显著，应同时查看两个独立半参考下差值的方向和幅度。

细节检查比较两侧 0–3 cm 接触核心亮度和 15–35 cm 外带的对比度。核心校验以同安全起点的共享缓存参考为准：限制新增过亮误差 `max(0,new-reference)-max(0,old-reference)`，同时限制绝对核心误差增长 `abs(new-reference)-abs(old-reference)`，避免把原先过暗的结果向参考纠正误判为接触细节丢失，也阻止继续压暗核心来通过检查。两项容差仍为 `0.03*max(referenceNear,abs(referenceContrast))`，原对比度 guard 及聚合 MSE / HP 的 1.02 上限保持不变；日志额外记录新旧绝对核心误差与过亮误差增量。专项另外检查每像素中心 alpha、未知中心、mask 外严格不变、参数变更清除历史、灯强线性、关灯零能量及小幅运动历史复用；运动检查只验证有限值和历史可用性，没有量化运动中的真值误差或拖影。

#### 1280×800、objects、4096 spp

包含软风险 mask 扩展的最终八 pass 配置已通过该专项，见 [最终高采样质量日志](../../build/curved_contact_padded_final.log)。比较上一轮配置与最终配置，在同一几何定义的 0–35 cm 接收区域测量，预热 12 帧后评估 12 个静止帧。下表报告未扣除参考噪声底的 raw 指标：

同视角总图：[上一轮配置](../../build/curved_contact_padded_final_objects_previous_total.png) · [最终八 pass 配置](../../build/curved_contact_padded_final_objects_improved_total.png)。

| 阶段 | 亮度 MSE 下降 | 残差 HP MSE 下降 | 静止帧间方差下降 |
| --- | ---: | ---: | ---: |
| 当前帧过滤 | 33.47% | 48.11% | 23.45% |
| 最终时域 | 36.65% | 2.19% | 26.93% |

最终静态高频残差是温和改善，不能将当前帧过滤的 48.11% 当作最终图像的降噪幅度。最终 `corrected_hp_ratio=0.799621`，但估计参考 HP 噪声底占旧误差的 **89.0865%**，`corrected_hp_identifiable=0`，不能据此宣称约 20% 的最终静态降噪。两个独立 2048-spp 半参考的最终配对 HP 差值都为负，分别为 `-8.91799e-9` 与 `-1.17831e-8`，支持温和改善的方向。

接触核心与外带的定义同上，数值为线性亮度。椭球核心向参考靠近，接触剖面对比也更接近参考：

| 接收侧 | 核心亮度：旧 → 新（参考） | 核心绝对误差：旧 → 新 | 外带减核心对比：旧 → 新（参考） |
| --- | --- | --- | --- |
| 椭球 | 0.0486003 → 0.0511120（0.0518745） | 0.00327424 → 0.000762518 | 0.0447392 → 0.0422036（0.0408870） |
| 方柱 | 0.0379425 → 0.0379160（0.0378963） | 0.0000462624 → 0.0000197526 | 0.0137564 → 0.0137691（0.0139664） |

两侧核心误差及对比度 guard、中心 alpha、22,079,285 个 mask-zero pixel-frame 不变、144 个未知中心 pixel-frame 保持未知、小幅运动历史复用和五项参数历史重置均通过。安全参考完全不可用的 3 个方柱接收点已从质量 ROI 排除，未计作黑色真值；灯强加倍最大 RGB 误差和关灯最大 RGB 均为零。

#### 640×400、三视图、1024 spp

相同最终八 pass 配置也已通过三个视图专项，见 [最终三视图质量日志](../../build/curved_contact_padded_views.log)。下表比较上一轮配置与最终配置，统计同一几何定义的 0–35 cm 接收区域，使用未扣除参考噪声底的 raw 指标；HP 为残差高通平方误差，时域方差来自预热后的 12 个静止帧：

| 视图 | 当前过滤 HP MSE 下降 | 当前过滤时域方差下降 | 最终 MSE 下降 | 最终 HP MSE 下降 | 最终时域方差下降 |
| --- | ---: | ---: | ---: | ---: | ---: |
| objects | 28.54% | 28.11% | 11.87% | 0.41% | 30.90% |
| near | 42.17% | 30.79% | 0.53% | 1.52% | 34.76% |
| detail | 30.03% | 18.18% | 1.19% | 1.62% | 23.38% |

当前帧过滤的空间高通误差下降约 **29%–42%**，最终静止帧间方差下降约 **23%–35%**；最终静态 raw HP MSE 只下降 **0.41%–1.62%**，不能把当前帧过滤的改善幅度作为最终空间残噪的改善幅度。三个视图的两个独立 512-spp 半参考都给出最终 HP 下降方向，例如 objects 的配对 HP 差值为 `-1.30462e-8` 与 `-1.73670e-9`。最终 HP 误差中估计参考噪声底占比为 87.4%–97.8%，三个视图均标记 `corrected_hp_identifiable=0`；扣除噪声底后的百分比不稳定，应与上方高采样单视图记录分别解读，本表只报告上述 raw 结果和配对变化方向。

接触核心为 0–3 cm，剖面对比度为 15–35 cm 外带减核心，数值均为线性亮度。这里只列实际生成完整剖面日志的项目，near 椭球侧及 detail 视图未产生这项完整剖面统计：

| 视图 / 接收侧 | 核心亮度：旧 → 新（参考） | 外带减核心对比：旧 → 新（参考） |
| --- | --- | --- |
| objects / 椭球 | 0.063629 → 0.050808（0.052345） | 0.029980 → 0.042756（0.040481） |
| objects / 方柱 | 0.038556 → 0.038197（0.037880） | 0.012247 → 0.012562（0.013984） |
| near / 方柱 | 0.039791 → 0.039738（0.039722） | 0.014623 → 0.014673（0.014592） |

上述已有统计的核心绝对误差均下降，objects 椭球的核心绝对误差从 0.0112847 降至 0.00153652，暗接触剖面对比得到保留。全专项的中心 alpha、mask 外不变、60 个未知中心 pixel-frame 保持未知、小幅运动历史复用及五项参数历史重置检查通过；灯强加倍最大 RGB 误差与关灯最大 RGB 均为零。本次参考 ROI 未排除未知接收点，更新后的参考核心误差 guard 通过。

1280×800 的高采样 objects 结果与 640×400 的三视图结果互为补充，后者额外覆盖 near / detail；两组记录都对应最终八 pass 配置。独立性能结果见下节，上一轮质量记录完整保留在历史章节。

#### CPU 与跨后端回归

- CPU 的 `surface_cache_test` 与 `rhi_frame_command_buffer_test` 均通过，分别见 [Surface Cache 设置 / 有效性验证](../../build/curved_contact_cpu.log) 和 [RHI 命令缓冲测试日志](../../build/curved_contact_frame_cpu.log)；后者成功退出时不输出文本，退出码为 0。
- [Vulkan 灯框专项](../../build/curved_contact_padded_lamp_vk.log) 通过：480×300、三个视图、13 个配置、256-spp 参考，`improve_assertions=1`；六项参数历史重置、灯强线性和灯灭检查均通过，最大 RGB 误差 / 灯灭值均为 0。
- [OpenGL 灯框兼容专项](../../build/curved_contact_padded_lamp_gl.log) 通过：320×200、三个视图、13 个配置，`reference_spp=0`、`improve_assertions=0`，仅作结构与兼容验证，不据此宣称 OpenGL 质量改善百分比；六项参数历史重置、线性误差 0、灯灭值 0 均通过。
- OpenGL GLSL 将两处 helper 的保留字形参 `packed` 重命名为 `encoded`，修复编译兼容，算法与 UBO 布局未变。[重命名后 Vulkan 曲面烟测](../../build/curved_contact_glfix_vk_smoke.log) 亦通过：320×200、objects、1024 spp，五项参数历史重置、线性误差 0、灯灭值 0；已验收的高采样质量及独立性能算法保持一致。

### 曲面接触性能专项（最终八 pass）

```text
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --software-rays --procedural --surface-cache-curved-contact-performance-test --surface-cache-curved-contact-view 0 --render-width 1280 --render-height 800
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --software-rays --procedural --surface-cache-curved-contact-performance-test --surface-cache-curved-contact-view 0 --render-width 2560 --render-height 1440
```

性能专项禁止 `--validation`，关闭呈现、AA、UI 和图像 / 诊断计数回读。三个配置为新掩码但关闭过滤（`filter_off`）、上一轮三轮过滤（`previous`）、新默认（`improved`）；关闭过滤时直接采用未过滤结果，没有仅为对照保留的复制 / resolve pass，过滤 scope 为空。三个配置驻留同进程，六轮分别采用不同顺序，每个批次预热 8 帧、计时 16 帧，每种配置共 96 个 GPU 样本。性能专项一次测一个视图，完整视图比较应分别指定 0 / 1 / 2。

同时记录三种 GPU 时间：整帧；contact trace 组（探针几何风险、接收点 mask 分类、同表面软风险扩展及实际接收点追踪）；contact filter 组（新增 measurement resolve 和三轮过滤）。两组独立时间戳都在 `EndRendering()` 之后写入，起止均采用 `BOTTOM_OF_PIPE`，包含对应最终 attachment STORE，并避免把尚未完成的生产者工作归给下一组；没有新增提交或计时用 draw。分组 query 只由 `MeasureFrames` 显式启用，普通交互帧不插入这些标记。Vulkan 使用当前队列的 `timestampValidBits` 处理差值回绕，乘设备 `timestampPeriod` 转为毫秒；每帧重置可用性，仅读取已写入的 query 对，并校验返回的 frameIndex，不能复用旧帧结果。

日志报告每组样本的均值、中位数、P10 / P90，以及同轮新旧批次差值和 4096 次重采样的 bootstrap 95% 区间。filter 组差值反映过滤组耗时（含 resolve）的变化，trace 组反映分类及追踪覆盖组的变化；它们是运行管线中的分组时间跨度，并非独占执行时间，不能直接称为端到端整帧净成本。完整方案的变化应看整帧配对差值。相同每像素射线预算不代表相同选区或总追踪成本。整帧变化若落在批间波动范围内，应保留不确定性，不能据此宣称新增过滤免费。

#### 1280×800、objects

最终八 pass 配置的性能专项已通过，见 [最终 1280×800 性能日志](../../build/curved_contact_padded_perf_1280.log)。设备为 RTX 4060 Laptop GPU，Vulkan 软件射线、Debug、objects 视角；validation、呈现、UI 和图像回读关闭。每种配置记录 96 个当前完成帧的有效时间戳，六轮批次配对。下表单位均为 ms，括号为逐帧 P10–P90：

| 配置 | 整帧均值（P10–P90） | trace 组均值（P10–P90） | 过滤组耗时（含 resolve）：均值（P10–P90） |
| --- | ---: | ---: | ---: |
| 新掩码，过滤关闭 | 231.424（210.242–246.353） | 82.120（74.610–88.860） | 0.002784（0.001024–0.004608） |
| 上一轮三轮过滤 | 218.051（199.216–232.474） | 62.433（57.476–67.711） | 6.103（3.150–10.566） |
| 最终八 pass | 232.372（209.813–249.621） | 81.255（75.330–87.665） | 3.148（1.935–4.466） |

同轮配对差值均为“最终八 pass 配置减对照”，单位为 ms；区间来自六组批次配对的 bootstrap 95% 区间：

| 对照 | 范围 | 配对均值差 | 95% 区间 |
| --- | --- | ---: | ---: |
| 新掩码，过滤关闭 | 整帧 | +0.948218 | [-0.967334, 2.89268] |
| 新掩码，过滤关闭 | trace 组 | -0.865973 | [-1.63289, -0.207936] |
| 新掩码，过滤关闭 | 过滤组（含 resolve） | +3.14571 | [2.73043, 3.70549] |
| 上一轮三轮过滤 | 整帧 | +14.3209 | [9.0954, 18.9052] |
| 上一轮三轮过滤 | trace 组 | +18.8218 | [17.4607, 20.1999] |
| 上一轮三轮过滤 | 过滤组（含 resolve） | -2.95402 | [-3.54942, -2.28514] |

最终过滤组（含 resolve）平均 **3.14849 ms**，相对完全关闭过滤的同组区间增加 **3.14571 ms**；关闭配置没有过滤 draw，约 0.0028 ms 是这次空时间戳范围的基线。这一分组差值不能称为实际整帧净开销：新掩码下开启过滤的整帧差为 **+0.948218 ms**，95% 区间 **[-0.967334, 2.89268]** 跨零，当前批次不足以可靠量出该端到端差值，也不能据此宣称过滤免费。

相对上一轮三轮过滤，最终过滤组少约 **2.954 ms**，但包含软风险分类和扩大真实接收点追踪的完整方案，整帧增加约 **14.321 ms**，95% 区间 **[9.095, 18.905]**；trace 组增加约 **18.822 ms**。整帧还包含其他阶段，分组跨度不是独占执行时间，不要求等于两个局部分组差值之和。

#### 2560×1440、objects

最终八 pass 配置的高分辨率性能专项也已通过，见 [最终 2560×1440 性能日志](../../build/curved_contact_padded_perf_2560.log)。设备、Debug / Vulkan 软件射线、关闭 validation / 呈现 / 回读及六组批次配对方法与 1280×800 相同，每种配置均有 96 个当前完成帧的有效样本。下表单位为 ms，括号为逐帧 P10–P90：

| 配置 | 整帧均值（P10–P90） | trace 组均值（P10–P90） | 过滤组耗时（含 resolve）：均值（P10–P90） |
| --- | ---: | ---: | ---: |
| 新掩码，过滤关闭 | 760.910（708.892–818.957） | 350.826（333.577–364.558） | 0.00170667（0–0.004096） |
| 上一轮三轮过滤 | 565.943（515.249–619.745） | 152.043（139.226–164.778） | 6.153（3.400–9.524） |
| 最终八 pass | 785.184（719.111–842.705） | 353.961（337.898–372.638） | 13.184（9.137–17.356） |

同轮配对差值仍为“最终八 pass 配置减对照”，单位为 ms：

| 对照 | 范围 | 配对均值差 | bootstrap 95% 区间 |
| --- | --- | ---: | ---: |
| 新掩码，过滤关闭 | 整帧 | +24.2738 | [16.0499, 33.7727] |
| 新掩码，过滤关闭 | trace 组 | +3.13553 | [0.0241173, 6.66724] |
| 新掩码，过滤关闭 | 过滤组（含 resolve） | +13.1823 | [11.0089, 15.3065] |
| 上一轮三轮过滤 | 整帧 | +219.241 | [209.006, 230.878] |
| 上一轮三轮过滤 | trace 组 | +201.919 | [197.708, 205.327] |
| 上一轮三轮过滤 | 过滤组（含 resolve） | +7.03082 | [5.28826, 8.77338] |

2560×1440 下最终过滤组（含 resolve）平均 **13.184 ms**，相对关闭过滤的同组跨度增加 **13.1823 ms**。相同新 mask 下开启过滤的实际整帧差为 **+24.2738 ms**，95% 区间 **[16.0499, 33.7727]** 全部为正；1280×800 的整帧差不确定结论不能概括到这个分辨率。

完整方案相对上一轮由 **565.943 ms** 增至 **785.184 ms**，增加 **219.241 ms（约 38.74%）**，95% 区间 **[209.006, 230.878]**。分组跨度中，trace 组增加 **201.919 ms**，明显大于过滤组增加的 **7.031 ms**，新增成本主要表现在接触检测与实际接收点补追组。分组耗时仍非独占执行时间，也不能称过滤免费。

上述2560×1440数据是显式指定尺寸、固定 objects 视角的高分辨率离屏专项，并非本场景默认启动配置或交互呈现帧率。本场景应使用 `RunSealedGiRoom.cmd`，默认1280×800；`RunGiRoom.cmd` 的2560×1440配置属于另一条旧 `--gi-room` / lumenGi路径。两种分辨率必须分别使用实测结果，不能按像素数线性外推。

## 历史：2026-10-19 接触补追区域的白噪声条带

墙角、天花板交界及椭球与方柱接触附近的颗粒条带，来自实际接收点补追的低采样信号。探针的三轮方向辐射率过滤发生在补追之前，不能过滤随后写入的 8 条/像素补追结果。旧补追仅经过一次小核过滤，还直接复用探针的 8 帧方向相位；静止后反复采到相同方向，形成固定的逐像素误差。`maxFrames=10` 是有限历史 EMA，继续等待不等于无限增加独立样本。

当前保留默认 8 条补追射线，改进如下：

- 补追使用独立的逐像素扰动及固定点 R2 二维时域位移，保持每帧径向分层。仅成功提交的 GI 帧推进序列；参数变更、camera cut 或历史失效从相位零重启。探针本身的方向相位与原始 GI 不变。
- 补追后默认执行三轮小核过滤，再进入原有时域。每一轮仍检查法线、反照率、双向切平面、世界距离和逐像素路径，不跨几何边界取样。只增加一张全分辨率 RGBA32F 暂存图，1280×800 约 15.63 MiB，2560×1440 约 56.25 MiB。默认 contact 由 4 个 pass 变为 6 个，射线预算不增加。
- 用连续的可读率置信权重取代相邻 alpha 必须在 0.001 内相等的条件。8 条射线的一条未知样本会让 alpha 相差 0.125，补追 mask 与探针结果混合也会连续改变 alpha；这不应被当作几何边界。未知中心与未知邻居仍不参与恢复，每轮原样保留中心 alpha，不改变射线归一化分母或丢弃已知黑色。

参考本机 UE 5.7.1 的 `LumenScreenProbeFiltering.cpp:18–45` 的三轮小核过滤，以及 `LumenScreenProbeFiltering.usf:355–368,446–462` 对接触遮挡和低时域支持的处理。这里对完整 RGB 补追积分做过滤，属于本实验的适配，未移植 UE 的逐方向 hit-distance 过滤或完整 Short Range GI；不能把这一局部修正称作完整 UE 降噪器。

新增 F1 参数为 `Contact current filter passes`（1–3，默认 3）、`Independent contact ray sequence` 和 `Contact coverage-weighted filter`（均默认开），三个参数均进入 GI 历史兼容键。将三者分别设为 1 / 关 / 关可恢复旧补追过滤和方向序列，便于同进程比较。

专项入口：

```text
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --procedural --surface-cache-contact-noise-test --render-width 1280 --render-height 800 --screenshot build\contact_noise_final.png
```

专项覆盖与反馈截图相同的绿/黄墙和天花板交界、椭球与方柱接触。区域由可见世界几何定义，包含距交界 0–6 cm、6–35 cm 和内部 40–95 cm；对照旧版、仅独立序列、当前默认。参考为每像素 32 seed × 32 射线（1024 spp），共享 Surface Cache，仍有采样误差与起点策略差异，不是路径追踪真值。每种配置预热 12 帧，评估其后 16 帧；分别记录总误差、误亮、误暗、去除参考真实变化后的空间高通残差及帧间方差。固定重复纹理可以有很低的帧间方差，因此不能仅凭这个指标判断画质。

专项同时核对上下游采样隔离、旧 8 相位确实重复而新序列不重复、中心 alpha、mask 外严格不变、小幅相机运动历史复用，并用无回读/无呈现的 ABBA 批次比较 GPU 耗时。灯框 fixture 另验证灯灭零能量、光强线性和新增参数清除历史。两个专项各有独立职责，接触 fixture 的有效区域不自动证明所有未知状态均被实际触发。

最终 1280×800、RTX 4060 Laptop、Debug、Vulkan validation、AA=None 结果见 `build/contact_noise_final.log`。下表比较旧配置与当前默认，范围均为距几何交界 0–35 cm 的同一组像素；百分比为相对 1024-spp 共享缓存参考的误差下降，不能等同于路径追踪误差或主观噪声幅度的同幅下降。

| 区域 | 当前补追过滤 MSE 下降 | 最终时域 MSE 下降 | 最终空间高通残差下降 |
| --- | ---: | ---: | ---: |
| 墙角 / 天花板 | 49.51% | 32.18% | 27.72% |
| 椭球 / 方柱接触 | 8.88% | 1.66% | 5.66% |

墙角最近 0–6 cm 的最终误亮平方误差下降 49.25%，曲面最近区域下降 0.22%；曲面该区域平均亮度偏差由 +0.001582 变为 +0.001644，仍有系统误差，不能宣称完全恢复真值。曲面 0–6 cm 与 6–35 cm 的帧间方差分别下降 31.13% 和 21.84%；墙角内部 40–95 cm 控制区逐项不变。两个视图各 16 个成熟帧的 32 / 48 个未知中心 pixel-frame 保持未知，所有像素中心 alpha 与 mask 外信号检查通过。小幅运动只验证历史复用和有限值，未量化运动中的参考误差或拖影。修正仍有残余噪声，较小分辨率的最近曲面区域也可能出现轻微参考误差波动，不能据此宣称所有视角都改善相同比例。

ABBA 整帧 GPU 毫秒为旧 157.723、新 165.339、新 161.261、旧 173.042；均值旧 165.383、新 163.300。批间波动明显大于均值差，不能据此宣称提速或没有性能成本，也无法可靠量出新增两轮过滤的净耗时。这是该墙角视图的整条管线计时，不是过滤 pass 的单独时间，不代表完整实时 GI 达到 60 FPS。

CPU 设置检查与 Vulkan 灯框回归分别见 `build/contact_noise_cpu.log`、`build/contact_noise_lamp.log`，灯框三视角继续优于纯探针重建，光强加倍最大 RGB 误差及灯灭最大 RGB 都为零，6 项参数历史重置检查通过。OpenGL 的高负载离屏预热会只等待 CPU 提交，连续排队 12 个 32 射线帧后再回读，曾触发 10 秒回读超时；兼容 fixture 改为默认 320×200，并在每个 GL 帧后实际回读确认 GPU 完成。最终 `build/contact_noise_opengl_final.log` 的三视角、13 配置结构回归通过，包含 32 射线、光强线性、灯灭和历史重置。Vulkan 质量 fixture、生产帧提交及上述 1280×800 数据不受这一测试调度调整影响。OpenGL 仅作结构检查，不据此宣称与 Vulkan 具有相同参考精度或性能。

## 2026-10-19：灯框接触阴影被探针重建扩散

仰视灯框的黑色暗瓣主要是空间表示误差，而非缺失探针或随机方向不足。实测一个位于灯框外约 0.45 mm 的天花板探针，64 条射线中 31 条在 10 cm 内命中黑框，入射积分约 0.07995；这些是可读取、起点有效的真实黑色遮挡。错误发生在把这一窄接触阴影借给相隔数厘米的同平面像素。法线与切平面深度权重无法区分这种局部半球可见性变化。继续增加该探针的射线，只会更稳定地保持原来的暗值。

另外，天花板底面与灯框底面法线相同、仅相差约 3 cm，按视深归一化的几何容差会允许跨层共享。粗网格原来的扩展双线性公式也会在切换单元时留下小幅权重跳变，但单独修正它对这次暗瓣收益有限。

当前处理如下：

- 基础探针按真实像素结点插值，包括被视口裁剪的末行/列；自适应探针继续使用实际位置 B2 核。`Continuous probe interpolation` 可关闭作旧公式对照。
- 根据真实命中距离、像素到探针的切向距离及原重建权重，识别无法可靠共享接触照明的区域。短命中倒数按全部射线预算平均，不根据 RGB 或“哪个探针更亮”选择样本。
- 平行层识别使用像素尺度容差，并给曲面法线变化留出几何余量；发现混层风险时请求实际接收点追踪，而非删除较暗探针。
- 在风险区域的真实 GBuffer 表面位置重新进行完整距离的余弦半球 GI 追踪。起点仍经过源三角形锚定和法线方向间隙限幅；合法黑色与未知射线均保留完整分母。
- 补追后增加小半径保边过滤，再进入原有时域累积。检查法线、反照率、双向切平面、世界距离及采样路径，不跨过不兼容几何。未选择的像素保持原信号，未知中心不借邻居补亮，可读率保留中心值。

这是本实验对有限探针分辨率的局部修正，不是 UE 普通 ScreenProbeGather 的逐行移植。UE 5.7.1 的 `LumenScreenProbeGather.usf:1153–1178` 对重建查询使用蓝噪声抖动及切平面验证；`LumenScreenProbeFiltering.usf:356–368` 在静止区域仍保留近命中角限制。查询抖动不能撤销已经扩散的窄阴影；再乘 Short Range AO 也不能恢复被错误压暗的像素，因此这里没有靠删除近黑色命中或叠加 AO 补偿。

F1 的 **GI contact reconstruction** 可调：

| 参数 | 默认 | 作用 |
| --- | --- | --- |
| Actual receiver contact rays | 开 | 关闭可恢复纯探针重建，省去当前默认八个 contact pass。 |
| Contact rays per pixel | 8，范围 8–32 | 每个选中像素的真实半球射线数；增加可降低细颗粒方差，但开销较高。 |
| Contact risk distance | 0.1 世界单位 | 识别短程几何风险的距离；**不截断补追射线**，补追仍使用总 GI 的 Ray distance。增大可能扩大追踪区域。 |
| Contact parallax angle | 10° | 位置偏移相对命中距离的容限；减小会更保守、追踪更多像素。 |
| Contact risky weight threshold | 0.05 | 原重建中危险贡献的占比阈值；减小扩大补追区域。 |
| Contact parallel layer validation | 开 | 对附近平行层的歧义增加实际接收点追踪。 |
| Contact current filter radius | 1，范围 1–2 | 首轮 / 非引导路径的支持半径；引导路径逐轮扩大至 guided max radius，每个方向逐像素检查，仍是中心与 8 个端点，不是完整方形核。 |
| Contact current filter strength | 1，范围 0–1 | 0 为严格保留未过滤补追信号；增大减少细颗粒，但可能软化像素尺度接触变化。 |
| Contact current filter passes | 3，范围 1–3 | 小核迭代次数；默认三轮，每轮都重新验证几何路径。 |
| Independent contact ray sequence | 开 | 使用独立于探针 8 相位循环的接触射线序列。 |
| Contact coverage-weighted filter | 开 | 用连续可读率置信权重支持同一表面邻居，中心可读率不变。 |
| Contact hit-distance / curved filter | 开 | 先滤实际接收点测量，用中点曲率兼容与近命中视差约束扩展支持，最后混合一次。 |
| Receiver screen contact detection | 开 | 用接收点周围的近处几何不连续补充探针遗漏的接触风险。 |
| Contact guided max radius | 4，范围 1–4 | 引导路径三轮支持半径的上限，默认 1 / 2 / 4 像素。 |
| Contact filter hit angle (degrees) | 10°，范围 1–45° | 超过 1 像素的额外支持使用该近命中视差上限；更近的合法支持保留。 |

所有参数进入 GI 历史兼容键。原 `ReadbackSurfaceCacheRawGi`、`ReadbackSurfaceCacheSpatialGi` 保持纯探针阶段；新增 ContactProbeRisk、ContactMask、ContactUnfilteredGi、ContactGi 回读，最后一项为实际进入时域的修正信号。安全起点认证失败仍保留 unknown，不宣称所有表面均无覆盖缺口。

原 `surfaceCacheRays` / `surfaceCacheProbeRayBudget` 仍描述探针预算，不含新增的接收点补追。灯框测试另以有效 mask 像素数 × 补追射线数报告 `max_extra_rays`；起点认证失败会减少实际发射数量，不能把该上限当作精确活动射线计数。

验证入口：

```text
output\sdf-banquet\Debug\bin\material_lab_demo.exe --surface-cache-lamp-test --vulkan --validation --software-rays --procedural --render-width 1280 --render-height 800 --screenshot build\lamp_check.png
```

`build/lamp_filtered.log` 为 RTX 4060 Laptop、Debug、Vulkan validation、1280×800、最终 AA=None 的三视角对照。该批的 `baseline` 是 16 射线/0.3 距离高质量配置，`economy8_10cm` 是验收后选用的当前默认 8 射线/0.1 距离。参考为每像素 8 seed × 32 射线，共 256 spp；共享 Surface Cache，仍有蒙特卡洛误差，不是路径追踪真值。

| 灯框外 0–35 cm 天花板区域，线性 GI 亮度 MSE | 纯探针 | 当前默认配置 |
| --- | ---: | ---: |
| 视角 0 | 0.000361307 | 0.000205190 |
| 视角 1 | 0.000330591 | 0.000200628 |
| 视角 2 | 0.000339385 | 0.000199533 |

三视角总误差减少约 39%–43%，暗误差减少约 18%–28%。视角 0 中，距灯框 3–8 cm 的平均亮度偏差从 -0.00839 变为 -0.000177；距灯框 0–1 cm 的误亮从 +0.07744 减至 +0.01021，真实窄接触阴影得到保留。受测天花板区域未新增起点认证缺口；全图仍有 5–9 个选中像素认证失败，位于其他几何边界。常量诊断绕过真实起点有效性，只验证归一化；另行记录真实可读率，不能将两者混为无缺口证明。

该批旧默认配置的补追范围在三个视角分别约占全图 14.8%、12.7%、11.7%。同进程 ABBA、每批 4 个无呈现/无回读计时帧中，关闭 contact 约 101 ms GPU，旧 8 射线 / 单轮过滤配置约 173 ms，高质量配置约 244 ms。这是加入三轮过滤之前的首次接触修正记录，不能替代上面的当前默认验收。画质修正仍有明显开销，尚未达到完整实时 GI 的性能目标；这些是整条管线开关差值，不能称作没有性能代价。8 射线节省了开销，但细颗粒噪声没有完全消失，过滤也不能保证亚像素接触阴影完全无损。

旧探针、空间、方向、传输与时域单项 fixture 显式关闭 contact，以保留各自独立验证职责。灯框 fixture 单独检查原始探针信号不受开关影响、未选像素严格复制、中心可读率保持、光强线性、灯灭零能量、参数变更清除历史及小幅镜头运动的历史复用；运动检查不等于已量化运动中的参考误差或拖影。

## 2026-10-18：远处细边缘与运动重建噪声

固定像素间距的探针，在远处对应更大的世界空间间距；角落射线又容易很快命中附近几何。原过滤器只从相邻大网格选代表，在默认 10° 命中角限制下，许多候选不能参与。同一网格内虽有自适应探针，却没有进入此邻居表，所以近看干净、远看仍有细碎斑点。当前保留原 8 条远邻槽，再加入最多 8 条局部代表，按实际投影间距连续加权；不放宽命中角，不跳过 BVH，不按光照明暗选择探针。

物体移动原本会通过模型矩阵与缓存世代让整幅历史失效，墙面也从一帧重新累积。现在保存成功提交帧的实例值快照，只有场景、模型源、对象次序、材质、灯光与绘制结构都相同，且变换变化全部属于可移动物体时才保留兼容历史。移动接收面仍进行几何重投影验证，不兼容时拒绝历史；没有物体速度缓冲，细小或切向运动可能通过容差。物体还会影响远端墙面的间接阴影，因此没有只清除物体包围盒；当前对全图采用动态帧数上限与裁剪，并在停止后短暂继续响应。这比完整的逐射线光照依赖追踪粗糙，仍需检查拖影。

新暴露、重投影失败或移动边缘的历史很短，单靠裁剪旧历史不能消除当前帧的低采样噪声。新增低历史恢复在时域 Pass 内对当前 GI 做小范围双边过滤，检查法线、双向切平面距离、世界距离、反照率和可读性，沿采样路径逐像素拒绝跨薄遮挡借光。达到默认 4 帧后停止，不能把空间样本当成更多历史帧。它不会消除真正无历史区域所需的新采样，只能减轻显露过程中的噪声。

参考 UE 5.7.1 的区别：普通 ScreenProbeGather 在 `LumenScreenProbeGatherTemporal.usf:585–604` 根据运动光照比例缩短历史，并传播上一帧快速更新状态；`LumenScreenProbeFiltering.usf:351–375,448–463` 在时域减弱时增强空间过滤。当前没有 UE 的逐探针运动光照比例、对象速度与依赖缓冲，使用严格变换认证加保守全局响应。低历史当前帧双边恢复参考的是 `LumenReSTIRGather.usf:1577–1623` 的消遮挡/方差过滤及 `LumenReflectionDenoiserSpatial.usf:73–79` 的短历史空间恢复思想，属于本地适配，不是普通 ScreenProbeGather 的原样移植。UE 普通探针级 TemporalFilter 默认关闭，不能将缺少它作为本问题的唯一原因。

对照入口：`--surface-cache-recovery-test`，Vulkan、9 个姿态、每姿态 8 seed × 32 条逐像素射线参考，共 256 spp。参考共享 Surface Cache，只隔离探针重建及过滤误差，不能视为路径追踪真值。远近视图瞄准同一世界角落；另测相机连续移动、物体移动后的旧地面区域和远端静态接收面。物体对照固定一个方向相位以核对原始信号一致，不能用这段的帧差宣称生产 8 相位的噪声方差。灯灭零光、未知状态、历史实际复用和 ABBA 性能分别检查。直射图单独保存，避免把面积光源积分或阴影采样条带归因于 GI。

```text
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --surface-cache-recovery-test --screenshot build\gi_recovery.png
Module\MaterialLab\RunSurfaceCacheGi.cmd --surface-cache-no-local-neighbors
Module\MaterialLab\RunSurfaceCacheGi.cmd --surface-cache-no-young-recovery
Module\MaterialLab\RunSurfaceCacheGi.cmd --surface-cache-reset-on-object-motion
```

本轮验收记录：`build/gi_recovery_quality.log`，RTX 4060 Laptop、Debug、Vulkan validation、1280×800、间距 32、64 射线/探针、最终 AA=None。相机每帧转向 0.015 rad 并平移 0.05，共 6 帧；物体沿 X 移动 1.2，俯视相机能同时看见旧足迹、新暴露地面及远端静态面。除原 1 相位匹配射线消融外，另比较 8 相位、4 个基础 seed 的实际物体运动；后者相位会因旧版全清而不同，所以不宣称逐帧射线相同。

| 指标（线性 GI 亮度 MSE） | 原行为 → 当前 | 解读 |
| --- | ---: | --- |
| 远处静止角落区域 | 2.96437e-5 → 2.96940e-5 | 约增加 0.17%，没有显著改善，不能称为已消除远角残斑。 |
| 远视图相机运动，全图 | 9.73798e-5 → 9.65996e-5 | 约降低 0.80%。 |
| 近视图相机运动，全图 | 4.93439e-5 → 4.92885e-5 | 约降低 0.11%，边际收益。 |
| 物体运动，8 相位 / 4 seed，全图 | 7.30546e-5 → 7.07155e-5 | 约降低 3.20%。 |
| 同上，减去参考物理变化后的帧间残差 | 1.60568e-5 → 1.46631e-5 | 约降低 8.68%；参考本身仍有采样误差。 |

不能将平滑度单独当作正确性：同一物体测试的未经参考校正帧差从 3.27422e-5 增至 4.04614e-5，包含真实几何/光照变化和不同射线相位。旧足迹的总 MSE 从 3.09070e-5 降至 2.98009e-5，但暗误差从 1.27737e-5 增至 1.82037e-5；当前原始射线的暗误差也增加，不能把所有差异归因于历史拖影，也不能声称每个暗区都有改善。默认动态上限与低历史恢复阈值同为 4，成功保留为 4 帧的像素依赖探针空间过滤和时域，不额外执行当前帧恢复。全局动态判定仍粗糙，后续更精细的运动光照比例/依赖信息及速度重投影才能进一步减少不必要的历史限制。

关键保护检查：30 帧 A/B 原始信号完全匹配；相机连续 12 帧均复用历史；单相位物体 4 次运动全部保留历史，累计 2,273,520 个静态接收像素复用；137,488 条不可读取射线保留原状态；唯一灯光关闭后 raw / spatial / temporal RGB 均为零。`build/gi_recovery_temporal.log` 另验证失败提交不会推进值快照、移动和 3 帧停止期均限制为 4 帧、随后恢复长历史；`build/gi_recovery_spatial.log` 的独立双精度三角形可见性检查中，经过过滤的穿几何邻居数为 0。CPU 两项检查通过；OpenGL 的 `build/gi_recovery_opengl_structural.log` 通过几何重建及灯灭保护，不将该后端不可用的 GPU 时间戳当作性能通过。

同进程静态 ABBA 四批 GPU 耗时为 124.305 / 129.059 / 126.514 / 122.645 ms，原行为均值 123.475 ms，当前 127.787 ms，增加约 4.312 ms（3.49%）；每批预热 12 帧、计时 4 帧，无回读与呈现。它主要量到新增邻居表/探针过滤，成熟历史不会执行低历史恢复，不能代表运动恢复的全部开销。整套基础软件 GI 在此 Debug 配置仍未达到实时 60 FPS。

隔离截图 `build/gi_recovery_far_direct.png` 在关闭 GI 时仍有墙面宽条带，因此这部分属于直射/阴影路径。GI 独立图已较平滑，当前局部邻居收益很小。规则面积光积分、阴影采样和灯具遮挡应在直射路径独立改进，继续加大 GI 模糊无法修正这部分；本轮保留其诊断证据，没有宣称解决所有条带。

## 数据与更新

每个对象保留六个正交方向的单层深度卡片，固定常驻页，默认每页 32×32。对象共享同一网格的局部捕获结果，切换相机或 MDF 精度不重新捕获。每张卡片保存逆变换、投影范围、页地址、表面误差容差、对象身份和材质。图集分为位置/有效性、法线/卡片身份、线性漫反射率三个平面；直接出射辐射率另存。

卡片捕获使用网格已有的三角形 BVH。世界 BVH 排序后的三角形到对象的映射用于精确选择卡片，避免只凭重叠包围盒借用另一个对象的光照。图集查询先检查朝向、捕获有效性、深度和距离，确定覆盖锚点，再默认对同对象、同卡片的照明做双线性读取；各 tap 仍检查深度、法线与切平面，不跨卡片寻找更亮的值。`Geometry-aware cache bilinear` 可恢复原最近样本读取作对照。

对象或材质变化更新对应卡片；光源变化重新计算直接光照。移动遮挡物也会使缓存光照失效，以更新其他物体上的阴影。静止时复用已经确定的直接光照，不把新旧帧混合。光照缓存的复用属于更新调度，不是时域滤波。

单层六方向捕获无法覆盖深腔、内壳和完全被外层遮挡的表面。未覆盖表面在 Coverage / Cache 视图中标为洋红色，参与 GI 时贡献零能量；有效黑色与缺失覆盖分开。后续可增加自适应卡片和多层捕获，不能靠放宽深度容差掩盖缺失。

## 光传输边界

默认基础 GI 复用之前研究的屏幕自适应表面探针：从 GBuffer 放置基础探针，对缺少几何覆盖的深度边缘、薄物体和曲面补充探针；光线由探针发射，像素从兼容的表面探针重建间接光。逐像素软件 BVH 路径保留为可切换的参考。

缓存存储 `L_direct = emission + rho * E_direct / PI`。探针用余弦半球 PDF 积分，原标量 `m0 = E_indirect / PI = mean(L_hit)` 始终保留；默认另存方向系数，在接收面法线处求值后再按几何权重重建。不提前乘探针落点的材质反射率，最终像素只乘自己的 `rho_receiver`。这样红墙上的探针不会把红墙反射率再次带到白色接收面。可读取黑色、离开场景的射线仍保留在总样本数中；缺失卡片不贡献能量，也不通过缩小分母补亮。

探针射线经过屏幕 HZB 追踪，再用 Mesh / Global SDF 补充屏外命中，并从 Surface Cache 读取单次反弹光照。探针 GI 不从上一帧最终 GI 画面中取光；屏幕命中也转换为缓存查询，避免将曝光、镜面或显示用颜色反弹到场景。

重建保留四个基础探针角点，并汇集有限支持范围内所有几何兼容的自适应表面代表。自适应权重按探针真实投影位置计算正值 B2 核，结合双向平面距离、法线及默认开启的屏幕线段可见性；不会把覆盖修补点假设在名义子格中心，也不按每像素最近胜出集合切换。所有 RGB 与可读性 alpha 使用相同的正权重归一化。这属于从探针到接收面的光场重建，不是对已经产生的 GI 图像做降噪。

默认启用下文的探针空间过滤与 GI 时域累积；分别用 H / T 或 `--surface-cache-no-spatial` / `--surface-cache-no-temporal` 关闭。两项都关闭且放置抖动关闭时恢复固定 seed 的原始单帧结果。默认放置相位固定为 0，时域模式只循环改变射线方向 seed；`Temporal placement jitter` 是独立且默认关闭的实验开关。原始探针积分与像素重建结果仍单独保留，不从历史画面向场景反弹光照。天空、镜面反射、透明材质传输与多次反弹仍不属于此单次漫反射版本。

## 曲率分配与凹缝射线起点

当前 GI 默认启用几何曲率加密与每探针起点短扫掠。屏幕 tile 仍作索引，4 / 8 / 16 个预留槽与每探针方向数不变；曲率加密增加的是活跃代表，不保证实际 GPU 开销不变。它不会通过放宽法线、对称切平面或遮挡门限混合不同表面。

原先均匀探针已覆盖的连续表面，只在几何交线附近激活额外代表。现在复用子格扫描，检查相对代表的法线变化、对称切平面误差，以及粗探针支持的加权误差。归一化误差通过 `smoothstep(0.5,1.5,error)` 连续激活现有主代表槽，预算处贡献为一半，避免越过阈值时突然加入满权重的随机光照。覆盖修补与真实交线仍优先满足原义务。代表仍是实际 GBuffer 像素；没有将世界位置平均到曲面内部。B2 核形状与双表面容量保持原定义，重建与空间滤波邻居权重都乘同一激活度。曲率预算控制分配而非射线随机性，代表像素和几何分类仍可能随镜头变化。

F1 → GI 新参数：

| 参数 | 默认 | 用途 |
| --- | --- | --- |
| Refine surface curvature | 开 | 连续曲面即使已有粗覆盖，也按误差预算加密；需要 Refine geometric intersections 开启 |
| Curvature normal budget (degrees) | 10 | 法线变化预算；越低越容易加密 |
| Curvature plane budget (pixels) | 0.75 | 切平面误差预算，按该表面世界像素足迹换算；越低越容易加密 |
| Constrain probe ray origins | 开 | 每个活跃探针只做一次三角形 BVH 短扫掠，屏幕与世界追踪复用同一起点 |
| Origin clearance fraction | 0.5 | 沿法线偏移遇到邻面时，仅使用扣除数值余量后安全距离的该比例；范围 0.05–0.95 |
| Origin numeric clearance | 0.00002 | 邻面端点的数值退让；另有随世界坐标增长的浮点精度下限，与几何 ray bias 分离 |
| Show actual ray origins | 关 | 无深度测试的起点叠加：青绿未限制、橙色已限制、紫红近距离/背面不确定、红色异常。标记中心是实际追踪起点，没有普通探针小球的显示法线偏移 |

起点图集为每探针一个 RGBA32F，xyz 是起点，w 为 0 非活跃、1 完整数值校正后偏移、2 已限制、3 近距离/背面不确定。负数为不可读原因：−1 数据或遍历异常，−2 无源面候选，−3 锚点误差超限，−4 朝向不符，−5 外侧数值余量不足。`ReadbackSurfaceCacheProbeOrigins()` 只读本帧已生成的结果；功能关闭时不返回旧图集。先沿主视线在极短窗口确认真实源三角形，修正深度重建可能略入自身表面的误差，再从该锚点做法线扫掠；两次 BVH 查询均为每探针一次，所有方向复用。源面校正严格限制两个法向平面误差；沿视线的校正量按夹角调整，并限制在四分之一投影足迹内，避免掠射表面被等向距离门限误拒绝。锚点校验失败、遍历耗尽或无法同时满足源面外数值余量与邻面距离约束时保持不可读。atlas 表面位置与法线不变，w=1 不表示 xyz 与旧偏移逐位相同。短扫掠会限制偏移跨越可检测的邻面，**不等于完整闭合体内部认证**，也不会修复已穿插的模型。默认椭球与高方柱保持原布局，未通过移开模型隐藏问题。

上一轮 GBuffer 位置由 Depth32F 反解，设备深度的一次浮点步进在远处或掠射视角不对应固定的世界误差。该版起点校验把该设备深度相邻的两个可表示浮点值分别反投影，以它们相对已存位置的世界位移，估计当地的深度量化及投影往返误差。沿存储法线与真实源三角形法线分别投影这段位移，加入各自的平面误差预算；沿视线允许量仍受原四分之一表面像素足迹限制。它只调整源面锚点的局部接受精度，不放大 ray bias、外侧数值 clearance 或邻面扫掠距离，也不放宽 −4 的正背面验证。修复前的五路消融中，wall 静止四帧累计 152 个 −3 误拒绝、首次移动 22 个；修复后两项均为零，仍保留 4 / 1 个背向源面的 −4。下面的直接位置捕获进一步移除了探针源位置对设备深度反解的依赖。

安全 GI 的屏幕命中还会以屏幕距离为有限查询上界，验证最近三角形；按真实对象与几何正背面读取 Surface Cache。未确认命中则继续 SDF，遍历耗尽保持不可读。空间滤波的探针对遮挡检查复用同一安全起点；命中角度也从这两个实际起点计算，避免把由偏移起点测量的距离与未偏移表面位置混用。几何相容权重仍由原始接收表面的位置和法线决定。保留真实背面、近命中、已知黑色和全射线数归一化，不用删除遮挡或抬高黑色来消除暗斑。

这份三角形扫掠及曲率预算是本实验的适配。UE 5.7.1 仍采用基础屏幕探针加自适应分配，并使用方向照明与接收像素的短程 AO / GI 配合；本版的一阶半球拟合、三点屏幕深度可见性仍是近似，不宣称已经完整移植 UE 的方向 gather、SH 或 Short Range GI。

离线入口：`--surface-cache-geometry-test`；A/B 可用 `--surface-cache-no-safe-origins`、`--surface-cache-no-curvature`，实际起点叠加可用 `--surface-cache-origin-overlay`。专项使用 20 / 45 / 60 / 90 / 120 度 V 槽，以及球体/方块的分离、接触与相交布局；参考来自独立双精度三角形求交与固定微小数值偏移，不以生产起点偏移作为真值。

### 2026-10-18：几何与曲率专项验收

最终深度精度修正后的独立专项见 `build/acute_geometry_geometry_depth.log`。测试为 Vulkan、256×192、32 像素基础间距、16 个候选槽、每探针 64 射线，关闭空间与时域过滤，以 0.06 世界单位的较大 ray bias 对近缝施压。八种布局中，旧偏移出现的 19 个跨入邻闭合体的起点全部被限制；safe 与 safe+curvature 的闭合接收物体失效数 `invalid_solid`、实际邻体内部起点数及独立判黑射线返回亮色的违例数均为零。保留 44–79 个背向单面诊断 sheet 的不可读起点，没有把它们伪装为可读黑色。有效前面、背面和空射线都被测试覆盖，关发光后最大原始 GI 为零，常量重建最大误差不超过 `2.98e-7`。这些是该组布局的独立 oracle 结果，不等于任意模型内部认证或每个像素均有探针；V60 仍有 2 个覆盖洞，三个球场景分别有 12 / 22 / 3 个洞。

仅打开曲率分配、其余安全起点设置相同时：

| 球体与方块关系 | 球面仿射重建 MSE 降低 | 球面残差梯度 RMS / P99 降低 | 实际 GI 球面 MSE 降低 |
| --- | ---: | ---: | ---: |
| 分离 | 27.06% | 10.14% / 4.73% | 0.32% |
| 接触 | 26.58% | 9.19% / 5.63% | 0.27% |
| 相交 | 27.19% | 7.97% / 6.53% | 0.53% |

**约 27% 是解析仿射光场的重建误差改善，不是实际 GI 降噪幅度。** 实际 GI 在这项独立曲率开关对照中仅改善约 0.3–0.5%；每场景增加 8 个活跃代表，总活跃数增加约 3.5–3.6%。球面解析残差按同一物体内部的相邻像素计算，避免用真实几何边缘的跳变代替曲面块色误差。

五个相隔四分之一像素的平移姿态中，关闭屏幕线段可见性、空间与时域过滤以隔离分配重建：仿射残差帧差 RMS / P99 降低 31.27% / 35.85%，但最大值 `0.0159164` 未下降。固定 seed=11 的实际 GI 帧差 RMS / P99 只降低 3.50% / 3.78%，且包含真实表面取样及物理光照变化，不能直接当作纯随机噪声方差。连续激活还由独立全探针归一 oracle 检查：实际激活度 `0.457871` 的探针在 1132 个像素上，选定探针脉冲与数学权重的最大误差 `1.74349e-6`；强行使用满权重的反事实最大偏差为 `0.190234`。这验证激活度确实参与重建，没有声称曲率误差估计本身已成为完整物理误差界。

### 五路消融与剩余暗尾部

`--surface-cache-feature-ablation` 在同进程比较 legacy、safe_only、curvature_only、current、no_screen_visibility。每个机位每项先预热 16 帧，再测静止 4 帧与首次移动 1 帧；两个机位共 10 项、50 个测量帧，全部复用有效历史。它不替代连续移动验收。初版日志为 `build/acute_geometry_ablation.log`，深度精度修正后的日志为 `build/acute_geometry_ablation_depth.log`，最终空间命中角度起点基准修正后的日志为 `build/acute_geometry_ablation_angle.log`。

最终 angle 批次中，current 相对同批 legacy 的曲面 ROI 如下；三个数字依次是 raw / spatial / temporal，负号表示误差下降：

| 机位与状态 | 总 MSE 变化 | dark MSE 变化 |
| --- | --- | --- |
| wall 静止 | −41.18% / −37.16% / −36.13% | +20.94% / +31.84% / +44.03% |
| wall 首次移动 | −39.18% / −30.49% / −35.74% | +34.42% / +45.98% / +35.18% |
| oval 静止 | −31.09% / −27.49% / −28.22% | −5.79% / +6.14% / +12.15% |
| oval 首次移动 | −33.15% / −29.68% / −27.87% | +6.11% / +15.57% / +16.15% |

总误差主要因偏亮尾部减少而改善，**暗尾部仍有回退，不能宣称黑斑已全面解决**。曲率单独开启多数情况下减小 dark MSE；safe_only 则增大暗尾部，连起点完全有效的 oval 静止情况也如此，因此问题不全是深度容差误拒绝。关闭三点屏幕线段可见性相对 current 的 dark MSE 变化不足约 0.3%，不是这批退化的主要解释。Depth32F 修正把 wall 静止/首次移动可读性从 `0.986514 / 0.987973` 恢复到 `0.999870 / 0.999754`，全边缘 ROI 的 dark MSE 下降约 2.1–3.3%，但曲面 ROI 数值基本不变。空间过滤还可能放大局部暗偏差，需继续用方向测量与表面邻域的独立对照定位，不能仅凭总 MSE 改善结案。

depth → angle 的最后小修正统一了空间命中角度计算与真实追踪起点的基准。其 legacy 和 raw 质量字段保持不变；四组曲面静止/首次移动的 spatial、temporal dark MSE 相对 depth 降低约 0.06–0.19%，总 MSE 降低约 0.01–0.14%。这是小幅改善，不能用它解释或抹去上表仍存在的暗尾部回退；Depth32F 对 −3 误拒绝的修复归因也保持独立。

五路与完整 edge 的 256 spp 逐像素参考共享现有 Surface Cache，并从 `p + n * rayBias` 使用原 BVH 参考路径起射；它避开屏幕假命中，但极薄或锐角处仍有旧起点偏移的边界，不是完整路径追踪真值。旧屏幕错误亮命中被移除后，可能暴露此前被抵消的局部暗误差；汇总 MSE 不能证明逐像素抵消关系。上述八布局的独立三角形 oracle 才是这轮起点和有效黑色语义的正确性证据。

### 复现入口与开销

```text
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --surface-cache-geometry-test --msaa 1
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --surface-cache-feature-ablation --render-width 1280 --render-height 800 --msaa 1
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --surface-cache-feature-perf --render-width 1280 --render-height 800 --msaa 1
```

feature-perf 在同进程按 legacy / current / current / legacy 的 ABBA 顺序测量，每批 12 帧，不含图像回读与呈现。RTX 4060 Laptop、Debug、1280×800、64 射线/探针下，最终 `build/acute_geometry_perf_angle.log` 四批 GPU 为 `76.3616 / 98.3036 / 99.0239 / 77.1136 ms`；legacy 均值 `76.7376 ms`，current 均值 `98.66375 ms`，增加约 **28.57%（约 29%）**。这是整组曲率分配、活跃射线变化、三角形起点及安全命中验证的管线开关对照，不是单一 shader 的隔离成本；固定容量没有增长不等于实际执行免费。本批次未达到 60 FPS。较早的 `build/acute_geometry_feature_perf.log` 是修正前的另一批测量，不能将跨批次绝对耗时差当作本次小修正的性能收益。

## 2026-10-18：源位置精度与整探针失效处理

探针使用的接收面位置默认改为直接光栅化捕获：复用材质的法线捕获管线、相同的 alpha/cutout 覆盖和不透明深度，在 RGBA32F 中保存插值后的 view position，再由放置阶段变换到世界空间。原来的深度反解仍可作 A/B 对照。直接捕获减少了深度量化及反投影条件数带来的源面误差；它没有改变几何法线、扩大安全偏移，也没有取消源三角形短查询或邻面扫掠。位置纹理复用原存储，仅另建共享深度的 framebuffer。

重建同时区分**整颗探针的发射起点无效**与**有效起点发出的部分方向不可读**。整起点认证失败的代表现在从光照加权分子和几何归一化分母中同时移除，避免把“没有合法测量”当成满权重黑色，拉暗周围区域。像素只在原有有限支持范围内，使用通过法线、对称切平面和可见性检查的其他代表；不会扩大搜索到另一面或跳过遮挡。没有足够可用支持时仍输出不可读，未新增保证填洞的射线或强制补亮。

该排除条件只查看起点图集的有效性，**不按 RGB、readability alpha 或亮度筛选探针**。有效黑色仍占完整权重；有效起点的部分方向缺卡片、超出距离场或遍历耗尽时，积分仍保留全部射线数作分母，不因删去未知方向而增亮。几何覆盖与常量/仿射等合成诊断保留各自定义，不能把它们的覆盖显示当作实际光传输全部有效的证明。

F1 → GI 提供两项按需回读，按钮点击后更新的是最近一次完成帧的统计，界面显示 `Last inspected` / `Last rays`；镜头继续移动时这些数字不会自动刷新，以免每帧回读造成停顿：

| 按钮 | 显示内容 | 如何判断 |
| --- | --- | --- |
| Inspect origin validity | 活跃、完整偏移、已限制、不确定，以及 −1～−5 五类整起点失败数 | `source / precision / facing / clearance` 分别对应无源候选、误差超限、朝向不符和外侧余量不足；这些才是起点认证统计 |
| Inspect ray status counts | 射线状态 6 / 16 / 14 / 15 的数量 | 分别为源数据/投影校验异常、起点不可用、距离场范围不足、遍历预算耗尽；状态 16 应结合起点统计看，不能将所有射线异常归为同一种问题 |

普通 P 探针标记中心显示的是选定方向的一条射线状态，红色不等于整颗探针的所有方向失效。`Show actual ray origins` 是另一项独立叠加：青绿为完整偏移，橙色为已限制，紫红为近距离/背面不确定，红色为整起点无效；它没有深度测试，可能透过前景显示，不能仅凭屏幕颜色认定探针属于眼前的平墙。

专项入口及深度反解对照：

```text
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --surface-cache-origin-test --msaa 1
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --surface-cache-origin-test --surface-cache-depth-positions --msaa 1
```

`build/probe_validity_origin_depth.log` 与 `build/probe_validity_origin_raster.log` 各覆盖 16 个姿态，包括 640×400、1280×800、2560×1440、近距离掠射墙面/天花板及移动后的机位；每探针 16 条射线，关闭空间与时域过滤。独立双精度三角形 oracle 在远离几何边界的平墙内区测得以下**捕获源位置到真实源平面的误差**，不是把安全偏移后的起点要求在平面上：

| 指标 | Depth32F 反解 | 直接位置捕获 |
| --- | ---: | ---: |
| 平墙内区样本数 | 15626 | 15627 |
| 平墙内区无效起点数 | 0 | 0 |
| 内区源平面误差 RMS（世界单位） | 3.768e−5 | 4.6522e−7 |
| 内区源平面绝对误差最大值（世界单位） | 2.24717e−4 | 3.46452e−6 |

两组有效起点进入其他闭合体、无效起点射线契约违例均为零。本轮几何、空间过滤与时域回归也通过。**深度反解基线在这 16 个姿态的平墙内区同样没有失效，未复现截图中的普通平墙误拒绝，不能据此宣称每个红点均已根治。** 边界仍有 −4 朝向不符等保守拒绝，直接捕获后的一个近掠射姿态也保留 2 个边界 −3；它们未被隐藏或改成可读黑色。实际剩余暗斑还可能来自有限方向采样、支持缺口与近处几何差异，须结合起点和射线计数继续定位。

`--surface-cache-transport-test` 独立验证重建规则，最终日志为 `build/probe_validity_transport_final.log`。1280×800、固定 seed=17、每探针 16 方向，关闭方向模型、空间/时域过滤及屏幕线段可见性，以隔离标量重建算术。CPU 从独立三角形确定接收材质，遍历全部活跃代表的实际位置 B2 核及四个粗角点，核对 66,494 个接收像素；RGBA 最大差 `2.60428e-4` 是 GPU 浮点与双精度 oracle 的数值差，不是画质 MSE。一个失效起点原本影响 124 个像素，它们的 RGB 也确实变化：与保留旧分母的反事实相比，RGB 向量最大差 `0.00312751`，并非只修改 alpha。45 个部分 unknown 探针仍按完整方向数积分，独立逐射线核对最大差 `8.61152e-8`；关灯后 2,174 个有效全黑探针和 1,024,000 个可读接收像素继续保留，RGB 为零。这证明该测试中的失效分母修正和已知黑色/部分未知语义，不等于证明所有视角均无暗斑或漏光。

源位置捕获的额外成本可用 `--surface-cache-source-perf --render-width 1280 --render-height 800` 测量。它在其余设置不变时，同进程按 depth / raster / raster / depth 顺序预热并各测 12 帧；`build/probe_validity_source_perf.log` 四批 GPU 耗时为 `111.135 / 111.889 / 112.895 / 112.511 ms`，均值 `111.823 / 112.392 ms`，该批约增加 `0.569 ms`（0.51%）。环境仍为 RTX 4060 Laptop、Debug、64 射线/探针，不含回读和呈现；波动不可忽略，这不是整套修复的隔离成本，也不能与上一批绝对耗时直接比较。

当前版本的几何、空间、时域、实际位置重建和方向模型 GPU 回归均通过；OpenGL 为结构检查通过，未将其等同独立高采样画质验收。`build/probe_validity_edge_final.log` 还记录 640×400、12 个 256 spp 参考姿态、64 个连续移动帧全部复用历史，关灯 raw/temporal 为零；它是本版本的运动回归，不用于沿用上一版 1280×800 的改善百分比。CPU 的 PBR 与 Surface Cache 两项回归通过。

## 当前表面代表、方向重建与局部可见性

自适应容量仍为每基础 tile 的 4 / 8 / 16 个候选槽，但现在每个空间子格最多占用两个槽，分别表示两个几何不兼容的可见表面。对应布局为 2×1 / 2×2 / 4×2 个子格，每格两个代表。默认 32 像素基础间距与 8 个槽意味着 2×2 个 16×16 像素子格，而不是旧版 4×2 个单表面子格。代表由实际 GBuffer 像素选出，保留各自位置与法线，不把前景和背景平均成一个悬空点。基础覆盖薄弱的表面和真实交线可以请求代表；均匀且已有覆盖的子格不必消耗两个探针的射线。

自适应重建采用按真实投影位置计算的可分离二次 B 样条核 `B2(dx / step.x) * B2(dy / step.y)`。单轴核在 `|t| < 0.5` 时为 `0.75 - t²`，在 `0.5 ≤ |t| < 1.5` 时为 `0.5 * (1.5 - |t|)²`，更远处为零。默认步长为 16 像素，单轴支持半径为 24 像素。搜索窗口包含完整核范围，候选离开范围时贡献连续降为零，不依赖最近四点、整体替换或交线元数据的周期遮罩；四个存储桶只汇总贡献，不限制参与探针数量。

正值归一化能保持常量场；规则、完整采样位置上的 B2 也有线性精度。但实际代表会因几何分组、覆盖缺口和屏幕边界偏离规则格，几何及可见性拒绝又会截断支持，因此不保证任意不规则分布上的仿射场精确重建。每格最多两个表面，第三层或更多互不兼容表面、极细物体和强曲率区域仍可能缺覆盖。增加候选数或降低基础间距可以增加预算，不能通过混入其他表面来保证填满所有像素。

`Probe segment visibility` 在像素与探针分别沿法线偏移后的线段上，于 1/4、1/2、3/4 位置检查当前 GBuffer 深度。确定的前景遮挡拒绝借用，深度容差带平滑减权；越界、背景和无效深度保持不确定，不凭空产生黑色遮挡。放置覆盖判断和最终重建使用同一函数与捕获纹理，避免分配器说“已覆盖”而接收端又拒绝。它独立于空间过滤的探针对 BVH 可见性检查：前者检查像素到探针，后者检查探针到邻探针。

三点检查只是屏幕空间近似：可能漏掉采样点之间的薄遮挡，无法读取隐藏层或屏外几何，前景投影也可能造成保守误拒绝。它会依赖当前视角，并可能增加真实支持缺口；不能宣称消除全部漏光，也不能替代逐像素短程 GI。原有法线、双向平面距离和未知覆盖规则保持独立，不通过放宽几何门限补偿被拒绝样本。

`Directional probe reconstruction` 为每探针增加横向四个 RGBA texel：第一个是原 `m0 / readability`，其余分别为 RGB 的世界坐标方向系数 `bx / by / bz`。系数来自现有余弦半球射线的中心化方向矩；按颜色通道限制一阶模型的幅度，避免负方向瓣。接收面求值为 `m0 + (2/3) * b · (n_receiver - n_probe)`，原法线处保持原标量积分，readability alpha 也保持原值。基础探针、自适应代表和覆盖回退都在逐探针聚合前求值，过滤后路径使用过滤后的射线重新拟合。

这是受限的一阶半球拟合，不是完整球面 SH。探针未采到背向半球，不能把缺失方向当作测量后直接除以 `cos_probe` 外推；模型的非负限制也不代表所有方向已有真实覆盖。它可以改善不同接收法线的方向表达，但同法线、不同位置的遮挡差异仍然存在，不能单靠方向系数修复墙角过遮挡。当前仍没有完整逐像素短程补追和多次反弹。原标量图集及 API 保留；方向模型为原始及启用的空间过滤结果各增加一次小图集积分，不新增主追踪射线，但会增加系数读取、图集和执行开销。

F1 的 GI 组中，以下开关相互独立；改变估计器配置会重建 GI 历史：

| 参数 | 当前默认 | 用途与限制 |
| --- | --- | --- |
| Directional probe reconstruction | 开启 | 按接收法线求值受限方向模型；关闭保留原标量 `E/PI` 对照。 |
| Probe segment visibility | 开启 | 开启像素到探针的三点屏幕深度检查，不替代空间过滤的 BVH 配对检查。 |
| Temporal placement jitter | 关闭，实验性 | 自动改变基础放置相位及依赖覆盖判定的自适应选择。它独立于默认 8 个射线方向相位，可能增加重建波动，不作为默认降噪保证。 |
| Adaptive candidates / tile | 8，可选 4 / 8 / 16 | 固定槽预算中每子格最多两个表面；增加容量可能增加活动探针和射线。 |
| Probe spacing (pixels) | 32，面板 4～128 | 同时影响基础密度、子格尺寸和实际位置核半径。减半间距大约增加四倍基础槽。 |
| Refine geometric intersections | 开启 | 启用交线主动补点和当前连续核重建路径；关闭可观察旧重建路径，传统缺覆盖补点仍有预算。 |
| Intersection refinement band (tiles) | 0.5，面板 0.25～1.5 | 控制交线主动分配的范围，受检测范围限制；不再充当逐像素粗细混合遮罩，也不是 B2 核半径。 |

在 `GI inspection = Indirect GI` 且使用表面探针时，`Probe reconstruction check` 还提供 `Actual-position affine`：逐探针按实际世界位置赋值 `clamp(0.45 + 0.04 * (x + 2y + 0.5z), 0.05, 0.95)`，再进行真实加权，用于检查名义中心与实际位置是否混用；不将目标像素的解析值直接写入结果。`Selected probe support` 则仅让 `Support diagnostic probe` 选定的探针为 1，其余有效探针为合法黑色 0，显示实际支持范围和权重。原 `Constant illumination` 与 `Base / adaptive weights` 继续可用。合成诊断绕过接收反射率与 GI 强度，暂停该视图的空间和时域过滤，不改写真实射线或积分图集；退出后重新建立真实历史。

### 2026-10-18：本轮实际位置重建验收

对照对象是上一版固定细格版本（`build/edge_mosaic_fixed.log`），不是最初的马赛克版本。Vulkan validation、1280×800、32 像素间距、每探针 64 射线，场景及空间 / 时域参数相同。两机位共 12 个姿态的 256 spp 逐像素单次反弹参考，96 项数值完全一致，两张静态参考 PNG 的 SHA256 也相同。参考共享当前 Surface Cache，不是完整路径追踪真值或 UE 实测。

| 机位 | 阶段 | 时域亮度 MSE 降低 | 暗误差降低 | 亮误差降低 |
| --- | --- | ---: | ---: | ---: |
| 墙顶 | 静止 | 34.22% | 21.99% | 39.13% |
| 墙顶 | 首移动帧 | 30.95% | 12.04% | 38.58% |
| 墙顶 | 连续移动 | 25.88% | 8.90% | 31.11% |
| 椭球 / 后墙 | 静止 | 27.21% | 8.78% | 33.29% |
| 椭球 / 后墙 | 首移动帧 | 28.61% | 17.40% | 32.92% |
| 椭球 / 后墙 | 连续移动 | 47.67% | 19.87% | 56.36% |

两机位各连续渲染 32 个运动帧，有对应参考的 10 个检查点参与误差比较；64 帧全部复用 GI 历史。未经过时空过滤的原始 GI，连续移动 MSE 也分别降低 27.30% / 45.07%，改进发生在重建源头。完整日志为 `build/adaptive_surface_final_quality.log`，逐项结果为 `build/adaptive_surface_final_compare.csv`。

不能据此声明光斑完全消除：真实 GI 的 16 像素纵向相位残差在部分阶段增加。例如墙顶连续移动由 1.90950e-7 增至 3.41329e-7，椭球静止由 2.70697e-7 增至 1.10339e-6。合成粗细标签的周期性改善不能代替该真实残差指标。静态及首移动帧的真实孔洞仍为零；连续移动墙顶由 72 降至 13 / 7,158,630 样本，椭球则由 0 增至 102 / 5,182,433 样本，约 0.00197%。低支持计数的质量总和尺度随核改变，不能直接解释为置信度改善比例。关灯 raw / temporal 为零，可读取黑色保留；真实可读率约 99.97%～99.99%。

结构回归在 4 / 8 / 16 槽与五个四分之一像素平移位置核对真实 GBuffer 来源、双表面槽、常量归一化和独立全局 B2 参考；选定探针在实际半径外及不兼容表面贡献为零。方向模型另核对实际射线积分、原法线恒等、非负界、未知覆盖、开关恢复、过滤后数据来源、历史失效与关灯归零。Vulkan 的既有空间过滤、时域和密闭房间回归、PBR CPU 回归及 OpenGL 的结构诊断均通过；OpenGL 的独立高采样参考限制沿用下文历史记录，不将结构诊断视为该参考验收。

为减少固定支持窗口扫描空槽，当前每个基础 tile 生成一枚活动槽位掩码，再跳过未生成的子格 / 代表。16 位掩码在 RGBA32F 中精确保存；所有活跃样本的访问顺序及权重不变。该优化前后两机位的常量、粗细标签、raw 和 temporal 共八张 PNG 逐字节一致。它新增一个很小的 tile 图集和汇总 pass，不增加 GI 追踪射线。

局部可见性的独立俯视检查找到 5 个可读的像素 / 基础探针对，原始变换后三角形的双精度求交确认线段被遮挡；关闭检查时这些探针有贡献，开启后所选探针贡献均归零。这只证明该明确遮挡子集，不证明三点检查能识别任意薄遮挡。最终日志为 `build/adaptive_surface_structure_release.log`。

同一 1280×800 密闭房间、64 射线 / 探针、64 个面积光采样，Vulkan validation，预热后各测 12 帧，不呈现、不限帧、不回读图像，包含覆盖诊断。重新成对测得：

| 版本 | GPU ms | 含提交与等待的整帧 ms |
| --- | ---: | ---: |
| 上一版固定细格 | 84.74 | 90.17 |
| 当前表面代表与方向 / 可见性检查 | 95.20 | 100.69 |

当前配置增加了约 12.3% GPU、11.7% 整帧开销，不能宣称这次画质改进免费或达到 60 FPS。另一批次的当前版本为 87.51 / 93.45 ms，体现了本机运行波动；不同批次差值不等于隔离的 shader 耗时。成对日志为 `build/adaptive_surface_perf_{before,after}_verified.log`，掩码前后的单批次记录为 `build/adaptive_surface_perf.log`、`build/adaptive_surface_mask_perf.log`。关闭方向模型或局部可见性可作开销对照，但会改变估计器。

最终审查还补齐了冻结分配对 normal bias / relative thickness 的兼容性检查与 GI 历史参数键；冻结位置不会掩盖遮挡参数变更。针对性回归确认参数改变会刷新分配并拒绝旧历史，配置稳定后继续复用冻结结果。发布构建与最终结构、方向、时域、CPU 日志为 `build/adaptive_surface_release_build.log`、`build/adaptive_surface_{structure,directional,temporal,cpu}_release.log`。方向纹理关闭或未采集时，读回 API 明确返回错误，不提供旧帧系数。

## Spatial Filtering 与自适应覆盖修正

执行顺序是“屏幕追踪与 SDF 补追 → 探针方向辐射率空间过滤 → 探针积分 → 几何兼容重建 → 时域累积 → 直射合成”。空间过滤处理每个探针的入射方向样本，发生在乘接收面反射率之前，因此不会把红墙材质直接模糊到白色物体。原始射线、原始 `E/PI` 和未过滤的像素 GI 仍分别保存，可与过滤后、时域前的 GI 对照。

本版参考 Lumen 的固定相邻探针模板、相对深度指数权重和近处命中距离限制。每一轮检查上下左右的邻居，半径为 2 时再检查两格处；重复迭代逐步传播兼容的低噪声估计，不使用逐轮扩大的 à-trous 步长。命中角度过大时不混合，且将邻居命中距离限制到当前命中距离，防止远处的亮样本洗掉近处接触阴影。几何配对和可选 BVH 可见性检查只对探针对执行一次，射线单元复用配对结果。

与 UE 的完整实现存在明确差异：UE 使用世界方向的等面积球面单元，本版使用法线朝向的余弦半球分层单元，对于法线几乎相同（点积大于 `1 - 1e-6`）且使用同一个切线基分支的平面探针，按相同分层单元一一对应，保留独立抖动的分层平均意义，避免最近方向查找重复使用或遗漏某些角度测量造成平面均值偏差。对不同法线或切线基的邻居，则把当前世界方向反变换到邻居的局部余弦半球，在最近四个分层单元中按实际射线方向选择角度最近的测量；选择在读取有效性之前完成，不会因最近样本不可读而另选更亮或有效的样本。空间命中角度另按 UE 的共同世界锥方向计算，即使用“邻居起点 + 当前方向 × 被限制的命中距离”，避免把独立随机方向的抖动错误地当作几何视差；10° 门限只约束空间视差，权重不依赖亮度。曲面间的最近方向重采样仍是近似，并非严格保持角度积分或能量的重采样，可能带来小量偏差；尚未实现 UE 将原始追踪散射到共享八面体方向图集的完整步骤。本版额外增加法线硬门限、双向表面平面距离检查和探针间的 BVH 可见性检查。自适应候选也可以替换不兼容的基础邻居。不可读取的射线继续保持不可读取，原始状态、命中位置、方向与追踪诊断不变；空间过滤不能替缺失卡片编造光照。已知黑色是有效测量，参与归一化过滤，避免只平均亮样本造成整体偏亮。

按 F1 打开 GI 组：

| 参数 | 默认 / 面板范围 | 含义与调节建议 |
| --- | --- | --- |
| Spatial filtering | 开启 / H | 开关探针方向辐射率过滤。配合 T 可分别比较单帧过滤和时域累积。 |
| Spatial passes | 3 / 1～4 | 固定相邻模板的迭代次数。越大越平滑、开销越高，也更容易削弱真实的局部变化；建议先用 2～3。 |
| Spatial half kernel | 1 / 1～2 个探针格 | 1 检查四邻居，2 再检查距离两格的四邻居。增大探针间距时，世界空间实际过滤范围也会变大。 |
| Spatial strength | 1 / 0～1 | 每轮原始值与邻域归一化结果的混合比例。0 精确保留原始单帧结果；细节变软时先尝试 0.5～0.8。 |
| Spatial position weight scale | 1000 / 100～10000 | 权重为 `exp2(-scale * relativeDepthDifference²)`。越大越少混合不同深度探针；减少时过滤更强，但更可能损失接触细节。 |
| Spatial hit angle | 10° / 1～60° | 接受邻居命中方向的最大夹角。减小保护近处遮挡，增大允许更多过滤；建议 5～15°，不建议先靠大角度消除光斑。 |
| Spatial normal threshold | 30° / 5～60° | 配对探针允许的法线夹角。较小保护墙角和曲面轮廓，过严会让曲面残留更多噪声。 |
| Spatial plane / view depth | 0.01 / 0.001～0.05 | 两个探针在彼此法线方向上的位移，相对较近的视深度的上限。越小越能阻止跨层混合；薄板漏光时先减小。 |
| Spatial pair visibility test | 开启 | 对偏移到表面外的探针对之间发射软件 BVH 遮挡射线；穿过物体的配对不共享光照。关闭可减少这项工作，但薄遮挡或近距离并行表面更可能串光。 |

调整过滤开关或参数会清空旧 GI 历史，下帧重新累积，避免混用不同估计器。空间过滤会引入有限邻域近似，不能同时保证无噪声和保留所有细小光照变化；优先增加真实采样与完善覆盖，再调过滤强度。

此前已经启用自适应探针，但候选只在每格少量离散位置检查几何，薄柱和球轮廓可能落在这些位置之间；重建的自适应搜索还可能沿用了抖动基础网格的四角位置，遗漏实际覆盖当前像素的单元。因此“开着自适应”仍不等于每个有效像素都拥有兼容探针。

当前补充候选按固定 4 / 8 / 16 槽预算划分为 2 / 4 / 8 个子格，每格最多保留两个不兼容表面的实际代表。接收端按上节实际位置 B2 核的完整范围累计所有兼容自适应贡献，不再仅在基础覆盖不足时选择 top-four 胜出集合。法线、双向平面和默认三点可见性共同约束重建；有限预算仍允许真实缺口，不通过放宽边界权重掩盖它们。

B 或 `GI inspection → Probe geometric coverage` 显示独立的几何覆盖：绿色为基础探针支持，橙色为自适应探针补充，洋红为有效接收面没有兼容探针，黑色为背景。它与 Cache coverage（是否存在卡片）以及 P 叠加显示的射线命中颜色分别回答不同问题。不可读取射线不代表接收像素没有探针；合法黑色光照也不等于覆盖缺失。

P 叠加视图中，探针标记的外环现在区分蓝色基础探针和橙色自适应探针；内侧仍显示射线命中 / 不可读取等状态色。选中的探针外环优先显示白色。这样可以直接确认自适应探针是否实际生成，不必从混杂的射线状态颜色推断探针类型。

发现洋红区域时，先确认 `Adaptive placement` 开启，再将 `Adaptive candidates / tile` 从 8 提到 16；仍有缺口可把 `Probe spacing` 从 32 降至 16。增加候选数会增加预留槽和潜在射线预算，减半间距大约将基础槽数增至四倍。不要用更大的空间过滤角度或更松的平面阈值把其他物体的光照填进缺口。

面板的 `Inspect active probe counts` 按钮回读当前探针，显示实际活动的基础 / 自适应数量；统计里的 `Probe slots` 和 `Software GI rays` 仍是预留容量上限。实际数量只在按下按钮时刷新，避免为了显示计数每帧等待 GPU。

```text
Module\MaterialLab\RunSurfaceCacheGi.cmd --surface-cache-spatial-passes 3 --surface-cache-spatial-radius 1
Module\MaterialLab\RunSurfaceCacheGi.cmd --surface-cache-no-temporal --surface-cache-view indirect
Module\MaterialLab\RunSurfaceCacheGi.cmd --surface-cache-no-spatial --surface-cache-no-temporal --surface-cache-view indirect
Module\MaterialLab\RunSurfaceCacheGi.cmd --surface-cache-view probe-coverage
```

## Temporal Accumulation

当前沿用 2026-10-16 调整的 10 帧总历史、8 个方向相位、8 帧运动历史上限和每帧 3 像素的帧数缩短阈值。Sigma 6 的历史裁剪同时覆盖超过 0.1 像素的缓慢相机运动；几何边缘另保留 2 帧历史和更严格的裁剪。当时 1280×800、每探针 64 射线的 Vulkan 完整转向、平移与组合轨迹验证通过；其数据是旧重建版本的参数调节依据，不替代新增方向与表面代表版本的运动验收。

时域环节位于“自适应探针积分 → 像素重建”之后、“间接光与直射合成”之前，累积的是线性 HDR 间接漫反射辐射率。它与最终图像 TAA 完全分开；直射、UI、曝光后的显示结果不进入 GI 历史。

历史资源独立不代表运动判定已经消除 TAA 抖动：当前 GBuffer 使用最终图像 TAA 的抖动投影，开启 TAA 时，静止相机的投影抖动也可能超过 0.1 像素，触发缓慢运动裁剪或边缘保护。因此当前默认及主要画质对照采用最终图像 AA=None；已有 TAA 共存检查验证历史复用，未证明此组合具备相同的长期收敛效果。

当前像素的世界位置投影到上一帧，检查相邻四个历史像素的世界位置误差、法线和有效性；只在同一表面上做双线性重投影。遮挡后重新露出的像素、越界、无效几何从当前帧重新开始。每像素保存有效累计帧数，达到上限之前采用逐步平均，达到上限后继续以 `1 / MaxFrames` 的权重加入当前样本，并非永久冻结结果。默认每 8 帧循环一次方向采样，给累积器提供不同的蒙特卡洛样本。

本版用相机重投影后的像素移动量区分历史帧数限制与历史裁剪：超过 3 像素才将普通表面历史限制至 8 帧，超过 0.1 像素就允许较宽的 Sigma 6 裁剪，避免慢速移动时长历史滞后于视角相关的探针重建。历史裁剪只限制旧值；2026-10-18 新增的低历史恢复另过滤当前样本。运动中的几何边缘仍用短历史与较紧裁剪，静止边缘正常累积。经严格认证的可移动物体纯变换现在可以保留兼容历史，材质、光源、拓扑或无法认证的修改仍清除整幅历史。

UE 默认关闭 `RejectBasedOnNormal` 和 `FastUpdateModeUseNeighborhoodClamp`，45° 是可启用法线检查的角度默认值。本版始终检查法线，并默认开启运动历史裁剪；这是为当前重建误差采用的更保守策略。UE 的快速更新程度来自命中运动物体的光照比例（`LumenScreenProbeGatherTemporal.usf:585–596`），并非简单的相机屏幕位移。本版尚无物体速度与光照运动缓冲，局部几何边缘保护也是本实现的安全调整，不是 UE 的同名完整算法。总历史 10 帧与方向相位 8 保留 UE 默认值；运动阈值、运动上限、Sigma 6 与边缘保护参数则根据本版实测调节。历史使用未经曝光的线性辐射率，显示曝光变化无需进行历史预曝光补偿。

相机切换、手动重置、开关 GI / 时域、切入卡片检查视图、分辨率、基础 seed、探针间距、射线数和时域参数变化都重建历史；正常相机移动通过逐像素检查复用兼容表面。总光、间接光与 History length 三个视图共享同一份历史。

按 F1 打开面板，在 GI 组调节下列参数：

| 参数 | 默认 / 面板范围 | 含义与调节建议 |
| --- | --- | --- |
| Temporal accumulation | 开启 | T 切换；关闭时查看固定 seed 的原始单帧误差，逐像素参考路径不使用此历史。 |
| Max accumulated frames | 10 / 1～64 | 越大静止噪声越少、响应越慢。1 只使用当前帧，开启低历史恢复时仍可空间过滤；调高前应同时检查运动轮廓和接触细节，不能只看静止方差。 |
| Temporal direction phases | 8 / 1～64 | 独立射线方向相位的循环长度。1 重复同一批射线，无法靠时域减少其采样误差；增大历史时可同步比较更多相位，但不保证整体画质更好。 |
| History distance / view depth | 0.01 / 0.001～0.05 | 接收表面与历史位置的相对误差基准，另有 0.002 世界单位下限和有上限的像素足迹余量；表面法线方向的误差仍受基准约束。减小可减少跨几何拖影，但会拒绝更多历史、增加闪烁。 |
| History normal threshold | 45° / 5～90° | 可接受的最大法线夹角。薄物体或曲面串光可调到 15～30°；太严会使曲面频繁重新累积。 |
| Moving history frames | 8 / 1～最大累计帧数 | 超过屏幕位移阈值时，普通运动表面的历史上限。1～2 反应快，但相机会使大面积表面恢复较强噪声；建议先比较 6～8，并保留边缘保护。 |
| Motion threshold (pixels) | 3 / 0.1～32 | 控制普通运动表面何时缩短历史帧数，不控制已启用的缓慢运动裁剪。按实际 GI 渲染分辨率的每帧位移计量，也受帧率影响。 |
| History neighborhood clamp | 开启 | 控制普通表面的历史裁剪。关闭可能更平稳，但可能滞后于当前重建；几何边缘保护是独立开关。 |
| Clamp slow camera motion | 开启 | 在普通历史裁剪开启时，将裁剪扩展到每帧位移超过 0.1 像素的表面，修正低于 3 像素阈值的慢速重建变化。关闭恢复仅快速更新区域裁剪；不同时关闭边缘保护。 |
| History clamp sigma | 6 / 1～8 | 普通运动区域兼容邻域统计区间的宽度。越大保留历史越多；太小会把历史拉回当前噪声。边缘保护另使用不大于 3 的 Sigma，不随该值无限放宽。 |
| Geometry edge protection | 开启 | 在移动轮廓、薄物体附近及历史支持不足时采用短历史与更紧裁剪。关闭用于对照；不增加射线，但会增加运动时的几何读取。 |
| Edge history frames | 2 / 1～总历史与运动上限的较小值 | 边缘区域的独立历史上限。1 响应最快但噪声更高；2 为当前默认。调高可减轻边缘噪声，也可能重新出现轮廓拖尾。 |
| Local adaptive probe neighbors | 开启 / Spatial 组 | 原有相邻大网格邻居之外，加入同一网格内本子单元及相邻子单元的有效代表，最多 8 个。按真实投影位置连续加权，并保留 BVH 可见性及命中角限制。用于减少远处细边缘的局部采样孤立。 |
| Young history spatial recovery | 开启 | 对历史不足的当前 GI 做小范围双边恢复。成熟历史不额外读取邻居；不会增加历史帧数，也不填亮未知像素。 |
| Recovery radius (pixels) | 3 / 1～4 | 当前帧 9 点核的半径。每条采样路径逐像素检查几何、材质和可读性；增大会更平滑，也增加读取和暂时软化细节的风险。 |
| Recovery below frames | 4 / 2～16 | 有效历史达到此值后完全停止恢复；低于它时平滑衰减强度。新暴露与完全重置帧也覆盖。 |
| Recovery strength | 0.75 / 0～1 | 低历史当前信号与兼容邻域平均的混合量，不按亮度剔除黑样本。0 用于隔离对照。 |
| Retain history on movable transforms | 开启 | 仅认证同一场景中可移动对象的纯变换。保存已提交帧的实例值快照；接收点仍逐像素检查历史位置与法线。未知映射、静态物体、模型源、灯光或材质变化仍重置。 |
| Dynamic lighting history frames | 4 / 1～16 | 认证运动及收敛期的历史上限。当前为全图保守响应，尚无 UE 的逐像素运动光照比例；太大会拖影，太小会增加噪声。 |
| Dynamic lighting clamp sigma | 2 / 0.5～6 | 无论相机是否移动，运动光照期间都对旧历史进行邻域裁剪。越小响应更快，也越容易回到当前采样误差。 |
| Dynamic settling frames | 3 / 0～16 | 物体停止后的若干成功提交帧继续动态响应，帮助旧位置的间接阴影更新。失败或丢弃帧不推进计数。 |
| Reset GI history | 按钮 / G | 清除旧历史，下帧从一帧开始；用于观察收敛过程。 |

V 切换到 History length：蓝色表示短历史，逐渐转绿，再到达到上限的红色；黑色为无效表面。新露出表面从一帧开始，静止表面逐渐达到上限。通过 T 开关比较光斑的时间稳定性；时域只能降低变化中的采样方差，不能修复 SDF / 卡片覆盖错误、几何串光或方向分布偏差。

```text
Module\MaterialLab\RunSurfaceCacheGi.cmd --surface-cache-view indirect --surface-cache-history-frames 10 --surface-cache-history-directions 8
Module\MaterialLab\RunSurfaceCacheGi.cmd --surface-cache-view history
Module\MaterialLab\RunSurfaceCacheGi.cmd --surface-cache-no-temporal --surface-cache-view indirect
```

### 相机移动时的噪声与调节

旧默认是 10 帧总历史、2 帧运动历史、每帧 1 像素阈值并开启历史邻域裁剪。相机轻微转动就可能让整片静态墙面超过 1 像素，使当前帧权重从 10% 升为 50%；裁剪还可能把已收敛历史拉回局部当前帧噪声。停下后历史重新增加，画面才逐渐稳定。这个现象既包含新露出区域必须重新采样的正常部分，也包含旧快速更新参数对静态相机运动过于保守的部分。

当前默认（日志名 `balanced_edges`）不放宽深度 0.01 和法线 45° 的历史拒绝条件。普通快速运动区域的当前样本权重由旧上限 2 帧时的 50% 降至上限 8 帧时的 12.5%；轮廓附近仍采用 2 帧历史。较宽的 Sigma 6 裁剪还覆盖超过 0.1 像素的缓慢运动，边缘则使用不大于 3 的 Sigma。旧参数对照显式关闭缓慢运动裁剪与边缘保护，保留原来的快速运动裁剪算法。参数修改自身会重置一次历史，比较前应先等待收敛。

边缘保护只在重投影位移超过每帧 0.1 像素时工作。有效双线性历史权重合计不足 0.9，或当前像素两像素范围内出现背景、法线不连续、双向表面平面距离过大时，触发边缘短历史和兼容表面裁剪。法线点积门限取历史门限与 0.9 的较大值；平面距离门限为 `max(0.002, min(历史基准容差, 0.002 * 视深度))`。这些是局部保护的固定实现值，不会通过放宽主历史检查来填补缺口。静止边缘仍可正常累积；关闭普通历史裁剪不会同时关闭几何边缘保护。

该边缘检查没有增加纹理、主追踪射线或全屏 Pass。当前时域参数块为 272 字节，另含低历史恢复和动态响应参数。运动且历史支持完整的像素最多检查 24 个几何邻居；发现不连续时提前退出，历史支持已不足时跳过检测。裁剪另使用 3×3 兼容辐射率样本，低历史恢复增加小范围读取。实际开销需单独计时。

曾比较关闭裁剪的长历史候选：小尺寸压力图中更稳定，正式分辨率却增加轮廓误差，因此没有采用。仅在快速移动时裁剪也会遗漏慢速组合运动的重建变化，当前另用缓慢运动裁剪修正。不能只根据小图、单条路径或时间方差选择参数，也不能把低闪烁等同于低误差。

连续运动回归入口为 `--surface-cache-motion-test`，覆盖纯转向、纯平移及组合路径。每条路径预热 24 帧，再向外运动 8 帧、原路返回 8 帧、停下 8 帧。9 个独立相机位置各用 4 个独立 seed、每探针 256 条射线建立未过滤参考。相同方向相位的配置逐帧核对原始 GI 一致，避免随机序列差异污染对照；SDF 资源准备完成后才开始比较。

未指定分辨率时，测试采用 320×200、间距 8、16 射线压力配置；正式大小通过 `--render-width 1280 --render-height 800` 指定，使用间距 32 与默认 64 射线。两种尺寸均只比较旧参数和当前默认两组。运动阈值按测试高度相对 800 同比缩放，但边缘两像素范围与 0.1 像素触发值保持实际像素单位，所以小图不能代替正式分辨率验证。`--surface-cache-motion-tune` 单独执行转向路径，对比旧参数和三组边缘方案。

误差在曝光前的线性 HDR 间接光上逐位置对照参考。平面内部记录亮度 MSE，将上一帧误差重投影到同一表面后记录误差变化 MSE；薄物体和曲面轮廓另记录边缘 MSE 与绝对误差第 95 百分位。不将正常的相机画面变化算作闪烁。背景、缺少兼容探针及不可读取当前 GI 的像素不能借历史填亮；这组旧测试按物体移动全清执行，新增的认证移动路径单独覆盖几何拒绝、远端光照响应和动态历史限制。

2026-10-16 正式接受的记录为 `build/surface_cache_motion_verified_vulkan.log`：RTX 4060 Laptop、优化 Debug 构建、Vulkan validation、显式软件追踪，实际 1280×800、间距 32、每探针 64 射线，最终图像 AA=None。以下分别列各运动路径，避免合计均值掩盖某一路径退化。

| 路径 | 平面亮度 MSE：旧 → 新 | 误差变化 MSE：旧 → 新 | 几何边缘 MSE：旧 → 新 |
| --- | ---: | ---: | ---: |
| 转向 | 6.43641e-6 → 5.49241e-6 | 1.05452e-5 → 7.15123e-6 | 1.79526e-5 → 1.80067e-5 |
| 平移 | 6.59991e-6 → 5.28071e-6 | 1.08108e-5 → 6.89229e-6 | 1.71856e-5 → 1.72313e-5 |
| 转向与平移组合 | 5.87106e-6 → 5.43570e-6 | 8.98195e-6 → 6.71156e-6 | 1.71810e-5 → 1.72376e-5 |
| 合计 | 6.30236e-6 → 5.40299e-6 | 1.01119e-5 → 6.91816e-6 | 1.74397e-5 → 1.74918e-5 |

三条路径的平面 MSE 分别降低约 14.7%、20.0%、7.4%，误差变化 MSE 分别降低约 32.2%、36.2%、25.3%；合计分别降低约 14.3% 和 31.6%。几何边缘 MSE 合计变化约 +0.30%，边缘绝对误差 P95 由 0.00889104 变为 0.00889758（约 +0.074%），与旧方案基本相当。运动区间平面有效历史均值由 2.33 增至 8.83 帧；72 个对应帧的原始 GI 最大差为零。背景、不可读取区域、历史重置和灯光 / 物体变化检查均通过。这些是有限测试房间的结果，不表示任意场景绝无拖影。

OpenGL 另通过 320×200、16 射线的三条连续运动路径回归，逐路径与合计检查均通过，72 个对应帧的原始 GI 最大差同样为零，记录见 `build/surface_cache_motion_verified_opengl.log`。这是另一后端的小尺寸正确性回归，不等于在 OpenGL 下完成 1280×800 画质或性能验证。

原有独立时域回归也在 Vulkan 与 OpenGL 通过，覆盖静止收敛、正常重投影、重新露出区域、历史失效与 TAA 共存。该基线显式关闭新增边缘保护和缓慢运动裁剪，确认保留旧路径兼容性，不将其静止方差收益当作新参数的运动收益。记录为 `build/surface_cache_motion_temporal_vulkan.log`、`build/surface_cache_motion_temporal_opengl.log`；TAA 共存检查的范围仍受上文限制。

更平稳仍有响应代价：停下后的前 8 帧区间，新配置平面 MSE 为 2.81537e-6，旧配置为 2.19167e-6，新配置约高 28.5%；同一区间误差变化 MSE 则由 3.71544e-7 降至 8.97926e-8，降低约 75.8%。因此不宣称所有时刻误差都更低；若更重视停下后的快速响应，可比较降低运动历史上限，同时保留边缘保护。

同一日志另独立测量连续相机运动开销：各配置同位置预热 24 帧，再测 12 个连续移动帧，不呈现、不限帧、不回读图像，保留 GI 历史链。下表为整条渲染管线，非隔离着色器时间。

| 配置 | GPU ms | 含提交与等待的整帧 ms |
| --- | ---: | ---: |
| 旧参数 | 27.15 | 41.46 |
| 当前参数，关闭边缘保护 | 27.38 | 41.39 |
| 当前参数，开启边缘保护 | 26.73 | 37.27 |

这些结果处于同一耗时量级，受 GPU 频率和提交调度波动影响，不能据该批次差值宣称边缘保护加速或没有开销。它仍有上述额外几何读取；GPU 约 27 ms、整帧约 37～41 ms，也没有达到稳定 60 FPS。截图前缀为 `build/surface_cache_motion_verified_`，包含各路径的运动峰值与停下结果；先前探索日志不代替这份正式验收记录。

```text
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --surface-cache-motion-test --render-width 1280 --render-height 800 --surface-cache-probe-rays 8 --screenshot build\surface_cache_motion.png
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --surface-cache-motion-tune --render-width 1280 --render-height 800
output\sdf-banquet\Debug\bin\material_lab_demo.exe --surface-cache-motion-test
```

## 探针射线与条带光斑

旧逐像素参考在 1 条射线时使用固定半径的确定性积分点，倾角恒为 45°，只改变方位角。这覆盖一个圆锥而非完整余弦半球，会形成与遮挡形状相关的亮暗条带；仅旋转 seed 或添加蓝噪声不能消除这种方向分布偏差。

探针路径用二维分层采样：默认 8×8 个角度单元，每个单元内部两个维度都独立抖动，再通过 `r=sqrt(u)`、`phi=2*PI*v` 映射到法线朝向的余弦半球。不同探针和不同采样单元具有独立散列扰动，避免相邻探针重复同一条固定圆锥。可切换 4×4 / 8×8 / 16×16，即每探针 16 / 64 / 256 条射线。

这修正方向分布，但不承诺无过滤的有限采样完全没有光斑：16 条射线仍可能把积分方差重建成较宽的亮暗块；几何采样不足、卡片缺失和 32×32 卡片的光照离散也可能产生残余变化。观察 `indirect` 视图，先提高每探针射线数，再减小探针间距，分别检查角度积分误差和空间重建误差；不要把真实的漫反射亮度变化一并抹平。

## 密闭陶瓷房间与方形顶灯

双击 `Module/MaterialLab/RunSealedGiRoom.cmd`，或在现有程序后添加 `--surface-cache-sealed-room`。这是一套独立场景，原有红绿墙房间和茶壶宴会继续保留。

新场景默认收起文字 HUD 和调节面板，便于完整观察顶灯。F1 打开调节面板；文字帮助可在面板 `Help / scene controls` 中开启。

房间内径为 8×7，高 4.5 世界单位。四面厚墙分别为珊瑚红、翡翠绿、钴蓝和赭黄；地面为浅米灰，天花板为暖白。顶棚由四片实体围出中央方孔，孔内有封闭背板、内衬、黑色边框和朝下的 1.8×1.8 方形漫射灯面。六个陶瓷球体、椭球、立方体及棱柱摆放在灯下。相机恢复为之前 Surface Cache 场景的 55° 视角，默认正面机位；1/2/3 分别切换正面、侧面和近景，抬头可查看顶灯。右键转向使用原始鼠标输入，保持原有 0.0025 弧度/鼠标单位的灵敏度，WASD 与 Q/E 移动；相机限制在房间内。

唯一光源为与灯面一致的单面矩形面积灯，默认线性辐射率为 `(24, 23.04, 21.36)`。太阳、点光、聚光、天空和环境补光均关闭。L 开关顶灯；F1 的 GI 面板提供 `Ceiling lamp` 和 `Ceiling emitted radiance`（0～80），同时更新可见灯面与真实光源。F5 关闭或开启间接 GI 时，直接面积光和缓存软阴影仍保留；V 可只看间接光、卡片光照、历史与覆盖。

陶瓷是非金属介质，线性漫反射率约 0.80～0.86、粗糙度 0.16～0.24；面积灯的直接 GGX 高光表现光滑釉面，彩墙通过现有单次漫反射 GI 染色。镜面间接反射及多次漫反射仍未实现，反射率高不代表将金属度设为 1。

新增矩形灯默认使用 8×8（可配置 4×4）确定性面积求积，卡片照明对每个采样点使用共用三角形 BVH 检查可见性。直接 PBR 软阴影使用各采样点的六面深度图 atlas，避免对每个画面像素重复遍历三角形。默认 64×6 个 128² 深度瓦片合计 24 MiB；只有几何、光源位置或形状变化才重建，相机移动、灯光颜色和亮度变化会复用它。4 tap 深度比较带有有界接收平面修正，瓦片内部合并为一次 textureGather 读取，边界保留独立读取以免跨面取样。它仍是有限阴影图分辨率和面积求积近似，接触处可能有偏差。此专用材质用于当前全不透明房间；普通 PBR 场景保留原路径。

灯面可见发光，而其卡片 emission 为零，由解析面积灯负责直接光输运，避免把同一盏灯经随机发光命中再次计入 GI。灯光变化会使卡片照明与时域历史失效，阴影几何缓存则不因亮度变化而重新生成。关闭 GI 后直接面积光与阴影仍保留。

独立检查与截图入口：

```text
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --sealed-gi-room-test --render-width 1280 --render-height 800 --screenshot build\sealed_gi_room.png
```

该入口检查密闭几何、灯面可达、间接光正能量、GI 开关对照，以及关闭唯一光源后的全黑结果，并输出 `_total`、`_indirect`、`_direct`、`_lamp_off` 图片。

2026-10-17 初版 76° 对角机位验证：Vulkan validation、显式软件追踪、1280×800 下通过 299 条密闭检查射线、9 条灯面可达路径及六个陶瓷材质检查；GI 开关改变约 100 万像素。关灯后原始 GI、时域 GI、卡片照明与最终图像全部为零。相机位移、恢复机位及灯光亮度变化均不重建阴影图，移动方块后更新 384 个面，随后继续复用。旧房间 Surface Cache、独立时域与材质 CPU 回归通过。日志为 `build/sealed_gi_room_final_vulkan.log`、`build/sealed_gi_baseline_vulkan.log`、`build/sealed_gi_temporal_vulkan.log`。

OpenGL 另通过 640×400 同一场景检查，包括关灯全黑和阴影缓存更新；记录为 `build/sealed_gi_room_final_opengl.log`。这是跨后端正确性检查，不代表 OpenGL 完成了上述分辨率的性能验收。

上述初版机位在 RTX 4060 Laptop、优化 Debug 构建、64 射线/探针与 64 个面积光采样下，静态预热后 12 帧平均 GPU 65.11 ms、含提交与等待 70.71 ms，不含呈现、限帧或图像回读。这是画质优先的观察场景，尚未达到稳定 60 FPS；启动器的 60 仅是上限。相比最初逐像素 BVH 面积阴影方案（16 个面积采样，GPU 322.99 ms）明显降低开销，但两者采样数不同，不能视作同画质性能对照。Gather 优化前后的直接光与完整 GI 截图 SHA256 完全一致。

恢复 55° 正面机位后，1280×800 的同一 Vulkan 场景检查再次通过，记录与截图为 `build/sealed_gi_camera_vulkan.log`、`build/sealed_gi_camera_total.png`。该机位 12 帧平均 GPU 45.64 ms、含提交与等待 51.84 ms；可见内容已变化，不能据此视作同画面渲染算法加速。

## 历史：墙顶交线与固定细格修复

本节保存先前“固定细格双线性重建”的设计和验收，已被上文“实际位置 B2 核＋每格双表面代表＋受限方向模型”替代。以下旧版参数语义、四顶点实现、误差与耗时只描述当时版本；当前算法及新增验证入口以上文和“验证入口”为准。

嵌入顶灯会让墙顶产生真实的遮挡暗带，单次反弹也会使未受直射的天花板方向较暗。需要修正的是这些局部暗值被稀疏探针扩展成整个网格大小的块状阴影；本轮不增加环境补光，也不消除真实暗角。

当时参考 UE 5.7.1 的 `LumenScreenProbeGather.usf::CalculateUpsampleInterpolationWeights` 与 `SurfaceCache/LumenSurfaceCacheSampling.ush`，增加以下处理：

- Surface Cache 的照明在已覆盖的同一个对象、同一张卡片内做双线性采样，并检查深度、法线和切平面，不跨页寻找更亮的值。有效黑色参与完整权重；真正不可读的照明仍保持不可读。
- 仅在 GI 路径中，根据相邻 GBuffer 表面的法线和平面变化检测几何交线，并验证交线落在双方有限的世界空间邻域内。分离的前景椭球和后墙即使无限平面相交，也不直接触发真实交线细化。薄物体和曲面缺覆盖补点仍优先。
- 主动细化采用跨基础 tile 共享的固定细格，按照实际取整后的子格中心做双线性插值。每个顶点独立检查几何兼容性和细化包络；缺少或不兼容的细顶点仅将自己的权重留给基础场。取消最近四象限竞争、四点整体有效门槛、四点最小支持度及基础探针的方形查询遮罩，避免 8×16 像素周期的细粗切换。
- 几何支持不足时，兼容候选以连续紧支撑核累计后归一化，不再按 top-four 胜出集合整体替换。有效黑色和不可读 alpha 均使用相同权重，不以可读数量或亮度重新选择探针。`0.01` 表示充分几何覆盖，`0.0001` 表示仍可用的低置信度估计；原法线和双向平面硬拒绝保持不变。

上述固定细格属于本实验对 UE 按角点处理自适应贡献思路的适配，并非完整 GPU 压缩调度的逐行移植。当时正常细化路径只查询四个固定细顶点，几何覆盖不足时才搜索邻近候选；只存标量 `E/PI`，未实现方向重建、逐像素短程 GI 校正及多次反弹。空间过滤和 GI 时域继续处理剩余方差；该次改动未增加 UBO 大小、长期图像历史、全屏 Pass 或射线图集容量。

该历史版本 F1 的 GI 面板参数如下，当前面板的新增开关及分配范围语义见上文：

| 参数 | 默认 / 范围 | 用途 |
| --- | --- | --- |
| Geometry-aware cache bilinear | 开启 | 切换深度 / 法线约束的卡片照明双线性采样，关闭可对照原 nearest 读取。 |
| Refine geometric intersections | 开启 | 交线补点与连续重建的总开关；关闭保留旧估计器作为对照。 |
| Intersection detection (probe tiles) | 0.75 / 0.25～2 | 几何交线检测范围，以基础探针格为单位。增大覆盖更宽区域，也会激活更多探针。 |
| Intersection blend width (tiles) | 0.5 / 0.25～1.5 | 细化区域到基础场的过渡宽度，按固定顶点的交线置信度计算，不再将查询点距最远顶点的距离作为整组开关。有效宽度不超过检测范围。 |
| Intersection normal angle | 45° / 15～75° | 触发交线检测的法线差异角。减小会在较缓曲面上增加细化。 |

在 `GI inspection = Indirect GI` 且使用探针时，`Probe reconstruction check` 可切换：

- `Real lighting`：正常间接光，默认值。
- `Constant illumination`：所有有效探针赋照度 1，绕过接收材质和 GI 强度，用于检查归一化及真正的支持缺口。
- `Base / adaptive weights`：基础探针赋 0.25，自适应探针赋 0.75，隔离粗细混合比例与光线噪声。几何接缝两侧可以有真实标签差，不能将所有相邻跳变都当作错误。

合成诊断自动暂停该视图的空间和时域过滤，不覆盖真实射线或照度图集；退出后重建真实 GI 历史。真实黑色样本仍参与正常估计器。覆盖修补点可在子格中偏移，固定基函数仍是有限空间近似，不等同于精确的逐像素可见性校正。

`--surface-cache-edge-test` 新增轮廓回归。深度 / 法线断层两侧 16 像素均计入，包含球体、椭球、箱体、墙面、紧贴接缝及低支持区域，不按覆盖状态剔除难点。两个机位各建立 6 个姿态的 256 spp 同缓存逐像素参考，并连续渲染 32 帧平移与转向、原路返回；静态、首移动及有对应参考的运动检查点分别报告原始、空间和时域结果。正常方向相位保持一致，参考仍只验证当前单次反弹近似，不是独立路径追踪真值。

`--surface-cache-edge-diagnostics` 是只运行合成诊断和静态实际光照的快入口，不执行高采样参考或运动验收。两入口均检查常量保持、凸组合范围、关灯归零和合法黑色；正式 1280×800 的两个固定机位还限制双色诊断中大于 0.1 的相邻跳变比例低于 2%。这是包含真实几何接缝的保守回归界限，不等于任意场景没有跳变。原马赛克版本在两机位均超过此界限。Vulkan 全尺寸与 OpenGL 回归通过 opt-in GPU CTest 注册，保持串行执行。

### 历史 2026-10-18：固定细格版轮廓马赛克修复验收

1280×800、Vulkan validation、RTX 4060 Laptop、优化 Debug 构建。原马赛克版本与固定细格版本使用相同的场景、射线预算、放置相位和过滤参数；两组共 12 个参考姿态的数值及静态参考图一致。参考为同一个完成光照缓存上的 256 spp 逐像素单次反弹，不能作为 UE Lumen 或独立路径追踪真值。下面统计深度 / 法线边界两侧全部接收区域的时域输出，不排除曲面、低支持或接缝。连续运动每个机位实际渲染 32 帧，在有相应高采样参考的 10 个检查点计算误差。

| 机位 | 阶段 | 亮度 MSE 降低 | 负向亮度误差降低 |
| --- | --- | ---: | ---: |
| 墙顶交线 | 静止 | 38.64% | 50.80% |
| 墙顶交线 | 移动首帧 | 40.75% | 49.98% |
| 墙顶交线 | 连续移动 | 36.73% | 49.52% |
| 椭球 / 后墙 | 静止 | 43.98% | 28.09% |
| 椭球 / 后墙 | 移动首帧 | 44.58% | 23.58% |
| 椭球 / 后墙 | 连续移动 | 28.95% | 17.73% |

未经过空间或时域的原始 GI，连续运动 MSE 也分别下降 35.20% 和 30.52%，说明改进来自重建本身。双色诊断中大于 0.1 的相邻跳变数量分别由 11,364 降至 6,532、9,550 降至 2,670，减少 42.52% / 72.04%；占相邻配对的 1.48% / 0.84%，通过上述 2% 回归界限。常量诊断没有支持缺口，归一化 MSE 约 1.4～1.6e-15。真实几何接缝仍可能产生 0.5 的标签跳变，不能以最大跳变归零作为目标。

64 个运动帧全部复用 GI 历史。两个静态机位及移动首帧的真实支持缺口为零；墙顶机位连续运动的 7,158,630 个边缘像素样本中，缺口由 54 增至 72（约 0.0010%），椭球机位保持为零。关灯后的原始及累积 GI 均为零，同时保留 1,024,000 个可读取的合法黑色像素。仍有不足：墙面单独统计的负向误差在部分阶段增加约 5～14%，曲面均值偏差也未完全消除；总 MSE 下降不代表每个区域、每种误差都改善。宽光照偏差、有限采样及覆盖修补点的子格位置近似仍需要后续的方向重建和短程校正。

同一 1280×800 密闭房间，64 射线 / 探针、64 个面积光采样，预热后各测 12 帧，包含覆盖诊断，不呈现、不限帧、不做图像回读：

| 版本 | GPU ms | 含提交与等待的整帧 ms |
| --- | ---: | ---: |
| 原马赛克版本 | 88.31 | 94.14 |
| 固定细格版本 | 80.00 | 85.10 |

整条管线批次耗时约下降 9.4% / 9.6%，活动探针分布也有变化，不能将差值归为单个重建着色器的耗时。当前画质配置仍未达到 60 FPS；查看器默认不采集覆盖诊断，绝对耗时可能不同。前后日志为 `build/edge_mosaic_before.log`、`build/edge_mosaic_fixed.log`，逐项比较为 `build/edge_mosaic_fixed_compare.csv`，性能记录为 `build/edge_mosaic_perf_{before,after}.log`。最终合成结构检查见 `build/edge_mosaic_verified_diagnostics.log`。

Vulkan 的既有空间过滤、时域累积和密闭房间回归通过，未报告 validation 错误；日志为 `build/edge_mosaic_{spatial_regression,temporal_regression,perf_after}.log`。641×401 的非整格尺寸结构诊断也通过，见 `build/edge_mosaic_odd_edges.log`。PBR CPU 回归通过。OpenGL 640×400 的常量、粗细混合、真实静态 GI 和关灯检查通过，但独立逐像素参考的两组 seed 均值未产生可测差异，尚未通过参考独立性检查；本轮不据此声明 OpenGL 的高采样画质验收通过。`--surface-cache-edge-test` 完整参考入口因此限定 Vulkan，OpenGL 使用 `--surface-cache-edge-diagnostics`，对应 GPU CTest 也只检查该结构路径。完整画质与运动数据来自 Vulkan。

最终二进制的重建诊断分别记录在 `build/edge_mosaic_final_vulkan.log`（1280×800）与 `build/edge_mosaic_opengl_verified.log`（640×400），构建与 CPU 日志为 `build/edge_mosaic_final_build.log`、`build/edge_mosaic_cpu.log`。

`--surface-cache-corner-test` 对比无过滤估计器；`--surface-cache-corner-filtered-test` 对比实际 Spatial + Temporal 路径。两者均使用同一完成光照缓存上的独立逐像素三角形 BVH 参考，比较两个机位、墙顶两侧的同平面区域。后者固定探针放置相位，采用 1024 spp 参考，并单独记录镜头移动首帧、8×8 完整同平面块的误差和每组帧耗时。参考不进入实际渲染历史。测试仍检查关灯全黑、几何覆盖及背景无假光。

`--surface-cache-corner-sweep` 另比较 0.75 / 1 / 1.25 的作用范围，普通回归只运行原版、仅交线细化、仅卡片过滤、两者组合四组。正式 1280×800 对照采用两个机位、32 像素基础间距、每探针 64 条射线、3 轮空间过滤及原有 GI 时域参数。误差在 HDR 间接光上计算；暗块指标是完整同平面 8×8 块相对参考的负向亮度误差平方，不能将其下降百分比理解为整体 GI 误差下降。

交互查看器默认不计算未显示的全屏探针覆盖诊断，按 B 切入覆盖视图时自动恢复。公共设置 `collectProbeCoverage` 仍默认开启，兼容诊断调用；关闭后，覆盖回读明确返回未采集错误，不会返回旧帧。下文历史交线性能对照关闭此独立诊断，光照、空间过滤和时域累积保持开启；本节新的密闭房间性能对照则包含覆盖诊断，各自配置单独列明。局部加密依然会增加有效射线和重建成本，并非免费降噪。

以下是固定细格修复之前的更早历史结果，见 `build/surface_cache_corner_accepted.log`（Vulkan validation、RTX 4060 Laptop、优化 Debug），不代表当前实际位置核版本。两个机位共 235,787 个交线区域像素，每组统计四个连续帧；天花板有 2,032 个完整 8×8 块样本，墙壁有 9,880 个。

| 指标 | 原版 | 改进后 | 变化 |
| --- | ---: | ---: | ---: |
| 天花板负向块误差 | 4.96788e-7 | 2.26941e-7 | 降低 54.3% |
| 天花板像素负向误差 | 5.98244e-6 | 4.79305e-6 | 降低 19.9% |
| 全部区域亮度 MSE | 8.18700e-5 | 8.15534e-5 | 基本持平，降低 0.39% |
| 两个收敛机位之间的重投影误差变化 MSE | 1.64032e-5 | 1.38705e-5 | 降低 15.4%，不是连续运动指标 |
| 静止相位误差变化 MSE | 9.48247e-8 | 6.49680e-8 | 降低 31.5% |
| GPU / 整帧 ms | 55.38 / 61.03 | 87.24 / 93.17 | 边缘补采样仍有明显开销 |

以上是本场景的局部误差对照，不能称作整体误差降低 54%。天花板总块误差约增加 0.47%，墙壁总块误差降低约 53.6%，但墙壁很小的负向块误差从 9.94e-9 增至 3.72e-8。镜头移动首帧的整体 MSE 增加约 2.7%，负向误差降低约 13.3%；因此也不能称作每一帧、每一种误差都改善。角落仍有真实遮挡和单次反弹近似造成的暗区。

性能各测 12 帧，不呈现、不限帧、不回读图像；不是交互 FPS。该历史场景每帧有效自适应探针均值从 141.5 增至 877，容量不变，新增有效射线使整帧尚不满足 60 FPS。面板可关闭交线细化以取舍成本，卡片过滤可单独保留。曾尝试裁掉未组成相邻局部四点格的细探针，虽然减少射线，却增加整体偏亮误差，因此未采用。参考图及改进前后间接光图为 `build/surface_cache_corner_accepted_{reference,before,after}_{0,1}.png`；这些图只显示间接光，灯面本身呈黑属于该诊断视图的正常表现。

最终构建、PBR CPU 回归、Vulkan 空间过滤 / 时域回归、OpenGL 320×200 交线回归以及 Vulkan 1280×800 密闭场景回归均通过。空间回归保留了 982 条不可读取射线的状态，元数据误差为零；独立几何检查的穿遮挡配对为零。新交线入口另验证诊断关闭后不会返回旧覆盖图或重置 GI 历史，关灯后仍存在可读取的合法黑色样本。密闭场景关灯后的 raw、temporal、atlas 均为零。日志为 `build/surface_cache_corner_accepted_{spatial,temporal,opengl,sealed}.log`。小尺寸 OpenGL 检查没有完整天花板 8×8 块样本，不能用它代替正式分辨率的天花板质量指标。

## 启动和观察

双击 `Module/MaterialLab/RunSurfaceCacheGi.cmd`，默认以 Vulkan、1280×800、间距 32 像素的自适应探针和每探针 64 条射线运行红绿墙测试房间，启用 3 轮探针空间过滤、10 帧 GI 时域累积与 8 个方向相位，限制 60 FPS。普通快速更新历史上限为 8 帧、阈值为每帧 3 像素，启用 Sigma 6 历史裁剪与 2 帧几何边缘保护。启动器显式关闭原生光追；60 是上限，不是性能承诺。主光沿敞开的房间正面照入，使用 CSM 计算可见表面的太阳直射阴影，卡片直接光照使用软件 BVH 遮挡。关闭天空和环境补光。房间、卡片与实际渲染使用相同的固定线性漫反射材质；茶壶的三个 raster LOD 在此模式均为相同灰色，不将精度调试色注入 GI。

| 操作 | 按键 |
| --- | --- |
| 总光 / 卡片光照 / 间接光 / 卡片覆盖 / 图集 / 累计帧数 / 探针覆盖 | V，或调节面板的 GI inspection |
| 空间过滤开关 / 探针几何覆盖 | H / B |
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
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --surface-cache-spatial-test --screenshot build\surface_cache_spatial.png
output\sdf-banquet\Debug\bin\material_lab_demo.exe --surface-cache-spatial-test
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --surface-cache-adaptive-test
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --surface-cache-directional-test
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --surface-cache-performance-test --render-width 1280 --render-height 800 --msaa 1
```

新增 `--surface-cache-adaptive-test` 调用 `SurfaceCacheAdaptiveSurfaceQuality`（`Test/adaptive_surface_test.inl`）。它独立于旧轮廓画质入口，检查 4 / 8 / 16 槽配置、同子格多表面代表、常量场、实际世界位置仿射场、真实位移探针的 impulse 支持范围和亚像素相机变化。B2 核检查使用实际探针位置的独立 CPU 权重参考，仿射场相对目标像素的误差另行报告，不要求不规则样本达到零误差。核结构检查先关闭屏幕可见性以隔离职责；独立的可见性 A/B 部分再与原三角形的双精度求交对照，不能把三点屏幕近似当作精确几何 oracle。

新增 `--surface-cache-directional-test` 调用 `SurfaceCacheDirectionalQuality`（`Test/surface_cache_directional_test.inl`），固定 320×200 与每探针 64 射线。它回读实际 GPU 系数、探针法线和射线，检查 `m0/readability` 与原标量积分一致、非负方向界、原法线保持、不同接收法线求值、开关 A→B→A 和旧图集不可读保护；空间阶段的系数均值另与实际过滤后射线独立积分比较。故意不相关的卡片捕获提供真正未知覆盖，关灯提供有效黑色；两者的 alpha 不得混淆。方向开关变化还应使 GI 历史失效。

启用 `MATERIAL_LAB_GPU_TESTS` 且 Vulkan 后端可用时，新增 CTest 名称分别为 `material_lab_surface_cache_adaptive_vulkan_gpu_test` 与 `material_lab_surface_cache_directional_vulkan_gpu_test`，均使用 validation、显式软件追踪并串行执行。这些结构、积分与状态检查不替代真实场景的成对画质、连续运动和性能验收，也不复用本节历史表作为新版本结果。

原有卡片、探针与时域质量入口显式关闭新增的空间过滤；卡片与探针质量入口还关闭 GI 时域，保持原来的独立单帧基线。质量入口使用独立小房间，检查无光全黑且黑色卡片仍有效、关闭屏幕追踪后的缓存反弹、光强加倍时原始 HDR 反弹加倍、A→B→A 相机与光源变化后结果一致、静态帧不更新卡片光照、移动单物体只更新六张卡片，以及故意提供不相关卡片时显示洋红缺失覆盖但不注入 GI 能量。卡片太阳光另以独立双精度逐三角形遮挡测试和 `rho * sunlight * cosine / PI` 核对，覆盖受光、被遮挡和屏外地板样本，不复用 GPU BVH 或卡片查询。

新增空间质量入口单独关闭时域，用 8 个独立 seed、每探针 256 条射线建立离线参考，再检查另外 6 个 seed 的 16 射线结果：比较平面内部亮度误差、16×16 宽区块误差以及独立识别的几何边界误差。还检查强度为零时等于原始结果、无光无能量、光强翻倍线性、不可读取射线不被填充，以及状态、命中和方向元数据完全不变。自适应覆盖在三个视角下比较均匀与补充探针的真实几何缺口，分别要求薄柱和球面拥有有效支持；缺少兼容探针的像素和背景不因过滤获得虚构能量。探针对的防漏光检查使用实际准备网格及实例变换，以双精度逐三角形求交独立验证每个被接受的配对，不复用任何 BVH 遍历；对照关闭可见性检查时确实穿过遮挡的配对。最后检查空间输出进入时域，以及修改过滤参数会清空旧历史。

### 2026-10-16：空间过滤验证与开销（相机运动调参前）

2026-10-16 最终空间回归在 RTX 4060 Laptop 的 Vulkan validation 与本机 Intel OpenGL 均通过，显式软件追踪。下表列 Vulkan 结果。噪声误差测试关闭时域与最终图像 AA，比较线性 HDR GI；参考只在测试端由独立 seed 的高射线结果构成。256×192 的间距 24 是粗网格压力配置，另测间距 8，使屏幕探针数与 1280×800、间距约 40 的查看器接近。它不等同于在实际 1280×800 分辨率运行，也不覆盖所有 SDF / 卡片共同偏差。

| 指标 | 未过滤 | 空间过滤 | 变化 |
| --- | ---: | ---: | ---: |
| 粗网格平面像素亮度 MSE | 1.06050e-4 | 9.86998e-5 | 下降 6.93% |
| 粗网格 16×16 区块平均亮度 MSE | 9.23442e-5 | 8.82018e-5 | 下降 4.48% |
| 粗网格几何边界亮度 MSE | 1.45041e-4 | 1.43145e-4 | 下降 1.31% |
| 较密网格平面像素亮度 MSE | 8.82197e-5 | 4.44170e-5 | 下降 49.65% |
| 较密网格 16×16 区块平均亮度 MSE | 4.70363e-5 | 3.20082e-5 | 下降 31.95% |

较密网格使用 8 个独立参考 seed、每探针 256 条射线，评估另外 12 个 seed、每探针 16 条射线。相对参考误差和宽区块误差必须分别下降，边界误差另设上限；不以更换 seed 或放宽几何阈值代替算法修正。时域与空间的降低噪声作用不能直接相加为绝对画质收益：时域主要减小随帧变化的误差，空间混合引入邻域近似，仍会保留真实的几何亮度变化，并可能带来小量偏差。

同一优化 Debug 构建、RTX 4060 Laptop、1280×800、Vulkan validation、间距 32、每探针 64 射线，预热后各测 12 帧，不呈现、不限帧、不回读图像。表中是整条管线的开关对照，包含射线方向相位等变化，不能把批次差值完全等同于隔离的过滤着色器耗时。

| 场景 / 配置 | GPU ms | 含提交与等待的整帧 ms |
| --- | ---: | ---: |
| 房间：空间关、时域关 | 19.70 | 24.18 |
| 房间：仅空间过滤 | 21.22 | 27.31 |
| 房间：仅时域累积 | 21.34 | 26.24 |
| 房间：空间过滤 + 时域 | 22.31 | 27.42 |
| 茶壶宴会：空间关、时域关 | 40.66 | 46.68 |
| 茶壶宴会：仅空间过滤 | 42.96 | 49.14 |
| 茶壶宴会：仅时域累积 | 43.48 | 49.84 |
| 茶壶宴会：空间过滤 + 时域 | 46.01 | 54.16 |

单独开启空间过滤的 GPU 批次差值约为房间 1.52 ms、茶壶宴会 2.31 ms。默认 3 轮过滤对应统计中的 6 个空间 pass：1 次探针对几何 / 可见性表、3 次方向辐射率过滤、1 次过滤后探针积分、1 次过滤后像素重建。配对可见性不是对每条角度射线重复发射；原始积分和未过滤 GI 另行保留用于诊断。射线预算仍为 576,000 条的预留容量上限，过滤不增加主 GI 追踪射线数。日志为 `build/surface_cache_spatial_perf_room.log` 与 `build/surface_cache_spatial_perf_banquet.log`，最终时空组合效果图为 `build/surface_cache_spatial_room_final.png`、`build/surface_cache_spatial_banquet_final.png`。默认房间和宴会整帧时间仍未达到稳定 60 FPS，不能只用 GPU 时间倒数当作交互帧率。

三个视角的覆盖压力测试（间距 64、每格候选 16）中，真实缺口由 34,501 个降为 644 个；默认间距 32、候选 8 时，147,456 个有效接收像素中的缺口由 15,662 个降为 525 个，减少约 96.6%，剩余约占有效像素的 0.36%，但没有归零。这仍是 256×192 的测试图像，并非 1280×800 查看器的实时缺口数量或活动探针数。默认三个测试视角合计实际活动基础探针 144 个、自适应探针 283 个，这是活动数而非预留容量。独立逐三角形可见性核对中，关闭配对可见性检查时存在 9 个穿过遮挡的配对；开启后为 0，同时保留了 244 个被独立证实可见的配对。552 条活动但不可读取的射线保持原样，元数据误差与光强加倍误差均为零。较密网格的一帧中，12,833 条可读射线有 4,427 条被空间邻域改变。日志与覆盖图见 `build/surface_cache_spatial_vulkan.log`、`build/surface_cache_spatial_opengl.log`、`build/surface_cache_spatial_coverage_default.png`。这些有限场景的检查不等于任意场景下绝无漏光或无覆盖缺口。

加入空间环节后，Vulkan 原有时域、探针与逐像素参考回归继续通过，分别见 `build/surface_cache_spatial_temporal_regression.log`、`build/surface_cache_spatial_probe_regression.log`、`build/surface_cache_spatial_reference_regression.log`。这些旧入口显式关闭空间过滤，保留独立验证职责：时域稳定区域 49,071 个像素的时间方差由 `2.93636e-4` 降至 `4.20130e-6`，均值能量误差约 0.084%；探针测试实际活动 176 个（其中自适应 96 个），11,264 条射线的积分最大误差约 `1.47e-7`，接收面反射率只乘一次、16 / 64 / 256 射线收敛继续成立；参考路径的 72 张卡片、光强线性和 Mesh / Global 分阶段补追也通过。这些分别是未过滤的既有路径验证，不将其收益再叠加到空间过滤的参考误差百分比上。

分阶段追踪分别运行 Mesh-only 与 Global-only 两组：先关闭 Surface Cache 记录原本不可读取的几何命中，再开启缓存，要求原来的 11 / 12 状态变为 9 / 10，保留相同世界命中位置，且源像素坐标为 -1（从卡片读取）。测试还要求实际屏外命中得到补充，并保持未命中、覆盖缺失、步数耗尽和非法起点等状态的分类。

探针质量入口读取实际 GPU 射线方向，以独立解析积分核对余弦分布的均值、二阶与四阶矩、球冠概率和切向对称性；固定 45° 圆锥会直接失败。还核对 `E/PI` 与实际射线辐射率的积分、已知黑色分母、自适应探针对最终图像的实际贡献、光强线性、固定 seed 的 A→B→A 无历史依赖、角度分辨率与空间间距对应的射线调度，以及缓存视图不追踪无用 GI 射线。

对条带与宽光斑另做量化收敛检查：固定相机、表面探针位置和光照，用 8 个独立 seed、每探针 256 条射线的结果在测试端形成参考，再用另外 4 个 seed 分别测量 16 / 64 / 256 条射线。检测平面上的探针入射光误差、最终 HDR 像素亮度误差，以及分表面的 16×16 区块平均误差，要求随着射线增加而下降。区块统计只用于离线误差分析，不写回渲染器；参考消除了真实几何亮度渐变对“光斑”的混淆，但无法证明所有档位共同存在的 SDF、卡片或重建偏差已经消除。

时域质量入口读取实际 raw GI、累积 GI 和每像素累计帧数，检查方向相位逐帧改变、静止方差降低、帧数上限、单帧模式、相机切换与重新露出表面的拒绝、正常平移的历史复用，以及光照、几何、参数、分辨率和模式切换后的历史失效。比较在最终曝光和色调映射之前进行，主对照检查关闭最终图像 TAA；另有两帧显式打开 TAA，验证其相机抖动不会清除独立的 GI 历史。

2026-10-16 首次时域版本的 Vulkan 回归（相机运动调参前，256×192 小房间、每探针 16 条射线、8 帧历史 / 8 个方向相位）中，48,456 个稳定接收像素的逐帧 RGB 方差由 `0.000361598` 降为 `0.000004925`，下降约 98.64%。这是同一表面上随方向相位变化的时间方差，不代表相对于真实光传输的绝对误差下降同样比例。相机平移 0.12 世界单位后，47,617 个像素复用了通过几何检查的历史，817 个像素从一帧重新开始。原有关闭时域的探针 / 卡片质量测试继续作为单帧对照。

Vulkan 与 OpenGL 的完整时域回归均通过，稳定区域的平均能量相对同批原始方向相位均值偏差约 0.09%。测试另覆盖聚光灯位置与方向改变、接收面材质改变时清空历史，以及 TAA 相机抖动下独立 GI 历史的复用；关闭时域后的累计帧数视图不调度无用探针射线。记录见 `build/surface_cache_temporal_vulkan.log`、`build/surface_cache_temporal_opengl.log`，单帧回归见 `build/surface_cache_temporal_probe_regression.log`、`build/surface_cache_temporal_reference_regression.log`。

以下为 2026-10-16 首次时域版本、相机运动调参前的历史计时，不替代当前默认参数的运行测量。同一优化 Debug 构建、RTX 4060 Laptop、1280×800、Vulkan validation、显式软件追踪，间距 32 像素、每探针 64 条射线，各 12 个无呈现/不限帧/无图像回读的计时帧：

| 场景 | 关闭时域 GPU / 整帧 ms | 开启时域 GPU / 整帧 ms |
| --- | ---: | ---: |
| 红绿墙房间 | 15.52 / 20.74 | 17.49 / 23.52 |
| 茶壶宴会 + Clipmap | 30.22 / 38.16 | 31.23 / 40.24 |

这是整条管线的开关对照，时域模式也会更换射线相位，差值不是完全隔离的时域着色器耗时。新增四个全屏 pass，不增加每帧探针射线预算。历史采用双缓冲颜色/计数、世界位置、法线/深度及一个可读覆盖输出，共七张 RGBA32F 纹理；1280×800 时约 109 MiB，调整最大累计帧数不会按帧数增加纹理数量。整帧结果仍未达到稳定 60 FPS，物体动态更新仍有现有 CPU 几何/SDF 更新成本。性能记录为 `build/surface_cache_temporal_perf_room.log`、`build/surface_cache_temporal_perf_banquet.log`，默认模式截图为 `build/surface_cache_temporal_{room,banquet}_final.png`。

性能入口预热后分别测量无 GI、16 / 64 / 256 条探针射线、64 条射线额外启用时域、较密的 16 像素探针间距、逐像素参考的 1 / 4 / 8 条射线、缓存视图与动态对象更新。每个静态项目测量 12 帧，批次内不呈现、不限帧、不回读图像；GPU 时间和包含提交等待的整帧时间分别记录。加上 `--sdf-clipmaps` 可测量茶壶宴会，不能把房间结果代替高精度模型负载。`surfaceCacheRays` 和 `surfaceCacheProbeRayBudget` 是含非活动预留槽的配置上限；实际有效探针与射线数需通过诊断回读单独统计。

## 历史：标量自适应探针的光斑收敛记录

以下为早期标量积分与旧覆盖分配的采样收敛记录，保留作实现演进对照，不代表当前实际位置核和方向模型的画质收益。

`build/surface_cache_probe_vulkan.log` 的 320×240 房间测试中，101 个有效探针包含 21 个自适应探针，共发射 6,464 条有效射线（默认每探针 64 条）。实际 GPU 方向的 `E[cos(theta)]=0.66718`、`E[cos²(theta)]=0.500166`、`E[cos⁴(theta)]=0.333363`，分别符合余弦 PDF 的 `2/3`、`1/2`、`1/3`；不存在所有射线固定在 45° 的圆锥。积分最大误差为 `1.24e-7`。保持缓存入射光不变、将接收地板反射率减半后，26,618 个地板像素的间接光恰好减半，探针入射积分保持不变。

固定位置，针对 69 个平面探针、58,788 个墙壁/天花板/地板像素和 287 个分表面的 16×16 区块，以独立 seed 的离线高采样参考测得：

| 每探针射线 | 探针 RGB 均方误差 | 平面 HDR 亮度均方误差 | 区块平均亮度均方误差 |
| --- | ---: | ---: | ---: |
| 16 | 6.82550e-4 | 1.27535e-4 | 1.27382e-4 |
| 64 | 1.26987e-4 | 2.79871e-5 | 2.70075e-5 |
| 256 | 1.99783e-5 | 3.69185e-6 | 3.82548e-6 |

在该场景与参考下，64 条射线相对 16 条的宽区块误差下降约 79%，256 条相对 64 条再下降约 86%。这是多个 seed 的统计收敛结果，不表示每一个 seed、每一个像素都会单调改善，也不证明卡片分辨率和几何覆盖的共同误差已经消失。测试端形成的参考和区块统计不参与交互渲染，未加入时空过滤。

Vulkan（RTX 4060 Laptop，validation、显式软件追踪）和 OpenGL（Intel）均通过以上探针回归，包括冻结位置后改变相机时强制重建屏幕格子。逐像素参考、旧 Mesh / Global SDF 追踪与三项 CPU 回归也通过。记录见 `build/surface_cache_probe_{vulkan,opengl,reference_regression,legacy_sdf}.log`。

## 先前无过滤探针版本性能记录

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

卡片直接光支持方向光、点光、聚光和矩形面积灯；天空漫反射尚未接入缓存，开启该组合会给出明确校验错误。主画面点光的直射仍继承现有 PBR 路径，独立阴影尚未接入；默认太阳场景使用 CSM，新密闭陶瓷场景为矩形灯单独启用缓存深度图软阴影，卡片中的支持光源则使用软件阴影射线。六向单层卡片、固定页和完整 BVH refit 也是后续优化项，不等同于 UE 的虚拟分页与调度实现。
