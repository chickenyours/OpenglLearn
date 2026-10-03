# 材质渲染管线

`MaterialRenderPipeline` 位于 Render 模块，CMake target 为 `render_effects`。它消费 `RenderFrame` 中的材质快照，组织多 Pass 渲染。MaterialLab 仅提供场景、参数交互及 GPU 验证，不再拥有单独的光照管线。

## 已实现流程

| 顺序 | Pass | 输入与输出 |
| --- | --- | --- |
| 1 | 四级 CSM | 场景深度变体 → Depth32F 2×2 阴影 atlas |
| 2 | 平面反射 | 反射相机、裁剪平面、CSM → 可配置尺寸的 HDR 反射图；MSAA 开启时先 resolve |
| 3 | 主视图深度 | 材质深度变体 → 主视图 Depth32F；MSAA 开启时 resolve 深度 |
| 4 | SSAO + 双边模糊 | 主视图深度 → 半分辨率 AO |
| 5 | HDR Forward / resolve | 材质、点光、方向光、CSM、镜面、AO → RGBA16F 场景与单采样深度 |
| 6 | TAA，可选 | 当前 HDR/深度、上一执行帧颜色/深度 → 新历史 HDR；另存 Depth32F 历史 |
| 7 | 相机运动模糊，可选 | HDR、当前深度、前后未抖动相机矩阵 → HDR 模糊结果 |
| 8 | Bloom | 原始 texel 软阈值与 13 tap 预过滤 → 1..6 层 13 tap 降采样 → 归一化 Tent 上采样与 scatter 混合 |
| 9 | 显示合成 | HDR + Bloom → 曝光 → Reinhard / ACES / 无 tone map → Gamma → 调色/暗角 |
| 10 | 屏幕滤镜 | None / 灰度 / 反相 / 锐化 / 浮雕 / 边缘 / Gaussian / 波纹 → 独立显示目标 |
| 11 | FXAA 或直接复制 | 显示颜色 → 默认 framebuffer；TAA 模式不会再叠加 FXAA |
| 12 | Overlay | 绕过后处理和抗锯齿的覆盖层，使用未抖动相机 |

开启 SSAO 才录制主视图深度预通道；TAA 和相机模糊读取颜色 Pass 之后的场景深度。主场景及镜面支持独立于后处理 AA 模式的 MSAA，后续采样统一使用 resolve 后的单采样纹理。关闭效果时跳过对应绘制和处理；阴影 atlas 与 AO 仍清为合法默认值。Bloom 的每一维至少为 1，在两维均为 1 时停止添加层。历史与显示中间纹理采用独立读写目标，避免纹理反馈回路。

阴影只作用于对应方向光；AO 只作用于环境项。点光、自发光和未被遮挡的其他光源不会被阴影结果整体乘暗。反射视图关闭屏幕 AO，使用反射视角的点光高光和主相机 CSM；镜面不递归采样自身。

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

正式 PBR 模板位于 `Public/Material/pbr_material.h`，模板名 `StandardPbr`，112 字节。保留原金属度/粗糙度/AO 和五贴图模型，增加：

- `emissiveColor`：线性 HDR 自发光 RGB；参数允许超过 1，RGBA16F 写入限制在 60000，避免半浮点溢出。
- `reflectionStrength`：表面镜面采样权重，与管线 reflection.strength 相乘。
- `alphaCutoff`：颜色 Pass 和阴影/深度 Pass 使用同一透明裁剪值。

`MaterialPassResources.shadowPipeline` 是可选深度变体，必须使用相同材质模板/参数布局和纹理。PBR 提供 `PbrShadowVertexShader()`、`PbrShadowFragmentShader()`、`PbrShadowPipelineSpec(program)`。将其 shader/program/pipeline 纳入材质 lifetime。缺少深度变体的对象仍可进入颜色绘制，但不写 CSM 或 SSAO 的深度预通道；透明混合层也不进入深度预通道。

