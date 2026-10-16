# 相机跟随的多级 Global SDF

双击 [RunSdfBanquet.cmd](RunSdfBanquet.cmd)。入口现默认使用 `--sdf-clipmaps`，在真实导入的 12 个 Utah Teapot、三张桌面和地板中显示四级 Global 距离场。相机附近保留细体素，向外依次使用更大的体素；走动时只重新合并进入覆盖范围的砖块。颜色表示**实际命中所用的层级**，三维表面来自相应距离纹理的求交。新版求交加速、测量结果与复现方式见 [Clipmap 性能验证](SDF_CLIPMAP_PERFORMANCE.md)。

这一步扩展了 [模型与近中远 MDF 场景](SDF_MODEL_BANQUET.md)。原 `--sdf-banquet` 入口仍保留固定世界 Global 体积，作为上一阶段的对照；本文只描述新的相机 clipmap 入口。两者共享同一批真实 OBJ、Model 导入和 24 / 48 / 128 Mesh SDF 资产。

## 查看与操作

```text
Module\MaterialLab\RunSdfBanquet.cmd
Module\MaterialLab\RunSdfBanquet.cmd --sdf-view split
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --sdf-clipmaps --render-width 1280 --render-height 800 --msaa 1 --fps 60
```

默认从总览观察三排茶壶及中、远层级。按 2 从前排第二个茶壶上方靠近，检查细一级的壶盖和前壳；按 1 返回总览，按 3 观察远处。只改变视角不会重新烘焙茶壶的局部距离场或重建世界三角形结构。

| 操作 | 按键 |
|---|---|
| 自动选级 → 强制 L0 → L1 → L2 → L3 → 自动 | K |
| 开关命中层级颜色 | C |
| 开关覆盖边框及表面上的层级边界线 | H |
| 开关实际采样体素网格 | B |
| 普通模型 / Mesh 三维 / Global 三维 / 左右分屏 | V |
| 总览 / 近处茶壶 / 远处观察 | 1 / 2 / 3 |
| 相机移动、升降和转向 | WASD、Q/E、鼠标右键；Shift 加速，滚轮调速 |
| 移动前排第二个茶壶 / 重置 | 左右方向键 / R |
| Mesh 自动距离选择 / 手动 LOW、MEDIUM、HIGH | L / F12 |
| 开关实际模型 Raster LOD | T |
| 帮助 / 退出 | F1 / Esc |

K 只强制 Global 三维显示和切片使用哪一级，探针的 Global 回退仍按世界命中位置自动选级。强制某一级时，只显示该级覆盖内真正存在的零面；远处视角的 L0 小范围可能没有物体，可按 2 靠近茶壶检查。C 与 H 只改变叠加显示，不改变纹理或求交结果。L、F12、T 控制局部 Mesh 与模型质量；它们不会把四级 Global 都改成当前 Mesh 档。

其余探针、屏幕追踪、切片、对象选择和状态色按键见 [距离场追踪说明](SDF_PROBE_TRACING.md)。O 只隔离 Mesh 三维视图，Global 始终包含全部场景实例。

## 四级实际距离体积

交互场景每级使用 `64³` 个节点，砖块为 `4³`。公共 CPU 类的默认尺寸仍为 `96³`，CPU 测试同时覆盖其默认配置；交互入口显式选择 64，使一行体素与宽 1024 的二维图集更容易合并为局部上传矩形。

| Global 层级 | 世界体素间距 | 64 节点的存储周期 | 茶壶合并输入 | 默认层级色 |
|---|---:|---:|---|---|
| L0，最近 | .03125 | 2 | HIGH 128 | 青蓝 |
| L1 | .125 | 8 | MEDIUM 48 | 绿 |
| L2 | .5 | 32 | LOW 24 | 橙 |
| L3，最远 | 2 | 128 | LOW 24 | 紫 |

