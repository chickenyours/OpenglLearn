# Screen Probe 第二阶段：HZB 屏幕光线追踪

本实验接在 [探针布置阶段](PROBE_PLACEMENT.md) 后：从已经落在可见表面的探针发射方向射线，通过 Lumen 风格的 stackless HZB 遍历查找屏幕命中，并读取命中像素的真实 HDR RGB。右下角面板显示所选探针的方向网格，便于逐条观察命中、离屏、深度不确定等结果。

范围是 **屏幕光线追踪及取色**，尚未将方向辐射积分为 GI，也没有 GI 去噪、多次反弹或世界/天空回退。原 `RunProbePlacement.cmd` 继续只做布置；新入口独立启用第二阶段。

## 启动与观察

双击 [RunScreenProbeTracing.cmd](RunScreenProbeTracing.cmd)。脚本优先使用 Release 程序，默认 Vulkan、1280×800 窗口、MSAA1、90 FPS，并保留附加参数与程序退出码。

```text
output\Release\bin\material_lab_demo.exe --vulkan --screen-probe-tracing --procedural --render-width 1280 --render-height 800 --msaa 1 --fps 90
```

| 操作 | 按键 |
|---|---|
| 开关自适应布置 | F2 |
| 基础探针间距 16 / 32 / 64 像素 | F3 |
| 标记 / 候选 / 布置覆盖；冻结时仅标记 / 候选 | F4 |
| 冻结 / 恢复探针世界位置更新 | Space |
| 固定 phase 0 / 八相位布置序列 | J |
| 开关屏幕光追 | F5 |
| 方向辐射 / 状态视图 | F6 |
| 每探针方向网格 4×4 / 8×8 | F7 |
| 选择屏幕投影距离最近的活跃探针 | 鼠标左键单击 |
| 预设视角 | 1 / 2 / 3 |
| 移动前景遮挡块 / 重置 | 左右方向键 / R |
| 转向 / 移动 | 鼠标右键，WASD，Q/E；Shift 加速，滚轮调速 |
| 帮助 / 退出 | F1 / Esc |

追踪开启时，探针小球按各方向的 HDR / 状态结果显示颜色；F5 关闭追踪后恢复蓝色基础探针和橙色自适应探针。F4 的候选视图继续使用第一阶段的布置显示，覆盖视图也只表示布置支持，不表示射线命中覆盖。

默认优先选择屏幕中心附近、至少有两条有效 `Hit` 的活跃基础探针，便于立即观察真实 HDR；没有满足条件的探针时才退回其他活跃探针。单击可改选屏幕投影距离最近的活跃探针，选中球有白边，HUD 显示 `ProbeID`。手动选择按屏幕距离计算，当前不做相机全分辨率深度的遮挡筛选。右下角方向网格的每格对应一条实际 GPU 射线；F6 切换辐射与状态，F7 改变方向数量。

1280×800 交互截图见 [screen_probe_tracing_overview.png](../../build/screen_probe_tracing_overview.png)。也可在启动脚本后附加 `--screenshot build\screen_probe_tracing_overview.png` 保存当前交互画面。

## 射线与真实取色

方向使用完整球面的等面积映射，每个方向 texel 取中心，4×4 和 8×8 分别生成 16 和 64 个方向。当前实验不对方向 texel 做时间 jitter；J 改变的是布置相位。实验额外使用起点法线的半球拒绝保护，因此背向方向可返回 `Backface`，方向生成本身仍是球面映射。

取色源是**当前帧、探针标记绘制前的 HDR 场景颜色**。成功命中后，从命中像素读取 RGB，写入 ray radiance 结果；显示用的探针小球、物体平均颜色以及诊断状态色都不作为返回辐射。即使真实命中 RGB 为黑色，它仍是 `Hit`，不能据此判为 miss。

没有当前屏幕命中或来源不可用时，返回对应状态，不补入天空或世界缓存颜色。冻结探针只锁住布置位置，屏幕追踪依然受当前相机可见深度和当前帧取色源限制。

| 值 / 状态 | 含义 | 显示约定 |
|---|---|---|
| 0 `Inactive` | 未参与追踪的空槽 | 深灰 |
| 1 `Hit` | 有效屏幕命中并读取 HDR | radiance 视图为真实 HDR RGB；状态视图为绿 |
| 2 `OffScreen` | 射线离开屏幕可见范围 | 紫红 |
| 3 `NoHit` | 有效搜索范围内未命中 | 青 |
| 4 `UncertainDepth` | 厚度、自相交或命中法线不足，无法确认可靠命中 | 黄 |
| 5 `Backface` | 法线背向保护拒绝 | 灰 |
| 6 `OriginUnavailable` | 探针起点在当前屏幕来源中不可用 | 红 |
| 7 `InvalidRadiance` | 命中颜色读取无效 | 粉 |
| 8 `StepBudget` | 遍历步数预算耗尽 | 紫 |

