# 材质渲染管线

`MaterialPipelineSettings.spotLight` 提供默认关闭的 GI 聚光灯，支持方向、RGB 轴向强度、内外光锥半角（弧度）和有限光源半径。CPU 探针烘焙、实时世界 GI 和 Lumen 二次命中评估均应用光锥和可见性；常规主 PBR 直射尚未消费该灯。`RealtimeGiConstants` 因此为 192 字节，前端 RHI 接口保持不变。

`lumenGi.lightingView` 默认 `Material`，保留原材质着色。`SurfaceCache`、`IndirectOnly`、`DirectOnly` 是纯漫反射光照诊断，分别显示总出射、反弹出射、首次照射 RGB 辐射亮度。默认仍从世界缓存重建；设置 `fullResolutionDirectLighting=true` 后，`SurfaceCache` 和 `DirectOnly` 在每个可见接收像素使用实际三角面位置、几何法线计算解析直射及光源遮挡，总出射再加 `rho * E/pi` 的世界缓存反弹项。`IndirectOnly` 读取世界缓存的间接项。此开关默认 false，密闭 GI 房间显式开启，避免把稀疏缓存的二值遮挡和手电光斑放大成格状边缘；开关本身不改变缓存密度或反弹射线数，可见直射增加逐像素光源查询。

`traceDirectLighting` 独立控制反弹射线的命中点直射，默认 false；开启后在原生光追或软件世界追踪的实际命中点计算解析直射及遮挡，再加缓存的高阶反弹。Surface Cache 直接光仍持续更新，用于总缓存合成与高阶反弹分离，避免重复计入首次照射。密闭 GI 房间同时启用两个直射开关，并将 `surfaceRays` 从上一轮的 16 提高为 32，`surfaceUpdatesPerFrame` 保持 1024；上一轮直射修复的质量和性能数据不能作为当前反弹修改的验证结果。

诊断结果已包含材质反射率和对应的自发光，PBR 不再重复乘材质颜色、AO 或 π，也不再次加入自发光。这些模式要求 Lumen 已启用、不透明接收面且关闭反射；不改变缓存的光源、几何和更新历史。模式通过 176 字节 `SurfacePassConstants.screenJitter.z` 传递，XY 保持原 TAA 网格偏移。逐像素直射使用独立编译的 resolve 着色器，仅反弹和普通材质视图使用原缓存重建着色器，避免未执行的直射查询增加寄存器负担。屏幕接收 ID 缺失时用短世界查询确认真实接收面，查询失败为黑，不把探针辐照度误当成出射亮度。诊断模式不计算未使用的世界探针。

出射重建只对反射率及有效自发光相同的共面邻接面连续混合，保留材质边界；入射场继续允许跨材质重建。`BuildLumenGiScene` 第三个可选参数允许指定 1..32 的三角面格数上限，默认 16；全局仍限制在 65536 个样本内，refit 保留该布局。密闭房间请求 32 格，常规场景不增加内存或追踪预算。

世界缓存重建和入射空间滤波采用非负、归一化的几何权重。完整内部样条保持仿射精度；靠近采样凸包边界时约束 MLS 系数，退化核使用凸包内插值，不再进行有负权重的边外亮度外推。这样非负光源相加不会把接收面算暗，重建也不会超出参与样本的亮度范围。边界允许有限的采样中心偏移，避免用线性外推换取黑色振铃；共面接缝和真实墙角隔离仍保留。此修复不增加射线、缓存分辨率或 RHI 接口。

