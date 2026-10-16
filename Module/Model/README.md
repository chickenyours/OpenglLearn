# Model：静态网格导入与 PBR 渲染

骨骼网格、动画序列、IK 中间资产生成与预览编辑器见 [SKELETAL_ASSETS.md](SKELETAL_ASSETS.md)。运行 `Module/Model/RunSkeletalAssetEditor.cmd` 可打开资产树、骨架、动画时间轴、表情、IK 和重映射详情。

独立裙摆网格、服装插槽、XPBD 布料与身体碰撞见 [CLOTH.md](CLOTH.md)。生成示例后运行 `Module/Model/RunMahiroCloth.cmd`，可在同一预览器检查和替换服装。

`model_importer` 在 CPU 导入模型，不需要窗口或图形上下文。`model_module` 实现 `IModule`，借用已启动的 `Render::RHIDevice`，将模型交给现有 Material 材质系统和 Render 渲染接口。模块不持有窗口，不创建独立渲染循环。

目前通过 Assimp 导入 `.fbx`、`.obj`、`.pmx` 等格式；实际格式列表由 `StaticModelLoader::SupportedExtensions()` 返回，取决于所链接 Assimp 的导入器。传入模型文件或只含一个支持模型的目录均可。目录有多个候选文件时会返回错误，调用方应明确文件名。

## 构建与运行

在仓库根目录使用现有 Debug 配置：

```powershell
cmake -S . -B build -G Ninja "-DCMAKE_TOOLCHAIN_FILE=clang-msvc.cmake"
cmake --build build --target model_viewer static_model_test gltf_material_test model_lifecycle_test
ctest --test-dir build -R "static_model_test|gltf_material_test|model_lifecycle_test" --output-on-failure
bin/model_viewer.exe
```

查看器默认加载 `Asset/Model/Rikka_Uniform`，自动按边界居中并适配视距。按住鼠标左键或右键拖动旋转视角，滚轮缩放，R 重置，Esc 退出。自己的模型使用 `--model`：

`--toon` 启用独立二次元渲染管线，按 **T** 可在 PBR 与二次元效果间切换。也可运行 `Module/Model/RunRikkaToon.cmd`；分段光照、描边、白背景及地面阴影的用法与参数见 [TOON.md](TOON.md)。

```powershell
bin/model_viewer.exe --model "Asset/Model/Rikka_Uniform/Rikka_Uniform.pmx"
bin/model_viewer.exe --smoke-test --screenshot build/model_rikka.png
bin/model_viewer.exe --model "C:/Models/scene.fbx" --frames 120 --screenshot build/scene.png
```

`--smoke-test` 使用隐藏的 OpenGL 窗口，默认渲染三帧。检查真实模型相对空场景有足够可见像素、PBR 场景绘制、1e12 世界坐标原点重定位，并在最后一帧释放应用持有的模型，验证已提取材质快照仍保留网格与纹理。`--screenshot` 可保存 PNG；`--frames N` 可限定运行帧数。该检查需要可用 OpenGL 设备，普通 CPU 测试不需要窗口。

配置 `MODEL_GPU_TESTS=ON` 后，CTest 会额外注册 `model_rikka_gpu_test`。`model_lifecycle_test` 则检查未启动设备/模块的错误与生命周期约定。

Model 使用 `Model::Assimp` 依赖目标，优先选择 `find_package(assimp 5.4 CONFIG)`，其次使用 `MODEL_ASSIMP_SOURCE_DIR` 指定的 Assimp 源码。也可显式启用 `MODEL_ASSIMP_FETCH=ON` 下载固定的 Assimp 6.0.5。源码方式默认启用 `MODEL_ASSIMP_DOUBLE_PRECISION=ON`，以 double `ai_real` 解码支持该精度的源数据，并向导入器传递匹配的宏与头文件。仓库现有 Debug Assimp 5.4.3 导入库与 DLL 可作为本机 Debug 回退。Release 需要匹配构建配置、运行时库和架构的外部 Assimp 库或源码，不能复用 Debug DLL。

Assimp 6.0.5 原版的 glTF 读取器会把 FLOAT 字节直接复制到 double 容器，损坏位置、法线、表情等数据。本项目对固定的 FetchContent 6.0.5 缓存自动应用兼容补丁；显式 `MODEL_ASSIMP_SOURCE_DIR` 只检查、不修改，未打补丁的 6.0.5 双精度源码会在配置时明确报错。对自己提供的可写 **6.0.5** 源码，先显式运行下列补丁命令，再构建预览器。脚本核对版本和官方文件 SHA256，重复运行仅校验；`-DCHECK_ONLY=ON` 可单独只读检查。其他版本不会自动套用这个补丁或被这项版本检查拦截，但仍应运行导入回归检查：

```powershell
cmake "-DASSIMP_SOURCE_DIR=C:/Dependencies/assimp-6.0.5" -P cmake/PatchAssimp605GltfDouble.cmake
cmake --preset windows-release "-DMODEL_ASSIMP_SOURCE_DIR=C:/Dependencies/assimp-6.0.5" -DMODEL_GPU_TESTS=ON
cmake --build --preset windows-release --target skeletal_asset_editor model_viewer static_model_test skeletal_import_test cloth_solver_test
ctest --test-dir build/windows-release -R "^(static_model_test|skeletal_import_test|cloth_solver_test)$" --output-on-failure
.\output\Release\bin\skeletal_asset_editor.exe --character Asset/Generated/MahiroCloth/Character.asset.json
```