`RenderItem.castsShadows=false` 可关闭投影，`visibleInReflections=false` 可排除镜面自身。接收阴影由颜色 shader 决定。PBR 颜色输出现在是线性 HDR，必须经过正式管线输出转换；Lab 的旧 PBR 头文件保留 using 转发，但其旧 `LabPipeline` 已移除。Unlit 和原有 `ForwardRenderPipeline` 路径仍保留，原默认 framebuffer 使用方式不变。

进入 HDR 场景层的所有颜色 shader 都应输出线性颜色。现有 Unlit/Sprite 不自动解码 sRGB，需提供线性输入或使用相应 shader 变体；屏幕 UI 使用 Overlay 绕过后处理。反射共享主视角 CSM，镜中可见但超出主视角级联覆盖的对象按越界策略不受阴影。

材质域在该管线保留纹理槽 0..7；8..15 为 Pass 预留。使用了保留槽的材质会被明确拒绝。整个材质基础库仍支持 0..15，不影响不使用此管线的旧场景。

| 数据 | 绑定 / 大小 |
| --- | --- |
| View / Object | UBO 0 / 1，80 / 64 字节 |
| Material | UBO 2，PBR 为 112 字节 |
| 点光/环境光/高光抗锯齿 | UBO 3，176 字节；原 0..159 偏移不变，`specularAA` 位于 160；原曝光/outputOptions 仅保留 ABI |
| CSM/镜面/方向光/裁剪/屏幕 AO | UBO 4，544 字节，分 256+256+32 三条 inline update 上传 |
| 后处理 | UBO 5，240 字节；原 0..223 偏移不变，`bloomStability` 位于 224，依次为 soft knee / firefly range / scatter / radius |
| TAA / FXAA / 相机模糊 | UBO 6，384 字节，分 256+128 字节上传 |
| CSM / Reflection / SSAO | texture 12 / 13 / 14 |

全屏 Pass 的纹理槽是局部约定：普通后处理使用 0=输入颜色、1=Bloom/第二图、2=场景深度；Temporal Pass 使用 0=当前颜色、1=历史颜色、2=当前深度、3=上一帧深度。它们在各 Pass 重绑定，不改变材质自身的 0..7 槽契约。

UBO 4 的原 0..495 字节偏移保持不变；末尾新增 `cascadeWorldTexelSize`（496）、`cascadeInverseDepthRange`（512）和 `shadowFilter`（528），各为 vec4。前两个向量分别存四级的世界纹素宽度和光深度跨度倒数；`shadowFilter` 依次存 texel 深度偏差、texel 法线偏差、远距淡出比例、RPDB 安全上限的纹素系数（使用时再乘真实三角面坡度因子）。原 `screenAndAo.w` 的保留分量用于阴影诊断模式，不移动既有字段。

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

CPU 测试 target：`render_target_test`、`msaa_render_target_test`、`scene_effects_test`、`post_process_test`、`material_pipeline_lifecycle_test`、`temporal_effects_test`，覆盖附件契约、模拟 GL 的 resolve/状态恢复、失败清理、CSM/反射数学、544/240/384 字节 ABI 和抖动/设置边界；`material_lab_pbr_test` 覆盖材质与 176 字节 PBR Pass 布局。独立 GPU target 为 `shadow_quality_gpu_test`、`bloom_stability_test`、`ssao_quality_gpu_test`、`pbr_specular_aa_gpu_test`、`temporal_effects_gpu_test`、`material_pipeline_history_test`，分别覆盖阴影采样质量、完整 Bloom 链、SSAO 斜面/接触遮蔽、高光抗锯齿、时域 shader 及 Record/Complete/Discard 历史状态；需要 OpenGL 4.5 上下文。真实 MSAA 绘制与 resolve 由 Lab `--quality-test` 验证。

这些可执行 target 始终创建；上述六项 GPU 测试默认不注册到 CTest，使用 `-DRENDER_EFFECTS_GPU_TESTS=ON` 显式启用。Lab 的 `MATERIAL_LAB_GPU_TESTS` 是独立开关，其中包括 `material_lab_shadow_test` 和 `material_lab_closeup_test`。

