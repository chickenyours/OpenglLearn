# MaterialLab：新材质系统的独立试验模块

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

ctest --test-dir build -R '^material_lab_.*_test$' --output-on-failure
```

Lab 的四项 GPU 测试默认不注册到 CTest；配置 `-DMATERIAL_LAB_GPU_TESTS=ON` 后增加 `material_lab_gpu_test`、`material_lab_quality_test`、`material_lab_shadow_test` 和 `material_lab_closeup_test`。四种测试模式均使用隐藏窗口，默认采用程序纹理。Render 的六项独立 GPU 测试（包括新增 `ssao_quality_gpu_test`、`pbr_specular_aa_gpu_test`）通过另一个默认关闭的开关 `-DRENDER_EFFECTS_GPU_TESTS=ON` 注册；对应可执行 target 始终可构建、可单独运行，需要 OpenGL 4.5。`bin` 下截图和本机遗产贴图被仓库忽略，不属于模块必需源文件。

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

## 场景与操作

上方 3 行 × 5 列材质球：从上往下 metallic 为 `0 / 0.5 / 1`；从左往右 roughness 为 `0.08 / 0.25 / 0.5 / 0.75 / 1`。下方依次是铜、五贴图材质、迁移的遗产瓷砖预设，底排这三个材质球贴地。铜球采用镜像和非均匀缩放，用于观察 TBN 处理；地面展示平面反射和 CSM 阴影，左右两个发光球用于观察 HDR/Bloom。上方网格球和发光球按展示布局悬空；判断接触漂浮应观察底排或独立阴影场景。

按 G 进入独立阴影场景：接触地面的球与盒子、薄遮挡物、15° 斜面和曲面接收者，用于分辨接触漂浮、表面条纹和过滤质量。此模式关闭镜面和点光，只保留方向光与环境项；也可直接设置 `FrameSettings.shadowStudy=true`。`--shadow-test` 还关闭 Bloom/SSAO 和初始 AA，避免其他效果掩盖阴影问题。

交互模式默认使用 **TAA + MSAA4 + 全分辨率镜面**，相机运动模糊关闭，点光动画暂停。通用 Render API 的默认值仍是 FXAA + MSAA1，Lab 在交互入口显式覆盖。窗口标题显示所选球参数、`N/M/R/A` 贴图开关、曝光、AA、MSAA、模糊和镜面比例。

| 操作 | 效果 |
| --- | --- |
| Tab / Shift+Tab | 下一个 / 上一个材质 |
| F | 切换单球观察与完整场景 |
| ← / → | 减少 / 增加粗糙度，同时关闭粗糙度贴图以使用标量 |
| ↓ / ↑ | 减少 / 增加金属度，同时关闭金属度贴图以使用标量 |
| N | 切换法线贴图 |
| T | 同时切换金属度、粗糙度、AO 贴图 |
| R | 恢复当前材质的资产默认值和开关 |
| PageUp / PageDown | 调整管线曝光 |
| Space | 暂停 / 播放点光源旋转 |
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
// pass 还需有效 pipeline、112字节参数UBO、5个纹理绑定和GPU资源owner。
// 设置 pass.shadowPipeline 为 PbrShadowPipelineSpec 编译的深度变体，参与阴影/SSAO。
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

所有 GPU 资源通过 RHI 异步创建，回调到达后登记所有权。共享的 pipeline/参数 UBO/纹理由一个 lease 持有；逐帧命令保留 lease 和材质快照。材质参数 UBO 可以共享，因为管线在每次材质切换时上传完整快照。当前管线要求一个待确认帧：Lab 的 `Render` 使用可靠提交，等待执行完成回调后在应用线程调用 `CompleteFrame(token)`，才允许下一帧使用历史；未提交失败调用 encoder.Cancel 和 `DiscardFrame(token)`。没有把可能被覆盖的 latest 帧当成历史。

## 遗产 PBR 的对应关系

源实现：`Engine/src/engine/Resource/Material/Interfaces/BPR.h` 与 `bin/shaders/Final/PBR/20250719/PBRStaticLights.fs`。保留 Cook–Torrance / GGX / Smith / Schlick、F0=0.05、四点光逆平方衰减及 AO 仅作用于环境项的计算方式。

| 遗产表达 | 新表达 |
| --- | --- |
| IBPR::Property，binding 32 | 模板内 metallic / roughness / ao，生成的 MaterialData，binding 2 |
| USE_NORMAL/METALLIC/ROUGHNESS/AO_MAP 宏 | 实例布尔参数，可实时切换 |
| albedo / normal / metallic / roughness / AO map | 必需纹理槽 0 / 1 / 2 / 3 / 4 |
| 着色器内的点光数组、时间 | Pass UBO 3 的灯光位置/颜色；CPU 更新动画 |
| cmp.values[0] 的颜色控制 | baseColor 线性颜色因子，参与光照计算 |
| fragment 内色调映射 | PBR 输出线性 HDR，统一后处理 Pass 完成曝光、色调映射和 Gamma |

瓷砖预设显式迁移自本机 `bin/materials/BPR/hh.json`：metallic=0.6、roughness=0.5、ao=1，使用 normal/roughness/AO 贴图，metallic 使用标量。当前没有通用旧 JSON 导入器；新材质通过独立的 shadowPipeline 变体参与阴影。

参数块为 112 字节，新增 emissiveColor、reflectionStrength、alphaCutoff；View/Object/Material/Lighting/SceneEffects/PostProcess/Temporal 分别使用 UBO 0/1/2/3/4/5/6。光照 UBO3 为 176 字节，原 0..159 偏移不变，末尾 160 的 `specularAA` 依次存法线方差比例、最大新增 GGX `alpha²`、启用标志和保留值，默认 `{.15,.20,1,0}`。场景 UBO4 为 544 字节，保留原 0..495 偏移，追加 496 的 `cascadeWorldTexelSize`、512 的 `cascadeInverseDepthRange` 和 528 的 `shadowFilter`，按 256+256+32 三段上传。后处理 UBO5 为 240 字节（末尾 224 偏移为 `bloomStability`，依次为 soft knee / firefly range / scatter / radius），时域 UBO6 为 384 字节。顶点是 position3/normal3/uv2/tangent4，共 48 字节。粗糙度在 shader 内限制到至少 0.045，TBN 处理非均匀缩放及镜像 handedness。

高光抗锯齿在 PBR 着色时根据法线屏幕导数对 NDF 做有界滤波，方差加到感知粗糙度的四次方后再换算，避免通过硬截亮度掩盖微小高光。它不改材质中保存的粗糙度，独立于 TAA、MSAA 和 Bloom；`frame.effects.lighting.specularAA.z=0` 可关闭。实现参考 [Tokuyoshi / Kaplanyan 2019 的保守各向同性滤波](https://yusuketokuyoshi.com/papers/2019/ImprovedGeometricSpecularAA.pdf)，不包含法线贴图 mip 方差预过滤，也不修复轮廓覆盖或阴影终止线几何问题。

颜色约定：albedo 默认按 sRGB 解码，baseColor 是线性颜色；其他四类贴图按线性数据使用。后处理默认使用旧 `color/(color+0.2)` 色调映射和 gamma=2.2，因此不是旧截图的逐像素复刻。当前 RGBA8 纹理由 shader 做颜色解码，尚无硬件 sRGB 过滤/线性空间 mipmap。管线绑定目标时关闭 `GL_FRAMEBUFFER_SRGB`，保证显示编码只做一次。

## 正式渲染管线

当前完整顺序：四级 CSM → 平面反射及 MSAA resolve → SSAO 所需主视图深度/resolve → SSAO/双边模糊 → HDR Forward 与颜色/深度 resolve → 可选 TAA/历史深度保存 → 可选相机模糊 → 多级 Bloom → 曝光/色调映射/Gamma/调色 → 屏幕滤镜 → FXAA 或复制到屏幕 → Overlay。TAA 和 FXAA 二选一，MSAA 可独立叠加。全部效果由 `Module/Render` 调度，Lab 负责场景与交互；旧 `LabPipeline` 已移除。

通过 `FrameSettings.effects` 设置阴影、反射、后处理和时域参数；Lab 的 `exposure` 覆盖管线曝光，`lightTime` 驱动示例点光源和波纹。例如：

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
frame.effects.deltaSeconds = elapsedSeconds;    // 实际帧间隔，有限且 > 0
frame.effects.cameraCut = didTeleport;          // 跳切只置位该帧
if (!lab.Render(frame)) throw std::runtime_error(lab.LastError());
// Render 内部完成匹配尺寸/MSAA/镜面比例的 Resize、可靠提交和 CompleteFrame。
```