这些状态与 radiance 分开保存；状态色用于解释结果。失败射线没有真实命中 RGB，不用其他场景颜色伪造返回值。

## HZB 遍历

场景深度生成最多 11 层独立纹理。第 0 层保存正线性视深度，更粗层取子 texel 的最小值，代表该单元内最近的可见表面；奇数尺寸采用向上取整，边缘 texel 也参与归约。

UE 使用 reverse-Z 的 closest HZB `max`，本实验的正线性深度 `min` 表示相同的“最近表面”。这只是深度存储表示的转换：实际射线遍历使用投影 NDC 深度插值，保持透视正确，不能把线性视深度直接沿屏幕参数线性插值。

stackless 遍历维护当前射线参数和 mip：射线确定在单元最近表面前方时，跨过 tile 的 XY 边界并升到更粗 mip；可能穿过表面时，下降到更细 mip。最终回到全分辨率深度进行厚度确认，并记录命中位置、步数和深度读取统计。达到屏幕边界、最大距离或步数预算时，以相应状态结束。

跨 tile 时，本实验采用固定 `.005` 像素的边界越界偏移；UE 本体使用 mip 单元尺寸乘 `.005`。固定像素偏移减小粗层跨边界后的额外前进距离，用于减少跳过薄几何的风险。

## UE 5.7.1 源码阅读入口

本机源码根目录为 `D:/UE5/engine/UE_5.7`，版本 UE 5.7.1、Changelist 48512491。以下行号对应这份源码。

| 入口 | 观察内容 |
|---|---|
| [TraceScreenProbes](<D:/UE5/engine/UE_5.7/Engine/Source/Runtime/Renderer/Private/Lumen/LumenScreenProbeTracing.cpp:650>) | 屏幕追踪调度、HZB 参数和 shader 变体 |
| [GetScreenProbeTexelRay](<D:/UE5/engine/UE_5.7/Engine/Shaders/Private/Lumen/LumenScreenProbeTracingCommon.ush:122>) | 等面积球面方向生成 |
| [ScreenProbeTraceScreenTexturesCS](<D:/UE5/engine/UE_5.7/Engine/Shaders/Private/Lumen/LumenScreenProbeTracing.usf:56>) | 起点重建、法线偏移与逐方向追踪 |
| [TraceScreen](<D:/UE5/engine/UE_5.7/Engine/Shaders/Private/Lumen/LumenScreenTracing.ush:14>) | 将 closest HZB 和场景深度传给 TraceHZB |
| [TraceHZB](<D:/UE5/engine/UE_5.7/Engine/Shaders/Private/HZBTracing.ush:33>)、[stackless 循环](<D:/UE5/engine/UE_5.7/Engine/Shaders/Private/HZBTracing.ush:121>) | 视锥裁剪、跨 tile、升降 mip 与相交确认 |
| [厚度与不确定性](<D:/UE5/engine/UE_5.7/Engine/Shaders/Private/HZBTracing.ush:206>) | 最细层厚度检查及后续薄特征 uncertainty 判断 |
| [closest HZB 归约](<D:/UE5/engine/UE_5.7/Engine/Shaders/Private/HZB.usf:256>) | reverse-Z 最近深度的 max 归约 |
| [历史验证与取色](<D:/UE5/engine/UE_5.7/Engine/Shaders/Private/Lumen/LumenScreenProbeTracing.usf:320>) | HistorySceneDepth 检查，随后读取 PrevSceneColorTexture |

UE 默认路径在历史深度验证后读取重投影的上一帧颜色，实际取色见 [Tracing.usf:340](<D:/UE5/engine/UE_5.7/Engine/Shaders/Private/Lumen/LumenScreenProbeTracing.usf:340>)；固定红色只属于其 `DEBUG_VISUALIZE_TRACE_TYPES` 分支。本实验明确使用本帧 HDR，尚未复现历史重投影、历史深度验证、完整薄特征 uncertainty、植被/毛发处理、wave occupancy 退出和后续世界追踪回退。

`LumenScreenTracing.ush` 中的 `SampleSceneColorAtHit` 主要供反射辅助路径使用；本说明的 ScreenProbe 本体入口是 `ScreenProbeTraceScreenTexturesCS`，不把辅助函数的法线拒绝规则混入本体流程。