`MODEL_BUILD=OFF` 可关闭模块。找不到可用依赖时，配置阶段会警告并关闭 Model 目标，其余项目仍可构建。测试通过 `MODEL_SOURCE_DIR` 定位真实资源，并在 `build/model_test_fixtures/` 下为每次运行创建独立目录，保留 fixture 供排查。

## 使用方式

只做导入或离线检查：

```cpp
#include "Model/Public/static_model.h"

std::string error;
Model::LoadOptions options;
options.strictTextures = false;
auto source = Model::StaticModelLoader::Load(
    std::filesystem::path(L"Asset/Model/Rikka_Uniform"), options, &error);
if (!source) { /* 输出 error */ }
// source->meshes / instances / materials / textures / bounds / warnings
```

已有 Render 设备的应用可直接加载与提交：

```cpp
#include "Model/Public/model_module.h"

Model::ModelModule models(device);
models.Startup();
auto character = models.Load(std::filesystem::path(L"Asset/Model/Rikka_Uniform"));
// 失败时读取 models.LastError()；成功后可读取 Source().warnings。

Render::RenderFrame frame;
if (character) {
    character->AppendTo(frame); // 按源节点层级生成 RenderItem 和材质快照
    // 使用现有 MaterialRenderPipeline 录制 frame 到 encoder。
    character->KeepAlive(encoder); // 提交前保留资源，直到该帧执行完成
}
// 先结束并释放提交帧，再释放 character，最后关闭设备。
```

`ModelModule` 不拥有设备。设备必须晚于 `ModelResource`、材质快照和所有已提交帧销毁。加载、上传及追加帧在应用线程调用，与 MaterialService/RHI 的线程约定一致。

## Material 与 Render 的衔接

每个源材质生成 PBR 参数，再经现有 Material 模板、材质资产、MaterialService 实例和 MaterialSnapshot 进入 Render 流水线。`Materials()` 与 `MaterialHandles()` 允许应用继续编辑运行时材质。渲染使用 RHI 顶点/索引缓冲和 UInt32 索引，不经过旧 Engine 模型加载类。

导入器解析基础色、法线、金属度、粗糙度、AO、自发光、透明度、双面标记及常见贴图引用。传统 diffuse/shininess 材质转换成近似的金属度/粗糙度 PBR 材质；不会由一张颜色贴图推测真实物理参数。ORM 等打包纹理按通道拆分，并将标量因子合入标量贴图，以适配现有 PBR shader 的“贴图或常量”规则。基础色贴图使用 sRGB，数据贴图使用线性空间。

外部贴图以模型目录、源引用及 `textureSearchPaths` 查找，也支持导入器返回的嵌入贴图。解码图像保留顶行在前的顺序；一般格式默认翻转 V，PMX 使用其原生 UV。可用 `flipUVs=false` 调整一般格式。缺图或无法转换的贴图默认写入 `warnings` 并使用中性材质回退；`strictTextures=true` 则使加载失败。

## 精度与当前范围

导入不主动减面、量化或降低几何细节；保留独立材质组、节点层级、实例变换、源法线和 UV，缺失时生成法线/切线。CPU 位置、节点矩阵及边界使用 double，索引使用 UInt32，因此超过 65535 顶点的网格也无需截断。这里的高精度指保留可取得的数据与几何细节；当前仓库自带的 Assimp DLL 使用单精度 `ai_real`，已经在文件解码阶段损失的小数无法通过转换为 double 恢复。需要更高源坐标精度时，使用上述 Assimp 源码构建选项；源文件本身使用 float 的数据仍受其原始精度限制。

GPU 使用 Render 现有 float 顶点和矩阵。`AppendTo(frame, placement, renderOrigin, view)` 在转换为 float 前，以 double 计算源节点与 placement 变换并减去 `renderOrigin`，用于大世界坐标的相机相对渲染。相机 view、灯光和其他物体必须采用相同原点。

当前是静态网格：骨骼模型显示导入的静态姿态，通常为绑定姿态；不播放动画，不执行蒙皮或表情，也不模拟 PMX 物理。MMD 的 toon、描边、sphere map 及物理参数无法与 PBR 一一对应，只能得到静态 PBR 近似效果。复杂 DCC 节点材质也受 Assimp 可导出的参数和现有 Material shader 功能限制。原模型的单位与坐标保留，应用可用 placement 调整缩放与朝向。

真实 Rikka 回归检查全部 16 个材质组、8745 个三角面、2048×1024 贴图以及原生 UV。另有 OBJ/MTL 非对称 UV、源法线、切线和纹理方向、FBX 父子层级与实例边界、UInt32 大网格、Unicode 路径、目录选择和严格缺图行为的 CPU 检查。独立的 `gltf_material_test` 使用嵌入 buffer/PNG 验证 ORM 通道、AO 强度、自发光与透明度。