`lumen_reflection_gpu_test` 验证高对比非负光源的叠加、范围、边界回退、完整内部仿射精度，以及边界有效采样中心与几何支持范围；同时保留接缝、噪声降低、墙角隔离和三角面斜边的局部采样回归。MaterialLab 的 `--gi-room-test` 用独立 CPU 可见性和 Lambert/光锥参考检查薄挡板真实受光边缘及手电的光锥、阴影；反弹测试另用源三角面的等面积积分参考，检查亮度/RGB 误差、空间 RMSE、真实梯度的对比度增益和误差曲率，避免仅靠模糊或压平通过。缓存直射/16 射线对照已包含当前局部查表与稳定哈希，不能作为完整旧版本。数值检查关闭 AA，另保存总光和仅反弹 TAA 画面。2026-10-06 的当前 Vulkan 2K validation、完整 OpenGL 房间以及通用 Lumen Vulkan validation 回归均通过；通用回归覆盖移动光源/自发光、多次反弹、TAA、反射和缓存消费，保留 DDGI，后端错误为 0。具体质量与性能见 [密闭 GI 房间验证](../MaterialLab/README.md#密闭-gi-房间)。

`MaterialRenderPipeline` 位于 Render 模块，CMake target 为 `render_effects`。它消费 `RenderFrame` 中的材质快照，组织多 Pass 渲染。MaterialLab 仅提供场景、参数交互及 GPU 验证，不再拥有单独的光照管线。

## 已实现流程

| 顺序 | Pass | 输入与输出 |
| --- | --- | --- |
| 1 | 四级 CSM | 场景深度变体 → Depth32F 2×2 阴影 atlas |
| 2 | 平面反射 | 反射视图不透明层 → 独立颜色/深度快照 → 排序透明层；MSAA resolve 后得到 HDR 反射图 |
| 3 | 主视图深度 | 材质深度变体 → 主视图 Depth32F；MSAA 开启时 resolve 深度 |
| 4 | SSAO + 双边模糊 | 主视图深度 → 半分辨率 AO |
| 5 | 单次漫反射间接光，可选 | 独立深度、直射漫反射辐射/自发光、几何法线捕获 → 半分辨率追踪 → 三次几何感知去噪 → 上采样 |
| 6 | 天空背景 + HDR 不透明层 / resolve | 可选天空背景；材质、点光、方向光、区域光、天空 IBL、CSM、镜面、AO、间接光 → RGBA16F 场景与单采样深度 |
| 7 | 透明层，可选 | 独立复制不透明颜色/深度 → 从远到近混合透明材质及屏幕空间折射 → 颜色 resolve |
| 8 | TAA，可选 | 当前 HDR/深度、不透明快照、上一执行帧颜色/深度及 reactive alpha → 新历史 HDR；另存 Depth32F 历史 |
| 9 | 相机运动模糊，可选 | HDR、当前深度、前后未抖动相机矩阵 → HDR 模糊结果，避让透明反应区域 |
| 10 | Bloom | 原始 texel 软阈值与 13 tap 预过滤 → 1..6 层 13 tap 降采样 → 归一化 Tent 上采样与 scatter 混合 |
| 11 | 显示合成 | HDR + Bloom → 曝光 → Reinhard / ACES / 无 tone map → Gamma → 调色/暗角 |
| 12 | 屏幕滤镜 | None / 灰度 / 反相 / 锐化 / 浮雕 / 边缘 / Gaussian / 波纹 → 独立显示目标 |
| 13 | FXAA / 复制 / 输出重建 | 显示颜色 → 默认 framebuffer；可独立设置输出尺寸，TAA 模式不会再叠加 FXAA |
| 14 | Overlay | 绕过后处理和抗锯齿的覆盖层，使用未抖动相机 |

开启 SSAO 才录制它所需的主视图深度预通道；间接光另有独立的单采样深度捕获。TAA 和相机模糊读取颜色 Pass 之后的场景深度，透明层不改写该不透明深度。主场景及镜面支持独立于后处理 AA 模式的 MSAA，后续采样统一使用 resolve 后的单采样纹理。关闭效果时跳过对应绘制和处理；阴影 atlas、AO 和间接光仍清为合法默认值。Bloom 的每一维至少为 1，在两维均为 1 时停止添加层。快照、历史与显示中间纹理采用独立读写目标，避免纹理反馈回路。

阴影只作用于对应方向光；AO 只作用于环境/间接项。材质 AO 调制环境项与新漫反射间接光，屏幕 SSAO 调制不透明环境项；透明层不套用其后方不透明表面的 SSAO。点光、区域光、直射漫反射/镜面、自发光和透射背景不会被 AO 整体乘暗。反射视图关闭屏幕 AO 与主视图屏幕空间 GI，但可以查询同一世界空间探针；使用反射视角的光照和主相机 CSM，镜面不递归采样自身。

## 调用与所有权

设备必须已运行。调用方负责窗口、相机、Begin/End/Submit，所有管线 API 在应用线程使用。以下示例假设 `device`、`window`、场景和外部资源 owner 已初始化，采用一个在途帧的可靠提交：

```cpp
Render::MaterialRenderPipeline pipeline(device);
const auto require = [](bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
};
require(pipeline.Initialize(), pipeline.LastError());
Render::MaterialPipelineSettings settings;
settings.shadows.atlasResolution = 4096; // 默认 2×2 atlas，每级 2048×2048
settings.temporal.msaaSamples = 4;
settings.temporal.antialiasing = Render::AntialiasingMode::TAA;
settings.reflection.resolutionScale = 1.0f;
settings.post.bloomEnabled = true;
settings.post.toneMap = Render::ToneMapMode::ACES;
std::uint64_t frameIndex = 0;

// 在应用线程逐帧调用；下一帧必须等本次 CompleteFrame 之后才能 Record。
const auto renderOneFrame = [&] {
    settings.deltaSeconds = elapsedSeconds; // 实际帧间隔，必须有限且 > 0
    require(pipeline.Resize(width, height, settings.shadows.atlasResolution,
                           settings.temporal.msaaSamples, settings.reflection.resolutionScale),
            pipeline.LastError());
    Render::RHICommand::BeginFrame begin;
    begin.frameIndex = ++frameIndex;
    begin.framebufferWidth = width;
    begin.framebufferHeight = height;
    auto encoder = device.BeginFrame(begin);
    if (!encoder.KeepAlive(sceneGpuOwner) || !pipeline.Record(encoder, sceneFrame, camera, settings)) {
        const auto error = pipeline.LastError();
        encoder.Cancel(); // Record 失败也可能留下部分命令
        if (const auto token = pipeline.RecordedFrameToken()) pipeline.DiscardFrame(token);
        throw std::runtime_error("Record failed: " + error);
    }
    const auto token = pipeline.RecordedFrameToken(); // 固定本帧 token
    if (!encoder.End(true)) {
        encoder.Cancel();
        pipeline.DiscardFrame(token);
        throw std::runtime_error("End frame failed");
    }
    auto completed = std::make_shared<bool>(false); // 回调不能引用临时栈变量
    if (!device.async_SubmitFrameCommands(encoder.GetCommandBuffer(),
                                         [completed] { *completed = true; })) {
        encoder.Cancel();
        pipeline.DiscardFrame(token);
        throw std::runtime_error("Submit frame failed");
    }
    while (!*completed) {
        device.returnSystem.DrainCallbacks(); // 在应用线程分发执行完成回调
        if (!*completed) {
            window.PollEvents();
            device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));
        }
    }
    require(pipeline.CompleteFrame(token), pipeline.LastError());
};
renderOneFrame();
```

`PipelineCamera` 使用 OpenGL [-1,1] 裁剪深度和右手 view/projection；nearPlane/farPlane 必须与 projection 相符。向 Record 传入未抖动 projection，TAA 的抖动由管线添加。改变视口、阴影分辨率、MSAA 样本数或镜面比例后，先调用带匹配五个参数的 `Resize`。相同配置的 Resize 是空操作；真正重建必须等当前 token 完成或丢弃。完整构建新附件后才替换旧资源，帧通过 KeepAlive 保活目标；重建失败保留旧目标。

`Record` 成功只产生待确认状态，尚不能作为下一帧历史。只在可靠提交的执行完成回调到达后调用 `CompleteFrame(token)`；未提交的帧在 Cancel 后调用 `DiscardFrame(token)`。零值、过期或重复 token 会被拒绝。有待确认 token 时不能再 Record。已接受提交的帧不能因等待超时就当作未执行帧丢弃；应保留设备/资源，等待执行结果或走受控设备关闭。此管线当前要求串行确认，不接入可能静默覆盖帧的 latest 提交路径。

管线持有自己的 shader、全屏 mesh、UBO、FBO 和附件。材质快照持有材质 shader/pipeline/纹理；调用方必须单独保活场景 mesh 及 legacy RenderItem.pipeline/texture。这一点也适用于在途帧。Shutdown 管线和释放材质、外部 owners 后，再 StopAndRelease 设备及排空回调，最后销毁窗口。

初始化与 Resize 使用有超时诊断的异步资源等待；失败清理仍会接收未完成创建的回调，保证句柄被回收。禁止与 device.StopAndRelease 并发。它们不是资源流式加载接口，窗口大小连续变化时应由应用合并 resize 请求。

## 画质与历史参数

通用 `TemporalEffectsSettings` 默认 `FXAA + msaaSamples=1`；MaterialLab 交互模式显式选择 `TAA + MSAA4`。`antialiasing` 为 `None / FXAA / TAA` 单选，MSAA 可另外设为 `1 / 2 / 4 / 8`；设备不支持请求的附件样本数时创建失败，不静默改变设置。镜面 `resolutionScale` 默认 1，即全分辨率；允许 0.25..2，各维为 `ceil(viewport * scale)` 且不超过 8192。主场景和镜面都使用所选 MSAA 样本数。

TAA 使用 8 帧 Halton 抖动、HDR 历史重投影、深度拒绝、YCoCg 邻域约束和随亮度变化/运动减小的历史权重，默认 `historyWeight=.9`。它针对相机运动下的静态几何，没有 object velocity buffer。几何/model、材质参数/纹理句柄，以及影响 HDR 的阴影、镜面、AO、环境光设置变化会通过内容 hash 使 TAA 历史失效；新 Capture 的快照对象地址不参与判断。原地上传 mesh/texture 内容不会改变句柄，调用方必须在帧间 `ResetHistory()`。动画点光位置/颜色不加入该 hash，而由 shader 的亮度反应降低历史权重，避免动画每帧强制清历史。

出现透明层时，TAA 比较最终 HDR 与本视图不透明快照，估计透明反应值，并把当前值存入历史颜色的 alpha。重投影同时检查当前与历史反应值，降低移动、流动或已离开该像素的透明内容所占历史权重；这里的 alpha 不再表示场景覆盖率。相机模糊也避让这些区域，避免用后方不透明深度拖曳透明表面。该机制依赖颜色差，不能替代透明物体速度、分层深度或完整运动补偿。区域光与间接光设置也参与内容 hash；`settings.time` 驱动的流动不会仅因时间推进就每帧重置历史。

相机跳切可设置一帧 `cameraCut=true` 或在帧间调用 `ResetHistory()`；resize、AA 模式改变、projection 改变以及大幅相机移动也会使相应历史失效。`deltaSeconds > .25` 按跳切处理。`ResetHistory`/camera cut 同时使相机模糊首帧不使用旧相机；仅材质 hash 改变只拒绝 TAA 颜色历史。历史索引与 Halton 序列只随成功确认的帧推进。TAA 思路参考 [Brian Karis：High-Quality Temporal Supersampling](https://advances.realtimerendering.com/s2014/)，本实现不等同于完整 UE TAA。

FXAA 是显示映射之后、Overlay 之前的简化方向滤波；`fxaaEdgeThreshold=.125`、`fxaaMinThreshold=.0312` 控制边缘触发。它不会使用历史，也不会与 TAA 同时执行。集成位置参考 [NVIDIA FXAA white paper](https://developer.download.nvidia.com/assets/gamedev/files/sdk/11/FXAA_WhitePaper.pdf)。

PBR 另在光照计算中执行高光抗锯齿：根据着色法线的屏幕导数估计变化，向 GGX 的 `alpha²`（感知粗糙度的四次方）加入有界滤波宽度，再用于光照。`settings.lighting.specularAA={.15,.20,1,0}` 依次为法线方差比例、最大新增 `alpha²`、启用标志、保留值；前两项允许 0..1，启用标志为 0 或 1。它不改材质存储的粗糙度，也不通过硬截高光亮度来隐藏亮点；设置改变会使 TAA 内容历史失效。实现采用保守的各向同性 NDF 滤波，参考 [Tokuyoshi / Kaplanyan 2019](https://yusuketokuyoshi.com/papers/2019/ImprovedGeometricSpecularAA.pdf)。它处理已采样法线的变化，不提供法线贴图 mip 方差存储，也不解决轮廓覆盖或阴影终止线几何误差。

相机模糊默认关闭。开启 `motionBlurEnabled` 后，采样跨度由 `motionBlurStrength * motionBlurShutterSeconds / deltaSeconds` 缩放前后相机位移；默认 strength=1、shutter=1/120 秒、总跨度上限 32 像素、12 个样本（允许 2..32）。速度使用未抖动矩阵，TAA jitter 不会产生伪运动；深度权重抑制跨轮廓混色，静止相机与背景保持原样。它不生成移动物体自身的模糊。位置重建方法参考 [GPU Gems 3，第 27 章](https://developer.nvidia.com/gpugems/gpugems3/part-iv-image-effects/chapter-27-motion-blur-post-processing-effect)。

## CSM 质量与调参

默认阴影 atlas 为 **4096×4096**，四级各占 **2048×2048**。`splitLambda=.65` 混合对数和均匀切分，`distance=45` 为相机空间阴影最远距离，`cascadeBlend=.1` 在分界前与下一级交叠混合。切分分配应匹配场景的可见深度；把 lambda 调得很高会把大量分辨率留在近处，并可能让远处展示物集中到最后一级。

拟合使用 double 精度，在相机空间计算并量化包围球半径，在固定光空间基底上对齐世界中心到纹素网格，最后转为 GPU float 矩阵。每边留 3 个纹素的过滤保护区，另留半纹素容纳中心对齐。Light Z 紧贴接收者范围，保留小的深度保护余量，`casterPadding=30` 只向光源上游扩展以纳入视锥外投影物，不向两端对称扩大深度范围。

阴影采样使用固定 **16 tap、相位连续的 tent PCF**：逐个读取实际纹素中心的深度、先比较，再按接收点的亚纹素相位加权；不会先模糊深度。每个 tap 使用接收面深度修正（receiver-plane depth bias，RPDB），避免拿中心点的深度比较整片倾斜表面。世界坐标的 `dFdx/dFdy` 在所有 discard、级联和光照分支之前计算，再传给独立的 `PbrDirectionalShadowGLSL()`。接近奇异的投影使用有界回退；所有 tap 被限制在自己的 tile 内。比较后过滤及逐纹素深度修正分别参考 [GPU Gems，第 11 章](https://developer.nvidia.com/gpugems/gpugems/part-ii-lighting-and-shadows/chapter-11-shadow-map-antialiasing) 和 [Microsoft CSM 文档](https://learn.microsoft.com/en-us/windows/win32/dxtecharts/cascaded-shadow-maps#calculating-a-per-texel-depth-bias-with-ddx-and-ddy-for-large-pcfs)。

| `ShadowSettings` 参数 | 默认值 | 单位与作用 |
| --- | --- | --- |
| `depthBiasTexels` | .05 | 以当前级联的世界纹素尺寸计量，再换算为归一化深度偏差 |
| `normalBiasTexels` | .20 | 以当前级联的世界纹素尺寸计量，沿几何法线偏移，掠射角时作用更强 |
| `depthBias` | .000002 | 额外的归一化阴影深度偏差，保留旧参数单位 |
| `normalBias` | 0 | 额外的世界单位法线偏移，保留旧参数单位 |
| `receiverPlaneClampTexels` | 4 | RPDB 安全上限的纹素系数；按真实三角面坡度放大，再换算到归一化深度；0 关闭 RPDB |
| `distanceFade` | .10 | 在最后 10% 阴影距离内淡出到完全可见，超距不采阴影 |
| `casterPadding` | 30 | 向光源方向扩展的世界距离 |

设第 i 级世界纹素宽度为 `t`，光深度跨度的倒数为 `r`，基础比较偏差为 `depthBias + depthBiasTexels*t*r`；法线偏移为 `normalBias + normalBiasTexels*t*sin(theta)`，theta 为几何法线与朝向光源方向的夹角。RPDB 不使用法线贴图，而从接收三角面的世界位置导数拟合。偏移与太阳终止处理使用网格插值几何法线；几何 `N·L` 在 0..0.05 范围渐隐，法线贴图不会让几何背光面接受太阳直射。

RPDB 每 tap 的绝对修正上限为 `receiverPlaneClampTexels*t*r*max(1,tan(phi))`，其中 phi 使用 `cross(worldDx,worldDy)` 得到的真实三角面法线与光方向计算，取 `abs(Nface·L)`。默认系数 4 覆盖 tent 核的二维采样距离，避免固定深度上限截断陡斜面的有效负修正、重新产生自阴影；这个坡度因子只放宽安全上限，不增加常量偏差。投影导数退化、非有限，或 `abs(Nface·L)<=.001` 时放弃平面修正，使用额外 `.25*t*r` 的有界比较偏差。曲面仍按当前三角面求导，不能保证消除几何轮廓及极端掠射处的所有离散误差。

调参先用可见度诊断排除贴图、反射和 tone mapping 的影响，再检查分级覆盖与分辨率。平面自阴影优先检查 RPDB、几何/投影和 caster 范围；剩余数值误差再小幅调整 texel 单位偏差。接触处漂浮时先减小偏差，特别是额外 `normalBias` 和 `depthBias`，不要靠增大偏差掩盖所有条纹。分辨率加倍时每级纹素更小，texel 单位偏差随之缩小，而 atlas 存储约增为四倍。

`ShadowDebugView::Visibility` 输出线性可见度（白为可见、黑为遮挡），`Cascades` 输出级联颜色，`None` 返回正常光照。模式通过原 `screenAndAo.w` 传递；诊断输出关闭 Bloom、色调映射、Gamma/调色和屏幕滤镜，避免改变掩码含义。检查原始边缘时可同时关闭 AA；MaterialLab 提供接触、斜面、薄遮挡物和曲面场景，以及 G/J/K 操作和 `--shadow-test` 导出。

## 材质契约

正式 PBR 模板位于 `Public/Material/pbr_material.h`。`MakePbrTemplate()` 返回 `Domain::Surface` 的 `StandardPbr`，`MakePbrTemplate(Domain::Translucent)` 返回 `TranslucentPbr`；两者共享 **208 字节**参数布局，原字段偏移保持不变，`twoSided` 位于 192。模板对象身份仍须与 `MaterialPassResources.expectedTemplate` 一致，不能因为字节数相同就互换模板。

| 参数 | 约定 |
| --- | --- |
| `baseColor` / `metallic` / `roughness` / `ao` | 线性颜色因子与金属工作流；贴图开启时对应标量由贴图 R 替代 |
| `uvTransform` | xy 缩放、zw 偏移 |
| `uvFlow` | xy 为第一层 UV/秒速度；zw 非零时启用第二滚动层，两层采样等权平均 |
| `useFlowMap` / `flowStrength` | flowMap 的 RG 从 [0,1] 解码到 [-1,1]，乘 strength 作为局部速度；采用五秒双相位交叉淡化和独立纹理梯度，避免累计 UV 拉伸；时间来自 `settings.time` |
| `emissiveColor` / `emissiveIntensity` / `useEmissiveMap` | 线性 HDR 自发光 × 强度 × 可选线性 RGB 发光图；输出限制在 60000，避免半浮点溢出 |
| `reflectionStrength` | 表面平面反射采样权重，与管线 reflection.strength 相乘 |
| `alphaCutoff` | 颜色、捕获和深度变体共享流动 UV；Surface 域按 albedo.a × baseColor.a × opacity 裁剪；正式透明快照路径的覆盖率只取 albedo.a × baseColor.a |
| `opacity` | Translucent 域为 0..1 的**光学效果强度**：0 无效果，1 完整材质；缩放折射位移、吸收/透射及表面辐射。Surface 域仍保持覆盖 alpha/裁剪语义 |
| `transmission` | 0..1 的介质透射比例，独立于覆盖率；金属部分不透射 |
| `ior` / `thickness` | 折射率 1..2.5 / 世界单位光学厚度 0..100；透明介质 F0 由 IOR 计算，不透明 PBR 保留 .05 |
| `distortionStrength` | 0..256 **像素**，同时限定投影折射偏移和自定义 RG 扰动强度；0 禁止屏幕位移 |
| `absorptionColor` | RGB 表示经过一个世界单位后的透过率，范围 0..1；按斜视光程做幂次衰减，1 为不吸收，并非直接染色或吸收系数 |
| `twoSided` | 默认 false，透明闭合表面只合成朝向相机的一面；true 表示双面薄片，背面翻转法线后按入射面处理，不将同一体积的前后面重复混合 |

材质纹理槽固定为 `0=albedoMap`、`1=normalMap`、`2=metallicMap`、`3=roughnessMap`、`4=aoMap`、`5=distortionMap`、`6=flowMap`、`7=emissiveMap`。前五个必需，未使用时绑定白色/平坦法线；后三个可选，缺失会显式解绑。开启对应功能时应绑定真实贴图；扰动/流向的中性 RG 为 (.5,.5)，发光图按线性数据读取。除 albedo 可选择 sRGB 解码外，其他贴图均为线性数据。流动沿用纹理地址模式，循环水面应使用 Repeat。

`MaterialPassResources.shadowPipeline` 是可选深度变体，必须使用相同材质模板/参数布局和纹理。PBR 提供 `PbrShadowVertexShader()`、`PbrShadowFragmentShader()`、`PbrShadowPipelineSpec(program)`。缺少深度变体的对象仍可进入颜色绘制，但不写 CSM、SSAO 或间接光的深度预通道；透明混合层也不进入这些深度捕获。

另有两个可选不透明捕获变体：`diffuseRadiancePipeline` 使用 `PbrFragmentShader(PbrOutput::DiffuseRadiance)` 输出直射漫反射、天空照亮的漫反射辐射加自发光；`normalPipeline` 使用 `PbrFragmentShader(PbrOutput::WorldNormal)` 输出世界几何法线。两者使用正式 PBR 顶点 shader、相同模板/纹理/参数块，并设 `depthWrite=false`，读取已捕获的深度。捕获不包含镜面高光、旧常量环境色或上一轮 GI，因此不会把高光当作漫反射光源，也不会递归累加间接光。未知材质即使没有这两个变体，只要提供 shadowPipeline 仍能写深度、遮挡追踪射线；缺少捕获变体时不会贡献自身辐射。所有变体及其 shader/program 都须进入同一个真实资源 lifetime。

`RenderItem.castsShadows=false` 可关闭投影，`visibleInReflections=false` 可排除镜面自身。接收阴影由颜色 shader 决定。PBR 颜色输出现在是线性 HDR，必须经过正式管线输出转换；Lab 的旧 PBR 头文件保留 using 转发，但其旧 `LabPipeline` 已移除。Unlit 和原有 `ForwardRenderPipeline` 路径仍保留，原默认 framebuffer 使用方式不变。

进入 HDR 场景层的所有颜色 shader 都应输出线性颜色。现有 Unlit/Sprite 不自动解码 sRGB，需提供线性输入或使用相应 shader 变体；屏幕 UI 使用 Overlay 绕过后处理。反射共享主视角 CSM，镜中可见但超出主视角级联覆盖的对象按越界策略不受阴影。

材质域在该管线保留纹理槽 0..7；8..15 为 Pass 预留。使用了保留槽的材质会被明确拒绝。整个材质基础库仍支持 0..15，不影响不使用此管线的旧场景。

| 数据 | 绑定 / 大小 |
| --- | --- |
| View / Object | UBO 0 / 1，80 / 64 字节 |
| Material | UBO 2，Surface / Translucent PBR 均为 208 字节，twoSided 位于 192 |
| 点光/环境光/高光抗锯齿 | UBO 3，176 字节；原 0..159 偏移不变，`specularAA` 位于 160；原曝光/outputOptions 仅保留 ABI |
| CSM/镜面/方向光/裁剪/屏幕 AO | UBO 4，544 字节，分 256+256+32 三条 inline update 上传 |
| 后处理 | UBO 5，240 字节；原 0..223 偏移不变，`bloomStability` 位于 224，依次为 soft knee / firefly range / scatter / radius |
| TAA / FXAA / 相机模糊 | UBO 6，400 字节，分 256+144 字节上传；`reactive` 位于 384 |
| 表面视图/时间/快照状态 | UBO 7，176 字节；view / projection / screenTime / options / screenJitter |
| 矩形区域光 | UBO 8，256 字节；最多四灯，每灯四个 vec4 |
| 屏幕空间间接光追踪 | UBO 9，224 字节；projection / inverseProjection / view / trace / options |
| 类 Lumen 混合 GI | UBO 10，288 字节；重投影矩阵、表面缓存/追踪预算、距离体积与反射信息 |
| 天空光 | UBO 11，176 字节；9 个 SH vec4 / options / transform，既有 UBO 不变 |
| 世界空间漫反射探针 | UBO 12，14400 字节；最多 64 个探针，每个 224 字节 |
| 探针运行时设置 / 局部可见性 | UBO 13，16 字节；UBO 14，16384 字节，独立保存每个探针的 16 个首次命中平面 |
| 实时 GI 设置 | UBO 15，144 字节；动态模式复用 texture 15 保存 GPU SH/距离矩缓存，原材质参数布局不变 |
| 不透明颜色 / 深度快照 | texture 8 / 9，主视图与反射视图各自绑定 |
| 天空预过滤反射 / BRDF LUT | texture 10 / 11，普通二维 RGBA16F，无 cubemap RHI 扩展 |
| CSM / Reflection / SSAO / 漫反射辐照度 | texture 12 / 13 / 14 / 15 |

全屏 Pass 的纹理槽是局部约定：普通后处理使用 0=输入颜色、1=Bloom/第二图、2=场景深度；Temporal Pass 使用 0=当前颜色、1=历史颜色、2=当前深度、3=上一帧深度、4=不透明快照。间接光收集使用 0=源漫反射辐射、1=世界法线、2=深度，上采样时槽 0 改为半分辨率辐照度。它们在各 Pass 重绑定，不改变材质自身的 0..7 槽契约。

UBO 4 的原 0..495 字节偏移保持不变；末尾新增 `cascadeWorldTexelSize`（496）、`cascadeInverseDepthRange`（512）和 `shadowFilter`（528），各为 vec4。前两个向量分别存四级的世界纹素宽度和光深度跨度倒数；`shadowFilter` 依次存 texel 深度偏差、texel 法线偏差、远距淡出比例、RPDB 安全上限的纹素系数（使用时再乘真实三角面坡度因子）。原 `screenAndAo.w` 的保留分量用于阴影诊断模式，不移动既有字段。

## 创建透明与流动材质

下面假设异步创建已完成，`program` 由 `PbrVertexShader(Domain::Translucent)` 和 `PbrFragmentShader(Domain::Translucent)` 编译。以这个 spec 创建颜色 pipeline，不能沿用写深度的不透明状态：

```cpp
using namespace Render::Material;
auto schema = MakePbrTemplate(Domain::Translucent);
Render::PipelineSpec spec = PbrPipelineSpec(program, Domain::Translucent);
// spec.depthTest=true、depthWrite=false、blendEnable=true、BlendMode::Alpha。
// 使用 spec 异步创建后取得 translucentPipeline；parameterBuffer 分配 208 字节。
auto asset = MaterialAsset::Create(schema, {
    {"baseColor", glm::vec4(.3f, .8f, .88f, 1)},
    {"roughness", .18f}, {"opacity", .85f}, {"transmission", .88f},
    {"ior", 1.333f}, {"thickness", .2f}, {"distortionStrength", 18.0f},
    {"absorptionColor", glm::vec4(.7f, .92f, .98f, 1)},
    {"uvFlow", glm::vec4(.08f, .035f, -.04f, .06f)},
    {"useNormalMap", true}, {"useFlowMap", true}, {"flowStrength", .15f},
    {"useDistortionMap", true}, {"twoSided", true} // 水面为双面薄片
});
MaterialPassResources pass;
pass.expectedTemplate = schema;
pass.pipeline = translucentPipeline;
pass.parameterBuffer = parameterBuffer;
pass.parameterBufferBytes = 208;
pass.textures = {{0, white}, {1, waterNormal}, {2, white}, {3, white},
                 {4, white}, {5, distortionTexture}, {6, flowTexture}};
pass.lifetime = materialGpuOwner; // 真正拥有上述 pipeline/program/UBO/纹理
MaterialInstance instance(asset);
std::string error;
auto snapshot = MaterialSnapshot::Create(instance, pass, {}, &error);
if (!snapshot) throw std::runtime_error(error);
Render::RenderItem item;
item.mesh = waterMesh;
item.draw.indexCount = waterIndexCount;
item.model = waterModel;
item.material = snapshot; // EffectiveLayer 自动选择 Transparent
// 放入 RenderFrame；由上面的可靠 Record/Submit/CompleteFrame 流程绘制。
settings.time = elapsedSinceStartSeconds;
```

透明层按相机距离从远到近混合，depthTest 保留而 depthWrite 关闭。玻璃等闭合表面默认单面合成；水面等薄片可显式 twoSided=true，shader 翻转背面法线，并兼容镜像变换绕序。正式快照路径把光学强度与覆盖率分开：opacity 在同一个背景采样坐标上调整光学系数，只有 albedo.a × baseColor.a 表示的部分覆盖边缘才与原目标混合，避免物体内部同时保留原像和折射像。没有快照的兼容路径退回普通 alpha 混合。

折射只采当前视图的独立不透明颜色/深度快照；屏幕边缘平滑缩小偏移，采样背景时按深度剔除前景，并用有效足迹置信度减小位移；无法恢复隐藏背景时退回未扰动可见颜色。反射视图先绘制自己的不透明层和快照，再绘制透明层，不能借用主视图坐标或附件；深度复制写入独立 depth-only FBO，不会改写颜色快照。透明层之间可排序混合，但不会相互递归折射；没有几何背面厚度求解、色散、粗糙折射预过滤或透明自阴影，`thickness` 是材质指定近似光程。

## 区域光与单次漫反射间接光

矩形区域光是额外的直接光源，最多四盏，无需 LTC 表。`halfAxisU/V` 是互相垂直的世界半轴，面积为 `4*length(cross(U,V))`，叉积指向发光面；默认 X/Z 半轴朝 -Y，适合顶灯。`radiance` 是线性发出辐射，尺寸增大会增加总能量；`twoSided=false` 时背面不发光。

```cpp
settings.areaLights.count = 1;
settings.areaLights.samplesPerAxis = 4; // 4×4 或 8×8
settings.areaLights.specularFilter = 1; // 0..2，0 是未过滤诊断对照
auto& area = settings.areaLights.lights[0];
area.center = {0, 3.8f, 0};
area.halfAxisU = {1.3f, 0, 0};
area.halfAxisV = {0, 0, .9f};
area.radiance = {4, 3.8f, 3.5f};
area.twoSided = false;
settings.indirect.enabled = true;       // 默认 false
settings.indirect.intensity = 1;        // 0..16
settings.indirect.radius = 4;           // 世界单位，默认 3
settings.indirect.thickness = .15f;     // 屏幕追踪命中容差
settings.indirect.bias = .03f;          // 接收表面起点偏移
settings.indirect.sampleCount = 12;     // 默认平衡档 12；质量参考 24；允许 1..32
settings.indirect.stepCount = 24;       // 4..64
```

区域光以固定网格积分真实面积、发光面余弦、接收面余弦和距离平方衰减。窄 GGX 高光按每个积分单元的角度范围扩大滤波足迹，减轻离散采样点斑；它是有限采样近似，没有独立区域光阴影、遮挡可见性求解或精确多边形镜面积分。8×8 更接近积分，但成本是每灯 64 个光照样本；近距离、大灯和极窄镜面仍存在质量/能量误差。独立 GPU 实测中，漫反射与解析矩形积分最大相对误差为 .000685，受控低粗糙度镜面的 33 相位变化降到约 2.4%–2.9%，不能据此保证所有场景无闪烁。

间接光在主视图捕获不透明/裁剪表面的直接/天空漫反射与自发光、几何法线和深度，以余弦半球方向做屏幕空间射线步进。默认平衡档为 12 个方向、每方向 24 步，可将 sampleCount 设为 24 取得质量参考。每像素固定整数哈希分别打散方位角和径向分层；射线命中后按有限角度足迹过滤源辐射。追踪/去噪工作图为半分辨率，最长边最多 640 像素，并按整数比例保持宽高比，避免高分辨率窗口成倍增加追踪工作。三次几何感知 à-trous 去噪步长为 1/2/4，最后按全分辨率深度与法线上采样；只过滤间接辐照度，不模糊材质颜色或最终场景。启用时 `indirectPasses=6+IndirectLightingDenoisePasses`，当前为 9。

额外的 RGBA32F 视空间位置图只重建一次几何位置，供射线、源过滤、去噪和上采样共享（全分辨率，每像素 16 字节）。射线在内层以线性 clip 坐标步进，并在空像素或前方样本上提前退出，减少重复投影、逆投影与法线取样。UBO9 保持 224 字节，内部缓存使用追踪 shader 的纹理槽 3；每次切换目标后才绑定缓存，避免附件反馈。方向光强度为零且未启用阴影诊断时跳过 CSM 绘制；零辐射点光也跳过 BRDF 计算。画质与性能可用 [MaterialLab 批次计时](../MaterialLab/README.md#性能测量) 同时验证。

它能表达一跳漫反射颜色渗透和发光表面对邻面的照射；金属不贡献直接漫反射，接收端也按非金属比例、albedo/π 和材质 AO 处理。捕获不包含已有 GI，所以只有一次反弹。它没有屏幕外、被前景遮住的表面、世界空间缓存或镜面间接光；未命中贡献零，视角变化可能改变结果。透明层既不作为捕获源，也不接收这张不透明主视图 GI；反射视图不复用它。遗产的环境 IBL 不等同于这类场景反弹。

## 天空光与环境反射

`SkyLightSettings` 独立于 `PbrPassConstants`，不移动原材质/光照 ABI。Render 管线默认关闭，调用方可开启程序日光或指定已烘焙的线性 HDR：

```cpp
settings.sky.enabled = true;
settings.sky.intensity = 0.8f;
settings.sky.background = true; // 仅背景可独立隐藏，照明保持
settings.sky.rotation = glm::radians(30.0f);
settings.sky.diffuseStrength = 1.0f;
settings.sky.specularStrength = 1.0f;
settings.sky.occlusionStrength = 1.0f;
// 自定义程序天空，或以线性 RGB 像素调用 BakeSkyEnvironment(width,height,pixels)。
settings.sky.environment = Render::MakeProceduralSkyEnvironment(
    {0.18f,0.38f,0.80f}, {0.75f,0.82f,0.90f}, {0.12f,0.10f,0.08f});
// 旧常量环境色仍为兼容接口保留；通常应清零，避免与天空重复补光。
settings.lighting.ambientAndExposure = {0,0,0,1};
```

预计算采用二阶 SH（9 项），用球面行的精确立体角权重积分并进行余弦卷积；系数已除以 pi。镜面采用 GGX 重要性采样，缓存六档 128×64 等距柱状图，每档使用 128 个固定采样，不在帧内追踪。各层有经度环绕与极点延拓的一像素 gutter；两档线性混合，避免粗糙度切换和层间颜色串扰。64×64 的 split-sum DFG LUT 使用每像素 256 样本、相关 Smith GGX 可见性；对粗糙金属加入近似多次散射能量补偿。运行时仅一次 SH 计算和约三次纹理取样。HDR 通道必须有限、非负且 <=60000，写入天空背景和材质结果时也钳制到 RGBA16F 可用范围。

天空照明使用材质法线、金属度、粗糙度及材质 AO；不透明表面叠加现有 SSAO，镜面遮挡使用视角/粗糙度相关的近似。AO 不作用于太阳、区域光或自发光。透明材质按透射比例衰减漫反射，镜面天空由 IOR 决定 Fresnel；屏幕折射背景快照包含天空。反射视图使用自身相机绘制天空，复用同一环境；天空背景在几何绘制之前写颜色，不写深度。天光漫反射可以进入单次反弹源捕获，镜面天空、旧常量 ambient 和上一轮屏幕 GI 不进入捕获，因此没有递归反馈。

烘焙是加载阶段 CPU 工作，首次开启默认天空或更换环境时同步创建纹理；调强度、旋转、AO、背景开关不会重烘焙。帧录制 pin 环境资源 owner，更换资源或 Shutdown 后已录制命令仍保有旧资源；天空开关/强度/旋转/资源/遮挡参数进入 TAA 内容 hash。天空反射图约 0.4 MiB，加 DFG LUT 约 32 KiB，所有资源仍使用原二维纹理创建接口。`PipelineStatistics.skyPasses` 记录主视图及镜面背景的绘制数，不把材质内 IBL 取样计为额外 pass。

全局 IBL 可配合下文的局部天空可见性、漫反射探针或类 Lumen 混合 GI；后者已通过距离场/硬件追踪计算局部遮挡。独立天空 IBL 没有单独的距离场 AO pass，尚无动态局部反射捕获或物理大气散射，高频 HDR 太阳过滤精度也有限。天空算法参考 [Filament IBL/SH/能量补偿说明](https://google.github.io/filament/main/filament.html)；UE Sky Light 的局部阴影能力与区别参见 [Epic Sky Lights 文档](https://dev.epicgames.com/documentation/unreal-engine/sky-lights-in-unreal-engine)。

验证：`sky_light_test` 检查解析常量/方向性积分、GGX 粗糙度、接缝、DFG 与非法参数；`pbr_specular_aa_gpu_test` 验证正式材质 shader 下白色金属在五档粗糙度的能量保持、AO 和关闭恢复；`material_pipeline_history_test` 检查天空参数改变时失效/复用；Lab `--sky-test` 检查实际场景照明、漫反射/镜面拆分、AO、一次反弹、透明镜面视图与静态 TAA。

## GPU 实时全局光照

`realtime_gi.h` / `realtime_gi_query.h` 与 `realtime_gi_shaders.h` 提供 GPU 动态漫反射 GI。它在现有 OpenGL 4.5 RHI 上使用二维 RGBA32F 几何 BVH、片元射线追踪与两个缓存目标，不依赖新增 compute/SSBO/硬件光追接口。灯光、天空和区域灯改变时无需 CPU 光照烘焙；几何/材质改变时，重新提取三角面并提交新的 `RealtimeGiScene`。

```cpp
#include "Render/Public/Pipeline/realtime_gi.h"
effects.realtimeGi.scene=Render::BuildRealtimeGiScene(opaqueTriangles);
effects.realtimeGi.enabled=true;
effects.realtimeGi.raysPerProbe=64;
effects.realtimeGi.probesPerFrame=16;
effects.probes.enabled=false;
effects.indirect.enabled=false;
effects.lighting.ambientAndExposure=glm::vec4(0,0,0,1);
// 灯光沿用原 effects.lighting、effects.shadows、effects.areaLights、effects.sky。
// 之后只需正常 Record/Submit/Complete，GPU 每帧更新，不调用 BakeDiffuseProbeVolume。
```

与 CPU 探针相同，`ProbeTriangle` 输入为世界坐标、不透明几何、线性漫反射率和自发光。CPU 只构建/打包 BVH，最多 32768 三角面；上传数据为每个 BVH 节点 2 个 vec4、每三角面 5 个 vec4。灯光修改不替换几何资产。MaterialLab 自动检测变换及不透明材质改变，纹理仍以平均颜色归约；连续几何变形目前需要 CPU 重建与整图上传，没有 BLAS/TLAS refit 或蒙皮支持。几何变化还会重置辐照度缓存，因此当前适合静态几何配动态灯光，连续移动物体时的高阶反弹不能充分收敛。

默认 4x4x4 探针，每探针 64 条固定于世界探针的 Fibonacci 射线，不同探针使用不同相位；首次初始化全部探针，之后每帧更新 16 个（1024 条主射线）。64 个探针约四帧覆盖一次，允许 32..128 射线/探针及 1..64 探针/帧。同一探针的方向不随帧号旋转，避免低射线预算下命中覆盖率变化导致静态墙面亮度浮动。灯光仍每批次重新追踪；射线命中时求取自发光、经过 BVH 遮挡的点光/太阳/区域灯漫反射，区域灯使用 2x2 样本。再查询上一执行帧的辐照度场，用有界 `bounceFeedback=.9` 传播更高阶漫反射。天空逃逸射线提供真实环境辐射；这个反馈不追踪高阶镜面路径。固定方向不会靠长期旋转继续增加方向覆盖；小发光体或狭窄开口的空间精度可通过提高 `raysPerProbe` 改善，仍受最多 128 条射线限制。

第二个 GPU pass 将射线积分为 9 项 SH、4x4 八面体方向的距离一阶/二阶矩与探针有效性。缓存为 64x26 RGBA32F：第 0..8 行是 RGB 余弦卷积 SH / alpha 天空可见性 SH，第 9..24 行是距离矩，第 25 行是有效性、背面比例、保留项和更新次数。距离矩、天空可见性和分类仅在几何/体积/射线配置初始化时计算，之后直接复用；改变灯光只滤波 RGB 辐射，不改变静态几何权重，并减少常规积分工作。八邻域查询用三线性、法线朝向、距离矩 Chebyshev 权重抑制隔墙漏光；多数背面命中的实体内探针剔除。尚无自动探针搬移或分层体积，4x4 方向与 SH 都是低频近似。

`historyWeight=.85` 对 RGB 辐射滤波，DC 辐射变化自适应降低历史权重以加快重新照亮；固定方向消除了原先被误判为灯光变化的采样旋转噪声。它仍需要收敛，并非同一帧精确解决所有反弹。没有任何光源、自发光或天空辐射时直接清除反馈，避免残留光。`normalBias=.12`、`visibilityBias=.15` 是世界距离，`rayBias=.005` 用于求交偏移。`maxDistance=30` 应覆盖本体积与遮挡物，超出追踪范围的物体不会阻挡天空。体积布局、源几何、射线数量或最大追踪距离改变会重新初始化；`reset=true` 用于一次性显式重置，并同时清空 TAA 历史。

实时 GI 复用原材质 GI texture 15，并以 UBO13 的 mode=2 切换查询，不增加正式 PBR 的采样器数量。它与 CPU 探针、屏幕 GI 互斥，冲突设置会被拒绝，尚无三者融合的屏幕最终收集。有效体积内用实时漫反射替换全局天空漫反射；范围外仍渐退回全局天空。透明层和镜面视图共享世界坐标缓存；局部天空镜面仅有可见性近似，没有场景镜面路径、SSR 或新的反射捕获。

双缓存严格遵守 `Record → Submit → CompleteFrame`：仅完成的帧推进缓存索引/更新批次，Discard 不推进；移动相机、viewport Resize 不重建世界光照。录制资源 owner 保护旧几何、缓存及程序，Shutdown 后已经录制的帧仍可执行。调试图为 `DebugTextures().realtimeGiCache`；统计 `realtimeGiPasses=2`、`realtimeGiUpdatedProbes`、`realtimeGiRays`。GPU 帧内不回读缓存；MaterialLab 的 `ReadbackRealtimeGiCache()` 仅用于离线验证。

这是一套 DDGI 风格的动态辐照度缓存原型，不能称为 UE5 Lumen 本体。其距离矩可见性/动态探针思路参考 [DDGI 论文](https://jcgt.org/published/0008/02/01/)；SH 编码、三角面软件追踪及资源组织是本项目实现。下面的混合 GI 模式独立实现 Surface Cache、屏幕/世界最终收集与基础场景反射；DDGI 算法继续保留。透明几何不遮挡间接射线，alpha 裁剪和纹理细节仍被归约。实时最终直射区域光/点光仍不投影，GI 内的遮挡不能代替直射阴影功能。

`--realtime-gi-test` 覆盖 GPU 常量天空能量和距离矩、动态天空强度、封闭遮挡、移动/变色区域光、多反弹反馈、自发光编辑、无光残留、相机独立性、镜面及 MSAA/TAA。CPU `diffuse_probe_test` 增补 BVH/输入边界，`material_pipeline_history_test` 覆盖实时缓存的首次初始化、丢弃、复用、重置、Resize、源替换、Shutdown 租约与重启。

`--realtime-gi-stability-test --procedural` 在开放/封闭房间及 TAA+MSAA4+Bloom 配置分别预热 384 帧，再测连续 96 帧。同时测量探针 DC 变异系数和背墙固定世界位置的 3x3 像素亮度均值，并检查天空可见性/距离矩/分类在静态场景和天空重照明时保持不变。门限为探针峰值 CV < 0.1%、墙面平均亮度标准差 < .03/255、峰值 < .15/255；不会用整图均值掩盖局部墙面波动。

2026-10-05 同机同配置的稳定性修复前后对比（固定几何和光源，64 射线、16 探针/帧）：

| 指标 | 修复前 | 修复后 |
| --- | ---: | ---: |
| 开放房间探针 DC 平均 CV | 0.710% | < 0.0001% |
| 封闭房间探针 DC 平均 CV | 2.110% | 0.00184% |
| 开放背墙，无 AA，平均/峰值亮度标准差（/255） | .3769 / .5425 | < .00001 / < .00001 |
| 开放背墙，TAA+MSAA4+Bloom，平均/峰值亮度标准差（/255） | .3086 / .4541 | .00180 / .01023 |

上述统计针对收敛后的静态墙面；初次加载和动态光源仍有正常的分帧更新/多反弹收敛过程。射线预算、历史权重和 RHI 接口保持原值。天空强度 1→2 的 16 帧响应仍约 94.3%，隔墙暗/亮 DC 比为 .00357。

2026-10-05 RTX 4060 Laptop、1280x800、TAA+MSAA4+Bloom、6036 个不透明三角面的 MaterialLab 场景，稳定性修改后实测：默认 16 探针/帧首批为 5.512 ms/帧，切回实时 GI 后 2.323 ms/帧，全量 64 探针/帧为 2.602 ms/帧；屏幕 GI 对照 7.925 ms/帧。均为独立批次 GPU interval，含 CPU 提交空隙，初次程序编译/BVH 上传不计入；不同批次受升频及后台活动影响，不能把时差直接视作算法增益，两种 GI 也并不逐像素等价。空场景天空强度 1→2 后 16 帧的 DC 为 6.68487，目标 7.08982（约 94.3%）；固定隔墙测试暗/亮半室的 DC 比为 .00357，整体静态 TAA 平均帧差 .0717/255。仅是上述测试场景的数据，不能保证任意细墙/小物体或大型场景的质量、帧率。

## 类 Lumen 混合全局光照

`lumen_gi.h` 提供独立 `LumenGiScene` / `LumenGiSettings`，实现位于 `lumen_gi.cpp`、`lumen_gi_shaders.h` 与 `lumen_gi_pipeline.inl`。原 DDGI 的设置、数据编码和求解保留；新模式在上层管线复用三角面 BVH、SH 查询、RHI encoder 与已有材质采样槽，不修改底层渲染接口。

```cpp
#include "Render/Public/Pipeline/lumen_gi.h"
// 世界坐标不透明三角面：线性 diffuseReflectance/emission，正确的表面朝向。
effects.lumenGi.scene=Render::BuildLumenGiScene(opaqueTriangles,.35f);
effects.lumenGi.enabled=true;
effects.lumenGi.surfaceUpdatesPerFrame=512;
effects.lumenGi.surfaceRays=16;
effects.lumenGi.probeSpacing=16;
effects.lumenGi.gatherRays=12;
effects.lumenGi.screenTraces=true;
effects.lumenGi.reflections=true;
// 与 DDGI / CPU 烘焙探针 / 独立 SSGI 互斥；冲突输入会拒绝 Record。
effects.realtimeGi.enabled=false;
effects.probes.enabled=false;
effects.indirect.enabled=false;
effects.lighting.ambientAndExposure=glm::vec4(0,0,0,1);
// 正常 Record / Submit / CompleteFrame，灯光沿用现有四套光源设置。
```

CPU 仅构建不可变几何与规则重心坐标三角面参数化；每三角面至少一个采样点、默认最多 16x16，总采样点不超过 65536，输入不超过 32768 个非退化三角面。`surfaceTexelSize` 是世界空间目标间距，范围 .05..10；达到全局上限后降低后续三角面的密度。属性图保存三角面图集范围和采样位置/法线，光照图使用直接辐射、原始入射辐照度、过滤入射辐照度和总辐射各两个 RGBA32F 目标，宽度 1024。世界传输命中的 `LmLookup` 采用实际三角面内邻近的三个规范采样点插值，权重非负且归一化；不再沿折叠方格的斜边读取远处样本，也不跨三角面混合。图集保留平均漫反射率/自发光。世界三角面的 A/B/C.w 保存镜面 F0.rgb，漫反射记录的 w 保存粗糙度；五个 vec4 的几何布局及 DDGI 编码保持不变。这仍是平均材质提取，不等同于 UE Mesh Cards 的完整材质捕获。

每帧四次表面缓存绘制，分别更新直接光、原始入射辐照度、过滤入射辐照度和出射辐射：光源发生变化时同帧重算全部直接漫反射/自发光，静态直接光复制上一结果；更高阶漫反射按预算分批传播。入射历史单独保存，反弹复用此前间接辐射；直接项默认读取当前直接缓存，`traceDirectLighting` 开启时改在命中点计算。参与当次反弹的直射项使用当前灯光，反弹仍按预算更新。零辐射点光的位置变化不触发重照明。太阳、点光、区域光在缓存内由 BVH 测遮挡；天空逃逸射线使用环境辐射。`bounceFeedback=.9` 控制表面间反馈，`surfaceHistory=.65` 平滑并对真实光照变化加速响应，反照率限制到 .95；迭代求解仍需数轮收敛。初次加载、拓扑变化、显式 `reset` 会清空并更新全部采样点；保持拓扑的变换保留图集与间接缓存并提高四帧更新预算。所有光源关闭时清除反馈得到精确零辐射。灯光/相机变化不重建图集。

反弹半球射线的相位由三角面 ID 和规范网格坐标的稳定哈希独立确定，折叠副本使用相同规范坐标。这样避免邻近 surfel 使用相关的低频位置相位而共同漏采窄光斑；相位不随帧旋转，静态几何与光源的方向集合保持固定。仅真实重照明和反弹收敛推进历史，不引入时域方向随机。

主视图捕获不透明深度和几何法线；法线图 alpha 附带 roughness。接收者网格间距默认 16 像素，最长边最多 160 点，每点只用一条短反向射线识别三角面。全分辨率验证附近四个三角面的平面、法线和重心坐标，按确切世界位置查询入射缓存，不重新积分半球。重建位置的微小误差可能把接收点放到自身背面；偏移先越过毫米范围内的自身背面，再测邻近接触面的安全间距。真实黑色命中不会回退天空，也不额外叠加 DDGI / SSGI / 全局天空漫反射。

漫反射最终图直接保存世界缓存结果，不运行原视图历史与空间模糊，RGB 为辐照度 E、alpha 为余弦平均天空可见性；`gatherHistory=.85` 目前只用于反射历史。透明层、平面镜面视图及未能匹配接收三角面的像素查询低频世界缓存：复用原 SH/距离矩格式，但命中读取已求解的 Surface Cache，不在探针内再求一遍 DDGI 反馈。默认 4x4x4 布局应覆盖接收表面，单个体积仍限 64 探针。

场景反射使用 HDR 不透明源和材质法线贴图。粗糙度达到 maxReflectionRoughness 时使用世界入射缓存与 GGX DFG，阈值之前 .1 的范围渐变；较光滑表面做 GGX 可见法线重要性采样。默认每像素 4 条固定射线、每维为视口的 1/4、最长边 256，可用 `reflectionTraceMaxDimension=320/512` 增加细节。屏幕源和两系数输出默认每维为视口的 1/2，`reflectionResolutionScale=1` 可恢复全尺寸；主画面和漫反射辐照度保持原生分辨率。关闭屏幕追踪时省去反射源捕获。屏幕命中读取可见材质，屏外命中读取表面缓存并计算太阳/点光/区域光的 GGX 高光以及一次有界后续镜面反射。分别积分 Fresnel 的 F0 系数和掠射角系数；PBR 用原 texture 8/9 合成 `F0*R0+R1` 并应用 AO，避免再次乘 DFG。两套历史使用几何有效的四点重投影插值，按位置、法线和粗糙度拒绝，以邻域方差约束辐射；粗糙反射的平均命中距离变化降低历史权重，镜面才执行严格距离拒绝，随后两轮几何/材质过滤和引导上采样。透明层恢复 texture 8/9 的折射快照并切回世界辐射查询；原平面镜面仍独立运行。采样依据 [Heitz GGX VNDF](https://jcgt.org/published/0007/04/01/paper.pdf)。

| 设置 | 默认 / 范围 |
| --- | --- |
| `intensity` / `maxDistance` | 1 / 30 世界单位；0..16 / .1..1000 |
| `rayBias` / `thickness` | .015 / .15 世界单位；偏移用短射线约束接触表面穿越 |
| `surfaceUpdatesPerFrame` / `surfaceRays` | 512 / 16；1..65536 / 1..32 |
| `probeSpacing` / `gatherRays` / `screenSteps` | 16 / 12 / 16；4..32 / 1..32 / 4..64；gatherRays 保留兼容，世界缓存收集不再使用该射线预算 |
| `surfaceHistory` / `gatherHistory` / `bounceFeedback` | .65 / .85 / .9；历史0..0.98，反馈0..0.95 |
| `origin` / `spacing` / `counts` | 世界辐射体积布局；各维至少2，总数不超过64 |
| `distanceFields` / `reflectionRays` | true / 4；反射射线预算 1..32 |
| `fullResolutionDirectLighting` | false；`SurfaceCache` / `DirectOnly` 诊断在可见像素计算解析直射和几何遮挡，密闭 GI 房间开启 |
| `traceDirectLighting` | false；在反弹射线的实际命中点计算解析直射与遮挡，缓存保留高阶反弹，密闭 GI 房间开启 |
| `reflectionResolutionScale` / `reflectionTraceMaxDimension` | .5 / 256；.5..1 / 128..512 |
| `maxReflectionRoughness` | .4；0..1，0 保留全部粗糙度的独立射线，用于质量对照 |

UBO 10 为 288 字节，原 0..223 偏移不变，在 224..287 追加距离体积原点/间距/尺寸和反射参数；保留 UBO 9 / 11..15 的布局。UBO13 mode=3 在主不透明视图读取 texture15 的最终辐照度 E/pi；透明/镜像视图 mode=2 读世界缓存。UBO7 options.y 启用混合反射、z 指定直接光捕获；2026-10-06 在原 160 字节末尾追加 screenJitter，表面 UBO 为 176 字节，208 字节材质参数布局不变，正式 PBR 仍限 16 个片元采样器。screenJitter.xy 将主视图抖动栅格坐标映射到稳定 GI/反射网格，光学快照继续使用主视图投影。全屏 GI 局部槽 0..8 按 pass 重绑定，其中 8 为距离场；正式材质纹理 0..7 的契约不变。

仅 `CompleteFrame` 推进表面/世界/视图双缓存和更新批次，Discard 不提交；已录制 encoder 持有程序/图集/视图 owner，Shutdown 后仍可执行。反射 HDR 源持有独立深度附件，MSAA 在同尺寸切换时不会引用被释放的主视图附件。相机移动保留世界光照；resize 重建依赖尺寸的视图资源，内容 hash / 跳切控制历史。调用方负责在几何/材质变化后提交新 `LumenGiScene`，MaterialLab 自动提取；源在帧内不执行 CPU 光照烘焙或 GPU 回读。

统计为 `lumenGiPasses`、`lumenSurfaceTexels`、`lumenUpdatedSurfels`、`lumenScreenProbes`、`lumenHistoryUsed`；内部世界缓存另保留 `realtimeGiPasses=2` 诊断，独立 `indirectPasses=0`。调试图 `lumenSurfaceCache`、`lumenDirectLighting`、`lumenGather`、`lumenReflections` 为借用句柄，不能删除；`lumenReflections` 当前显示 R0 系数，完整材质反射为 `F0*R0+R1`。Lab `ReadbackLumenSurfaceCache`、`ReadbackRealtimeGiCache` 和 `ReadbackLumenGather` 仅用于离线诊断。

`lumen_gi_test` 验证三角面参数化、查询反变换、空资产、32768 三角面的有界覆盖与设置边界；`material_pipeline_history_test` 增补初始化/丢弃/重用/resize/同尺寸 MSAA、资产来回替换、模式冲突、黑色太阳下无光残留与 Shutdown 后旧帧执行。`--lumen-gi-test` 检查封闭天空遮挡、表面多次反弹、动态灯光/自发光、屏外缓存、屏幕追踪开关、场景反射、MSAA/TAA 与切回 DDGI。`--realtime-gi-stability-test --lumen-gi` 复用 DDGI 的局部墙面与探针稳定性门限，避免整体均值掩盖局部闪烁。

设计参考 [Epic Lumen 技术说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine)中的表面缓存和最终收集，以及 [Lumen 性能指南](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-performance-guide-for-unreal-engine)中的粗糙反射复用与更新预算。本项目有软件 BVH/距离体积与 Vulkan 原生光追路径，尚无 UE 的 Mesh Cards、距离场 clipmap、大世界流送、HZB、自适应方向探针或 TSR。图集平均材质忽略 alpha 裁剪、流动纹理与法线细节；透明几何不遮挡间接射线。太阳使用 CSM，常规 `Material` 视图的点光/区域光主表面直射尚无独立几何阴影；上述逐像素光源遮挡仅用于漫反射诊断模式。该有限场景实现不等同于 UE5 Lumen 本体或同等画质。

2026-10-06 的普通相机运动修复改用独立的世界入射辐照度缓存：每个 surfel 存 `E/pi` 和天空可见比例，再组合出射辐射 `direct + rho * E/pi`。低分辨率接收者查询只识别三角面 ID；全分辨率用平面、法线和重心坐标验证该 ID，在确切世界位置插值入射光。没有匹配的像素使用世界探针，不再每次转动相机重新产生 12 条漫反射半球样本，也不依赖增加视角历史权重来隐藏噪声。入射缓存与原缓存一起只在 CompleteFrame 后推进。

混合 GI 不再运行完整的漫反射 RGB 捕获，只保留接收者深度和法线；`diffuseRadiance` 调试纹理在此模式下为空。传统 SSGI 仍正常捕获该颜色图。屏幕反射读取独立的 HDR 反射源，其漫反射复用已解析的主视图辐照度，避免在每个源像素再次查询三维探针；源视图关闭 TAA 偏移，并使用自身分辨率生成归一化纹理坐标。

`LumenGiSettings.reflectionSourceResolutionScale` 独立控制 HDR 反射颜色捕获，默认 `.5`，允许 `.25..1`，尺寸为内部视图尺寸乘比例再向上取整。追踪继续读取完整内部深度/法线，颜色通过归一化 UV 采样；反射输出的 `reflectionResolutionScale` 和追踪网格预算不随颜色源改变。`.25` 相比默认 `.5` 只绘制四分之一的颜色源像素，会牺牲细小屏幕反射的色彩细节。改变源尺寸仅替换视图资源并重置屏幕历史，世界 Surface Cache 保留；比例改变但取整尺寸不变仍使历史失效。DebugTextures 的源颜色/深度句柄为借用资源，遵守原帧寿命规则。

独立发光三角面通过投影立体角和固定遮挡子样本积分；数量较多时用发光功率 CDF 限制到 16 个采样，最多 64 条遮挡射线/更新 surfel。`RenderItem::analyticEmission` / `ProbeTriangle::analyticEmission` 表示同一灯具已有点光或区域光表达，避免把光源发光再次作为 GI 源；可见材质不变，独立灯光关闭时调用方可撤销此标记。几何 emission.w 的 bit0 为双面、bit1 为解析光源归属，不改变原五个 vec4 的三角面布局。

表面图集改用规则重心坐标格，方形分配的多余半区镜像到三角面内，保留原 r*r 缓存布局和 refit 编号。查询使用线性的 (u,v)，避免原等面积坐标在顶点附近把积分误差拉成扇形光束。区域光整灯剔除检查零辐射、单面发光背面和接收地平线；跨地平线及双面光仍按原 4x4/8x8 样本积分。

原始入射历史与过滤结果分别双缓冲，每帧只在小尺寸表面图集中做一次 3x3 世界距离过滤，不反复过滤未更新的历史。属性尾部记录共边、共面且同向的三角邻接；非流形边和墙角不参与混合，refit 会重算连接。入射查询用最多九点二次 B-spline 核的加权仿射重建，约束为非负归一化权重，保留常量和完整内部核的线性精度与连续梯度；共边附近渐混邻居，避免沿网格对角线出现亮度接缝。稀疏外圈投影到合法采样凸包并约束形函数，保证亮度连续，不宣称边界线性外推精度或梯度处处连续。出射缓存继续保留材质边界，天空可见性差异限制空间过滤。

三角面采样间距在 Build/refit 时预计算，主视图不再每像素读取三个顶点计算面积。DDGI 查询在插值权重严格为零时跳过对应探针的分类、距离矩和 SH 读取；非零权重仍完整计算，包括极小权重。独立 GPU 回归与未剔除的 CPU 参考对照颜色、天空可见性和覆盖，检查面/边/角分别为 56/28/14 次读取，体积内部仍为 112 次。

缓存外圈不再采用局部梯度的无限制边外外推，退化核直接使用凸包上的正权重插值；强光和遮挡边缘不会因负系数产生黑色振铃。过滤范围同时考虑最长世界格步长，截断核的仿射形函数同样约束为非负；边界偏移通过有效采样中心和几何支持范围回归检查。共面邻接支持长边与短边的部分重合，按共线分桶和区间扫描建立，最多六个部分邻居；墙角、同侧重叠、非流形区间及超预算列表保守拒绝。完整共边仍沿用原 xyz 邻接，部分区间从 w 指向属性尾部列表，真实开口不补连接。CPU 回归覆盖 refit 后邻接变化和 32768 重叠三角面，GPU 回归覆盖非线性外圈跨格、内部梯度变化、非负光源叠加、空间噪声、常量/内部线性精度与墙角隔离。

2026-10-06，约束权重后的受控 Vulkan 重建回归：非负光源叠加误差 3.82e-6，外圈跨格跳变 1.43e-6，内部梯度差 .000954，空间噪声 RMS 为原始缓存的 .03280。旧三点重建在同一梯度 fixture 中约为 .302。完整内部核保留线性精度，边界用有效采样中心检查仿射关系；GPU 同时保留 NEE 投影能量、GGX 能量、接触面和真实开口隔离检查。这些数值只代表受控输入，不表示所有场景零噪声或完整 UE Lumen 画质。

`MaterialPipelineSettings.outputSize` 可以把内部渲染尺寸与最终输出尺寸分开。默认 `{0,0}` 保留原行为；显式输出两维必须为 1..8192。`Resize` 仍指定内部附件尺寸，调用方的 `BeginFrame.framebufferWidth/Height` 使用输出尺寸，相机 projection 使用输出宽高比。TAA、深度、GI、Bloom 和相机模糊在内部尺寸执行，最后用有界 Catmull–Rom 4x4 空间插值输出，中心 2x2 包络限制过冲；Overlay 在输出分辨率绘制。FXAA 先在内部尺寸处理再重建，1:1 输出沿用原复制/FXAA 路径。这里只提供空间重建与现有 TAA，不是 TSR。改变输出尺寸本身保留内部历史；改变内部尺寸重置屏幕历史，同时保留世界 GI 缓存。前端 RHI 不变。

`PbrPassConstants.lightColors[i].w` 为点光源世界半径，零保留原逆平方衰减；高光按有限角度范围过滤，半径内的衰减分母下限为 `max(radius²,1e-4)`，避免灯罩附近的辐照度无限增大。主材质、实时 GI、二次反射与 CPU 探针使用同一能量上限；高光角度仍按真实距离计算。世界遮挡射线缩短终点以排除灯罩自身。点光与显式几何法线版本的区域光使用几何朝向约束，贴图法线不能接收几何背面的灯光；区域灯旧 GLSL 调用签名仍保留原积分。该约束不代替物体间遮挡，点光/区域光主直接阴影仍未实现。UBO 大小仍为 176。

`ReflectionSettings.receiverBounds` 可提供反射接收面的世界包围盒，视锥外跳过镜面场景及其 MSAA 解析；未提供时保持原行为。Vulkan 帧内复用不可变常量快照和描述符，后续更新仍生成新版本，保留提交等待和资源寿命语义，RHI 前端未修改。

## 世界空间漫反射与局部天空遮挡

`diffuse_probe_volume.h` 提供独立 CPU 场景烘焙接口，通用管线以 `effects.probes` 启用；默认关闭，原调用方保持兼容。ECS/资产提取层提供世界空间 `ProbeTriangle`，无需回读 GPU 网格。`diffuseReflectance` 是线性反照率乘非金属漫反射权重，`emission` 是线性自发光辐射；材质归约由调用方负责。透明物体不进入当前遮挡/反弹几何，三角形正面朝向房间内部或实体外部，只有 `twoSidedEmission` 允许背面发光。

```cpp
#include "Render/Public/Pipeline/diffuse_probe_volume.h"
Render::ProbeBakeSettings bake; // 默认 4x4x4，256 射线/探针，两次漫反射反弹
// 按房间范围设置中心起点和间距；counts 的各维 >=2，总探针数 <=64。
bake.origin={-2.5f,.4f,-2.5f};
bake.spacing={1.666667f,1.066667f,1.666667f};
Render::ProbeBakeLighting lights;
lights.sky=effects.sky; // 必须与渲染天空环境、强度、旋转一致
lights.sunDirection=effects.shadows.sunDirection;
lights.sunColor=effects.shadows.sunColor;
lights.sunIntensity=effects.shadows.sunIntensity;
lights.pointPositions=effects.lighting.lightPositions;
lights.pointColors=effects.lighting.lightColors;
lights.areaLights=effects.areaLights;
std::vector<Render::ProbeTriangle> triangles=ExtractOpaqueProbeTriangles();
auto volume=Render::BakeDiffuseProbeVolume(bake,triangles,lights);
effects.probes.volume=volume;
effects.probes.enabled=true;
effects.lighting.ambientAndExposure=glm::vec4(0); // 避免旧环境色重复补光
// 如果探针覆盖所有接收表面，可以省去屏幕空间 GI。
effects.indirect.enabled=false;
```

CPU BVH 使用实际不透明三角面求交；球面方向采用固定 Fibonacci 采样，路径使用确定性的余弦半球漫反射，最多允许四次反弹。源包含经过几何遮挡的天空、点光、太阳、矩形灯，以及自发光；矩形灯在烘焙中为固定 2x2 积分，最终直射区域灯仍沿用原实时积分且没有区域阴影。探针的 RGB 保存余弦卷积并除以 pi 的二阶 SH，alpha 保存未卷积的方向性天空可见性；不得再次乘天空强度。主体内部探针按多数首次命中为背面而剔除，尚不自动移动探针。

接收表面查询八个相邻探针，使用三线性、法线朝向和遮挡权重。每个探针另存 4x4 八面体方向的首次命中距离/面；平面侧判断让位于地板上方的接收者保持可见，并降低隔墙插值漏光。`normalBias=.12`、`visibilityBias=.15` 均为世界距离，按模型尺度调整。天空镜面可见性随粗糙度混合方向性和余弦平均遮挡；这只是局部环境遮挡近似，并未生成局部镜面反射图。有效覆盖内用探针漫反射替换全局天空漫反射和屏幕 GI，避免二次计入；范围外渐退到原天空/屏幕 GI。所有邻居被拒绝时保守输出暗值。不要将一份探针体积用于未覆盖的大场景。

烘焙资产不可变。管线仅在资产更换时创建 UBO 12/14，每帧上传 UBO 13 的 16 字节参数，不在帧内追踪世界射线；两个静态块都不超过 OpenGL 保证的单块 16 KiB 上限。主视图、透明层和反射视图共享世界坐标查询。资产、更改强度/偏差/开关会使 TAA 历史失效，录制的旧帧继续持有旧资源，即使随后替换资产或 Shutdown 也安全。

通用 API 不自动察觉几何/光源修改，调用方须重新烘焙并提供新资产。MaterialLab 在 Q 场景保留 CPU 网格，以材质、变换、光源及天空内容 hash 检测修改：首次同步准备，以后单个后台任务更新，继续使用上一份有效结果；相机移动不重烘焙。纹理目前归约为平均线性颜色/金属度/自发光，不保留棋盘细节、流动、法线贴图或 alpha 裁剪轮廓。连续移动光源会有烘焙延迟，当前适合静态及低频编辑场景。

这是有限预算的 CPU 烘焙 SH 体积，不是 DDGI、DFAO 或 Lumen。4x4x4 网格和 4x4 方向可见性不能保证任意薄墙、小物体或狭窄门洞无漏光；有限面被近似为平面也可能造成过度遮挡，SH 会模糊高频照明。验证通过的封闭房间/单隔墙结果不能外推为任意关卡。可见性感知探针的进一步方案参见 [DDGI 论文](https://jcgt.org/published/0008/02/01/)；本项目另有独立实时 DDGI 模式实现距离矩与动态更新。

验证：`diffuse_probe_test` 覆盖恒定天空、全封闭遮挡、薄隔墙、实体内部剔除、屏外点光颜色反弹、零反弹和边界；`pbr_specular_aa_gpu_test` 比较 CPU/GPU SH 查询，并通过 GL 反射检查三个新 UBO 的大小/绑定；`material_pipeline_history_test` 覆盖启用、复用、替换、偏差、更换资源和 Shutdown 后旧帧执行；Lab `--probe-test` 检查封闭房间无天空漏光、自发光反弹、相机独立缓存、后台更新、透明/镜面视图及 MSAA/TAA 稳定。

2026-10-05 本机 RTX 4060 Laptop，1280x800、程序化贴图、TAA+MSAA4+Bloom 的独立批次测得探针方案 5.259 ms/帧，恢复探针后 3.260 ms/帧；相同预设切换屏幕 GI 为 10.560 ms/帧。有效探针 63 个，首次 CPU 烘焙 2424.780 ms，不计入帧测量；GPU interval 包含 CPU 提交空隙，受升频和后台活动影响，不能视为固定收益或视觉等价比较。封闭房间关闭所有内部光源后均值为 0/255；TAA 稳定帧平均差为 .0242/255。

## 技术实现与遗产来源

- **CSM**：参考 `Engine/src/engine/RenderPipe/Pass/CSMpass.cpp`。遗产的五层 depth array/geometry shader 改为四级 2×2 atlas 和四次深度绘制，减少底层新增接口。当前采用默认 4096 atlas、lambda=.65、double 稳定拟合、单侧 caster padding、连续 16 tap tent PCF、RPDB、级联混合与末端距离淡出；参数和单位见上文。
- **镜面**：参考 `Engine/src/engine/Resource/RenderPipe/Passes/mirror_pass.h`。任意平面反射矩阵作用于相机，独立场景绘制，世界空间 fragment 裁剪，表面投影采样反射图。默认全分辨率，2 倍比例可提高小细节质量；不具备粗糙度预过滤。PBR 使用双面绘制处理镜像绕序；自定义材质需提供裁剪逻辑与适当剔除规则。
- **Bloom**：继承 `Engine/src/engine/programs/hh.cpp` 的 HDR 多级链。首级在原始 texel 上做软阈值，再手动双线性和 13 tap 预过滤；后续使用明确的 13 tap 降采样，不再让单轴 Gaussian 同时承担缩小尺寸。重建使用 `mix(large, normalizedTent(small), scatter)`，最终只应用一次 strength。13 tap 权重参考 [Unity 官方 Sampling.hlsl](https://github.com/Unity-Technologies/PostProcessing/blob/v2/PostProcessing/Shaders/Sampling.hlsl)，归一化粗细层混合参考 [Unity 官方 Bloom.shader](https://github.com/Unity-Technologies/Graphics/blob/master/Packages/com.unity.render-pipelines.universal/Shaders/PostProcessing/Bloom.shader)。
- **SSAO**：参考 `Engine/src/engine/programs/SSAO.cpp` 与 `bin/shaders/SSAO`。改为主视图深度重建位置与法线，32 个确定性半球样本，4×4 旋转模式及深度感知模糊，避免增加 G-buffer 附件。法线重建先确定整数深度像素，再以整数偏移取左右上下邻居，并使用各自真实 texel 中心重建位置；修复半分辨率 UV 落在全分辨率像素边界时，浮点加减再 floor 重复取中心或跳过邻居产生的斜面暗条纹。AO 仅衰减环境光。
- **滤镜**：参考 `bin/shaders/depthTest/screenShader.fs` 的反相、灰度、浮雕、Gaussian、边缘核和径向波纹。锐化补充标准核。遗产注释中的 Vignette 实为波纹，这里将暗角作为独立设置。
- **Tone mapping**：旧 PBR 的 `color/(color+0.2)` 与 `bin/shaders/shaderToy/nsea.fs` 的矩阵 ACES fitted。显示编码仅执行一次；无 tone map 模式仍有 Gamma 和 LDR 饱和裁剪。

`bloomSoftKnee=.5`，范围 0..1；`bloomFireflyClamp=0` 默认关闭局部异常亮点抑制。设为正值后使用局部亮度参考限幅和归一化 [Karis 加权](https://graphicrants.blogspot.com/2013/12/tone-mapping.html)，恒定 HDR 区域不会被压到该数值，但运动亮点的能量可能随邻域变化，因此它是可选 firefly 控制，不能代替稳定降采样。HDR 输入的 NaN/Inf 会被处理，半浮点输出限制在 60000。

`bloomScatter=.8`（0..1）控制每级重建时来自较粗、较宽光晕的比例；0 只保留当前细层，1 只保留粗层重建结果。`bloomRadius=1.25`（.5..2）是上采样 tent 的采样半径，单位为较小输入图的 texel。两者仅改变 Bloom 图，不模糊原始 HDR 场景。每级权重和为 1，使层数主要控制光晕分布，不再产生旧 `large + .5*small` 的逐层亮度增益；`bloomStrength` 独立控制最终合成强度。层数增加不会继续放大恒定亮区，旧场景若依赖累加增益应显式调整 strength。

独立 GPU 测试用保持输入能量的亮点跨 33 个亚像素相位移动，与旧单次双线性采样加同一软阈值对比。当前测试机测得 Bloom 能量的变异系数（标准差/均值）：

| 输入亮点能量 | 旧提取 | 新默认提取 | 降幅 |
| --- | --- | --- | --- |
| 32 | 0.138445 | 0.016476 | 88.1% |
| 128 | 0.031848 | 0.003257 | 89.8% |
| 1500 | 0.002662 | 0.000322 | 87.9% |

这组结果验证特定受控输入，不表示任意场景完全无闪烁。恒定 HDR 100 的软阈值结果为 99.1875（理论 99.2，RGBA16F 误差 .0125），下一 mip 的积分能量误差小于 .3%；测试同时覆盖奇数尺寸、1×N 和边缘亮点。

完整金字塔回归另外执行真实 GPU 提取、逐级降采样及重建。默认 scatter/radius 下，恒定 HDR 100 在 1..6 层均输出 99.1875，旧重建从 99.1875 增至 195.25；有足够边界留白的亮点在六层重建后保留首层积分能量的 .997811。五层光晕与旧上采样比较时，先按各自总能量归一化，以区分柔和度与亮度降低：

| 五层亮点分布指标 | 旧重建 | 新默认重建 |
| --- | --- | --- |
| RMS 半径，半分辨率 Bloom texel | 6.773178 | 20.396257 |
| 中心半径 4 texel 内的能量占比 | .766096 | .360349 |
| 峰值像素的能量占比 | .126073 | .050069 |

保持 scatter 不变，将 radius 从 .5 调至 2 时，RMS 半径从 15.078107 增至 28.165229。三档亮点各移动 33 相位，先消除已知平移和整体强度变化，再测每帧归一化形状差：

| 输入亮点能量 | 旧完整链形状差 | 新完整链形状差 | 新完整链能量变异系数 |
| --- | --- | --- | --- |
| 32 | .019471 | .008125 | .016466 |
| 128 | .018851 | .007789 | .003269 |
| 1500 | .018774 | .007757 | .000345 |

这组形状变化降低约 58%，完整链额外积分能量偏差最多约 .22%。能量变异系数基本保持提取阶段水平；不能据此声称消除了 BRDF 源亮度自身的闪烁。高光抗锯齿、TAA 与 Bloom 分别在着色、时域与光晕重建阶段处理问题。

## 底层兼容扩展

新增 `RenderTargetSpec` / `CreateRenderTargetDesc`、异步创建/删除、`SetRenderTarget` 与 `ResolveRenderTarget` 帧命令；旧命令 ID 不变。支持 RGBA16F / Depth32F、`mipmaps=false` 空纹理和 1/2/4/8 样本附件。多采样纹理仅分配存储，不接受普通上传/mipmap；通过 resolve 得到颜色/深度采样输入。FBO 借用附件，创建时验证格式、尺寸、样本数和完整性；支持单颜色附件加可选深度，以及 depth-only。

切换目标会设置 viewport、关闭 scissor 与 framebuffer-sRGB、按需清理，并使 pipeline/mesh 状态缓存失效；全屏/场景 Pass 都重新绑定必要状态。活动附件不能同时作为采样输入；失败目标阻止继续绘制到旧目标。资源释放先删除 FBO，再删除其纹理；已录制帧持有资源 owner。

## 验证与当前范围

CPU 测试 target：`render_target_test`、`msaa_render_target_test`、`scene_effects_test`、`post_process_test`、`material_pipeline_lifecycle_test`、`temporal_effects_test`，覆盖附件契约、模拟 GL 的 resolve/状态恢复、失败清理、CSM/反射数学、544/240/400 字节 ABI 和抖动/设置边界；`material_lab_pbr_test` 覆盖材质与 176 字节 PBR Pass 布局，`advanced_material_test` 覆盖 208 字节共享参数布局、透明域路由及排序兼容。

独立 GPU target 为 `shadow_quality_gpu_test`、`bloom_stability_test`、`ssao_quality_gpu_test`、`pbr_specular_aa_gpu_test`、`temporal_effects_gpu_test`、`material_pipeline_history_test`、`area_light_gpu_test`、`indirect_lighting_gpu_test`。前六项覆盖阴影、完整 Bloom、SSAO、高光抗锯齿、时域 shader 及 Record/Complete/Discard 历史状态；区域光测试复用正式 PBR helper，验证解析辐照度、距离/面积/单双面、256 字节 UBO 和镜面相位稳定性；间接光测试验证正式 GPU 追踪与上采样的颜色渗透和屏幕可见性边界。需要 OpenGL 4.5 上下文，真实 MSAA 绘制与 resolve 另由 Lab `--quality-test` 验证。

这些可执行 target 始终创建；上述八项及 `lumen_reflection_gpu_test` 默认不注册到 CTest，使用 `-DRENDER_EFFECTS_GPU_TESTS=ON` 显式启用。`-DRENDER_VULKAN_GPU_TESTS=ON` 在 Vulkan 可构建时注册后端契约、原生光追反射及软件反射三个测试，需要 SDK 验证层；这些 GPU 测试串行执行。Lab 的 `MATERIAL_LAB_GPU_TESTS` 是独立开关，注册 smoke、quality、shadow、closeup、advanced、advanced-quality、sky、probe、realtime-gi、realtime-gi-stability、lumen-gi、lumen-gi-stability；两个混合 GI 模式超时为 240 秒。

MaterialLab `--smoke-test` 验证材质、效果开关、HDR/阴影/AO 附件、resize、透明裁剪、极端发光和 GL 错误；`--quality-test` 验证镜面比例、MSAA resolve、FXAA、TAA 累积及历史失效、静止/运动相机模糊；`--shadow-test` 使用独立阴影场景，检查可见度/级联诊断、2048/4096/8192 atlas 重建恢复、掠射方向光、移动相机和非法偏差参数。指定 `--screenshot FILE.png` 时保存正常图以及同目录的 `FILE_visibility.png`、`FILE_cascades.png`。图像差异测试用于检测功能，不等同于所有场景的视觉质量评价。

`--closeup-test` 使用 640×724 竖幅近景、TAA + MSAA4，支持 `--yaw RADIANS --pitch RADIANS --distance UNITS`。需要 TAA 的配置重新稳定历史，检查静态帧差，并可导出逐步关闭 Bloom、AO、阴影及 TAA/FXAA/MSAA 的隔离截图；末张仍保留 PBR 高光滤波。截图后缀与命令示例见 [MaterialLab 近景验证](../MaterialLab/README.md#近景与效果隔离)。

`--advanced-test` 使用玻璃/水面/发光面板场景，逐项检查区域灯、单次漫反射反弹、发光贴图、透明光学强度、折射扰动与时间流动，并覆盖 MSAA4、TAA 历史、相机模糊、透明镜面视图执行和退化区域灯输入。它另检查旧常量 ambient 不进入 GI 源、非发光表面的漫反射颜色反弹；场景图像差异不作为光学精确性的证明。

`--advanced-quality-test` 使用 960×600 高俯视及透过玻璃观察盒子的固定预设，分别隔离 GI、透明层、Bloom 和 TAA，并检查静态重现、效果恢复、TAA 累积与跳切。两个 advanced 入口都支持 `--yaw / --pitch / --distance`；质量模式只要指定任一相机参数就改跑一个自定义视角，未指定分量保留普通 advanced 默认值。截图后缀及复现命令见 [MaterialLab 质量诊断](../MaterialLab/README.md#透明与间接光质量诊断)。

SSAO 的受控斜面回归中，旧邻居选择产生 1800/28260 个错误法线像素，AO 最低 .479736、均值 .99418；整数邻居修复后，五种偶数/奇数尺寸的最大法线误差小于 .000623，无遮挡 AO 最小值/均值均为 1、列差为 0，同时保留抬升板的接触遮蔽。PBR 高光抗锯齿的解析平滑法线测试中，太阳/点光在 roughness=.045/.08 时的能量相对极差（range/mean）分别由 32.006/17.9057 降至 .182620/.180236，约降低 99%；仍有约 18% 残余波动，不代替 TAA，也不是对真实球体或法线贴图 mip 的专项证明。平法线开关对照和完全粗糙表面保持不变。

本次受控 GPU 回归中，`N·L≈.297/.152` 的无遮挡斜面采用固定 RPDB 上限 4 时平均可见度为 `.963816/.826208`，改为真实三角面坡度自适应后为 `.999968/.999970`（1 表示完全可见）。跨所测五种坡度、256/1024 atlas 与三个亚纹素相位，最差平均值为 `.999965`、最小像素值为 `.999512`。Lab 阴影场景中盒子受光正面区域的平均误遮挡从约 `2.38%` 降至 `0`。这些数据验证了修正上限导致的自阴影问题，不能外推为所有曲面或极端掠射场景无误差。

同一回归的落地接触点可见度为 `0`，抬升对照为 `.999977`；连续移动采样相位与穿越级联混合区时的最大相邻可见度变化分别为 `.004395/.005859`。这些是固定测试几何和采样路径的结果，不是任意运动、光照或尺寸下的误差上限。

当前是固定顺序的多 Pass 前向管线，尚无通用 RenderGraph。CSM 只服务一盏方向光，点光与区域光暂不投影；过滤宽度固定，没有 PCSS 接触硬化、VSM 或光线追踪接触阴影。当前定位是可诊断、可调参的实用 CSM，不保证与现代引擎在所有场景下取得相同画质。镜面仍支持单平面单次反射，混合 GI 另提供基础屏幕/世界场景反射；透明折射基于各视图不透明快照，无递归镜面或透明层间递归折射。已支持全局天空 IBL、局部天空遮挡、CPU 烘焙体积、实时 DDGI 与混合 GI，但没有局部镜面反射探针、物体运动向量、物体运动模糊、景深或 weighted OIT；透明物体采用排序混合和基于颜色差的 reactive 历史处理，仍缺少独立速度与分层深度。平面反射图没有粗糙度预过滤，SSAO 与独立屏幕 GI 无法获知屏外表面；世界空间缓存具有有限分辨率、追踪距离和收敛延迟，参见各模式限制。

后续增加 Pass 时保持资源读写分离，将目标创建/尺寸依赖放入 Resize，将效果参数放入 settings，将表面属性保留在材质中。新增阴影类型、抗锯齿或透明算法可以继续增添 Pass，无需再把这些逻辑放回 MaterialLab。


## Vulkan 后端与距离场

`Private/Backend/Vulkan/backend.cpp` 实现真正的 Vulkan 1.3 图形后端：资源上传/更新/删除、GLSL→SPIR-V、图形管线、离屏目标、深度、混合、索引/实例绘制、4x MSAA 的颜色/深度解析、交换链呈现与 GPU 回读/时间戳。前端的资源描述、RHIFrameEncoder 方法、材质绑定以及 17 个帧命令的编号和数据结构保持不变。仅启动时选择后端：

```cpp
Render::RegisterVulkanBackend();
ApplicationWindow::Window window({1280,800,"Materials",true,Render::BackendType::Vulkan});
window.Activate();
auto context=*window.GetRenderContextAsVulkan();
context.requireHardwareRayTracing=true; // 可选：要求真实硬件能力，缺失时明确失败
Render::RHIDevice device;
device.Run(Render::BackendType::Vulkan,context);
// 原 async_Create* / BeginFrame / RHIFrameEncoder / Submit 使用方式继续适用。
```

有 Vulkan SDK 与 shaderc 时默认构建；`-DRENDER_VULKAN=OFF` 或缺少 SDK 时保留 OpenGL 构建，RegisterVulkanBackend 返回 false。shaderc_shared.dll 自动复制到程序目录；Vulkan loader 由显卡驱动提供。现阶段 Vulkan 使用一个完成后再提交的帧和 64 MiB 参数快照池，保持既有回调/资源释放顺序；不代表已经完成多帧并行或大场景流送优化。RGB 上传在内部扩展为 RGBA；采样、矩阵和底部行优先约定通过后端转换保留，只有最终呈现翻转 Y。

渲染 fence 仍保证提交完成回调与资源回收，但每帧呈现不再调用 queue idle。每个交换链图像各持有呈现完成 semaphore，重新获取该图像后才复用，按 [Khronos 呈现 semaphore 复用规则](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)处理；重建/关闭时仍等待设备完成。最小化且表面尺寸为零时只完成离屏渲染，恢复后继续呈现。纹理布局转换按颜色输出、深度测试、着色器或传输的实际读写阶段同步，前端 RHI 命令保持不变。

Vulkan 在支持 `VK_KHR_acceleration_structure` / `VK_KHR_ray_query` / buffer device address 的设备上建立真实 BLAS/TLAS，并在现有 GI/反射片元着色器执行原生 ray query，见 [Khronos ray query](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_ray_query.html)。后端私有适配器从既有 RT 几何纹理及 binding 15 的几何元数据建立加速结构；没有向前端新增 Vulkan 句柄或光追命令。不支持的设备或启动设置 hardwareRayTracing=false 使用原软件路径。几何资产替换/更新时重建并释放对应加速结构，灯光与相机变化不重建。此处是 ray query 实现，不要求 ray-tracing-pipeline 扩展或 SBT。

`mesh_distance_field.h` 构建有限世界空间距离体积，最长轴目标分辨率 48（单独构建可选 8..128），打包为 RGBA32F 2D 图集并通过现有纹理 RHI 上传。开放/零厚度网格用无符号距离；`BuildMeshDistanceField(scene,resolution,true)` 对闭合网格提供奇偶规则有符号距离。软件 GI/反射以距离减去采样位置误差形成保守步长，接近表面或用尽步数后精确 BVH 求交，保留薄墙和接触处遮挡；硬件路径直接使用原生求交。关闭 distanceFields 可与精确软件 BVH 对照。

`RefitRealtimeGiScene(previous,triangles)` / `RefitLumenGiScene(previous,triangles)` 要求输入三角面拓扑和顺序一致，返回新的不可变资产，保留 BVH 叶片三角面 ID、Surface Cache 分配与 topology key；更新包围盒、顶点、材质、surfel 坐标及法线。几何移动后 `DistanceFieldCurrent()` 为 false，过期距离场不再用于跳步，改走精确软件 BVH/原生光追；重新 Build 资产可产生新的场景距离场。不同拓扑必须 Build。管线保留兼容缓存，当帧重新计算直接光与世界可见性，并用四帧预算更新间接反弹。场景版本、可见性和反应预算只在 CompleteFrame 后提交，DiscardFrame 不会把未执行的移动当成已完成更新。RHI 前端资源/帧命令接口保持不变。

这仍是有限场景的类 Lumen 管线。尚无 Mesh Cards 材质捕获、距离场 clipmap/自动流送、动态 TLAS 实例增量更新、任意深度递归镜面、TSR 或完整 UE 大场景调度；屏外贴图和法线使用平均材质/几何法线。两系数 GGX 积分、有限后续反射和独立降噪已经实现，但不能据此宣称与 UE5 Lumen 等同。

2026-10-05 当前四射线、两系数反射版本，本机 RTX 4060 Laptop、1280×800、程序化贴图、TAA+MSAA4+Bloom：Vulkan 原生光追完整场景 GPU 时间 8.629 ms/帧，恢复完整配置后 6.222 ms/帧；对应 CPU 墙钟时间 11.369/8.391 ms/帧。世界追踪模式 4.645 ms/帧，仅漫反射 3.469 ms/帧，保留 DDGI 对照 2.399 ms/帧。Vulkan 用每帧 GPU 时间戳区间，不含帧间 CPU 空隙；初始化、距离场/加速结构建立和着色器编译不计入。

同场景 OpenGL 软件追踪完整/恢复批次 GPU interval 为 23.482/23.686 ms/帧，对应墙钟时间 23.584/23.766 ms/帧。OpenGL interval 包含批次内 CPU 提交空隙，不能与 Vulkan 数字作为纯算法耗时直接比较。独立批次受升频、温度和后台活动影响，结果仅代表此有限场景，建议在目标设备用 `--benchmark --lumen-gi` 复测。日志位于 `build/vulkan_lumen_benchmark.log` 与 `build/opengl_lumen_benchmark.log`。