## 结果读取与回归入口

公共设置为 `effects.screenProbePlacement.trace`，定义见 [screen_probe_trace.h](../Render/Public/Pipeline/screen_probe_trace.h)。默认每探针 8×8 方向、最多 128 步、最高 mip 10、最大距离 12，法线偏移下限 `.02`，绝对/相对厚度 `.01/.02`。起点偏移还考虑半像素角点沿法线的投影；命中容差为绝对厚度加相对厚度乘场景深度。

`MaterialLabModule::ReadbackScreenProbeTrace(bool includeSources=true)` 返回 `ScreenProbeTraceReadback`，定义见 [material_lab_module.h](Public/material_lab_module.h)。按 probe slot、方向 Y/X 顺序读取；空槽为 `Inactive`，容量不等于实际活跃探针数量。

| 字段 | 用途 |
|---|---|
| `atlasSize` / `sourceSize` / `uniformGridSize` | 实际 atlas、取色源和基础网格尺寸 |
| `radianceStatus` | RGB 为真实辐射，W 为 `ScreenProbeTraceStatus` |
| `hitWorldDistance` | XYZ 为命中世界位置，W 为距离 |
| `hitPixelStepsMip` | XY 为命中像素，Z 为遍历步数，W 为最高访问 mip |
| `rayDirectionDepthFetches` | XYZ 为射线方向，W 为深度相关读取次数，包含起点/HZB/命中几何核对 |
| `sourceHdr` / `sourceViewPositions` / `sourceWorldNormals` | 独立核对取色和可见表面 |
| `hzbSizes` / `hzb` | 逐层核对尺寸及最近深度归约 |

先确认状态为 `Hit`，再消费有效命中位置和像素；其他状态的端点及统计用于诊断。

```text
output\Release\bin\material_lab_demo.exe --vulkan --validation --screen-probe-trace-test --procedural --msaa 1 --screenshot build\screen_probe_trace.png
```

当前 Release 和 Debug 已构建，Release 的 Vulkan validation 与 OpenGL 专用回归均通过，日志为 `build/screen_trace_ready_vulkan.log` 和 `build/screen_trace_ready_opengl.log`。原探针布置的 Vulkan validation 回归及 showcase / GI room 两项 CPU 回归也通过。

主回归使用 640×400、固定布置 phase 0、32 像素基础间距、8×8 方向和 512 步预算，独立检查可见表面深度、逐层 min 归约、真实命中位置及源 HDR 取色。`maxMip=0` 对照保留同一追踪算法、起点和颜色源，只禁用粗层跳跃；它用于检查层次跳步，不作为独立几何真值。

| 回归检查 | 实测结果 |
|---|---|
| 8×8 层次追踪 | 11648 条活跃射线，834 条 `Hit` |
| 独立 CPU 可见首交抽样 | 108 / 125 匹配 |
| 层次 / mip 0 已完成射线对照 | 4637 条；排除 626 条预算耗尽射线 |
| 对照共同 / 并集命中及表面一致性 | 829 / 829，829 条表面一致 |
| 黑色可见表面 | 834 条有效 `Hit` 的 HDR RGB 为 0，状态与原组一致 |
| 4×4、默认 128 步预算 | Vulkan 217 条命中，OpenGL 216 条；两者预算耗尽均为 0 |
| 冻结探针后移动相机 | 1584 条正确返回 `OriginUnavailable` |
| 637×401 奇数尺寸 | 1125 条命中，边缘深度归约检查通过 |
| 1280×800 / 16 像素间距 / 8×8 方向 / 默认 128 步 | atlas 2048×4500；36000 个 probe slot、2304000 条 ray 容量；Vulkan 11879 / OpenGL 11881 条命中 |

640×400 的 OpenGL 4×4 组有一条边界射线落在深度不确定分类；1280×800 稠密组有两条命中数量差异，属于后端浮点边缘分类差。Atlas 宽度随容量调整，读回应使用 `atlasSize`，不能假定固定 1024 宽；slot / ray 容量包含未活跃的候选槽。独立 CPU 首交只统计其命中表面也对相机可见的抽样射线；108/125 不代表完整世界射线求交的覆盖率。

在完成的对照射线上，Vulkan 的逻辑深度读取量为 mip 0 对照的 `.0807249`（约减少 92%），OpenGL 为 `.0807189`。这项计数按**每条逻辑射线执行一次**统计；当前四个诊断 texel 各自重执行追踪，实际着色器工作约重复四次。它不是硬件纹理读取计数，也不是整帧 GPU 耗时或 FPS 收益。结果限定于上述场景、预算和已完成射线集合。