“存储周期”是 `N × spacing`；节点实际位置从 `origin` 到 `origin + (N−1) × spacing`。求交再留出一节点保护边界，避免插值和法线差分读取另一侧。表中的宽度不能直接当作有效节点端点距离。远层覆盖更广，细壶嘴、薄壁和窄缝也更容易在合并采样中丢失。

四级茶壶源场都由原版 HIGH 的 15704 个三角形烘焙，不能把 Global 的 L2 误解成简化 Raster OBJ。桌面和地板在各级使用各自不可变的局部场。HIGH 尚未准备完成时 L0 暂用已驻留 MEDIUM；后台完成后将受影响实例范围标为脏区，更新真正的合并输入。显示不会把一个未准备的 HIGH 标签套在 MEDIUM 数据上。

茶壶局部场继续使用显式薄壁偏移 `SurfaceBias = .5 × length(localVoxelSpacing)`；Global 合并的是 `rawSample − SurfaceBias`。更粗的 Global 采样仍可能漏掉细形状，此处没有额外实施 UE 的 coverage 页面、抖动透明或植被专用膨胀。资产、原始 signed-distance 样本、薄壁偏移和 Raster 三角形分别保留，详见 [茶壶几何及误差说明](SDF_MODEL_BANQUET.md)。

## 滚动与局部更新

每一级单独按世界砖块大小对相机位置取 floor，再得到节点原点。设节点数为 `N`、体素间距为 `s`、砖块边长为 `b=4`：

```text
snappedCell = floor(camera / (b*s)) * b
originCell  = snappedCell - N/2
worldNode   = origin + logicalNode * s
physical    = (logicalNode + scroll) positive-mod N
```

这里的环形索引只改变数据存放位置。旧、新体积重叠的世界节点继续使用原来的物理 texel；三线性插值的八个逻辑角各自映射到物理索引，不会把逻辑末端和起点当成相邻世界节点。

当新旧原点相差 `delta` 个节点时，`scroll` 增加同一个 `delta` 并作正模运算。只重新合并新进入的 X/Y/Z 砖块薄层，交叠的薄层只计算一次。没有物体变化时，新节点数量为：

```text
entering = N.x*N.y*N.z - product(max(N - abs(delta), 0))
```

例如当前交互配置跨过一个最细砖块，只进入 `4 × 64 × 64 = 16384` 个节点；若尚未跨过 L1 的世界砖块边界，其他三级不更新。相机在同一个砖块内的小幅移动只改变观察方向和位置，不改变距离场原点、体素或上传量。

若某轴的位移已达该级节点数，旧体积在该轴没有重叠，该级执行 teleport 全量重建。仍有重叠的其他层继续局部滚动，不能因为 L0 的瞬移而全量重建所有层。

每级保留独立的静态、可移动距离缓存。进入的薄层同时计算两者；只移动一个茶壶时，把其**旧位置与新位置**的扩展包围范围标为动态脏区，保留静态缓存，再取最小距离合并。扩展范围包含窄带、局部薄壁偏移、源插值误差与 Global 体素足迹，避免旧影残留或新位置缺场。

当前实现由 CPU 做合并。四级物理节点打包在同一宽 1024 的 float RGBA 二维 GPU 图集中，通道依次为合并距离、最近对象编号、静态距离、动态距离。上传器先把真实被写入的物理范围拆分，再合并相邻的同宽矩形，保留很窄的 X 薄层上传。

对 XYZ 同时滚动或物体移动产生的大量碎片，Global 上传区域限制为最多 128 个：先以补传 texel 数为代价，连接同宽矩形之间较小的纵向间隔；仍然过多时使用自适应 tile 覆盖。这样减少提交次数，同时允许补传少量未改变的样本。统计分别报告 CPU 重算节点与 GPU 实际上传 texel；后者包含间隔和 tile 补传，不能混为同一个指标。

如果禁用追踪期间 CPU 已更新多个 generation，恢复显示时不能只上传最后一次脏区。上传器检测错过的 generation 或空脏区，并完整同步四级图集。这是恢复一致性的补传；正常逐帧局部更新仍使用上述矩形路径。

