# 材质渲染管线

`MaterialRenderPipeline` 位于 Render 模块，CMake target 为 `render_effects`。它消费 `RenderFrame` 中的材质快照，组织多 Pass 渲染。MaterialLab 仅提供场景、参数交互及 GPU 验证，不再拥有单独的光照管线。

## 已实现流程

| 顺序 | Pass | 输入与输出 |
| --- | --- | --- |
| 1 | 四级 CSM | 场景深度变体 → Depth32F 2×2 阴影 atlas |
| 2 | 平面反射 | 反射相机、世界空间裁剪平面、CSM → 半分辨率 RGBA16F 反射图 |
| 3 | 主视图深度 | 材质深度变体 → 主视图 Depth32F |
| 4 | SSAO + 双边模糊 | 主视图深度 → 半分辨率 AO |
| 5 | HDR Forward | 材质、点光、方向光、CSM、镜面、AO → RGBA16F 场景 |
| 6 | Bloom | 亮部提取 → 1..6 层双向 Gaussian 降采样 → Tent 上采样叠加 |
| 7 | 显示合成 | HDR + Bloom → 曝光 → Reinhard / ACES / 无 tone map → Gamma → 调色/暗角 |
| 8 | 屏幕滤镜 | None / 灰度 / 反相 / 锐化 / 浮雕 / 边缘 / Gaussian / 波纹 → 默认 framebuffer |
| 9 | Overlay | 绕过 HDR 后处理的覆盖层，使用调用方提供的同一相机 |

开启 SSAO 才录制主视图深度预通道。关闭效果时跳过对应绘制和处理；阴影 atlas 与 AO 仍清为合法默认值，避免读取未初始化资源。Bloom 在尺寸缩小到 1×1 时停止继续添加层。最终显示中间纹理使用独立目标，因此没有读写同一纹理的反馈回路。

阴影只作用于对应方向光；AO 只作用于环境项。点光、自发光和未被遮挡的其他光源不会被阴影结果整体乘暗。反射视图关闭屏幕 AO，使用反射视角的点光高光和主相机 CSM；镜面不递归采样自身。

## 调用与所有权

设备必须已运行。调用方负责窗口、相机、Begin/End/Submit，所有管线 API 在应用线程使用：

```cpp
Render::MaterialRenderPipeline pipeline(device);
if (!pipeline.Initialize()) { /* report pipeline.LastError() */ }
Render::MaterialPipelineSettings settings;
settings.shadows.atlasResolution = 2048; // 每级 1024×1024
settings.post.bloomEnabled = true;
settings.post.toneMap = Render::ToneMapMode::ACES;

pipeline.Resize(width, height, settings.shadows.atlasResolution);
auto encoder = device.BeginFrame(begin);
encoder.KeepAlive(sceneGpuOwner); // 外部 mesh / legacy 资源由调用方保活
if (!pipeline.Record(encoder, sceneFrame, camera, settings) || !encoder.End(true)) {
    encoder.Cancel();
    // report pipeline.LastError()
} else if (!device.async_SubmitFrameCommands(encoder.GetCommandBuffer())) {
    encoder.Cancel();
}
```

`PipelineCamera` 使用 OpenGL [-1,1] 裁剪深度和右手 view/projection；nearPlane/farPlane 必须与 projection 相符。更改视口或阴影分辨率后先调用 Resize。Resize 完整构建新附件后才替换旧资源，已提交帧通过 KeepAlive 保留旧目标；失败时旧目标仍有效。

Record 返回 false 时可能已录制部分 Pass，必须 Cancel 整帧，不应提交部分内容。

管线持有自己的 shader、全屏 mesh、UBO、FBO 和附件。材质快照持有材质 shader/pipeline/纹理；调用方必须单独保活场景 mesh 及 legacy RenderItem.pipeline/texture。这一点也适用于在途帧。Shutdown 管线和释放材质、外部 owners 后，再 StopAndRelease 设备及排空回调，最后销毁窗口。

初始化与 Resize 使用有超时诊断的异步资源等待；失败清理仍会接收未完成创建的回调，保证句柄被回收。禁止与 device.StopAndRelease 并发。它们不是资源流式加载接口，窗口大小连续变化时应由应用合并 resize 请求。

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
| 点光/环境光 | UBO 3，160 字节；原曝光/outputOptions 仅保留 ABI |
| CSM/镜面/方向光/裁剪/屏幕 AO | UBO 4，496 字节，分两条 inline update 上传 |
| 后处理 | UBO 5，224 字节 |
| CSM / Reflection / SSAO | texture 12 / 13 / 14 |