直接使用 Render 管线时，`Resize(width, height, shadowResolution, msaaSamples, reflectionScale)` 必须与 settings 匹配。镜面每维为 `ceil(viewport * resolutionScale)`，上限 8192；MSAA 同时应用于主场景和镜面。材质使用纹理槽 0..7，CSM/镜面/AO 占 12/13/14。全屏后处理局部绑定 0=输入、1=第二图、2=深度；TAA 局部绑定 0=当前颜色、1=历史颜色、2=当前深度、3=历史深度。

TAA 使用相机/深度重投影、8 帧 Halton 抖动、邻域约束与亮度反应，没有 object velocity。材质参数、模型/几何句柄和纹理句柄等内容 hash 改变会拒绝旧历史；点光动画不参与每帧 hash 重置，由亮度变化降低历史权重。原地更新贴图或 mesh 内容时，直接管线调用方应在帧间调用 `ResetHistory()`。相机跳切、projection/尺寸变化和较长帧间隔也会清历史。动态物体和透明内容不能依靠当前 TAA 获得完整运动补偿。

相机模糊默认 shutter=1/120 秒、strength=1、12 个样本和 32 像素总跨度上限。实际速度乘以 `shutter / deltaSeconds`，使用未抖动矩阵以排除 TAA jitter；背景与静止相机保持不变。它不处理物体自身运动。FXAA、TAA、模糊的实现范围及对应一手资料见[管线文档](../Render/README_MATERIAL_PIPELINE.md)。