## 选级、交线和颜色

每个世界采样点优先选择包含它的最细可用保护区。射线推进限制在当前层级边界内；从较粗区域朝内追踪时，还要限制到即将进入的细层边界。穿过边界后重新选择实际采样层级，避免用粗层的大步长跨过近处细节。

不会对细、粗层的距离值直接线性混合。它们的采样误差和局部源偏移不同，跨层的符号变化也不能当成表面；二分求根与法线差分都在同一级内完成。三维显示根据该级真实三线性距离场作保守的单元内推进和求根，未用原始三角形或 BVH 补画表面。

当前使用硬切换，尚未实现跨层表面桥接或 coverage 修复。近处青蓝与绿色区域交界可能出现轮廓跳变、薄壁缺口或可见裂缝；远层也可能填平壶盖、把手等细节。它们来自不同分辨率、不同偏移的真实场在边界并不重合，不能把当前结果称作无缝表面重建。关闭 C/H 后仍能检查这些形状差异，后续过渡处理应在保留安全求交的前提下解决。

Global 与 Mesh 三维显示的 float 目标 alpha 保存**世界射线命中距离**，未命中为零。它用于独立重建世界命中点，核对该点确实位于所选距离纹理的零面。层级色、边界线和体素网格不进入这个距离通道。普通探针回退仍保留既有的局部候选段三角形精化，与纯距离纹理三维显示的职责不同。

超出最外层、没有真实命中或不能从当前屏幕读取命中点光照时，沿用专用回退状态色；这里尚未增加 Surface Cache 光照、GI 积分或去噪。

## 对照本机 UE 5.7 源码

本次核对本机 `D:/UE5/engine/UE_5.7` 的 UE 5.7.1、CL 48512491。以下列出阅读入口，行号对应该版本：

| UE 源码入口 | 参考行为 |
|---|---|
| [GlobalDistanceField.cpp:419](<D:/UE5/engine/UE_5.7/Engine/Source/Runtime/Renderer/Private/GlobalDistanceField.cpp:419>) | 页面图集边长 8，唯一节点边长 7，另留过滤足迹；窄带为 4 体素 |
| [GlobalDistanceField.cpp:1088](<D:/UE5/engine/UE_5.7/Engine/Source/Runtime/Renderer/Private/GlobalDistanceField.cpp:1088>)、[1123](<D:/UE5/engine/UE_5.7/Engine/Source/Runtime/Renderer/Private/GlobalDistanceField.cpp:1123>) | 世界页面大小由体素间距计算；相机按页面 round 对齐并更新范围 |
| [GlobalDistanceField.cpp:644](<D:/UE5/engine/UE_5.7/Engine/Source/Runtime/Renderer/Private/GlobalDistanceField.cpp:644>)、[1183](<D:/UE5/engine/UE_5.7/Engine/Source/Runtime/Renderer/Private/GlobalDistanceField.cpp:1183>) | 根据逐轴页面位移生成进入区域；动态层继承静态层相机更新范围 |
| [GlobalDistanceField.cpp:1157](<D:/UE5/engine/UE_5.7/Engine/Source/Runtime/Renderer/Private/GlobalDistanceField.cpp:1157>)、[1267](<D:/UE5/engine/UE_5.7/Engine/Source/Runtime/Renderer/Private/GlobalDistanceField.cpp:1267>) | 物体修改范围与环形滚动偏移 |
| [GlobalDistanceField.cpp:3244](<D:/UE5/engine/UE_5.7/Engine/Source/Runtime/Renderer/Private/GlobalDistanceField.cpp:3244>) | 完整场更新将 Mostly Static 页表作为父层输入 |
| [LumenScene.cpp:204](<D:/UE5/engine/UE_5.7/Engine/Source/Runtime/Renderer/Private/Lumen/LumenScene.cpp:204>) | Lumen 相邻 Global clipmap 范围按 2 倍扩大 |
| [GlobalDistanceFieldShared.ush:193](<D:/UE5/engine/UE_5.7/Engine/Shaders/Private/DistanceField/GlobalDistanceFieldShared.ush:193>) | 世界坐标环绕映射；第二次 frac 防止负坐标浮点舍入到 UV=1 |
| [GlobalDistanceFieldUtils.ush:15](<D:/UE5/engine/UE_5.7/Engine/Shaders/Private/DistanceField/GlobalDistanceFieldUtils.ush:15>)、[104](<D:/UE5/engine/UE_5.7/Engine/Shaders/Private/DistanceField/GlobalDistanceFieldUtils.ush:104>) | 选择包含采样点的细层、缩小边界保护足迹，并把当前层出口交给下一层追踪 |