## 技术实现与遗产来源

- **CSM**：参考 `Engine/src/engine/RenderPipe/Pass/CSMpass.cpp`。遗产的五层 depth array/geometry shader 改为四级 2×2 atlas 和四次深度绘制，减少底层新增接口。practical split 默认 lambda=.9，球包围拟合、纹素对齐、光方向 caster padding、3×3 PCF、级联重叠混合。PCF 限制在各自 tile 内。超出 shadowDistance 不采阴影。
- **镜面**：参考 `Engine/src/engine/Resource/RenderPipe/Passes/mirror_pass.h`。任意平面反射矩阵作用于相机，独立场景绘制，世界空间 fragment 裁剪，表面投影采样反射图。PBR 使用双面绘制处理镜像绕序；自定义材质需要提供自己的裁剪逻辑并选择适当剔除规则。
- **Bloom**：参考 `Engine/src/engine/programs/hh.cpp` 与 `bin/shaders/AfterRender/{filter,gauss,upSample,blend}.fs`。保留多级链和 5 tap Gaussian；上采样以 `large + 0.5*small` 控制能量，最终应用一次强度。全部在 tone mapping 前的 HDR 空间计算。
- **SSAO**：参考 `Engine/src/engine/programs/SSAO.cpp` 与 `bin/shaders/SSAO`。改为主视图深度重建位置与法线，32 个确定性半球样本，4×4 旋转模式及深度感知模糊，避免增加 G-buffer 附件。AO 仅衰减环境光。
- **滤镜**：参考 `bin/shaders/depthTest/screenShader.fs` 的反相、灰度、浮雕、Gaussian、边缘核和径向波纹。锐化补充标准核。遗产注释中的 Vignette 实为波纹，这里将暗角作为独立设置。
- **Tone mapping**：旧 PBR 的 `color/(color+0.2)` 与 `bin/shaders/shaderToy/nsea.fs` 的矩阵 ACES fitted。显示编码仅执行一次；无 tone map 模式仍有 Gamma 和 LDR 饱和裁剪。

## 底层兼容扩展

新增 `RenderTargetSpec` / `CreateRenderTargetDesc`、异步创建/删除、帧命令 ID 15 `SetRenderTarget`；旧命令 ID 不变。新增 RGBA16F / Depth32F 和 `mipmaps=false` 空纹理。FBO 借用外部附件，不自行删除附件，创建时验证格式/尺寸/完整性；当前支持一个颜色附件和可选深度附件，也支持 depth-only。

切换目标会设置 viewport、关闭 scissor 与 framebuffer-sRGB、按需清理，并使 pipeline/mesh 状态缓存失效；全屏/场景 Pass 都重新绑定必要状态。活动附件不能同时作为采样输入；失败目标阻止继续绘制到旧目标。资源释放先删除 FBO，再删除其纹理；已录制帧持有资源 owner。

## 验证与当前范围

CPU 测试覆盖 FBO 校验/命令兼容/回收顺序、CSM 分割/覆盖/稳定性/反射数学、后处理参数及各 std140 布局。MaterialLab GPU smoke 验证实际 HDR/阴影/AO 内容、所有效果开关与滤镜图像差异、odd/1×1 resize、材质参数、透明裁剪、极端发光和 GL 错误。

当前是固定顺序的多 Pass 前向管线，便于继续扩展，不是通用 RenderGraph。CSM 仅用于一盏方向光，点光暂不投影；镜面仅单平面单次反射，无 SSR、水面折射或递归镜面。当前没有 IBL、TAA/FXAA、运动模糊、景深或 weighted OIT；透明物体沿用排序混合。反射图没有粗糙度预过滤，近镜面效果为主。SSAO 为屏幕空间近似，无法获知屏外遮挡。

后续增加 Pass 时保持资源读写分离，将目标创建/尺寸依赖放入 Resize，将效果参数放入 settings，将表面属性保留在材质中。新增阴影类型、抗锯齿或透明算法可以继续增添 Pass，无需再把这些逻辑放回 MaterialLab。
