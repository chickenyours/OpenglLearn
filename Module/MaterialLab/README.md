# MaterialLab：新材质系统的独立试验模块

本次陶瓷椭球场景请启动 [RunSealedGiRoom.cmd](RunSealedGiRoom.cmd)（`--surface-cache-sealed-room`，默认 **1280×800**），按F1切换 `Static probe convergence (64 frames)`；`RunGiRoom.cmd` 是另一条旧点灯房间 / lumenGi路径。近距离静止残余印痕由新样本、64帧历史与匹配角度单元空间复用共同处理。`staticConvergence` 默认开启，仅在历史有效、无动态照明标志且view/projection精确相同时开放64帧上限，保留原运动保护；各probe仍独立jitter，不代表世界方向严格相同或立即无偏。没有新增GPU绑定接口、持久纹理 / UBO、pass、每帧射线数、空间模糊或receiver-query偏移，但静止常量多一份placement UBO版本snapshot，实际成本仍需配对测量。方法与本轮独立结果见 [近距静止椭球的探针采样收敛](SURFACE_CACHE_GI.md#2026-10-19近距静止椭球的探针采样收敛)。

```text
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --procedural --surface-cache-probe-imprint-test --surface-cache-probe-imprint-convergence --surface-cache-probe-imprint-view 0 --surface-cache-curved-contact-reference-seeds 32 --render-width 640 --render-height 400 --screenshot build\probe_stationary_front.png
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --procedural --surface-cache-probe-imprint-test --surface-cache-probe-imprint-convergence --surface-cache-probe-imprint-convergence-frames 64 --surface-cache-probe-imprint-view 1 --surface-cache-curved-contact-reference-seeds 32 --render-width 640 --render-height 400 --screenshot build\probe_stationary_near.png
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --software-rays --procedural --surface-cache-probe-imprint-performance --surface-cache-probe-imprint-convergence --render-width 1280 --render-height 800
```

收敛专项固定上一轮两项曲面 / 风险修正开启，仅比较静止收敛开关。质量默认分开报告32/64/128帧的当前GPU图、CPU累计平均及最近32帧平均，`--surface-cache-probe-imprint-convergence-frames 64` 可仅检查32/64帧；性能每批预热64帧、计时16帧。以下曲面颜色过渡章节及其质量 / 成本数字保留上一轮配置范围，不作为本轮静止收敛的实测结论。

[正式正面640×400对照](../../build/probe_stationary_front.log) 已完成128帧：当前GPU temporal图的椭球外壳RGB MSE下降44.8%，phase32残差能量仅下降5.6%，且早期检查点的phase32曾上升。新图MSE已接近1024-spp参考噪声底，因此不能把44.8%称为总噪声或瘢痕降幅。活动探针数和未知覆盖约束通过；[第128帧新旧图与完整限制](SURFACE_CACHE_GI.md)见方法小节，近接触与性能专项单独记录。

[正式近接触640×400对照](../../build/probe_stationary_near.log) 的第64帧当前temporal图中，椭球外壳 / 过渡RGB MSE下降42.39% / 13.55%，方柱核心 / 过渡下降27.51% / 24.25%；核心亮度和对比误差减少。但该视角椭球外壳phase32残差上升17.74%，不能泛化为印痕全面改善。新旧探针数均964，未知覆盖保留；[完整分阶段指标、核心ROI限制与新旧图](SURFACE_CACHE_GI.md#接触近景的正式结果)分开报告。

静止收敛的 [1280×800配对性能](../../build/probe_stationary_perf_1280.log) 为整帧358.082→333.923 ms，差−24.1582 ms（−6.74656%）、95%区间[−48.2311, −0.08528]。区间上界贴近零，证据有限，不能普遍承诺提速；trace与接触filter组区间均跨零。

[2560×1440第一轮](../../build/probe_stationary_perf_2560.log) 为919.460→894.377 ms，差−25.0833 ms、区间[−115.868, 65.701]；[同协议独立复测](../../build/probe_stationary_perf_2560_repeat.log) 为824.312→783.679 ms，差−40.6336 ms、区间[−101.346, 20.0791]。两轮三scope区间都跨零，首轮首对波动明显，结果不合并或挑轮，不能据此宣称零成本或普遍提速。完整三scope分布及测试范围见 [成本与回归](SURFACE_CACHE_GI.md#静止收敛的gpu成本与回归范围)。

[最终构建](../../build/probe_stationary_final_build.log)、[最终CPU设置](../../build/probe_stationary_final_cpu.log)、[Vulkan时域](../../build/probe_stationary_temporal_vk.log)、[OpenGL空间](../../build/probe_stationary_spatial_gl.log) 与 [OpenGL方向契约](../../build/probe_stationary_directional_gl.log) 已通过。OpenGL使用Intel GL4.5，和性能表的RTX 4060 Vulkan设备不同；这些结构 / 数学回归不能宣称两后端视觉等价，也不代表完整edge / recovery GPU覆盖。原Final Gather运动fixture关闭接触补追与新静止收敛以隔离probe覆盖和旧history cap；旧全reset检查另显式关闭运动历史保留，独立认证运动fixture继续测试默认保留路径。候选 / 断言 / 门限不变，[最终连续运动隔离回归](../../build/probe_stationary_legacy_motion.log) 的320×200、16条射线、三路径检查已通过，72个对齐帧raw相位公平性误差为0。

本轮针对椭球上的规则探针印痕与探针 / 接收点补追交接色差，加入受几何命中视差与法线旋转限制的曲面照明支持，并让 Contact mask 使用相同重建支持。物理表面聚类、选点和探针分配覆盖保留独立严格权重；最近真实几何命中限制额外借光，接收点命中视差在 **5–10°** 间退回旧支持；重建与空间过滤另按法线旋转在 **10–20°** 间退回旧支持，coverage与解析诊断使用相同法线支持。复用的 directional part1.w 存几何 `max(1/d)`，含黑色与缓存未知命中，RGB/readability 的完整预算分母不变。近命中比例提供几何风险下限，risk→mask 外带使用更宽渐变；年轻历史恢复按连续置信度处理交接。默认每探针64条、每选中接收点8条射线，复用已有 atlas/UBO；固定场景 / 相位下预期探针数量与位置保持一致，补追覆盖仍可能改变，实际计数和外带扩张的成本以专项核对。方法、元数据契约和最终记录见 [表面探针印痕与补追色彩过渡](SURFACE_CACHE_GI.md#2026-10-19表面探针印痕与补追色彩过渡)。

```text
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --procedural --surface-cache-probe-imprint-test --surface-cache-probe-imprint-view 3 --surface-cache-curved-contact-reference-seeds 32 --render-width 640 --render-height 400 --screenshot build\probe_imprint_views.png
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --software-rays --procedural --surface-cache-probe-imprint-performance --render-width 1280 --render-height 800
```

质量专项分开检查五级 HDR、平均图RGB误差、相位印痕、参考噪声底与接触剖面；相位/高通下降本身不构成画质证明。性能专项不运行质量 oracle、不回读图像，按四对AB/BA批次分别报告整帧、trace和filter。F1可切换 `Curved surface lighting weights` 与 `Coherent near-contact probe risk`。以下面积灯与接触过滤数据保留对应旧版本的历史范围，不能直接作为这轮组合的质量或成本结果。

[正式1280×800三视图质量对照](../../build/probe_imprint_verified.log) 已通过1024-spp共享Surface Cache参考检查。temporal平均图中，椭球外壳32像素相位残差能量在两近景减少44.78% / 41.97%，这是格相关残差指标，不是总噪声降幅；接触近景椭球过渡RGB MSE为+0.126%、基本持平，部分过渡相位指标仍上升。接触核心与对比约束、未知覆盖、两项历史失效和[8布局几何契约](../../build/probe_imprint_ship_geometry.log)均通过；实际探针数新旧一致，补追覆盖仍变化。完整指标、参考底限制与[新旧同视角图](SURFACE_CACHE_GI.md)见方法小节。

RTX 4060 Laptop、Debug、Vulkan软件射线、contact_near视角的配对性能，每配置64帧：

| 分辨率 | 旧整帧 ms | 新整帧 ms | 新减旧 ms | 配对95%区间 ms |
| --- | ---: | ---: | ---: | --- |
| 1280×800 | 253.307 | 257.823 | +4.516（1.783%） | [−10.825, 19.857] |
| 2560×1440 | 844.059 | 925.243 | **+81.185（9.618%）** | **[27.739, 134.631]** |

2560高分辨率专项的额外成本明确；1280区间跨零，不能称免费。2560的trace组为464.270→508.567 ms，filter（含resolve）20.883→24.506 ms；完整三scope均值、分位数及配对区间见 [GPU成本表](SURFACE_CACHE_GI.md#最终gpu成本与后端回归) 与 [1280日志](../../build/probe_imprint_perf_1280.log)、[2560日志](../../build/probe_imprint_perf_2560.log)。scope不是可相加的独占成本，不能仅用组差之和解释整帧净增；默认无额外pass也不等于无成本。

本轮 [Vulkan方向契约](../../build/probe_imprint_verified_directional.log)、[OpenGL方向契约](../../build/probe_imprint_directional_gl.log)、[Vulkan灯框质量回归](../../build/probe_imprint_lamp_vk.log) 和 [OpenGL灯框结构回归](../../build/probe_imprint_lamp_gl.log) 均完成并通过。后者没有高采样画质断言，直接光RGB8旧问题仍限制其范围，不能据此宣称两后端视觉等价。

墙顶静止宽条带现改为固定分层面光源采样：仍用64个源点和24 MiB缓存阴影atlas，但每个8×8单元的源点错开，让横纵投影各覆盖64个细子层，减少灯框同时遮住整排光源形成的明暗台阶。总光 / 直接光 / 独立GI分图已定位这部分结构性问题；它不是Lumen无法改进的固有限制。本次没有增加RGB模糊或时域抖动，CPU阴影atlas、不透明PBR与Surface Cache BVH三路共享源点。封闭顶灯房间默认开，普通面积灯默认保留旧中点模式；F1提供 `Stratified emitter samples` 和 `Direct area shadows enabled`。方法、UE职责、480×300诊断及高分辨率实测记录见 [墙顶过渡说明](SURFACE_CACHE_GI.md#2026-10-19墙顶静止宽条带与确定性面光源采样)。尚不宣称消除了全部随机GI颗粒或几何锯齿。

```text
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --procedural --surface-cache-wall-transition-test --render-width 1280 --render-height 800 --screenshot build\wall_transition_stratified_high.png
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --software-rays --procedural --surface-cache-wall-transition-performance --render-width 1280 --render-height 800
```

`--surface-cache-wall-transition-legacy` 切换旧中点诊断；性能专项在同进程自行交错新旧模式，禁用validation、呈现及回读。直接阴影的运行时开关与材质opt-in分离，避免误清普通PBR的天空纹理10 / 静态探针UBO12。下方保留前两轮接触GI结果，其配置、指标和性能范围与本次面光源采样修改不同。

[1280×800新旧三视图分图](../../build/wall_transition_final.log) 与缓存 / 历史断言已通过，原宽平行台阶明显减少：[旧总图](../../build/wall_transition_legacy_final_corner_lamp_total_renderer_ldr.png) · [新总图](../../build/wall_transition_final_corner_lamp_total_renderer_ldr.png)。RTX 4060 Laptop、Debug、Vulkan软件射线的同进程配对性能中，1280×800开启GI均值161.170→162.237 ms，差+1.067 ms、95%区间[-3.876,6.010]；2560×1440为449.073→451.640 ms，差 **+2.567 ms（+0.572%）**、区间[0.262,4.873]。这是仅改变源点分布的稳态整帧成本，接触过滤配置不变；跨零区间不能称为免费，现有GI绝对耗时仍高，也不能当作交互FPS。直接光单独测量、预热范围、GPU单元与灯框回归见[完整结果](SURFACE_CACHE_GI.md#仅改变源点分布的gpu开销)。

[Vulkan灯框质量回归](../../build/wall_transition_lamp_vk.log)（480×300、3视图、13配置、256 spp）和[OpenGL灯框结构回归](../../build/wall_transition_lamp_gl.log)（320×200、无高采样画质断言）均通过；OpenGL直接光RGB8读取为0，此项不作为直接阴影画质验证。独立面积灯GPU单元已在OpenGL中核对真实积分、采样结构和16/64点预算。

密闭 GI 房间的椭球 / 方柱接触现增加接收点几何风险检测、同表面 3×3 软风险扩展、曲率兼容过滤和命中距离视差约束。软扩展填补稀疏 mask 的小孔，只扩展几何风险，不混合 RGB；新增接收点仍独立认证起点并发射真实射线。默认仍为每个选中像素 8 条独立时域射线；三轮过滤逐步扩大支持，近接触保留合法的 1 像素降噪支持，最后只与探针基础光照混合一次。F1 的 `GI contact reconstruction` 可切换 `Contact hit-distance / curved filter` 和 `Receiver screen contact detection` 比较上一轮配置。实现、UE 来源、质量与独立 GPU 性能方法见 [SURFACE_CACHE_GI.md](SURFACE_CACHE_GI.md#2026-10-19椭球--方柱残噪与独立-gpu-计时)。

上一轮约 6% 的曲面最终高通残差下降受 1024-spp 参考噪声和参考起点差异限制，不能直接等同于可见噪声改善。新质量专项使用相同安全起点的 1024 / 2048 / 4096-spp 参考，并检查两侧五档距离的暗接触剖面；性能专项分别测整帧、分类 / 追踪组及过滤组，覆盖新增选区带来的实际射线开销。

最终八 pass 配置已通过 [1280×800、objects、4096-spp 质量专项](../../build/curved_contact_padded_final.log) 和 [640×400、1024-spp 三视图专项](../../build/curved_contact_padded_views.log)。高分辨率结果中，最终亮度 MSE 下降 36.65%、静止帧间方差下降 26.93%，最终静态高通残差下降 2.19%；两侧接触核心绝对误差均减少。当前帧过滤的高通改善为 48.11%，应与最终结果区分；参考噪声底较高，不能据扣底比率宣称约 20% 的最终静态降噪。完整剖面数据见实现说明。

同视角总图：[修改前](../../build/curved_contact_padded_final_objects_previous_total.png) · [修改后](../../build/curved_contact_padded_final_objects_improved_total.png)。CPU 设置与命令缓冲测试、Vulkan 灯框质量回归、OpenGL 灯框结构兼容回归和 GLSL 形参兼容修复后的 Vulkan 烟测均通过，范围与日志见实现说明。

RTX 4060 Laptop、Debug、Vulkan 软件射线的最终性能专项每种配置均测 96 帧、六组批次配对：[1280×800](../../build/curved_contact_padded_perf_1280.log) 的过滤组（含 resolve）均值 3.148 ms，完整方案相对上一轮整帧增加约 14.321 ms；[2560×1440](../../build/curved_contact_padded_perf_2560.log) 的过滤组均值 13.184 ms，完整方案增加 **219.241 ms（约 38.74%）**，95% 区间 [209.006, 230.878]，新增成本主要表现在检测与真实接收点补追组。相同新 mask 下，1280 的过滤开关整帧差区间跨零，2560 则增加 24.274 ms、95% 区间 [16.050, 33.773]；两种分辨率的结论应分别报告。上述2560×1440是高分辨率离屏专项；本场景正确入口 `RunSealedGiRoom.cmd` 默认1280×800，`RunGiRoom.cmd` 属于另一条旧点灯房间路径。分组跨度不能当作实际整帧净开销，也不能按像素数线性外推。

```text
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --procedural --surface-cache-curved-contact-test --surface-cache-curved-contact-view 0 --surface-cache-curved-contact-reference-seeds 128 --render-width 1280 --render-height 800 --screenshot build\curved_contact_final.png
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --procedural --surface-cache-curved-contact-test --surface-cache-curved-contact-reference-seeds 32 --render-width 640 --render-height 400 --screenshot build\curved_contact_views.png
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --software-rays --procedural --surface-cache-curved-contact-performance-test --surface-cache-curved-contact-view 0 --render-width 1280 --render-height 800
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --software-rays --procedural --surface-cache-curved-contact-performance-test --surface-cache-curved-contact-view 0 --render-width 2560 --render-height 1440
```

质量默认覆盖 objects / near / detail 三个视图；`--surface-cache-curved-contact-view 0|1|2` 可单测。性能需分别指定三个视图，禁止 `--validation`，不呈现、不回读图像。新增 measurement 图相对上一轮增加 15.625 MiB（1280×800）；过滤组包含额外 resolve，整帧数据还包含几何分类及扩大追踪覆盖的成本。历史白噪声条带记录保留在实现说明中。

## 实时参数调节窗口

所有交互入口接入 Dear ImGui 调节窗口，包括材质画廊、Showcase、密闭 GI 房间、Lumen adaptive 和探针 / SDF 场景。场景查看器启动时先收起窗口，保留原场景与 HUD；按 **F1** 打开或收起，材质画廊仍默认展开。窗口显示时，场景快捷键仍可用；操作控件、输入文本或弹出菜单时，相应输入交给界面。原有场景 HUD 帮助开关在面板的 `Help / scene controls` 中；使用 `--no-debug-ui` 可完全关闭界面，恢复原有 F1 场景帮助操作。离屏质量回归和性能测量自动关闭 UI。

面板按材质、光照 / 阴影、GI / 探针、后处理 / AA、统计和帮助分组。`Find panel` 按组名筛选；滑块支持 Ctrl+单击输入数值并限制合法范围；各参数组可重置到本场景启动值。`Store preset` / `Restore preset` 保存和恢复一份内存中的 effects / exposure 参数快照，不包含材质、相机、物体变换或场景灯光开关，重启或切换场景后不保留。

SDF 场景的 GI / 探针组还提供 Mesh / Global / 分屏视图、MDF 质量与自动选择、体素网格、对象隔离、切片，以及 Clipmap 层级、着色、边界和模型 LOD 控件。面板与原快捷键修改同一份设置；重置 SDF 检查只恢复距离场显示参数，不改变相机和物体位置。各交互场景 `.cmd` 统一优先使用 `output/sdf-banquet/Debug/bin` 中的当前完整程序，旧场景脚本仍可回退到 Release 和 `bin`，避免入口运行不同年代的构建；独立性能测量脚本保留原构建选择。

材质组复用 `InspectMaterial` / `SetMaterialParameter` / `ResetMaterial`；修改粗糙度或金属度标量会关闭对应贴图。Showcase、房间和探针场景使用自己的材质，不显示无效的画廊材质编辑。世界 GI 参数只在包含 GI 场景几何的入口开放；画廊中可先按 Q 进入 lighting study。统计读取上一帧已完成的管线计数，不额外读回 GPU。截图预热与离屏截图在 UI 帧结束前执行，保持场景截图不含调节窗口。

```text
bin\material_lab_demo.exe --procedural
bin\material_lab_demo.exe --vulkan --gi-room --procedural
bin\material_lab_demo.exe --vulkan --showcase --no-debug-ui
```

公共 `Module/DebugUI` 管理 ImGui / GLFW 的生命周期、绘制数据快照和 RHI 资源；通过 `RHIFrameEncoder` 在场景后处理之后、呈现之前叠加界面，OpenGL 和 Vulkan 使用同一适配器。材质实验室的业务面板集中在 `Test/material_lab_debug_panel.h`，不把 ImGui 控件或原生图形对象传进 Render 的接口。后续实验在 runner 的 `DebugPanelHost` 上注册回调即可增加一组参数，不必修改渲染后端：

```cpp
DebugPanelHost panels(lab, nativeWindow);
panels.AddPanel("experiment", "My experiment", [](DebugPanelHost::Context& context) {
    ImGui::SliderFloat("Exposure", &context.frame.exposure, .05f, 8.f,
                       "%.2f", ImGuiSliderFlags_AlwaysClamp);
});
// 每次 PollEvents 后创建；Finish 在最终呈现 Render 之前调用。
// 离屏截图或预热先完成，再 Finish；提前退出 / 最小化由 RAII 平衡帧。
DebugPanelFrame uiFrame(panels);
uiFrame.Finish(frame);
lab.Render(frame);
```

Lumen 探针布置学习入口：[专用场景说明](PROBE_PLACEMENT.md)，双击 [RunProbePlacement.cmd](RunProbePlacement.cmd) 启动。

第二阶段 HZB 屏幕光追：[Screen Probe 追踪说明](SCREEN_PROBE_TRACING.md)，双击 [RunScreenProbeTracing.cmd](RunScreenProbeTracing.cmd) 启动。

第三阶段 Mesh / Global SDF 回退：[距离场追踪说明](SDF_PROBE_TRACING.md)，双击 [RunSdfProbeTracing.cmd](RunSdfProbeTracing.cmd) 启动。默认三维分屏比较 Mesh 与 Global；F12 切低 / 中 / 高精度，L 自动按距离选择，V 切普通场景与距离场视图。

真实模型与远中近切档：[经典茶壶宴会说明](SDF_MODEL_BANQUET.md)，双击 [RunSdfBanquet.cmd](RunSdfBanquet.cmd) 启动。12 个导入 Utah Teapot 共享 24 / 48 / 128 Mesh SDF，并按距离选择真实 Raster LOD；首选已接入 Model / Assimp 的优化 Debug 程序。

后续 Global 多级滚动：[相机 Clipmap 说明](GLOBAL_SDF_CLIPMAPS.md)。`RunSdfBanquet.cmd` 现默认进入该场景，四级真实距离纹理按命中层级着色；K 检查自动 / L0–L3，C/H 开关层级色与边界，走动时仅合并新进入的砖块。

Surface Cache 与基础软件 GI：[数据结构、光传输和操作说明](SURFACE_CACHE_GI.md)，双击 [RunSurfaceCacheGi.cmd](RunSurfaceCacheGi.cmd) 启动。默认使用红绿墙房间，由自适应表面探针发射软件射线读取相机无关的卡片光照，再经探针方向辐射率空间过滤、几何兼容重建与独立 GI 时域累积。H / T 分别开关空间过滤与时域，B 查看实际探针几何覆盖，F1 调节过滤、探针密度与每格候选容量；V 轮换各诊断视图。使用 `--sdf-clipmaps` 可在茶壶宴会中检查同一路径。逐像素路径保留为 F6 显式参考。

GI 时域默认总上限 10 帧、方向相位 8；超过每帧 3 像素时普通表面保留最多 8 帧。Sigma 6 历史裁剪同时覆盖超过 0.1 像素的缓慢相机运动，几何边缘单独采用 2 帧历史与更紧裁剪。实际 1280×800、64 射线的 Vulkan 转向、平移和组合轨迹验证通过：合计平面误差降低约 14.3%，误差变化降低约 31.6%，轮廓指标基本相当；停下后短时间内仍有响应代价。F1 可独立调节 `Clamp slow camera motion` 和边缘保护。默认 AA=None，TAA 抖动尚可能触发运动裁剪；本机 GPU 约 27 ms，仍未达到稳定 60 FPS。完整结果和限制见[相机移动时的噪声与调节](SURFACE_CACHE_GI.md#相机移动时的噪声与调节)。

新的方形顶灯陶瓷场景：[RunSealedGiRoom.cmd](RunSealedGiRoom.cmd)。它使用当前 Surface Cache / 自适应探针 GI：8×7×4.5 的密闭房间、红绿蓝黄四色墙、中央嵌入式方形面积灯和六个釉面陶瓷几何体。L 开关唯一顶灯，F5 比较直接光与 GI，V 切换间接光等视图，1/2/3 切室内镜头；F1 的 GI 面板调灯亮度和采样参数。矩形灯的直接软阴影使用可复用的深度图缓存，GI 卡片照明使用软件 BVH 遮挡；关闭太阳、天空与环境补光。详见 [场景说明](SURFACE_CACHE_GI.md#密闭陶瓷房间与方形顶灯)。

## 密闭 GI 房间

双击 `Module/MaterialLab/RunGiRoom.cmd`，或者运行：

```text
output\Release\bin\material_lab_demo.exe --vulkan --gi-room --procedural --fullscreen --render-width 2560 --render-height 1440 --msaa 1 --fps 0
```

房间为 8×8×5 米的完全封闭内向壳体，只有一个暖色点光源、三件可移动的粗糙物体，以及相机携带的强聚光手电筒。墙面采用白、红、绿的无贴图漫反射材质，方便观察反弹光和遮挡。默认关闭太阳、天空、环境补光、反射、AO、泛光和动态模糊；保留曝光、色调映射与 TAA。点光源不可见，不使用自发光代理球，避免混入第二个光源。

房间启用 `lumenGi.fullResolutionDirectLighting`：“总 GI”在每个可见接收像素计算首次照射与几何遮挡，再加上材质反射率乘世界入射缓存的反弹光；“仅首次照射”显示同一逐像素直射项，“仅反弹光”读取世界缓存的间接项。这样阴影边缘和手电光斑不再放大稀疏表面采样的格状可见性。显示模式切换不会重建或清空世界光场。手电筒计算平滑光锥、距离衰减和遮挡；空气中的体积光束尚未实现。

房间另启用 `lumenGi.traceDirectLighting`：反弹射线命中实际几何后，直接在命中点计算解析直射及遮挡，再叠加 Surface Cache 中的高阶反弹；原生光追和软件世界追踪使用同一光照定义。Surface Cache 的直接光仍更新，供缓存合成与高阶反弹分离。世界传输查询改用三角面内邻近的三个规范采样点，避免折叠方格在三角形斜边读入远处样本；半球射线相位由三角面与规范网格坐标的稳定哈希独立确定，减少相邻采样共同漏掉窄光斑的现象。方向不随帧随机旋转，保持静态结果稳定。

| 操作 | 按键 |
|---|---|
| 移动 / 转向 | WASD、Q/E 升降、按住鼠标右键转向，Shift 加速，滚轮调速 |
| 手电筒 / 点灯 | F / F6；关闭两者时房间全黑 |
| 总 GI → 仅反弹光 → 仅首次照射 | G |
| 房间 / 遮挡物 / 顶棚视角 | 1 / 2 / 3 |
| 选择物体 | 单击 / Tab |
| 移动 / 旋转物体 | 方向键移动 X/Z，PgUp/PgDn 升降，Z/X 旋转 |
| 重置 | R 重置选中物体，Backspace 重置房间 |
| 内部渲染比例 | P：100% / 75% / 62.5% / 50%；窗口标题显示实际渲染与输出尺寸 |
| 调节窗口 / 退出 | F1 / Esc（场景帮助在面板 Help 中） |

相机和物体限制在房间范围内；移动物体沿用相同拓扑的 BVH/Surface Cache refit。诊断模式只支持不透明接收面，跳过不需要的世界探针、反射和常规 PBR 直射计算。默认原生 2K、不限制帧数，用于观察真实负载；降低内部比例不等同于提高原生 2K 性能。

```text
bin\material_lab_demo.exe --vulkan --validation --gi-room-test --msaa 1 --screenshot build\gi_room_quality.png
output\Release\bin\material_lab_demo.exe --vulkan --gi-room-performance-test --benchmark-present --fullscreen --render-width 2560 --render-height 1440 --msaa 1
```

第一条验证线性总光≈直射+反弹、无光源全黑、遮挡物同帧 refit 和手电筒转向响应，并输出各模式图。薄挡板近距离对照检查点灯、手电和双灯的真实受光边缘，以及增加光源后亮度不下降；另以向下和斜向手电检查地板、背墙、物体顶面的光锥和阴影，包含光锥暗侧。直射使用独立 CPU 几何遮挡和 Lambert/光锥参考，反弹使用独立 CPU 源三角面等面积积分参考，并保留缓存直射对照。反弹测试同时检查颜色、空间误差、真实亮度梯度和误差曲率，确保平滑结果符合物理参考，不能仅靠模糊或压平通过。数值检查关闭 AA，另保存总光及仅反弹 TAA 画面。第二条在预热后测量五组各 120 帧的 GPU 与实际提交耗时：点灯总光、反弹、直射、手电筒总光及双灯总光。`material_lab_gi_room_test` 的 CPU 回归检查闭室壳体、相机/物体边界、拾取和光源隔离。

房间单独使用 `.2 m` 目标采样密度及最大 32 格的三角面缓存（常规场景保持原来的最大 16 格），共 8756 个缓存样本，每帧更新 1024 个、每样本 32 条射线（上一轮直射修复为 16 条）。默认预算在 `MakeGiRoomFrame` 初始化，`ConfigureGiRoomLighting` 不再覆盖调用方的射线数与更新预算，允许质量参考采用独立控制。同材质的共面边共享连续重建，不跨不同反射率/自发光的边界混色。逐像素可见直射与反弹命中点直射均增加光源遮挡查询。直射和缓存重建使用独立编译的 resolve 管线；直射管线先确定接收面，再计算一次照明，避免重复内联光源查询。全屏 HUD 的帧率每秒更新一次。

2026-10-06，当前版本的 Vulkan validation 原生 2560×1440 房间质量回归通过，日志 `build/gi_bounce_2k_quality.log`。完整 OpenGL 房间质量回归也通过，绿色墙边地板的 traced32 亮度 L1 为 `3.37964%`，日志 `build/gi_bounce_opengl_quality.log`。Release/Debug 已重建，相关 CPU 回归 2/2 通过，Vulkan/OpenGL 的局部采样 GPU 回归均通过。通用 Lumen 的 Vulkan validation 回归通过移动光源、自发光、多次反弹、TAA、反射和屏外缓存消费，保留 DDGI，后端错误为 0；日志 `build/gi_bounce_general_quality.log`。

反弹质量使用一次漫反射测试场景：独立 CPU 对受光源三角面进行 32/64 等面积细分积分，包含实际遮挡和材质颜色，不复用 GPU 表面缓存格或半球射线方向。白墙和绿色墙边地板的两档参考收敛差分别为 `.459478%/.106444%`，均低于 2% 门限。Vulkan 的当前结果如下；亮度 L1 和 RGB L1 除以对应参考能量，空间 RMSE 和误差曲率除以平均参考亮度，对比度增益检查真实亮度梯度是否被压平。

| 接收面 / 配置 | 亮度 L1 | RGB L1 | 空间 RMSE | 对比度增益 | 误差曲率 |
|---|---:|---:|---:|---:|---:|
| 白墙 / traced32 | 1.77471% | 1.98511% | 1.98594% | .957623 | .528409% |
| 绿色墙边地板 / cache16 对照 | 13.2552% | 12.7644% | 14.0233% | 1.10413 | 1.85339% |
| 绿色墙边地板 / traced32 | 3.37543% | 3.29036% | 3.80023% | .868621 | 1.07736% |

`cache16` 对照已使用本次局部三点查表和独立稳定哈希，因此该对照不是完整旧版本；它只比较当前源码中缓存直射/16 射线与命中点直射/32 射线的差异。断言要求 traced32 的亮度 L1、RGB L1、RMSE 均低于 6%，对比度增益在 `.65..1.35`，误差曲率低于 2%，且地板亮度误差至少减半。最终 TAA 图为 `build/gi_bounce_2k_indirect_green_wall_{full,bounce}_taa.png`，点光反弹图为 `build/gi_bounce_2k_point_bounce.png`；一次反弹对照为 `build/gi_bounce_2k_indirect_one_bounce_{cache_direct_16,traced_direct_32}.png`。这些固定接收面测试验证物理亮度、颜色和空间形状，不是任意场景的误差上限或全路径追踪参考。

同次 2K 直射回归中，光锥/阴影区域归一化亮度 L1 在地板、背墙、物体顶面分别为 `.033089%/.164148%/.211115%`；薄挡板三个受光边缘的直射/CPU 参考比例为 `.999659/.999602/.999640`，错误黑边与多灯叠加亮度损失均为 0，总光分解误差 `.0325692%`。对应图使用 `build/gi_bounce_2k_{beam_downward,beam_oblique}_{cached_direct,native_direct,full_taa}.png`。

当前 Release、RTX 4060 Laptop、Vulkan 原生光追、2560×1440 渲染与输出、MSAA1、TAA、无 validation 的离屏性能如下。预热后每组 120 帧，测量期间无并行构建；含提交耗时不包含交换链呈现，离屏 FPS 不能直接当作全屏交互帧率。日志 `build/gi_bounce_native_performance.log`，下表数值经四舍五入。

| 静态场景配置 | GPU ms | 含提交 ms | 离屏 FPS |
|---|---:|---:|---:|
| 点灯总 GI | 3.586 | 4.280 | 233.6 |
| 点灯仅反弹显示 | 3.145 | 4.103 | 243.7 |
| 点灯仅首次照射显示 | 2.624 | 3.470 | 288.1 |
| 手电筒总 GI | 3.432 | 4.342 | 230.3 |
| 点灯 + 手电筒总 GI | 3.621 | 4.406 | 227.0 |

显示“仅首次照射”仍持续求解反弹缓存，避免切回总光时重新预热；移动灯光或物体会触发重照明、几何 refit 和更大的反弹更新批次。数值仅代表该静态小房间，不能推导综合展示场景或交互负载。

历史记录保留在 `build/gi_detail_2k_quality.log`、`build/gi_detail_final_idle_performance.log` 和 `build/gi_detail_baseline_performance.log`，对应上一轮可见直射修复、16 条反弹射线且未启用命中点直射；更早的全缓存直射结果为 `build/gi_edge_2k_quality.log` 和 `build/gi_edge_native_performance.log`。当前测量与这些独立批次的设备频率、后台负载和配置不同，不据此宣称性能提高。此前负权重重建引起的黑边已修正，当前进一步处理反弹传输的空间采样。

本模块实现 `IModule`，提供 `material_lab_module` 静态库和 `material_lab_demo` 可执行程序。材质通过 MaterialTemplate → MaterialAsset → MaterialService/Instance → MaterialSnapshot，交给正式的 `Render::MaterialRenderPipeline` 绘制。窗口由 runner 持有，模块内部管理自己的 RHIDevice，必须先 Shutdown 模块再销毁窗口。

这是可以实际运行和编辑参数的 PBR 验证场景，目前编辑结果只保存在运行时，没有材质文件保存器或图形材质编辑器。场景直接提取 RenderItem，暂不启动完整 ECS world；后续 ECS 提取系统可以使用相同的材质句柄/快照机制。

## 构建和运行

在仓库根目录执行（使用项目现有的 `build` 配置）：

```powershell
cmake --build build --target material_lab_demo material_lab_pbr_test material_lab_geometry_test --parallel 4
.\bin\material_lab_demo.exe
```

首次配置可使用：

```powershell
cmake -S . -B build -G Ninja '-DCMAKE_BUILD_TYPE=Debug' '-DCMAKE_TOOLCHAIN_FILE=clang-msvc.cmake'
```

需要 OpenGL 4.5。默认交互运行时自动检查可执行文件旁的 `materials/tite`：四张遗产瓷砖贴图齐全则复用它们，否则使用程序纹理。不依赖工作目录中的旧资源管理器或旧 ShaderProgramConfig。

交互演示默认限制为 90 FPS，默认关闭 VSync，标题显示平滑帧率；Windows 使用高精度等待定时器。`--fps 120` 可调整上限，`--fps 0` 关闭软件上限，`--vsync` 显式开启垂直同步。`--fullscreen` 使用主显示器全屏；配合尺寸参数时请求该显示模式，驱动可能选择最接近的支持模式，启动日志显示实际呈现尺寸。回归和性能测量不使用软件上限。`--msaa 1|2|4|8` 调整多重采样，默认仍为 4。

```powershell
# 完全不依赖遗产资源
.\bin\material_lab_demo.exe --procedural

# 显式指定原始瓷砖贴图目录；缺文件会报告错误，不静默回退
.\bin\material_lab_demo.exe --legacy-textures .\bin\materials\tite

# 隐藏窗口GPU验证，并导出真实渲染结果；默认强制使用程序纹理
.\bin\material_lab_demo.exe --smoke-test --screenshot .\bin\material_lab.png

# 验证原始遗产贴图
.\bin\material_lab_demo.exe --smoke-test --legacy-textures .\bin\materials\tite

# 隐藏窗口画质/时域回归：镜面尺寸、MSAA、FXAA、TAA、相机模糊
.\bin\material_lab_demo.exe --quality-test --procedural --screenshot .\bin\material_lab_quality.png

# 隐藏窗口阴影场景：同时导出正常图、_visibility.png 与 _cascades.png
.\bin\material_lab_demo.exe --shadow-test --procedural --screenshot .\bin\material_lab_shadow.png

# 640×724 竖幅近景及逐步关闭效果的对照截图
.\bin\material_lab_demo.exe --closeup-test --procedural --yaw -1.35 --pitch 0.38 --distance 9 --screenshot .\bin\material_lab_closeup.png

# 透明玻璃/流动水面、区域光、发光贴图和单次漫反射间接光
.\bin\material_lab_demo.exe --advanced-test --procedural --screenshot .\bin\material_lab_advanced.png

# 960×600 高俯视/玻璃近景的效果隔离与最终 TAA+MSAA4 结果
.\bin\material_lab_demo.exe --advanced-quality-test --procedural --screenshot .\bin\material_lab_advanced_quality.png

ctest --test-dir build -R '^material_lab_.*_test$' --output-on-failure
```

Lab 的六项 GPU 测试默认不注册到 CTest；配置 `-DMATERIAL_LAB_GPU_TESTS=ON` 后增加 `material_lab_gpu_test`、`material_lab_quality_test`、`material_lab_shadow_test`、`material_lab_closeup_test`、`material_lab_advanced_test` 和 `material_lab_advanced_quality_test`。这些模式均使用隐藏窗口，默认采用程序纹理，advanced-quality 超时为 180 秒。Render 的独立 GPU 测试通过另一个默认关闭的开关 `-DRENDER_EFFECTS_GPU_TESTS=ON` 注册，新增区域光、间接光测试见末尾；对应可执行 target 始终可构建、可单独运行，需要 OpenGL 4.5。`bin` 下截图和本机遗产贴图被仓库忽略，不属于模块必需源文件。

## 性能测量

```powershell
.\bin\material_lab_demo.exe --benchmark --procedural --screenshot .\bin\material_perf.png
.\bin\material_lab_demo.exe --benchmark --procedural --benchmark-width 1920 --benchmark-height 1080
```

固定第二场景、TAA + MSAA4 + Bloom，预热后计时 24 帧；依次报告平衡档、24 射线参考、关闭 GI、关闭区域光和同时关闭两者。计时批次不呈现、不读回，使用 GL_TIME_ELAPSED，结果包含 CPU 提交之间的 GPU 空闲间隔，所以也报告墙钟耗时，不能当作某个 pass 的纯 GPU 活跃时间。截图保存平衡档与 `_reference`，检查其平均 LDR 差异小于 1/255；不将硬件帧耗时作为 CTest 通过条件。

本机 RTX 4060 Laptop 的受控测量中，1280×800 早期基线约 20.5 ms/帧，优化后平衡档约 9.6 ms/帧、24 射线参考约 14.7 ms/帧；1920×1080 平衡档约 9.7 ms/帧。平衡档/质量参考平均图像差异约 0.055/255。实际结果受相机、设备频率、其他程序和窗口尺寸影响；软件帧率上限不参与这些测量。

## 天空光照

交互场景默认开启程序化日光天空，展示场景和 Q 场景均可使用。天空不是单纯抬高环境色：漫反射按表面朝向变化，金属/玻璃按粗糙度反射环境，AO 抑制间接照明；粗糙金属使用近似多次散射能量补偿。开启天空时 Lab 清零旧常量环境色，避免重复加亮。S 开关天空光，D 单独隐藏背景，`[` / `]` 调强度，`,` / `.` 绕 Y 轴旋转环境。轴对称的默认天空旋转后外观相同，非对称 HDR 可观察旋转效果。

```powershell
# 使用自己的等距柱状 Radiance HDR；像素为线性辐射，不做 sRGB 解码
.\bin\material_lab_demo.exe --sky-hdr "D:\Environments\daylight.hdr"
# 天空漫反射/镜面、AO、一次反弹、镜面视图和 TAA 回归，并导出最终场景
.\bin\material_lab_demo.exe --sky-test --procedural --screenshot .\bin\material_sky_study.png
# 性能测量显式开启天空；省略 --sky 保留旧性能基线
.\bin\material_lab_demo.exe --benchmark --procedural --sky
```

HDR 文件限制为 128 MiB、8192×4096，非有限、负值或超过 60000 的通道会报告错误。源图第一行为 +Y 天顶，U=0 指向 +X，U 随 +Z 方向增长；方向不符可预旋转资源或调整环境 yaw。当前反射预过滤基准为 128×64、六档粗糙度，并使用独立接缝/极点边界，不依赖新 cubemap RHI。资源创建/更换时 CPU 预计算，帧内只做 SH 与少量纹理取样；首次启用可能有一次资源准备停顿。太阳仍由 CSM 方向光提供，默认程序天空不绘制高亮太阳盘；HDR 中的亮太阳与方向光需由场景作者平衡。

管线公共设置为 `effects.sky`，底层默认关闭以兼容原调用方；只在 Lab 交互入口开启，其他回归仍沿用原基线。直接使用接口见 [天空光 API](../Render/README_MATERIAL_PIPELINE.md#天空光与环境反射)。Q 场景默认结合下文的类 Lumen 混合 GI，另外保留 DDGI 和 CPU 探针。混合 GI 用距离场/硬件追踪计算局部天空遮挡；独立天空 IBL 没有单独的距离场 AO pass，仍不具备 UE5 的完整光照系统。

本机 RTX 4060 Laptop、1280×800、TAA+MSAA4+Bloom 与默认 12 射线 GI 的天空开启测量约 10.1–11.2 ms/帧，24 射线参考约 16.0–16.6 ms；两档平均显示图差异约 0.057/255。`--sky` 额外输出同配置关闭天空的控制组及差值；短批次会受设备升频、后台程序和 CPU 提交影响，差值可能为负，不应把单次差值解释为固定天空成本。预计算和纹理创建不包含在稳定帧测量中。

## 世界空间探针与封闭房间

交互 Q 场景默认开启类 Lumen 混合 GI。Y 切换为 CPU 烘焙探针，Z 切换开放场景与封闭房间；W 切回 GPU DDGI，2 切回混合 GI。CPU 探针包含局部天空遮挡、屏幕外自发光/点光/太阳/区域灯照亮的表面反弹，默认最多两次漫反射反弹。移动相机不会改变烘焙结果或触发更新；返回原材质展示或 G 阴影场景会关闭场景 GI。I 切换独立屏幕空间 GI，并退出其他 GI 模式，避免重复计入。

默认 4x4x4 网格，主体内部的探针会剔除；场景变换/不透明材质、光源或天空变化后自动更新。首次 CPU 烘焙有加载停顿，后续单个后台任务执行时继续使用上一份有效结果；新结果就绪后换入并清空 TAA 历史。连续动画光源可能落后数秒。Lab 保留实际 CPU 网格三角面，纹理归约为平均颜色、金属度与发光，因此棋盘发光只生成低频照明；玻璃不作为实体遮挡。

```powershell
# 封闭天空遮挡、屏外发光、缓存、后台更新与抗锯齿验证
.\bin\material_lab_demo.exe --probe-test --procedural --screenshot .\bin\material_probe_scene.png
# 探针与屏幕 GI 的同预设性能对照（含天空）
.\bin\material_lab_demo.exe --benchmark --procedural --probes --screenshot .\bin\material_probe_perf.png
```

`--probe-test` 输出最终封闭场景及 `_sealed_dark`、`_unoccluded_sky`、`_open` 对照。无内部光源的封闭房间显示均值为 0/255；启用发光面板后产生间接光。其 CTest 名为 `material_lab_probe_test`，由 `MATERIAL_LAB_GPU_TESTS` 注册。离线验证设置 `waitForProbeBake=true` 保证截图使用新烘焙资产；交互默认 false。

2026-10-05 本机 RTX 4060 Laptop、1280×800、TAA+MSAA4+Bloom、程序化贴图测得探针 GPU interval 为 5.259 ms/帧，恢复后 3.260 ms/帧，屏幕 GI 对照为 10.560 ms/帧。63 个有效探针，首次 CPU 烘焙 2424.780 ms，未计入稳定帧测量；计时包含 CPU 提交空隙且受设备状态影响。两方案计算范围与光照近似不同，不是逐像素等价替换。

通用 Render API 需要显式提交 `ProbeTriangle` 和重新烘焙资产，Lab 自动更新仅是演示提取层。设置、单位、资源所有权及调用示例见 [世界空间探针 API](../Render/README_MATERIAL_PIPELINE.md#世界空间漫反射与局部天空遮挡)。这个 CPU 模式是最多 64 个探针和 4x4 方向可见性的有限体积，狭窄门洞、小遮挡物可能漏光或过度变暗；实时 DDGI 与混合 GI 是另两个独立模式。

## GPU 实时 GI

原有 DDGI 完整保留：Q 场景中 W 开关 GPU 动态辐照度缓存，Y 可回到上一节的 CPU 探针，I 切换独立屏幕 GI，2 切换下节的混合 GI。四种模式互斥。E 可直接开关区域灯，S 调整天空，材质编辑接口可以改变发光强度；这些变化无需 CPU 光照烘焙。DDGI 包含世界空间软件射线追踪、局部天空遮挡、点光/太阳/区域光照亮表面的反弹，以及上一帧场反馈产生的更高阶漫反射。

```powershell
# 直接进入实时 GI 场景
.\bin\material_lab_demo.exe --realtime-gi
# GPU 能量、距离矩、动态重照明、多反弹、遮挡与 AA 回归
.\bin\material_lab_demo.exe --realtime-gi-test --procedural --screenshot .\bin\material_realtime_gi.png
# 连续 96 帧的墙面/探针亮度稳定性（开放、封闭及 TAA 配置）
.\bin\material_lab_demo.exe --realtime-gi-stability-test --procedural
# 与屏幕 GI 比较稳定帧成本
.\bin\material_lab_demo.exe --benchmark --procedural --realtime-gi --screenshot .\bin\material_realtime_gi_perf.png
```

默认每帧更新 16 个探针，每探针 64 条主射线，约四帧覆盖全体积。初次初始化或几何改变时更新全部 64 个；世界坐标缓存不随相机移动/窗口 resize 重新生成。灯光变色、移动及天空变化由 GPU 分帧追踪并滤波；无任何光源的场景直接清除旧反馈。自发光或不透明材质/几何修改会重新提取并上传 BVH，但不做 CPU 射线烘焙。首次程序编译和几何上传仍可能产生加载停顿。

射线方向现在固定于世界探针，不再每轮旋转；距离矩、天空可见性和有效性在初始化后复用，只有 RGB 光照继续更新。这样避免静态墙面的采样噪声反复触发快速历史更新。相同预算下，收敛后连续 96 帧测试中，封闭房间探针 DC 平均 CV 从 2.110% 降到 .00184%；TAA+MSAA4+Bloom 的背墙采样点平均亮度标准差从 .3086/255 降到 .00180/255。天空强度翻倍仍在 16 帧达到新目标约 94.3%。固定方向不会通过长时间旋转继续增加采样覆盖，小发光体/窄开口可提高 `raysPerProbe`（最多 128）改善空间精度；动态变化仍有分帧收敛延迟。

本机 RTX 4060 Laptop、1280×800、TAA+MSAA4+Bloom、6036 个不透明三角面：稳定性修改后默认实时 GI 首批测得 5.512 ms/帧，切回实时模式后为 2.323 ms/帧；全量更新 64 探针/帧为 2.602 ms/帧，屏幕 GI 对照 7.925 ms/帧。GPU interval 包含 CPU 提交空隙，预加载不计入，受频率与后台活动影响，不宜直接用批次时差推断算法收益。空场景天空强度翻倍后 16 帧达到目标约 94.3%；隔墙测试暗/亮半室 DC 比约 .00357，整体静态 TAA 平均帧差约 .0717/255。

`effects.realtimeGi` 控制体积、更新预算、历史与反弹反馈；管线默认关闭，Lab 入口显式开启。完整接口和 ABI 见 [GPU 实时 GI](../Render/README_MATERIAL_PIPELINE.md#gpu-实时全局光照)。帧内不回读数据，诊断方法 `ReadbackRealtimeGiCache()` 与 `PreparedRealtimeGiScene()` 只服务测试。`material_lab_realtime_gi_test` 和 `material_lab_realtime_gi_stability_test` 由 `MATERIAL_LAB_GPU_TESTS` 注册。

DDGI 模式继续使用原有探针求解，不包含混合 GI 的 Surface Cache 或屏幕最终收集。现有平面反射和天空镜面继续工作；透明几何不进入 GI 遮挡，纹理使用平均颜色，动态蒙皮和连续几何变形暂无加速结构 refit。几何更新还会重置缓存，目前适合静态几何配动态灯光，连续移动物体时高阶反弹无法充分收敛。最多 64 个探针与 4x4 方向距离矩意味着小遮挡物、窄门洞和复杂关卡仍可能漏光或过度变暗，光照变化也有收敛延迟。

## 类 Lumen 混合 GI

Q 场景现在默认使用独立的 `effects.lumenGi` 模式。2 开关混合 GI，3 开关屏幕追踪以观察屏外世界追踪，4 开关场景反射；W 随时切回保留的 DDGI。Z 切换封闭房间，E 开关区域灯，S 开关天空。可直接运行：

```powershell
.\bin\material_lab_demo.exe --lumen-gi
# 多反弹、动态重照明、封闭遮挡、屏幕/世界射线、场景反射及 MSAA/TAA
.\bin\material_lab_demo.exe --lumen-gi-test --procedural --screenshot .\build\lumen_gi_quality.png
# 与 DDGI 同样的连续墙面亮度稳定性门限
.\bin\material_lab_demo.exe --realtime-gi-stability-test --lumen-gi --procedural
# 全功能、世界追踪、仅漫反射、DDGI 和恢复后的成本
.\bin\material_lab_demo.exe --benchmark --lumen-gi --procedural
```

世界三角面建立规则重心坐标表面图集，分别缓存直接光、原始/过滤入射辐照度及多次漫反射后的出射辐射。每帧默认更新 512 个表面采样点、每点 16 条固定余弦射线；接收者网格每 16 像素识别三角面，全分辨率在确切世界位置读取入射光，不随转头重新积分半球。缓存尺寸执行世界空间过滤，共面邻接消除三角接缝，墙角不混合。缺失接收者和透明层查询世界探针。最终 PBR 纹理与轮廓保持原分辨率。

反射覆盖全部粗糙度：光滑表面使用 GGX VNDF 采样、法线贴图、独立位置/法线/粗糙度/命中距离历史过滤和几何上采样；粗糙度超过 .4 时复用世界入射缓存与 GGX DFG，.3..4 渐变。光滑屏外命中包含表面漫反射/自发光、直接高光及一次后续镜面反射。默认每像素 4 条射线、追踪网格每维为视口的 1/4、最长边 256；`--lumen-reflection-detail 320` 恢复更密的追踪网格。HDR 反射源及 F0/掠射角两系数输出默认每维为视口的 1/2，`--lumen-reflection-scale 1` 恢复全尺寸；主画面和漫反射 GI 保持原分辨率。软件路径使用保守距离场行进与精确三角面细化，薄墙仍参与遮挡。透明层和平面镜面视图继续使用世界辐射缓存。

灯光/天空改变只更新 GPU 光照；相机移动不重建世界缓存。材质或几何改变由 Lab 提取层重建 BVH/表面图集，初次初始化会更新全部采样点。正式帧不做 CPU 光照烘焙或 GPU 回读。公共设置、预算、资源生命周期和限制见 [混合 GI API](../Render/README_MATERIAL_PIPELINE.md#类-lumen-混合全局光照)。GPU CTest 为 `material_lab_lumen_gi_test`、`material_lab_lumen_gi_stability_test`、`material_lab_lumen_adaptive_test`，由 `MATERIAL_LAB_GPU_TESTS` 显式注册。

2026-10-05 本机 RTX 4060 Laptop、1280×800、程序化贴图、TAA+MSAA4+Bloom，6880 个表面采样点（512 更新/帧）、4000 个屏幕探针：完整混合 GI 首批为 8.483 ms/帧，恢复后为 6.335；世界追踪对照 5.391，仅漫反射 5.219，DDGI 对照 2.260。GPU interval 包含 CPU 提交空隙，首次程序编译/几何准备不计入；批次受升频和后台活动影响，视觉近似也不同，不能把时差直接视作固定收益。640×400、收敛后连续 96 帧，开放背墙 TAA+MSAA4+Bloom 的平均/峰值亮度标准差为 .00212/.01144（/255）；封闭房间无 AA 为 .00649/.01374，静态天空可见性/分类变化均为0。封闭无光房间可见均值为0；红色区域灯的表面缓存多反弹/单反弹比为2.014。这些数值仅覆盖固定测试场景。

### 自适应屏幕探针

默认屏幕探针网格是固定间距的接收者采样。`lumenGi.adaptiveProbes` 在深度或法线变化超过门限的 tile 内把网格细化到 `probeSpacing / adaptiveSubdivision`（2..4），平坦 tile 仍每格一个探针，把追踪预算放在轮廓和小光源附近而不是均匀铺满屏幕；关闭开关或 `adaptiveSubdivision=1` 即回到原固定网格。细分判据逐 tile 从当前捕获的深度/法线计算，缺失样本视为轮廓，同一 tile 的所有细探针得到一致结果，因此稀疏细网格可以用固定局部窗口消费，不需要填充 pass。解析重建改为在 `(subdivision+1)²` 居中窗口内搜索接收三角面：该窗口必然包含一个粗角探针，至少覆盖原粗网格的接收者，并在细化 tile 中再读取周围细探针。空闲细探针不发射射线，只保留粗角探针密度，所以平坦区域开销与原来相当。

```text
# 固定16px、adaptive2、adaptive4 与 4px 均匀稠密参考的对照（隔离漫反射 GI）
.\bin\material_lab_demo.exe --lumen-adaptive-test --procedural --screenshot .\build\adaptive_probe.png
```

交互查看：一个自由飞行视窗，内置三个原有测试场景，可按键切换并实时对照：

```text
.\bin\material_lab_demo.exe --lumen-adaptive --procedural
```

| 操作 | 效果 |
|---|---|
| F5 | 切换场景：光照研究房间 / 庭院展示 / 封闭小屋 |
| F2 | 开关自适应屏幕探针（核心 A/B 对照） |
| F3 | 切换细分 2 / 3 / 4 |
| F4 | 循环 默认视图 / 覆盖诊断视图 / 屏幕探针漫反射视图（进入诊断时自动关闭直射光、天空与场景反射，退出恢复） |
| 鼠标右键转动 + WASD / Q / E 移动，Shift 加速，滚轮调速 | 自由飞行巡视 |
| P | 内部渲染比例 100% / 75% / 62.5% / 50% |
| 封闭小屋额外按键 | F 手电筒、F6 点灯、G 全 GI / 仅反弹 / 仅直射、1/2/3 视角、Tab 选择、方向键移动、Z/X 旋转、R 重置物体、Backspace 重置房间、F1 帮助 |

光照研究房间与庭院默认 Material 视图，这是自适应接收者解析真正生效的路径；封闭小屋保持**原来的诊断视图**，因此原有的逐像素解析直射阴影以及手电筒 / 点灯光照完全保留，F/F6/G/1/2/3/Tab/R/Backspace/F1 等按键与原 `--gi-room` 交互一致。切换场景会重建该场景的世界几何与材质（复用模块原有的场景提取），自适应开关保持不变。窗口标题显示当前场景、自适应开关与细分、视图、探针网格规模；封闭小屋还显示点灯 / 手电与当前选中物体。

F4 的覆盖诊断视图直接把自适应机制画出来：青色 tile 表示该 tile 已细化，绿色像素表示接收者由探针网格解析，红色像素表示回退到低频世界缓存。按 F2 开关自适应时，可以看到细化 tile 与红带沿物体轮廓出现/消失。该场景红带像素比例由固定 16px 的 15.4% 降到自适应 ×2 的 14.4%，而参考亮度梯度前 10% 的轮廓像素误差由 0.054 降到 0.017 /255——改善集中在轮廓，不是整体均匀提亮。

`--lumen-adaptive-test` 会为每个配置额外保存普通渲染场景 `build\adaptive_probe_<config>_scene.png`、隔离漫反射 `build\adaptive_probe_<config>.png`，覆盖诊断 `build\adaptive_probe_<config>_coverage.png`（仅前两档），以及三个场景的自适应图 `build\adaptive_probe_scene_{study,courtyard,room}_adaptive.png`。

测试对每个配置分别渲染“GI 开”和“GI 关”再相减，得到隔离的漫反射最终收集：直射、阴影、材质在无 AA 的确定性捕获中精确抵消，Surface Cache 与 `intensity` 无关。以 4px 均匀网格为稠密参考，取参考亮度梯度前 10% 作为轮廓像素统计误差。

| 配置 | 细网格规模（探针） | 轮廓 GI 误差 /255 | 全局误差 /255 |
|---|---:|---:|---:|
| 固定 16px | 1000 | 0.0539 | 0.0230 |
| 自适应 ×2 | 4000 | 0.0171 | 0.0065 |
| 自适应 ×4 | 16000 | 0.0157 | 0.0129 |
| 4px 均匀参考 | 16000 | 0（参考） | 0（参考） |

本机 RTX 4060 Laptop、640×400、程序化贴图，OpenGL 与 Vulkan 数值一致（Vulkan 固定16px 为 0.0540）。自适应 ×2 与固定 16px 共享相同的粗网格密度，仅靠轮廓 tile 的细探针把轮廓 GI 误差降到约 1/3；自适应 ×4 接近稠密参考，但大部分探针空闲、不发射射线。细网格规模是上限，实际活跃探针只在轮廓 tile 达到该密度。数值仅代表该固定光照研究场景，不是任意场景的误差上限。反射仍使用独立均匀网格。

### 屏幕探针漫反射的"乐高"对照

上面的 Material 视图用世界缓存解析到精确世界位置，本身不产生屏幕探针网格的块状感；覆盖诊断只显示接收者来源。为了**直接看到屏幕探针网格本身**，`LumenLightingView::ProbeDiffuse` 把最近活跃探针所对应的表面缓存着色辐射（含直射阴影）赋给整个屏幕单元——这正是低密度屏幕探针产生"乐高颗粒感"的方式。开启自适应后轮廓处的单元被细化，颗粒消失。

```text
# 交互：直接进入封闭小屋并显示屏幕探针漫反射，F2 开关自适应对照
.\bin\material_lab_demo.exe --lumen-probe-diffuse --procedural
# 静态：16px / 自适应×4 / 4px 稠密参考，并以稠密参考度量量化误差
.\bin\material_lab_demo.exe --lumen-probe-diffuse-test --procedural --screenshot .\build\probe_diffuse.png
```

封闭小屋的点灯、遮挡物与手电筒提供锐利阴影，屏幕探针单元在阴影/轮廓边界最明显；关闭直射与天空后画面完全由该视图驱动。以 4px 稠密网格为参考，取参考亮度梯度前 5% 作为轮廓像素：

| 配置 | 轮廓量化误差 /255 | 全局 /255 |
|---|---:|---:|
| 固定 16px | 13.026 | 3.193 |
| 自适应 ×4 | 1.219 | 1.157 |
| 4px 稠密参考 | 0（参考） | 0（参考） |

自适应把轮廓处的屏幕探针量化误差降低约 10 倍，颗粒感消失；静态图 `build\probe_diffuse_{uniform16,adaptive4,reference4}.png` 可直接对比。交互视窗中 F4 在 默认视图 → 覆盖诊断 → 屏幕探针漫反射 之间循环，F2/F3 实时改变探针密度。

这是本项目的类 Lumen 实现，结构参考 [Epic Lumen 技术说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine)。本次已加入距离体积、Vulkan 原生硬件 ray query、全部粗糙度的 GGX 积分、屏外高光，以及基于深度/法线方差的自适应屏幕接收者网格。它尚无 UE 的 Mesh Cards、距离场 clipmap/自动流送、UE 式自适应方向辐射探针、TSR 和任意深度反射递归。世界缓存用平均材质颜色/F0/粗糙度；屏外贴图法线、透明层间递归、复杂动态几何、小发光体和窄门洞仍有限制。屏幕直接光源与世界缓存的点光/区域光遮挡方式也不同，不能据此宣称达到 UE5 Lumen 同等画质。

## 近景与效果隔离

`--closeup-test` 以 640×724 竖幅、TAA + MSAA4 渲染材质展示场景，默认 `yaw=-1.35`、`pitch=.38` 弧度、`distance=9` 世界单位。可用 `--yaw`、`--pitch`、`--distance` 重现其他观察角度；这些参数当前用于近景测试。需要 TAA 的配置以 camera cut 开始并累积 32 帧，再捕获稳定图；初始配置另比较连续 16 帧的静态变化。指定 `--screenshot FILE.png` 后保存：

| 文件 | 当前配置 |
| --- | --- |
| `FILE.png` | 原始近景，全部默认效果 |
| `FILE_no_bloom.png` | 关闭 Bloom |
| `FILE_no_ao_bloom.png` | 再关闭 SSAO |
| `FILE_no_shadow_ao_bloom.png` | 再关闭 CSM |
| `FILE_visibility.png` | 恢复 CSM，输出阴影可见度诊断 |
| `FILE_no_aa_ao_bloom.png` | 恢复正常光照，关闭 TAA/FXAA 与 MSAA（samples=1）；Bloom/SSAO 仍关闭，CSM 保留 |

这些是逐步隔离的配置，适合区分高光、AO 条纹与阴影问题。最后一张关闭时域/屏幕 AA 和多采样，PBR 内的高光滤波仍保持默认开启。未指定截图路径时仍运行检查，不写图片。

## 透明与间接光质量诊断

`--advanced-quality-test` 固定在 960×600、`lightTime=1`，默认依次运行 `high` 高俯视（yaw=.25、pitch=.95、distance=15）与 `glass` 透过玻璃看盒子的近景（yaw=-.42、pitch=.10、distance=10.5）。两项 advanced 测试均接受 `--yaw / --pitch / --distance`；质量模式指定任一相机参数后只运行 `custom`，未指定分量使用普通 advanced 的 .25/.28/15。角度单位为弧度，距离与 FrameSettings 一致。例如：

```powershell
.\bin\material_lab_demo.exe --advanced-quality-test --procedural --yaw -0.42 --pitch 0.10 --distance 10.5 --screenshot .\bin\glass_inspect.png
```

指定 `--screenshot FILE.png` 后，按 `FILE_high_*`、`FILE_glass_*` 或 `FILE_custom_*` 保存下面的对照；原 FILE.png 是第一个视角的 raw。所有隔离图保持同一视角和时间，默认 Bloom 关闭、MSAA1，避免多种效果同时变化。

| 后缀 | 配置与用途 |
| --- | --- |
| `_raw` | GI/透明开启，关闭 TAA/FXAA/Bloom，观察原始结构 |
| `_no_gi` | 仅关闭 GI |
| `_no_transparency` | GI 开启，玻璃与水面 opacity=0 |
| `_opaque_no_gi` | GI 与透明均关闭 |
| `_gi_difference_x4` | 两张不透明图的绝对显示颜色差 ×4，突出间接光；不是 HDR 辐照度 |
| `_glass_half_no_gi` / `_glass_full_no_gi` | 仅调整玻璃光学强度 .5/1，关闭 GI/Bloom/TAA，检查是否残留双轮廓 |
| `_bloom` | 相对 raw 仅开启 Bloom |
| `_taa_first` / `_taa` | MSAA1，TAA 重置首帧 / 累积 16 帧 |
| `_taa_moved_history` / `_taa_moved_cut` | 相机 yaw 加 .015 后使用历史 / camera cut；包含抖动相位差，不能把像素差直接当作重影误差 |
| `_settled` | 回到原视角，开启 TAA + MSAA4 + Bloom，累积 16 帧的最终展示图 |

测试检查 raw 重复绘制与恢复参数后保持一致、固定预设实际包含 GI/透明效果、TAA 静态稳定、微小移动复用历史、camera cut 拒绝历史，以及最终图确实使用 MSAA4。未指定截图路径时仍执行检查。判断最终几何边缘请使用 `_settled`；MSAA1 的隔离图用于定位效果来源。

## 场景与操作

上方 3 行 × 5 列材质球：从上往下 metallic 为 `0 / 0.5 / 1`；从左往右 roughness 为 `0.08 / 0.25 / 0.5 / 0.75 / 1`。下方依次是铜、五贴图材质、迁移的遗产瓷砖预设，底排这三个材质球贴地。铜球采用镜像和非均匀缩放，用于观察 TBN 处理；地面展示平面反射和 CSM 阴影，左右两个发光球用于观察 HDR/Bloom。上方网格球和发光球按展示布局悬空；判断接触漂浮应观察底排或独立阴影场景。

按 G 进入独立阴影场景：接触地面的球与盒子、薄遮挡物、15° 斜面和曲面接收者，用于分辨接触漂浮、表面条纹和过滤质量。此模式关闭镜面和点光，只保留方向光与环境项；也可直接设置 `FrameSettings.shadowStudy=true`。`--shadow-test` 还关闭 Bloom/SSAO 和初始 AA，避免其他效果掩盖阴影问题。

按 Q 进入透明/光照场景：有色玻璃球、流动折射水面、发光贴图面板和不同颜色的不透明表面，用来观察透射、颜色渗透和区域灯。进入时设置顶置矩形灯、开启类 Lumen 混合 GI、选择 ACES 并自动推进时间；Space 可暂停流动与示例光源动画。I 切换独立屏幕 GI，E 切换矩形灯。离开 Q 场景关闭演示 GI/区域灯并恢复展示场景的镜面平面。

可编辑材质共 21 个，索引 0..17 保持原展示预设，新增 `18=Tinted glass`、`19=Flowing refractive water`、`20=Textured emissive panel`。Q 默认选择 18，可用 Tab 编辑后两项；返回展示场景后 F 可单独观察所选新材质。`FrameSettings.lightingStudy=true` 只选择几何场景，直接调用模块时仍需自行配置 `effects.areaLights`、`effects.indirect` 与 `lightTime`。

交互模式默认使用 **TAA + MSAA4 + 全分辨率镜面 + 天空光**，相机运动模糊关闭，点光动画暂停。通用 Render API 的默认值仍是 FXAA + MSAA1，Lab 在交互入口显式覆盖。窗口标题显示所选球参数、`N/M/R/A` 贴图开关、曝光、AA、MSAA、模糊和镜面比例。

| 操作 | 效果 |
| --- | --- |
| S / D | 开关天空光 / 单独隐藏天空背景 |
| [ / ] | 减少 / 增加天空强度 |
| , / . | 减少 / 增加环境旋转角 |
| W / Y / Z | Q 场景切换保留的 DDGI / CPU 烘焙探针 / 封闭房间 |
| 2 / 3 / 4 | Q 场景切换混合 GI / 混合模式屏幕追踪 / 混合模式场景反射 |
| Tab / Shift+Tab | 下一个 / 上一个材质 |
| F | 切换单球观察与完整场景 |
| ← / → | 减少 / 增加粗糙度，同时关闭粗糙度贴图以使用标量 |
| ↓ / ↑ | 减少 / 增加金属度，同时关闭金属度贴图以使用标量 |
| N | 切换法线贴图 |
| T | 同时切换金属度、粗糙度、AO 贴图 |
| R | 恢复当前材质的资产默认值和开关 |
| PageUp / PageDown | 调整管线曝光 |
| Space | 暂停 / 推进场景时间，同时驱动点光源、UV/flow map 流动与波纹 |
| B / C / M / O | 切换 Bloom / CSM / 平面反射 / SSAO |
| P | 循环原图、灰度、反相、锐化、浮雕、边缘检测、Gaussian、波纹 |
| L | 循环旧 Reinhard、ACES、关闭色调映射 |
| V | 切换暗角 |
| A | 循环 None / FXAA / TAA；MSAA 独立设置 |
| X | 切换 MSAA1 / MSAA4 |
| U | 切换相机运动模糊；静止相机不会被模糊 |
| H | 循环镜面 0.5× / 1× / 2× 分辨率比例 |
| G | 切换材质展示 / 阴影研究场景，并切换对应观察角度 |
| J | 循环阴影诊断 None / Visibility / Cascades |
| K | 循环阴影 atlas 2048 / 4096 / 8192，每级边长为 atlas 的一半 |
| Q | 切换透明/光照场景；进入时启用区域光、混合 GI 并播放动画 |
| I / E | 切换独立单次漫反射屏幕 GI / 矩形区域灯 |
| 鼠标左键拖动 / 滚轮 | 环绕观察 / 缩放 |
| Esc | 退出 |

白色与平坦法线是合法兜底贴图，所以没有专门贴图的网格球也可以切换开关：例如打开白色粗糙度贴图会取到 roughness=1。

## 创建、编辑、使用

正式模板、顶点布局和 GLSL 位于 `Module/Render/Public/Material/pbr_material.h`，Lab 旧头文件保留兼容转发。`Private/material_lab_module.cpp::Initialize` 展示完整的 shader/pipeline/纹理创建及资源所有权装配。以下是其中的上层用法，`pass` 是已经装配好的 MaterialPassResources：

```cpp
using namespace Render::Material;
auto schema = MaterialLab::MakePbrTemplate();
auto asset = MaterialAsset::Create(schema, {
    {"baseColor", glm::vec4(0.72f, 0.29f, 0.10f, 1.0f)},
    {"metallic", 0.6f},
    {"roughness", 0.5f},
    {"useRoughnessMap", true},
    {"useAoMap", true}
});

// 必须是同一个 schema 对象，而非重新 MakePbrTemplate() 的另一个实例。
pass.expectedTemplate = schema;
// pass 还需有效 pipeline、208字节参数UBO、0..4必需纹理绑定和GPU资源owner。
// 设置 pass.shadowPipeline 为 PbrShadowPipelineSpec 编译的深度变体，参与阴影/SSAO。
// 若要作为间接光源，另提供 diffuseRadiancePipeline 和 normalPipeline 捕获变体。
MaterialService materials;
auto handle = materials.Register(asset, pass);

// 编辑实例：资产默认值及其他实例保持独立。
materials.SetParameter(handle, "useRoughnessMap", false);
materials.SetParameter(handle, "roughness", 0.25f);

Render::RenderItem item;
item.mesh = sphereMesh;
item.draw.indexCount = sphereIndexCount;
item.model = model;
item.material = materials.Capture(handle);
// 将 item 放入 RenderFrame / RenderWorld，由管线录制。
```

修改后重新 Capture 才能取得新快照；已提交的快照保持原值。贴图开关为 true 时，贴图 R 通道替代相应标量，不与标量相乘。`baseColor` 和 `uvTransform` 等标量/向量参数支持逐对象覆盖；贴图开关在实例级编辑。模块对外还提供 `SetMaterialParameter(index, name, value)`、`InspectMaterial`、`ResetMaterial`，便于接入之后的参数面板。

透明材质使用 `MakePbrTemplate(Domain::Translucent)`，并用同一域的 `PbrVertexShader` / `PbrFragmentShader` 编译颜色程序。`PbrPipelineSpec(program, Domain::Translucent)` 设置 depthTest=true、**depthWrite=false** 和 Alpha 混合，不能复用普通不透明 pipeline。参数布局与 StandardPbr 共享 208 字节，但 `expectedTemplate` 仍必须指向注册时的同一 schema。快照域让 RenderItem 自动进入排序透明层。完整创建与资源装配示例见[透明与流动材质](../Render/README_MATERIAL_PIPELINE.md#创建透明与流动材质)。

以下操作可直接修改本模块已经创建好的水面和发光材质：

```cpp
lab.SetMaterialParameter(19, "opacity", .85f);       // 透明域光学效果强度，0无影响
lab.SetMaterialParameter(19, "transmission", .88f);
lab.SetMaterialParameter(19, "ior", 1.333f);
lab.SetMaterialParameter(19, "thickness", .2f);      // 世界单位近似光程
lab.SetMaterialParameter(19, "distortionStrength", 18.0f); // 像素上限/贴图扰动强度
lab.SetMaterialParameter(19, "absorptionColor", glm::vec4(.7f, .92f, .98f, 1));
lab.SetMaterialParameter(19, "uvFlow", glm::vec4(.08f, .035f, -.04f, .06f));
lab.SetMaterialParameter(19, "twoSided", true);      // 双面薄片；玻璃球默认 false
lab.SetMaterialParameter(20, "emissiveIntensity", 2.0f);
// 实际应用应检查返回值，失败时读取 lab.LastError()。
```

Translucent 的 `opacity` 控制光学效果强度，缩放折射位移、吸收/透射和表面辐射：0 无影响，1 完整材质。正式快照路径的覆盖率只取 albedo.a × baseColor.a，仅软边缘的部分覆盖才混合原目标；不会在材质内部同时保留未折射物像。Surface 域仍保持 opacity 乘覆盖 alpha/裁剪的既有行为。`absorptionColor.rgb` 是经过一个世界单位后的透过率，1 为不吸收；shader 按厚度和观察角度计算衰减。`uvFlow.xy` 为第一层 UV/秒速度，zw 非零则启用第二层等权采样；`flowMap` 解码为有符号 RG 速度后乘 `flowStrength` 加入两层。自发光为 `emissiveColor × emissiveIntensity × 可选线性发光图`，可以进入 Bloom，也会作为间接光的辐射源。

所有 GPU 资源通过 RHI 异步创建，回调到达后登记所有权。共享的 pipeline/参数 UBO/纹理由一个 lease 持有；逐帧命令保留 lease 和材质快照。材质参数 UBO 可以共享，因为管线在每次材质切换时上传完整快照。当前管线要求一个待确认帧：Lab 的 `Render` 使用可靠提交，等待执行完成回调后在应用线程调用 `CompleteFrame(token)`，才允许下一帧使用历史；未提交失败调用 encoder.Cancel 和 `DiscardFrame(token)`。没有把可能被覆盖的 latest 帧当成历史。

## 遗产 PBR 的对应关系

源实现：`Engine/src/engine/Resource/Material/Interfaces/BPR.h` 与 `bin/shaders/Final/PBR/20250719/PBRStaticLights.fs`。保留 Cook–Torrance / GGX / Smith / Schlick、不透明 F0=0.05、四点光逆平方衰减；透明介质 F0 由 IOR 计算。AO 限于环境/间接项，不整体压暗直射、镜面、自发光或透射背景。

| 遗产表达 | 新表达 |
| --- | --- |
| IBPR::Property，binding 32 | 模板内 metallic / roughness / ao，生成的 MaterialData，binding 2 |
| USE_NORMAL/METALLIC/ROUGHNESS/AO_MAP 宏 | 实例布尔参数，可实时切换 |
| albedo / normal / metallic / roughness / AO map | 必需纹理槽 0 / 1 / 2 / 3 / 4 |
| 新增扰动 / 流向 / 自发光贴图 | 可选纹理槽 5 / 6 / 7，对应 distortionMap / flowMap / emissiveMap |
| 着色器内的点光数组、时间 | Pass UBO 3 的灯光位置/颜色；CPU 更新动画 |
| cmp.values[0] 的颜色控制 | baseColor 线性颜色因子，参与光照计算 |
| fragment 内色调映射 | PBR 输出线性 HDR，统一后处理 Pass 完成曝光、色调映射和 Gamma |

瓷砖预设显式迁移自本机 `bin/materials/BPR/hh.json`：metallic=0.6、roughness=0.5、ao=1，使用 normal/roughness/AO 贴图，metallic 使用标量。当前没有通用旧 JSON 导入器；新材质通过独立的 shadowPipeline 变体参与阴影。

Surface / Translucent 的共享参数块为 **208 字节**，保留原字段偏移并加入流动、发光强度、透射和 twoSided（偏移 192）；View/Object/Material/Lighting/SceneEffects/PostProcess/Temporal 分别使用 UBO 0/1/2/3/4/5/6。光照 UBO3 为 176 字节，原 0..159 偏移不变，末尾 160 的 `specularAA` 依次存法线方差比例、最大新增 GGX `alpha²`、启用标志和保留值，默认 `{.15,.20,1,0}`。场景 UBO4 为 544 字节，保留原 0..495 偏移，追加 496 的 `cascadeWorldTexelSize`、512 的 `cascadeInverseDepthRange` 和 528 的 `shadowFilter`，按 256+256+32 三段上传。后处理 UBO5 为 240 字节（末尾 224 偏移为 `bloomStability`，依次为 soft knee / firefly range / scatter / radius），时域 UBO6 为 **400 字节**，末尾 384 的 reactive 控制透明反应值。UBO7 为 160 字节的视图/时间/快照状态，UBO8 为 256 字节的四盏区域灯，UBO9 为 224 字节的间接光追踪参数。顶点是 position3/normal3/uv2/tangent4，共 48 字节。粗糙度在 shader 内限制到至少 0.045，TBN 处理非均匀缩放及镜像 handedness。

高光抗锯齿在 PBR 着色时根据法线屏幕导数对 NDF 做有界滤波，方差加到感知粗糙度的四次方后再换算，避免通过硬截亮度掩盖微小高光。它不改材质中保存的粗糙度，独立于 TAA、MSAA 和 Bloom；`frame.effects.lighting.specularAA.z=0` 可关闭。实现参考 [Tokuyoshi / Kaplanyan 2019 的保守各向同性滤波](https://yusuketokuyoshi.com/papers/2019/ImprovedGeometricSpecularAA.pdf)，不包含法线贴图 mip 方差预过滤，也不修复轮廓覆盖或阴影终止线几何问题。

颜色约定：albedo 默认按 sRGB 解码，baseColor 是线性颜色；其他贴图均按线性数据使用。后处理默认使用旧 `color/(color+0.2)` 色调映射和 gamma=2.2，因此不是旧截图的逐像素复刻；Q 场景显式选择 ACES。当前 RGBA8 纹理由 shader 做颜色解码，尚无硬件 sRGB 过滤/线性空间 mipmap。管线绑定目标时关闭 `GL_FRAMEBUFFER_SRGB`，保证显示编码只做一次。

## 正式渲染管线

当前完整顺序：四级 CSM → 平面反射的不透明层/独立快照/透明层与 MSAA resolve → SSAO 所需主视图深度/resolve → SSAO/双边模糊 → 可选间接光的深度/漫反射辐射/法线捕获、追踪、三次去噪与上采样 → 主视图 HDR 不透明层/resolve → 独立颜色与深度快照/排序透明层/resolve → 可选 TAA/历史深度保存 → 可选相机模糊 → 多级 Bloom → 曝光/色调映射/Gamma/调色 → 屏幕滤镜 → FXAA 或复制到屏幕 → Overlay。TAA 和 FXAA 二选一，MSAA 可独立叠加。全部效果由 `Module/Render` 调度，Lab 负责场景与交互；旧 `LabPipeline` 已移除。

通过 `FrameSettings.effects` 设置阴影、反射、区域光、间接光、后处理和时域参数；Lab 的 `exposure` 覆盖管线曝光，`lightTime` 驱动示例点光源、UV/flow map 流动和波纹。例如：

```cpp
MaterialLab::FrameSettings frame;
frame.width = 1280;
frame.height = 800;
frame.effects.shadows.atlasResolution = 4096;   // 四级各 2048×2048，默认值
frame.effects.shadows.depthBiasTexels = 0.05f;
frame.effects.shadows.normalBiasTexels = 0.20f;
frame.effects.temporal.antialiasing = Render::AntialiasingMode::TAA;
frame.effects.temporal.msaaSamples = 4;           // 1 / 2 / 4 / 8，取决于设备支持
frame.effects.reflection.resolutionScale = 1.0f; // 0.25..2，默认 full resolution
frame.effects.temporal.motionBlurEnabled = false;
frame.effects.temporal.motionBlurShutterSeconds = 1.0f / 120.0f;
frame.effects.post.bloomScatter = 0.8f;          // 0..1，较粗光晕的重建比例
frame.effects.post.bloomRadius = 1.25f;          // .5..2，较小输入图的 texel 单位
frame.effects.lighting.specularAA = {0.15f, 0.20f, 1.0f, 0.0f};
frame.effects.areaLights.count = 1;
frame.effects.areaLights.lights[0].center = {0, 3.8f, 0};
frame.effects.areaLights.lights[0].halfAxisU = {1.3f, 0, 0};
frame.effects.areaLights.lights[0].halfAxisV = {0, 0, .9f}; // cross(U,V) 朝下
frame.effects.areaLights.lights[0].radiance = {4, 3.8f, 3.5f};
frame.effects.areaLights.samplesPerAxis = 4;    // 4×4 或 8×8
frame.effects.indirect.enabled = true;
frame.effects.indirect.radius = 4;
frame.lightTime = elapsedSinceStartSeconds;    // 不推进则流动贴图保持静止
frame.effects.deltaSeconds = elapsedSeconds;    // 实际帧间隔，有限且 > 0
frame.effects.cameraCut = didTeleport;          // 跳切只置位该帧
if (!lab.Render(frame)) throw std::runtime_error(lab.LastError());
// Render 内部完成匹配尺寸/MSAA/镜面比例的 Resize、可靠提交和 CompleteFrame。
```

直接使用 Render 管线时，`Resize(width, height, shadowResolution, msaaSamples, reflectionScale)` 必须与 settings 匹配。镜面每维为 `ceil(viewport * resolutionScale)`，上限 8192；MSAA 同时应用于主场景和镜面。材质使用纹理槽 0..7，不透明颜色/深度快照占 8/9，CSM/镜面/AO/间接辐照度占 12/13/14/15。全屏后处理局部绑定 0=输入、1=第二图、2=深度；TAA 局部绑定 0=当前颜色、1=历史颜色、2=当前深度、3=历史深度、4=不透明快照。

TAA 使用相机/深度重投影、8 帧 Halton 抖动、邻域约束与亮度反应，没有 object velocity。材质参数、模型/几何句柄和纹理句柄等内容 hash 改变会拒绝旧历史；点光动画不参与每帧 hash 重置，由亮度变化降低历史权重。原地更新贴图或 mesh 内容时，直接管线调用方应在帧间调用 `ResetHistory()`。相机跳切、projection/尺寸变化和较长帧间隔也会清历史。动态物体和透明内容不能依靠当前 TAA 获得完整运动补偿。

透明层的最终 HDR 与本视图不透明快照比较，得到当前 reactive 值并存入 TAA 历史颜色 alpha；历史 alpha 同样参与拒绝，以减轻透明内容移动后残留。这里 alpha 表示反应值而非覆盖率。相机模糊避让当前反应区域，并拒绝从透明反应区域取样，减少按背景深度误拖曳；它仍不能替代透明速度和分层深度。区域光、间接光设置改变会使内容历史失效，单纯推进 `lightTime` 不会每帧强制清历史。

主视图和镜面各自先画不透明层，再复制颜色与深度到独立快照后画透明层。深度复制使用 depth-only FBO，避免破坏颜色快照；透明 shader 不读取活动颜色附件，也不会把主视图快照当成镜面背景。玻璃默认 `twoSided=false`，防止闭合表面的背面重复混合；水面明确为双面薄片，背面法线翻转后仍按入射面处理。透明层可排序混合，但互相不递归折射；偏移越界或采到前景时使用未扰动背景。

区域灯支持最多四个矩形、4×4 或 8×8 固定面积积分，半轴定义面积和朝向，radiance 是发出辐射而非点光强度。默认 `specularFilter=1` 按积分单元角度范围过滤窄 GGX，抑制离散点斑；普通 PBR 路径未启用独立面积阴影，密闭陶瓷场景单独启用缓存软阴影。受控 GPU 测试中漫反射解析误差最大约 .0685%，窄镜面 33 相位变化约 2.4%–2.9%；有限网格仍有误差，8×8 也不是精确多边形积分。

间接光默认为关闭，开启后捕获不透明/裁剪表面的直射/天空漫反射、自发光、几何法线和深度，执行一次屏幕空间反弹。默认平衡档为 12 个余弦半球方向、每方向 24 步，可将 sampleCount 设为 24 取得质量参考。每像素固定整数哈希打散两个采样维度，命中后按射线角度足迹过滤源辐射。追踪与三次去噪工作图为半分辨率、最长边最多 640 像素，最后按全分辨率深度/法线上采样；不模糊最终场景或材质纹理。视空间位置缓存避免逐射线和逐滤波 tap 重新逆投影；UBO9 仍为 224 字节，启用时统计为 9 个间接光 pass。

可观察颜色渗透和发光面照亮邻面；捕获排除镜面、环境项和已有 GI，金属没有直接漫反射贡献。未知材质可只提供 shadowPipeline 写深度遮挡射线；要贡献辐射还需 diffuseRadiancePipeline / normalPipeline，两者均使用相同参数和纹理、关闭深度写入。透明层不作为此 GI 的源/接收者，镜面不复用主视图 GI；屏外或被前景隐藏的表面不可用，未命中贡献零，所以不能替代世界空间间接光。

相机模糊默认 shutter=1/120 秒、strength=1、12 个样本和 32 像素总跨度上限。实际速度乘以 `shutter / deltaSeconds`，使用未抖动矩阵以排除 TAA jitter；背景与静止相机保持不变。它不处理物体自身运动。FXAA、TAA、模糊的实现范围及对应一手资料见[管线文档](../Render/README_MATERIAL_PIPELINE.md)。

CSM 默认 4096 atlas、四级各 2048×2048，practical split 的 lambda=.65。CPU 使用 double 稳定球拟合与纹素对齐，每边留 3 个过滤纹素及半纹素对齐余量；Light Z 紧贴接收者范围，并向光源上游扩展 `casterPadding=30` 世界单位。采样为 16 tap 相位连续 tent PCF，每 tap 按接收面导数修正深度；几何法线偏移与太阳终止处理不使用法线贴图。最后 10% 阴影距离平滑淡出，超距不采阴影。算法依据见 [Microsoft CSM 的逐纹素深度偏差](https://learn.microsoft.com/en-us/windows/win32/dxtecharts/cascaded-shadow-maps#calculating-a-per-texel-depth-bias-with-ddx-and-ddy-for-large-pcfs) 和 [GPU Gems 的 PCF](https://developer.nvidia.com/gpugems/gpugems/part-ii-lighting-and-shadows/chapter-11-shadow-map-antialiasing)。

`depthBiasTexels=.05`、`normalBiasTexels=.20` 以各级联的世界纹素尺寸计量，分辨率改变时自动换算；旧 `depthBias=.000002` 是额外归一化深度偏差，`normalBias=0` 是额外世界距离，不要混用单位。`receiverPlaneClampTexels=4` 是 RPDB 安全上限的纹素系数，还会乘真实三角面坡度因子 `max(1,tan(phi))`，避免截断陡斜面需要的逐 tap 修正；它不增加常量偏差，设为 0 则关闭 RPDB。极端掠射或退化导数使用有界回退。遇到漂浮先减小偏差，遇到表面条纹先检查级联覆盖、分辨率和几何，再小幅调整 texel 参数；增大世界偏移会直接扩大接触间隙。完整参数、RPDB 回退和上限见[CSM 调参说明](../Render/README_MATERIAL_PIPELINE.md#csm-质量与调参)。J 的 Visibility 用白/黑显示可见/遮挡，Cascades 显示级联颜色；这些诊断关闭输出调色、Bloom 和屏幕滤镜。检查原始阴影时可用 A 关闭 AA。

Bloom 默认 `bloomSoftKnee=.5`、`bloomFireflyClamp=0`、`bloomScatter=.8`、`bloomRadius=1.25`。首级在源 texel 上应用软阈值后进行双线性/13 tap 过滤，后续使用归一化 13 tap 降采样。上采样改为 `mix(large, normalizedTent(small), scatter)`；scatter（0..1）控制较宽光晕的比例，radius（.5..2）以较小图的 texel 计量。每级权重和为 1，层数控制分布、strength 控制最终强度，原始 HDR 图保持清晰。思路参考 [Unity 官方 Bloom 的 scatter 混合](https://github.com/Unity-Technologies/Graphics/blob/master/Packages/com.unity.render-pipelines.universal/Shaders/PostProcessing/Bloom.shader)。

完整 GPU 五层回归中，相同积分能量下的光晕 RMS 半径由 6.773178 增至 20.396257 个半分辨率 Bloom texel，中心半径 4 texel 内的能量占比由 .766096 降至 .360349。恒定 HDR 100 在 1..6 层均输出 99.1875，六层亮点保留首层积分能量的 .997811；32/128/1500 三档亮点连续移动时，对齐后的光晕形状变化降低约 58%，完整链额外积分能量偏差最多约 .22%。这些数据证明受控光晕重建更柔和、形状更稳定，不代表消除了着色源本身的闪烁。

原提取阶段的 33 相位测试仍保留，三档能量变异系数相对旧提取降低约 88.1%/89.8%/87.9%。正值 `bloomFireflyClamp` 是可选局部异常亮点抑制，会改变移动高光的能量，默认保持关闭；恒定 HDR 不受其限幅。完整指标、[Unity 13 tap](https://github.com/Unity-Technologies/PostProcessing/blob/v2/PostProcessing/Shaders/Sampling.hlsl) 与 [Karis 加权](https://graphicrants.blogspot.com/2013/12/tone-mapping.html)来源见管线文档。

SSAO 法线重建现在以整数深度像素选择左右上下邻居，再用各自真实 texel 中心重建位置。这样避免半分辨率采样落在全分辨率像素边界时，UV 浮点加减与 floor 选中重复/跨越邻居造成的斜面暗条纹。它仍保留深度感知模糊和真实接触遮蔽，且只作用于不透明环境光；材质 AO 另用于环境项和新漫反射间接光，透明层不会误用后方表面的 SSAO。

CSM 只服务一盏方向光，采用固定宽度过滤，没有 PCSS、VSM 或光线追踪接触阴影；当前目标是实用 CSM 质量，不承诺与现代引擎在所有场景下完全一致。镜面为单个平面、单次反射；折射来自独立不透明快照，没有透明层间递归折射、真实背面厚度求解或粗糙折射预过滤。已支持全局天空 IBL、世界空间漫反射探针、实时 DDGI 和混合 GI 的基础屏幕/世界反射，但没有递归镜面、物体速度或平面反射粗糙度预过滤。完整的 `Record → End → Submit → CompleteFrame` 与失败 `Cancel + DiscardFrame` 示例见[管线调用与所有权](../Render/README_MATERIAL_PIPELINE.md#调用与所有权)。

## 验证内容

- `material_lab_geometry_test`：球/平面的索引、CCW、法线、切线、极点和 UV 接缝；程序纹理尺寸及数据。
- `material_lab_pbr_test`：参数布局、范围、布尔编码、资产默认值、实例隔离、逐对象覆盖及 Pass 结构。
- `render_target_test` / `scene_effects_test` / `post_process_test`：FBO、纹理与命令兼容；CSM 稳定拟合、guard/尺度与反射数学、544 字节场景 ABI；240 字节后处理配置。
- `material_pipeline_lifecycle_test`：管线资源失败清理与所有权。
- `material_pipeline_history_test`：真实 GPU 管线的串行 token、Complete/Discard 与历史失效。
- `msaa_render_target_test`：CPU 模拟 GL，检查多采样附件与 resolve 契约、状态恢复；真实 MSAA 由 `--quality-test` 验证。
- `temporal_effects_test`：AA/模糊参数、Halton 抖动及 400 字节 ABI。
- `advanced_material_test`：208 字节共享 PBR 布局、流动/透射参数、透明域路由和排序兼容。
- `bloom_stability_test`：真实 GPU 提取及完整 1..6 层重建，恒定 HDR、奇数/1×N/边缘输入、归一化能量、光晕半径与中心能量占比；三档亮点各 33 相位，检查能量及对齐后形状变化。
- `ssao_quality_gpu_test`：正式 SSAO 法线重建与 AO shader，偶数/奇数尺寸的解析斜面法线、无遮挡面无条纹、抬升板接触遮蔽、关闭/背景输出白色。
- `pbr_specular_aa_gpu_test`：正式 PBR shader 的解析平滑法线区域，太阳/点光与低粗糙度的 33 相位高光变化、平法线开关不变、粗糙表面及 176 字节 UBO；不作为真实球体或法线贴图 mip 的针对性证明。
- `temporal_effects_gpu_test`：独立 GPU 的 FXAA、TAA 重投影/拒绝和相机模糊验证。
- `shadow_quality_gpu_test`：独立 GPU 的阴影采样质量回归，复用正式 CSM shader helper；覆盖真实光栅斜面的自阴影、256/1024 atlas 与多个亚纹素相位、接触/抬升对照及级联过渡。
- `area_light_gpu_test`：正式 PBR 区域光 helper 的真实 GPU 测试；解析矩形辐照度、面积/距离/旋转/单双面、四灯叠加、金属无漫反射、256 字节 UBO 和窄镜面相位连续性。
- `indirect_lighting_gpu_test`：正式 GPU 追踪与双边上采样，验证颜色渗透、遮挡、未命中和屏幕边界。
- `--smoke-test`：GPU uniform 布局、实际材质绘制、所有效果开关和滤镜、HDR/阴影/AO 附件内容、奇数/最小尺寸重建、参数编辑与重置、透明裁剪和极端发光；检查 GL 错误。
- `--quality-test`：全分辨率/2×镜面、MSAA4 resolve、FXAA 图像响应、TAA 相对 history-off 的稳定性、材质编辑/跳切/resize 的历史失效，以及相机静止不糊、运动才糊；检查 GL 错误。
- `--shadow-test`：接触/斜面/薄遮挡物场景、可见度与级联诊断、2048/4096/8192 atlas 重建恢复、掠射方向光、移动相机及非法偏差参数。指定截图路径时额外保存 `_visibility.png`、`_cascades.png`；对应 CTest 名为 `material_lab_shadow_test`。
- `--closeup-test`：640×724 竖幅近景、可配置相机、TAA + MSAA4 静态稳定性，以及逐步关闭效果的截图；对应 CTest 名为 `material_lab_closeup_test`。
- `--advanced-test`：玻璃/流动水面/发光贴图，区域灯与单次漫反射 GI 开关、透明光学强度和扰动图响应、主视图/镜面快照、MSAA4/TAA/相机模糊与非法区域灯参数；可导出稳定后的场景图，对应 CTest 名为 `material_lab_advanced_test`。
- `--sky-test`：天空漫反射/镜面、AO、零强度、一次反弹、主/镜面背景、TAA 历史与 HDR 载入；可导出场景及 `_gallery` 材质展示图，对应 CTest 名为 `material_lab_sky_test`。
- `--probe-test`：封闭房间局部天空遮挡、屏外发光、相机独立缓存、后台烘焙、主/镜面视图与 MSAA/TAA；对应 `material_lab_probe_test`。
- `--realtime-gi-test`：实时 GPU 能量/距离矩、动态天空、移动变色灯、多反弹、自发光、封闭/隔墙、缓存与抗锯齿；对应 `material_lab_realtime_gi_test`。
- `lumen_gi_test`：规则三角面参数化、共面邻接/refit、空资产、有界图集覆盖、世界查询反变换与设置边界。
- `--lumen-gi-test`：Surface Cache、多反弹、局部天空、屏幕/世界射线、动态灯光、自发光、反射、MSAA/TAA 及保留 DDGI 切换；对应 `material_lab_lumen_gi_test`。
- `--realtime-gi-stability-test --lumen-gi`：三个预设各预热384帧、测连续96帧的局部墙面及世界缓存稳定性；对应 `material_lab_lumen_gi_stability_test`。不带 `--lumen-gi` 检查原 DDGI。
- `--advanced-quality-test`：960×600 高俯视/玻璃近景或自定义相机，固定时间的效果隔离、TAA 静态/移动/跳切，以及 TAA+MSAA4+Bloom 最终图；对应 CTest 名为 `material_lab_advanced_quality_test`。

本次 GPU 斜面回归中，坡度自适应 RPDB 在所测五种坡度、两种 atlas 尺寸与三个亚纹素相位下的最差平均可见度为 `.999965`，最小像素值为 `.999512`；阴影场景中盒子受光正面的平均误遮挡由约 `2.38%` 降至 `0`。结果限定于这些测试场景，详细对照见[管线验证记录](../Render/README_MATERIAL_PIPELINE.md#验证与当前范围)。

SSAO 的五种尺寸斜面回归中，整数邻居修复后的最大法线误差小于 .000623，无遮挡面的 AO 最小值/均值均为 1、列差为 0。高光抗锯齿的解析平滑法线测试中，太阳/点光在 roughness=.045/.08 时的能量相对极差分别由 32.006/17.9057 降至 .182620/.180236，仍有约 18% 残余波动；这些受控结果不代表所有曲面、法线贴图或相机运动均无闪烁。

独立 GPU target 可单独构建运行：

```powershell
cmake --build build --target shadow_quality_gpu_test bloom_stability_test ssao_quality_gpu_test pbr_specular_aa_gpu_test temporal_effects_gpu_test area_light_gpu_test indirect_lighting_gpu_test --parallel 4
.\bin\shadow_quality_gpu_test.exe
.\bin\bloom_stability_test.exe
.\bin\ssao_quality_gpu_test.exe
.\bin\pbr_specular_aa_gpu_test.exe
.\bin\temporal_effects_gpu_test.exe
.\bin\area_light_gpu_test.exe
.\bin\indirect_lighting_gpu_test.exe
```

`async_ExecuteCode` 仅用于此测试模块的 uniform 反射和 framebuffer 读回诊断；材质绘制、数据上传和资源创建都走现有 RHI。


## 使用 Vulkan 硬件光追

```powershell
.\bin\material_lab_demo.exe --vulkan --lumen-gi
# 保留同一 RHI/材质/后期管线，开启 Khronos 验证层做完整回归
.\bin\material_lab_demo.exe --vulkan --validation --lumen-gi-test --procedural
# 同后端软件路径对照；默认自动检测硬件 ray query 能力
.\bin\material_lab_demo.exe --vulkan --software-rays --lumen-gi
.\bin\vulkan_backend_gpu_test.exe --validation
.\bin\lumen_reflection_gpu_test.exe --vulkan
.\bin\lumen_reflection_gpu_test.exe --vulkan --software-rays
.\bin\lumen_reflection_gpu_test.exe
```

默认启动仍为 OpenGL；--vulkan 创建无 GL 上下文的窗口，整个图形渲染与呈现使用新 Vulkan 后端。启动日志显示实际 GPU 与 native ray query=1/0。--validation 需要 SDK 验证层，普通运行只需显卡 Vulkan 驱动及随程序复制的 shaderc_shared.dll；无 Vulkan SDK 的构建继续支持 OpenGL。启动上下文、资源布局转换与后端限制见 [Vulkan 后端](../Render/README_MATERIAL_PIPELINE.md#vulkan-后端与距离场)。

2026-10-05 本机 RTX 4060 Laptop：原生 Vulkan ray query、4x MSAA 颜色/深度解析、半透明混合、几何着色器、窗口缩放及完整 GI/后期路径通过 GPU 回归；距离场/BVH 对零厚度墙的命中一致。GGX 白色环境测试粗糙度 .05/.35/.70/.71/.95/1 的能量依次为 1/.982197/.697764/.683826/.360161/.306861，与独立积分最大绝对误差 .000305；Vulkan 硬件、Vulkan 软件与 OpenGL 一致。Vulkan 连续 96 帧墙面亮度标准差平均/峰值（/255）：开放无 AA .000003/.000007，封闭无 AA .000934/.003196，开放 TAA+MSAA4+Bloom .004061/.038472；天空可见性与探针分类变化均为0。

当前四射线反射版本，1280×800、TAA+MSAA4+Bloom 的完整场景/恢复批次：Vulkan 原生光追 GPU 时间 8.629/6.222 ms/帧，CPU 墙钟时间 11.369/8.391 ms/帧；OpenGL 软件追踪 GPU interval 23.482/23.686 ms/帧，CPU 墙钟时间 23.584/23.766 ms/帧。Vulkan 按帧计时，OpenGL 区间包含批次内 CPU 提交空隙；两者排除初始化与编译，受升频和后台活动影响，不是严格算法速度对照。上面早期性能数字来自旧单射线反射路径，当前版本应以重新运行 `--benchmark --lumen-gi` 为准；硬件路径加 `--vulkan`。

## 2K 稳定性与 90 FPS 配置

这一节的性能数据来自旧测试房间。新的室内外 Showcase 含更多物体、灯光和水面反射，性能配置与实测另见后文，不能直接套用旧房间的 90 FPS 数字。

GI 射线起点会先越过重建误差造成的自身背面，再限制与接触面的距离，消除随 TAA 八帧采样周期重复的墙面假遮挡。重投影使用几何有效的四点插值和方差裁剪。光源变化时直接光整图同帧重算，间接反弹仍按预算更新；被关闭的点光位置动画不再触发整图直接重照明。Vulkan 渲染完成后保留原回调语义，呈现独立进行，每张交换链图像使用自己的 semaphore；最小化时跳过零尺寸呈现。

2026-10-05 本机 RTX 4060 Laptop，程序贴图、2560×1440 渲染、完整 GI/反射/Bloom/SSAO：旧 4×MSAA 配置 GPU/墙钟为 14.575/16.672 ms；优化后一次 96 帧呈现测试为 8.461/10.907 ms（91.7 FPS），更长的 360 帧流动材质动画测试为 9.247/11.621 ms（86.0 FPS）。TAA + 2×MSAA 的同类 360 帧测试为 8.824/11.110 ms（90.0 FPS），因此需要 90 FPS 时显式选 2×MSAA；默认保留 4×MSAA。256 与 320 反射追踪网格的全图平均差异为 .151/255，不能据此保证所有局部细节相同。呈现到本机原生 2560×1600 屏幕，渲染尺寸仍为 2560×1440，日志分别显示两者。其他设备、复杂场景或后台活动下不保证 90 FPS。

64 帧局部墙面检测：2K 完整 GI 的静止亮度峰值标准差由 2.891 降至 .018/255；微幅环绕相机的最大相邻帧跳变由 26.546 降至 .115/255。该测试同时隔离反射、AO/Bloom、屏幕追踪及 TAA，避免靠后期平均隐藏问题。Vulkan 验证、硬件/软件/OpenGL GGX 能量、重建误差与接触面、视图历史资源和同帧重照明回归通过。

```powershell
# 90 FPS 档：全屏显示，明确以 2560×1440 渲染；VSync 默认关闭
.\bin\material_lab_demo.exe --vulkan --lumen-gi --fullscreen --render-width 2560 --render-height 1440 --msaa 2 --fps 90

# 原 4×MSAA；更高反射细节可另外加 --lumen-reflection-detail 320 --lumen-reflection-scale 1 --lumen-source-scale 1
.\bin\material_lab_demo.exe --vulkan --lumen-gi --msaa 4

# 六个世界位置的局部墙面稳定性，包含静止、微幅环绕与效果隔离
.\bin\material_lab_demo.exe --vulkan --validation --lumen-view-stability-test --procedural --benchmark-width 2560 --benchmark-height 1440

# 可见全屏呈现及 360 帧 UV/flow 动画计时；计时不受 --fps 上限控制
.\bin\material_lab_demo.exe --vulkan --benchmark --lumen-gi --procedural --benchmark-width 2560 --benchmark-height 1440 --benchmark-present --fullscreen --msaa 2
```

`--render-width` / `--render-height` 仅用于 Vulkan 的显式渲染尺寸，适合屏幕原生尺寸与性能目标不同的情况；未指定时交互渲染跟随真实窗口。`--lumen-view-stability-test` 由 `MATERIAL_LAB_GPU_TESTS` 注册为可选 OpenGL CTest，需 GPU，240 秒超时且串行运行。性能测试不以设备帧率作为通过条件；Vulkan GPU 时间戳不含帧间 CPU 空隙，墙钟时间含提交、呈现与诊断回调。

最终版本使用 `--render-width/height` 再测 360 帧：GPU/墙钟 9.171/11.139 ms，89.8 FPS（`build/lumen_last_90_profile.log`）；两次 2×MSAA 长批次为约 89.8–90.0 FPS。该档六个墙面点的峰值标准差 .0067/255，微幅环绕最大相邻跳变 .115/255，Vulkan 验证错误为零。修正 OpenGL 测试窗口尺寸并要求每个测量点亮度大于 1 后，真实 OpenGL 2K 检测也通过，避免把越界黑像素当作稳定画面。

## 连通室内外光照场景

`--showcase` 打开庭院、展厅和水池组成的同一场景；原材质画廊中按 `5` 也可进入。场景使用程序几何和现有 PBR 材质，不依赖额外模型。展厅有真实门洞、侧窗和天窗，庭院有遮阳架、长椅、植物和路径灯。金属球、陶瓷方块、玻璃、流动折射水面、粗糙墙面与发光面板覆盖不同光照响应。

构建后可直接双击本目录的 `RunShowcase.cmd`，启动 Vulkan、2K 全屏、2×MSAA、90 FPS 上限的综合场景。

`RunShowcasePerformance.cmd` 提供单独性能档：输出仍为 2560×1440，内部以 1600×900 渲染，使用 TAA、单采样附件、有界三次空间重建和 `.25` 反射颜色源比例。两个脚本优先使用 `output/Release/bin/material_lab_demo.exe`，没有 Release 时回退到 `bin` 的 Debug 文件，并从现有 `bin/materials/tite` 读取瓷砖。`RunShowcase.cmd` 保留原生 2K 与 2×MSAA。Debug 回退的帧率较低。

`--render-scale .5..1` 控制 Showcase 内部尺寸；按 `P` 在 `1/.75/.625/.5` 切换，HUD 和窗口标题显示实际内部/输出尺寸。P 只改变分辨率，不修改抗锯齿设置。`--lumen-source-scale .25..1` 单独控制屏幕反射的 HDR 颜色源，默认 `.5`；完整内部深度/法线、反射解析尺寸和追踪网格不变。该空间重建不是 UE TSR，细小反射和纹理细节可能比原生尺寸柔和。

```powershell
.\bin\material_lab_demo.exe --vulkan --showcase --fullscreen --render-width 2560 --render-height 1440 --msaa 2 --fps 90
```

默认开启类 Lumen GI、天空光、CSM 太阳阴影、4 个点光、2 个矩形区域光、自发光、TAA、Bloom、SSAO、粗糙反射及水面半分辨率平面反射。`--showcase --realtime-gi` 可使用保留的 DDGI 路径。`--sky-hdr` 仍可替换天空；`--procedural` 保证不读取旧贴图。

| 操作 | 按键 |
| --- | --- |
| 自由相机移动 / 升降 | WASD / Q、E |
| 转动相机 / 加速 / 调整移速 | 按住鼠标右键 / Shift / 滚轮 |
| 庭院、室内、水池快捷视角 | 1、2、3 |
| 选择物体 / 切换六个可移动物体 | 鼠标左键 / Tab、Shift+Tab |
| 看向选中物体 | F |
| 沿世界 X/Z 轴移动 / 升降物体 | 方向键 / PageUp、PageDown |
| 旋转物体 / 恢复物体 / 恢复全部物体与相机 | Z、X / R / Backspace |
| 开关太阳 / 点光 / 区域光 / 自发光 / 天空光 | F5 / F6 / F7 / F8 / F9 |
| 开关 GI / Bloom / AO / 切换抗锯齿 / 相机模糊 | L / B / O / N / M |
| 切换内部渲染比例 | P |
| 点光运动 / 屏幕帮助 / 退出 | Space / F1 / Esc |

六个可移动物体为庭院金球、陶瓷方块、室内铬球、红色方块、玻璃球、发光面板。移动发光面板时，对应矩形区域光同步移动、转动。灯具表面自发光与点光/区域光可以独立关闭，方便隔离直射和 GI 贡献。屏幕左上显示选中物体、光源开关和 GI 更新状态；帮助走 Overlay 阶段，不受曝光、Bloom、TAA 影响。相机为自由飞行观察相机，不带人物碰撞。

物体变换当帧进入主视图、CSM、平面反射和 GI 追踪几何。相同拓扑使用 BVH refit，保留三角面顺序、Surface Cache 分配和已有间接光；直接光及世界探针可见性当帧更新，间接反弹在随后四帧提高更新预算。移动后过期的距离场停止参与跳步，使用精确 BVH/原生光追；初始静态场景仍构建距离场。相机移动和光源强度/位置变化不重建几何。GI 球体使用 24×32 几何代理，主视图、CSM、平面反射继续使用原高精度网格。当前 CSM 只处理太阳直射阴影；点光与区域光的直射独立阴影仍未实现，其 GI 追踪具有场景可见性。这一场景比旧测试房间更复杂，90 是帧率上限，实际性能应单独测量。

```powershell
# 三视角截图与动态几何/灯光回归；输出 _courtyard、_interior、_pool 三张 PNG
.\bin\material_lab_demo.exe --vulkan --validation --showcase-test --procedural --msaa 2 --screenshot build\showcase.png
.\bin\material_lab_showcase_test.exe
# 连续帧局部画面、水面运行半小时后的稳定性、移动物体当帧追踪与缓存保留
.\bin\material_lab_demo.exe --vulkan --validation --showcase-stability-test --msaa 2 --legacy-textures bin\materials\tite --render-width 2560 --render-height 1440 --screenshot build\showcase_stability.png
```

几何与相机控制位于 `Public/showcase_scene.h` / `Private/showcase_scene.cpp`，演示输入位于 `Test/showcase_demo.inl`。所有绘制、HUD 纹理更新与资源管理仍使用现有 RHI，未修改底层渲染接口。

静态建筑、遮阳架、家具和灯具按材质/阴影状态合并网格，减少多个渲染 Pass 的小物体提交；可移动物体和透明水面保留独立变换及排序。水面使用专用低频水波法线和流向图；流动采用有限相位双层交叉淡化，不再把空间变化的流向无限乘以总时间。水面不绘入自己的平面反射或不透明 CSM，平面反射按 IOR/Fresnel 控制贡献。

GI/反射捕获使用稳定的非抖动相机，主材质按 TAA 偏移映射回该网格。反射法线按追踪网格覆盖面积过滤，法线缩短产生的方差进入粗糙度；主画面与反射捕获使用一致的法线抗锯齿粗糙度。屏幕反射必须与世界追踪命中吻合，粗糙反射的后续高光按角度覆盖过滤，区域光使用有限面积过滤；重建使用更宽的有效邻域减少无匹配像素产生的暗块。

当前漫反射在世界空间保存独立的入射辐照度，屏幕只识别接收三角面，再按确切世界位置重建；普通转头和走动不再重新生成低密度半球采样。规则重心格子、九点连续梯度重建与局部共面边连接减少顶棚三角斑块，不跨越真实天窗。独立自发光源按投影立体角与遮挡积分，灯具已有点光/区域光时不重复注入发光能量；关闭对应直接灯光后仍可检查单独自发光。天花灯只向下发光。点光半径 `.14/.15/.18/.18` 世界单位，有限光源过滤降低瓷砖高光闪点，半径内的辐照度限制为 `I/max(radius²,1e-4)`。点光与区域光主材质使用几何朝向约束，贴图法线不能接受几何背面的光；这不代替物体间局部直射阴影。粗糙度超过 `.4` 的反射复用世界缓存，`.3..4` 渐变，光滑金属保留独立追踪。

水池离开相机视锥时跳过整套平面反射场景和镜面 MSAA 解析。Vulkan 在每帧内复用相同常量快照和描述符，局部更新、材质切换仍产生对应独立版本；前端 RHI 不变。

`--showcase-motion-test` 覆盖相机左右移动约 `.7` 世界单位、转头约 `20.6°` 的连续帧，直接读取 GI 浮点图并跟踪同一世界位置，隔离本来就随视角变化的镜面高光。接收点有 CPU 遮挡验证，避免把镜面球遮住墙面的变化误认为墙面 GI 闪烁。包含背墙、天花板、低视角地面与所有光源开启的性能批次。

```powershell
.\bin\material_lab_demo.exe --vulkan --validation --showcase-motion-test --legacy-textures bin\materials\tite --msaa 2 --render-width 2560 --render-height 1440 --screenshot build\showcase_world_motion.png
# 同样的三视角、全部光源开启，实际全屏呈现；跳过连续帧读回，只统计稳定帧性能
.\output\Release\bin\material_lab_demo.exe --vulkan --showcase-performance-test --benchmark-present --fullscreen --legacy-textures bin\materials\tite --msaa 1 --render-width 2560 --render-height 1440 --render-scale .625 --lumen-source-scale .25 --screenshot build\showcase_performance.png
```

`--render-width/height` 在 Showcase 中指定最终输出尺寸；内部尺寸由输出尺寸乘 `--render-scale` 后取整。相机使用输出宽高比，HUD 与读回截图使用输出尺寸，GI 连续帧检测则读取实际内部浮点图并跟踪世界位置。切换内部尺寸重置屏幕 TAA/反射历史，保留世界 GI 缓存。该比例同时适用于 Showcase 的 quality、stability、motion 和 performance 入口；普通材质画廊与旧 Benchmark 仍采用原生内部尺寸。性能批次不受 `--fps` 上限影响，统计排除初始化、着色器编译和截图读回。

2026-10-06 最终版本，RTX 4060 Laptop、Vulkan 原生光追、Release、原始瓷砖、全部光源与后期开启，输出均为 2560×1440，全屏交换链为本机 2560×1600。每个视角先离屏收敛 256 帧，再预热 32 帧呈现，统计 60 帧均值；不启用 validation、不限制帧率。时间戳覆盖每帧 GPU 执行区间，墙钟包含提交、等待与呈现，FPS 为 `1000/墙钟毫秒`。

| 视角 | 原生 2K、2×MSAA、颜色源 .5：GPU / 墙钟 / FPS | 性能档 1600×900→2K、TAA、颜色源 .25：GPU / 墙钟 / FPS |
| --- | --- | --- |
| 背墙 | 15.59 / 18.20 ms / 55.0 | 6.72 / 8.59 ms / 116.4 |
| 天花板 | 14.09 / 16.92 ms / 59.1 | 5.95 / 8.47 ms / 118.0 |
| 地面掠射 | 18.92 / 21.94 ms / 45.6 | 7.61 / 10.47 ms / 95.5 |

日志为 `build/showcase_release_native_performance.log` 与 `build/showcase_release_balanced_performance.log`。这些是三个测试视角的平均值，不是逐帧最低帧率，也不是原生 2K 已达到 90 FPS；性能脚本将帧率上限设为 90。与 Debug 配置的较大差异包含 CPU 编译优化，不全归因于 GI 算法。单独将反射颜色源从 `.5` 降到 `.25` 的同配置 Debug 对照仅节省约 `.3–.4 ms`，该参数不会提高世界缓存精度。

同一最终 Release 性能配置、Vulkan validation 的普通移动/转头回归通过：固定世界接收点的 GI 峰值变异系数为背墙 `.0364%`、顶棚 `.0130%`、地面 `.0110%`，最大相邻帧相对变化 `.0526%`。这是浮点 GI 的局部统计，镜面高光本来随视角变化，未计入该稳定性指标。日志 `build/showcase_release_final_motion.log`；三视角、灯光开关、12 次物体变换、同帧几何、透明材质和渲染比例恢复也通过 `build/showcase_release_final_quality.log`，验证错误为零。Vulkan 原生与 OpenGL 软件追踪的能量、非线性入射重建和不同尺寸反射颜色源回归通过，近源上限/几何朝向 GPU 回归及 CPU 测试通过。

实现参考了 Epic 的[性能指南](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-performance-guide-for-unreal-engine)中世界缓存、粗糙反射复用与内部尺寸预算的方法。这里仍是项目自己的类 Lumen 实现，使用现有 TAA 和空间重建，没有 UE TSR 或完整 Mesh Cards/局部直射阴影系统。

构建优化文件：`cmake --preset windows-release`，然后 `cmake --build --preset windows-release --target material_lab_demo --parallel 4`。旧 Debug 构建与测试入口保留。

诊断 `--showcase-water-diagnostic` 可输出关闭后期、仅直接光、仅天空光的水面连续帧对照。

2026-10-06，RTX 4060 Laptop、Vulkan 原生光追、2560×1440、2×MSAA、原始瓷砖贴图和 validation：连续帧稳定性回归通过。六处墙面/地面在静止时峰值标准差 .046/255、相邻帧均值跳变 .137/255；缓慢移动相机时最大跳变 .478/255。水面最大跳变 .127/255，模拟运行半小时并跨越流动相位复位后 .200/255（此前同配置约 7/255）。12 次连续物体变换的追踪几何延迟为 0 帧，保留已收敛间接光。验证包含固定时间、固定相机、无平面反射、无太阳光对照；这些数值只代表所选局部区域和测试视角，不能替代任意场景的质量验证。日志 `build/showcase_clean_2k.log`。

2026-10-05：本机 RTX 4060 Laptop 的 Vulkan 原生光追、2560×1440、2×MSAA 验证回归和 OpenGL 软件追踪、960×600 回归均通过，包含三视角、局部光贡献、连续修改后的最终 GI 位置、自发光变化与透明渲染；相机/选择/光源控制的 CPU 测试通过。OpenGL 测试每六帧排空 GPU 队列，避免大量软件追踪帧积压导致诊断读回超时。这个综合场景的软件追踪开销较高，实际交互优先使用 Vulkan 原生光追配置。