本实验保留相机对齐、环形复用、进入薄层、静态/动态缓存与有保护边界的层级追踪这些原则，存储和调度做了明确简化：

| 项目 | 本实验 | 所读 UE 实现 |
|---|---|---|
| 合并与存储 | CPU dense 体积，GPU 二维 float 图集 | GPU 稀疏页面、页表和页面分配 |
| 相机对齐 | floor 到 `4 × voxelSize` | round 到 `7 × voxelSize` 的世界页面 |
| 层级尺度 | 4 倍，方便展示近、中、远差异 | Lumen 范围按 2 倍 |
| 求交边界足迹 | 留出完整一个节点间距 | 所引 shader 的 `ClipmapVoxelExtent=extent/res`，为世界体素宽度的一半 |
| 本场景采样 | 四级 64³；公共类默认 96³ | 由 Lumen 设置与页面对齐规则决定 |
| 输入精度 | 128 / 48 / 24 / 24 不可变资产 | MDF 常驻与流送 mip、页面驻留和预算 |
| 其他数据 | float 窄带，简单对象编号 | 量化距离、coverage、远层 mip 和对象页面信息 |

这里没有实现 UE 的稀疏 GPU 页分配、磁盘 MDF mip 流送、完整大世界坐标、远距离 mip 传播、植被 coverage 或调度摊帧。它是用于检查 Global clipmap 计算与真实纹理求交的实验，不应称为 UE Global Distance Field 的完整移植。

## 验证入口

在仓库根目录使用已接入 Model / Assimp 的优化 Debug 配置构建：

```powershell
cmake --build build/sdf-banquet --target material_lab_demo probe_sdf_clipmap_test --parallel 4
ctest --test-dir build/sdf-banquet -R '^probe_sdf_clipmap_test$' --output-on-failure
```

GPU 两后端入口：

```text
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --sdf-clipmap-test --screenshot build\sdf_clipmap_check.png
output\sdf-banquet\Debug\bin\material_lab_demo.exe --sdf-clipmap-test --screenshot build\sdf_clipmap_check_gl.png
```

CPU 测试使用解析盒、无符号平面和独立全物体距离合并，检查正负 XYZ 滚动、物理范围、重叠节点、边界插值、teleport、静态复用、动态旧/新范围、对象增删与输入校验。GPU 测试使用实际导入场景，完整读回四级物理图集，并独立读取局部 MDF 八角样本做无分块的源场合并参考；另检查 no-op、砖内移动、进入薄层、真实上传矩形、相机移动时几何保持，以及 auto / forced L0–L3 的真实世界命中距离。图像回归为 320×240，另保存 1280×800 总览与近处图；包含读回和 validation，不能当作交互分辨率下的 FPS 测量。

2026-10-15，CPU clipmap 回归与已有的场景、质量缓存、模型导入三项 CPU 基线通过。更新后的近处相机 Vulkan validation 与 OpenGL 4.5 回归均通过，见 [Vulkan 日志](../../build/sdf_clipmap_vulkan.log) 和 [OpenGL 日志](../../build/sdf_clipmap_opengl.log)。Vulkan 设备为 RTX 4060 Laptop；本次构建沿用已接入 Assimp 的优化 Debug 配置。