CSM 默认 4096 atlas、四级各 2048×2048，practical split 的 lambda=.65。CPU 使用 double 稳定球拟合与纹素对齐，每边留 3 个过滤纹素及半纹素对齐余量；Light Z 紧贴接收者范围，并向光源上游扩展 `casterPadding=30` 世界单位。采样为 16 tap 相位连续 tent PCF，每 tap 按接收面导数修正深度；几何法线偏移与太阳终止处理不使用法线贴图。最后 10% 阴影距离平滑淡出，超距不采阴影。算法依据见 [Microsoft CSM 的逐纹素深度偏差](https://learn.microsoft.com/en-us/windows/win32/dxtecharts/cascaded-shadow-maps#calculating-a-per-texel-depth-bias-with-ddx-and-ddy-for-large-pcfs) 和 [GPU Gems 的 PCF](https://developer.nvidia.com/gpugems/gpugems/part-ii-lighting-and-shadows/chapter-11-shadow-map-antialiasing)。

`depthBiasTexels=.05`、`normalBiasTexels=.20` 以各级联的世界纹素尺寸计量，分辨率改变时自动换算；旧 `depthBias=.000002` 是额外归一化深度偏差，`normalBias=0` 是额外世界距离，不要混用单位。`receiverPlaneClampTexels=4` 是 RPDB 安全上限的纹素系数，还会乘真实三角面坡度因子 `max(1,tan(phi))`，避免截断陡斜面需要的逐 tap 修正；它不增加常量偏差，设为 0 则关闭 RPDB。极端掠射或退化导数使用有界回退。遇到漂浮先减小偏差，遇到表面条纹先检查级联覆盖、分辨率和几何，再小幅调整 texel 参数；增大世界偏移会直接扩大接触间隙。完整参数、RPDB 回退和上限见[CSM 调参说明](../Render/README_MATERIAL_PIPELINE.md#csm-质量与调参)。J 的 Visibility 用白/黑显示可见/遮挡，Cascades 显示级联颜色；这些诊断关闭输出调色、Bloom 和屏幕滤镜。检查原始阴影时可用 A 关闭 AA。

Bloom 默认 `bloomSoftKnee=.5`、`bloomFireflyClamp=0`、`bloomScatter=.8`、`bloomRadius=1.25`。首级在源 texel 上应用软阈值后进行双线性/13 tap 过滤，后续使用归一化 13 tap 降采样。上采样改为 `mix(large, normalizedTent(small), scatter)`；scatter（0..1）控制较宽光晕的比例，radius（.5..2）以较小图的 texel 计量。每级权重和为 1，层数控制分布、strength 控制最终强度，原始 HDR 图保持清晰。思路参考 [Unity 官方 Bloom 的 scatter 混合](https://github.com/Unity-Technologies/Graphics/blob/master/Packages/com.unity.render-pipelines.universal/Shaders/PostProcessing/Bloom.shader)。

完整 GPU 五层回归中，相同积分能量下的光晕 RMS 半径由 6.773178 增至 20.396257 个半分辨率 Bloom texel，中心半径 4 texel 内的能量占比由 .766096 降至 .360349。恒定 HDR 100 在 1..6 层均输出 99.1875，六层亮点保留首层积分能量的 .997811；32/128/1500 三档亮点连续移动时，对齐后的光晕形状变化降低约 58%，完整链额外积分能量偏差最多约 .22%。这些数据证明受控光晕重建更柔和、形状更稳定，不代表消除了着色源本身的闪烁。

原提取阶段的 33 相位测试仍保留，三档能量变异系数相对旧提取降低约 88.1%/89.8%/87.9%。正值 `bloomFireflyClamp` 是可选局部异常亮点抑制，会改变移动高光的能量，默认保持关闭；恒定 HDR 不受其限幅。完整指标、[Unity 13 tap](https://github.com/Unity-Technologies/PostProcessing/blob/v2/PostProcessing/Shaders/Sampling.hlsl) 与 [Karis 加权](https://graphicrants.blogspot.com/2013/12/tone-mapping.html)来源见管线文档。

SSAO 法线重建现在以整数深度像素选择左右上下邻居，再用各自真实 texel 中心重建位置。这样避免半分辨率采样落在全分辨率像素边界时，UV 浮点加减与 floor 选中重复/跨越邻居造成的斜面暗条纹。它仍保留深度感知模糊和真实接触遮蔽，且只作用于环境光。

CSM 只服务一盏方向光，采用固定宽度过滤，没有 PCSS、VSM 或光线追踪接触阴影；当前目标是实用 CSM 质量，不承诺与现代引擎在所有场景下完全一致。镜面为单个平面、单次反射；没有 IBL、SSR、水面折射、递归镜面、物体速度或粗糙度反射预过滤。完整的 `Record → End → Submit → CompleteFrame` 与失败 `Cancel + DiscardFrame` 示例见[管线调用与所有权](../Render/README_MATERIAL_PIPELINE.md#调用与所有权)。

## 验证内容

- `material_lab_geometry_test`：球/平面的索引、CCW、法线、切线、极点和 UV 接缝；程序纹理尺寸及数据。
- `material_lab_pbr_test`：参数布局、范围、布尔编码、资产默认值、实例隔离、逐对象覆盖及 Pass 结构。
- `render_target_test` / `scene_effects_test` / `post_process_test`：FBO、纹理与命令兼容；CSM 稳定拟合、guard/尺度与反射数学、544 字节场景 ABI；240 字节后处理配置。
- `material_pipeline_lifecycle_test`：管线资源失败清理与所有权。
- `material_pipeline_history_test`：真实 GPU 管线的串行 token、Complete/Discard 与历史失效。
- `msaa_render_target_test`：CPU 模拟 GL，检查多采样附件与 resolve 契约、状态恢复；真实 MSAA 由 `--quality-test` 验证。
- `temporal_effects_test`：AA/模糊参数、Halton 抖动及 384 字节 ABI。
- `bloom_stability_test`：真实 GPU 提取及完整 1..6 层重建，恒定 HDR、奇数/1×N/边缘输入、归一化能量、光晕半径与中心能量占比；三档亮点各 33 相位，检查能量及对齐后形状变化。
- `ssao_quality_gpu_test`：正式 SSAO 法线重建与 AO shader，偶数/奇数尺寸的解析斜面法线、无遮挡面无条纹、抬升板接触遮蔽、关闭/背景输出白色。
- `pbr_specular_aa_gpu_test`：正式 PBR shader 的解析平滑法线区域，太阳/点光与低粗糙度的 33 相位高光变化、平法线开关不变、粗糙表面及 176 字节 UBO；不作为真实球体或法线贴图 mip 的针对性证明。
- `temporal_effects_gpu_test`：独立 GPU 的 FXAA、TAA 重投影/拒绝和相机模糊验证。
- `shadow_quality_gpu_test`：独立 GPU 的阴影采样质量回归，复用正式 CSM shader helper；覆盖真实光栅斜面的自阴影、256/1024 atlas 与多个亚纹素相位、接触/抬升对照及级联过渡。
- `--smoke-test`：GPU uniform 布局、实际材质绘制、所有效果开关和滤镜、HDR/阴影/AO 附件内容、奇数/最小尺寸重建、参数编辑与重置、透明裁剪和极端发光；检查 GL 错误。
- `--quality-test`：全分辨率/2×镜面、MSAA4 resolve、FXAA 图像响应、TAA 相对 history-off 的稳定性、材质编辑/跳切/resize 的历史失效，以及相机静止不糊、运动才糊；检查 GL 错误。
- `--shadow-test`：接触/斜面/薄遮挡物场景、可见度与级联诊断、2048/4096/8192 atlas 重建恢复、掠射方向光、移动相机及非法偏差参数。指定截图路径时额外保存 `_visibility.png`、`_cascades.png`；对应 CTest 名为 `material_lab_shadow_test`。
- `--closeup-test`：640×724 竖幅近景、可配置相机、TAA + MSAA4 静态稳定性，以及逐步关闭效果的截图；对应 CTest 名为 `material_lab_closeup_test`。

本次 GPU 斜面回归中，坡度自适应 RPDB 在所测五种坡度、两种 atlas 尺寸与三个亚纹素相位下的最差平均可见度为 `.999965`，最小像素值为 `.999512`；阴影场景中盒子受光正面的平均误遮挡由约 `2.38%` 降至 `0`。结果限定于这些测试场景，详细对照见[管线验证记录](../Render/README_MATERIAL_PIPELINE.md#验证与当前范围)。

SSAO 的五种尺寸斜面回归中，整数邻居修复后的最大法线误差小于 .000623，无遮挡面的 AO 最小值/均值均为 1、列差为 0。高光抗锯齿的解析平滑法线测试中，太阳/点光在 roughness=.045/.08 时的能量相对极差分别由 32.006/17.9057 降至 .182620/.180236，仍有约 18% 残余波动；这些受控结果不代表所有曲面、法线贴图或相机运动均无闪烁。

独立 GPU target 可单独构建运行：

```powershell
cmake --build build --target shadow_quality_gpu_test bloom_stability_test ssao_quality_gpu_test pbr_specular_aa_gpu_test temporal_effects_gpu_test --parallel 4
.\bin\shadow_quality_gpu_test.exe
.\bin\bloom_stability_test.exe
.\bin\ssao_quality_gpu_test.exe
.\bin\pbr_specular_aa_gpu_test.exe
.\bin\temporal_effects_gpu_test.exe
```

`async_ExecuteCode` 仅用于此测试模块的 uniform 反射和 framebuffer 读回诊断；材质绘制、数据上传和资源创建都走现有 RHI。