MaterialLab `--smoke-test` 验证材质、效果开关、HDR/阴影/AO 附件、resize、透明裁剪、极端发光和 GL 错误；`--quality-test` 验证镜面比例、MSAA resolve、FXAA、TAA 累积及历史失效、静止/运动相机模糊；`--shadow-test` 使用独立阴影场景，检查可见度/级联诊断、2048/4096/8192 atlas 重建恢复、掠射方向光、移动相机和非法偏差参数。指定 `--screenshot FILE.png` 时保存正常图以及同目录的 `FILE_visibility.png`、`FILE_cascades.png`。图像差异测试用于检测功能，不等同于所有场景的视觉质量评价。

`--closeup-test` 使用 640×724 竖幅近景、TAA + MSAA4，支持 `--yaw RADIANS --pitch RADIANS --distance UNITS`。需要 TAA 的配置重新稳定历史，检查静态帧差，并可导出逐步关闭 Bloom、AO、阴影及 TAA/FXAA/MSAA 的隔离截图；末张仍保留 PBR 高光滤波。截图后缀与命令示例见 [MaterialLab 近景验证](../MaterialLab/README.md#近景与效果隔离)。

SSAO 的受控斜面回归中，旧邻居选择产生 1800/28260 个错误法线像素，AO 最低 .479736、均值 .99418；整数邻居修复后，五种偶数/奇数尺寸的最大法线误差小于 .000623，无遮挡 AO 最小值/均值均为 1、列差为 0，同时保留抬升板的接触遮蔽。PBR 高光抗锯齿的解析平滑法线测试中，太阳/点光在 roughness=.045/.08 时的能量相对极差（range/mean）分别由 32.006/17.9057 降至 .182620/.180236，约降低 99%；仍有约 18% 残余波动，不代替 TAA，也不是对真实球体或法线贴图 mip 的专项证明。平法线开关对照和完全粗糙表面保持不变。

本次受控 GPU 回归中，`N·L≈.297/.152` 的无遮挡斜面采用固定 RPDB 上限 4 时平均可见度为 `.963816/.826208`，改为真实三角面坡度自适应后为 `.999968/.999970`（1 表示完全可见）。跨所测五种坡度、256/1024 atlas 与三个亚纹素相位，最差平均值为 `.999965`、最小像素值为 `.999512`。Lab 阴影场景中盒子受光正面区域的平均误遮挡从约 `2.38%` 降至 `0`。这些数据验证了修正上限导致的自阴影问题，不能外推为所有曲面或极端掠射场景无误差。

同一回归的落地接触点可见度为 `0`，抬升对照为 `.999977`；连续移动采样相位与穿越级联混合区时的最大相邻可见度变化分别为 `.004395/.005859`。这些是固定测试几何和采样路径的结果，不是任意运动、光照或尺寸下的误差上限。

当前是固定顺序的多 Pass 前向管线，尚无通用 RenderGraph。CSM 只服务一盏方向光，点光暂不投影；过滤宽度固定，没有 PCSS 接触硬化、VSM 或光线追踪接触阴影。当前定位是可诊断、可调参的实用 CSM，不保证与现代引擎在所有场景下取得相同画质。镜面仅单平面单次反射，无 SSR、水面折射或递归镜面。没有 IBL、物体运动向量、物体运动模糊、景深或 weighted OIT；透明物体沿用排序混合，缺少独立速度/反应遮罩，TAA 对动态透明内容仍有限制。反射图没有粗糙度预过滤，SSAO 无法获知屏外遮挡。

后续增加 Pass 时保持资源读写分离，将目标创建/尺寸依赖放入 Resize，将效果参数放入 settings，将表面属性保留在材质中。新增阴影类型、抗锯齿或透明算法可以继续增添 Pass，无需再把这些逻辑放回 MaterialLab。