完整物理图集为 1048576 个 texel。各次正常同步后，GPU 四通道与 CPU 存储的最大差值均为 0；独立无分块源场合并核对 2916 个节点，最大差值 `1.19209e−7`。更新与上传实测如下，均使用本文新近处相机：

下表的 GPU texel 数只计 Global 距离图集。本次后续性能优化另增加了一个 256 KiB 的砖块区间图集，在 Global 变化帧同步；静止时两者均不上传，见 [性能与验证说明](SDF_CLIPMAP_PERFORMANCE.md)。

| 操作 | CPU 重算节点 | GPU 实际上传 texel | Global 上传矩形 |
|---|---:|---:|---:|
| no-op / 砖内小幅移动 | 0 | 0 | 0 |
| X 移动 .13，仅 L0 跨一个砖块 | 16384 | 16384，即全图的 1.5625% | 16 |
| 继续 X 移动，跨两级砖块 | 32768 | 32768 | 32 |
| 负向 XYZ 滚动 | 154112 | 160392，包含补传间隔 | 128 |
| 一个茶壶移动 .35 | 262848 | 404112，包含合并及 tile 补传 | 不超过 128 |

动态帧实际改变 43558 个物理 texel，静态缓存重新计算量为 0。重算量还包含旧、新包围范围内最终值未变的节点；GPU 上传量则进一步包含矩形补传，三者各有不同含义。相机滚动期间 immutable 烘焙次数和世界参考几何 generation 保持不变；teleport 只全量重建不再重叠的细层。禁用期间漏代后的完整同步也通过了逐 texel 比对。

自动显示中独立检查的 1564 个命中点分布为 `L0/L1/L2/L3 = 491/982/91/0`；该视角实际覆盖三级切换，测试要求自动模式至少命中两级。四个强制层分别得到 23857、72134、76800、76799 个真实距离命中。重建世界位置后，对所选层重新采样的最大 raw-field 零面残差，Vulkan 低于 `9.73e−5`、OpenGL 低于 `9.76e−5`。这个残差检查的是显示是否来自真实纹理零面，**不是原三角形几何误差**。

L0/L1 的 23857 个共同命中像素全部改变了世界命中距离，平均差为 `.0708025`；L0/L3 的实际覆盖变化为 52942 个像素。C/H 开关保持距离通道一致，G 关闭 Global 探针回退时也不改变 Global 三维表面，说明显示与层级数据、追踪开关分工明确。

两后端的原生屏幕未命中回退都获得 190 个 Global 命中，其中 8 条用独立逐三角形首次求交核对；Vulkan 最大距离差为 `1.66893e−6`，OpenGL 为 `4.76837e−7`。这一精确度来自探针回退的局部三角形精化，不属于纯纹理三维显示的精度。该组探针中“从粗层起点进入细层”的命中计数为 0，不能据此声称该分支已得到实测覆盖。

旧版固定体积的 Vulkan validation 回归也通过：[质量切换与真实零面](../../build/sdf_clipmap_legacy_quality_vulkan.log)、[屏幕补追与动态更新](../../build/sdf_clipmap_legacy_trace_vulkan.log)，覆盖屏幕成功命中保持、黑色合法光照、Mesh/Global 分离、旧新动态范围、漏帧补传与冻结探针重追。

可查看 [1280×800 总览](../../build/sdf_clipmap_check_Overview.png)、[近处实际层级与裂缝](../../build/sdf_clipmap_check_Close.png)、[关闭层级色](../../build/sdf_clipmap_check_Auto_NoColors.png)、[强制 L0](../../build/sdf_clipmap_check_Forced_Level_0.png)、[强制 L3](../../build/sdf_clipmap_check_Forced_Level_3.png) 与 [动态更新](../../build/sdf_clipmap_check_Dynamic_Global.png)。现有 [固定 Global 茶壶验证](SDF_MODEL_BANQUET.md#验证) 继续作为上一阶段独立基线。
