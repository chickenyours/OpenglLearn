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

ctest --test-dir build -R '^material_lab_.*_test$' --output-on-failure
```

GPU 测试默认不注册到 CTest，避免无图形环境失败；可以在 CMake 配置时设置 `-DMATERIAL_LAB_GPU_TESTS=ON`。`bin` 下的截图和本机遗产贴图被仓库忽略，不属于模块必需源文件。

## 场景与操作

上方 3 行 × 5 列材质球：从上往下 metallic 为 `0 / 0.5 / 1`；从左往右 roughness 为 `0.08 / 0.25 / 0.5 / 0.75 / 1`。下方依次是铜、五贴图材质、迁移的遗产瓷砖预设。铜球采用镜像和非均匀缩放，用于观察 TBN 处理；地面展示平面反射和 CSM 阴影，左右两个发光球用于观察 HDR/Bloom。

窗口标题显示所选球编号、参数、`N/M/R/A` 贴图开关和曝光。

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

所有 GPU 资源通过 RHI 异步创建，回调到达后登记所有权。共享的 pipeline/参数 UBO/纹理由一个 lease 持有；逐帧命令保留 lease 和材质快照。材质参数 UBO 可以共享，因为管线在每次材质切换时上传完整快照。示例使用一个在途帧，便于确定性验证，不代表最终性能架构。

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

参数块为 112 字节，新增 emissiveColor、reflectionStrength、alphaCutoff；View/Object/Material/Lighting/SceneEffects/PostProcess 分别使用 UBO 0/1/2/3/4/5。顶点是 position3/normal3/uv2/tangent4，共 48 字节。粗糙度在 shader 内限制到至少 0.045，TBN 处理非均匀缩放及镜像 handedness。

颜色约定：albedo 默认按 sRGB 解码，baseColor 是线性颜色；其他四类贴图按线性数据使用。后处理默认使用旧 `color/(color+0.2)` 色调映射和 gamma=2.2，因此不是旧截图的逐像素复刻。当前 RGBA8 纹理由 shader 做颜色解码，尚无硬件 sRGB 过滤/线性空间 mipmap。管线绑定目标时关闭 `GL_FRAMEBUFFER_SRGB`，保证显示编码只做一次。

## 正式渲染管线

当前流程：四级 CSM → 平面反射 → 主视图深度 → SSAO/双边模糊 → HDR Forward → 多级 Bloom → 曝光/色调映射/Gamma/调色 → 屏幕滤镜 → Overlay。全部效果都由 `Module/Render` 内的正式管线调度，Lab 负责场景和交互。旧 `LabPipeline` 已移除。

通过 `FrameSettings.effects` 设置阴影、反射和后处理参数；Lab 的 `exposure` 覆盖管线曝光，`lightTime` 驱动示例点光源和波纹。CSM 对一盏方向光生效，镜面为单个平面、单次反射；没有 IBL、SSR、水面折射或递归镜面。

详细的资源契约、Pass 输入输出、遗产来源和调用示例见 `Module/Render/README_MATERIAL_PIPELINE.md`。

## 验证内容

- `material_lab_geometry_test`：球/平面的索引、CCW、法线、切线、极点和 UV 接缝；程序纹理尺寸及数据。
- `material_lab_pbr_test`：参数布局、范围、布尔编码、资产默认值、实例隔离、逐对象覆盖及 Pass 结构。
- `render_target_test` / `scene_effects_test` / `post_process_test`：FBO、纹理与命令兼容；CSM/反射数学；后处理配置。
- `--smoke-test`：GPU uniform 布局、实际材质绘制、所有效果开关和滤镜、HDR/阴影/AO 附件内容、奇数/最小尺寸重建、参数编辑与重置、透明裁剪和极端发光；检查 GL 错误。

`async_ExecuteCode` 仅用于此测试模块的 uniform 反射和 framebuffer 读回诊断；材质绘制、数据上传和资源创建都走现有 RHI。
