# 骨骼中间资产与预览编辑器

这个功能在现有 Model 模块中增加 CPU 导入、独立中间资产包、骨骼/表情求值、IK 与重映射基础设施，以及引擎预览窗口。中间资产包包含渲染所需的网格、权重、材质与已解码纹理，加载包时不需要原始 PMX/FBX 或贴图目录继续存在。

它采用类似 UE/Unity 的资产分离方式，但格式是本项目自有格式，不是 `.uasset`、`.prefab` 或 Unity AssetBundle。

`.blend` 通过本机 Blender 后台桥接导入，并保留按材质 section 生成的身体、头发、衣物和饰品分类。女仆爱丽丝示例、正式启动入口和手动核对流程见 [BLEND_IMPORT.md](BLEND_IMPORT.md)。分类结果作为 `Package.asset.json` 的可选 `meshRegions` 保存，不会自动生成或启用布料求解器。

## 构建与生成

沿用项目当前的 CMake 构建目录：

```powershell
cmake -S . -B build
cmake --build build --target skeletal_asset_import skeletal_animation_import skeletal_asset_editor skeletal_asset_io_test skeletal_runtime_test skeletal_import_test animation_asset_io_test vmd_import_test animation_bake_test
```

当前 Debug 默认输出到 `bin`。使用 `windows-release` preset 时，输出位于 `output/Release/bin`。

将真寻 PMX 转换到一个新目录：

```powershell
.\bin\skeletal_asset_import.exe `
  --source "C:\Users\16620\Downloads\真寻酱（女仆装）_by_Sa（艾拉漫剪）_07df9bce2f9eeb31bc903e14f16e8582\真寻酱（女仆装）\真寻酱（女仆装）.pmx" `
  --output ".\Asset\Generated\MahiroMaid"
```

输出目录必须不存在或为空。生成器在同一父目录创建独立 candidate，完成写入并重新加载校验后，再重命名为最终目录；不会覆盖已有非空目录。重新导入时选择新目录，然后在预览器中打开新包。

查看一个包的统计与诊断，无需创建渲染窗口：

```powershell
.\bin\skeletal_asset_import.exe --inspect ".\Asset\Generated\MahiroMaid\Package.asset.json"
```

打开独立资产预览器：

```powershell
.\bin\skeletal_asset_editor.exe --package ".\Asset\Generated\MahiroMaid\Package.asset.json"
```

预览器使用现有 Material、Toon Render、RHI/OpenGL 模块显示模型。未传入启动参数时，优先打开 `Asset/Generated/MahiroMaid/Package.asset.json`；该包不存在时导入项目自带的 Rikka PMX。

## 预览窗口操作

窗口左侧是资产列表与骨骼树，中间是模型和时间线，右侧是当前所选资产的数据详情。

| 操作位置 | 可以查看或执行的操作 |
|---|---|
| 顶部 `Import source` / `Open package` | 导入源模型，或打开已有 `Package.asset.json` |
| 顶部 `Import VMD` | 输入动作文件或目录，可额外指定表情 VMD，设置采样 FPS、平移比例及 IK/append 烘焙后导入 |
| 顶部 `Open animation` | 加载已经绑定当前角色的独立 `.anim.json`，不再需要源 VMD |
| 中间模型窗口 | 按住右键拖动旋转相机；滚轮缩放；点击骨骼辅助点选择骨骼 |
| `Bones` / `Names` / `Frame model` | 切换骨骼线和名称显示，或恢复完整模型的取景 |
| 左侧骨骼树 | 查看层级、按原名或英文名搜索；右侧显示父节点、参考/当前 TRS、全局位置、绑定矩阵、源标志和受影响顶点数 |
| `Focus selected bone` | 将相机焦点移到当前骨骼，便于检查局部变形 |
| `Skeletal Mesh` | 查看子网格统计；选择网格和顶点，检查位置、UV、源顶点 ID、蒙皮模式及最多四项骨骼/权重 |
| `Animation Sequences` | 选择当前包的动画；右侧显示来源、时长、FPS、骨骼 T/四元数/Scale 与 Morph 曲线，可导出独立动画 |
| `Morph Targets / 表情` | 用滑块混合表情；悬停名称查看英文名、增量数量、源类型和面板编号 |
| `IK Rig / 识别与约束` | 选择 IK 链、移动预览目标，并检查语义角色映射、置信度和识别方法 |
| `Retarget / 动画重映射` | 打开另一资产包作为动画来源，构建映射并选择源动画预览，或烘焙到当前目标骨架 |
| `Diagnostics / 导入报告` | 查看资产 ID、版本、源路径、物理数量和未支持特性的诊断 |
| `Export revision` | 将当前资产及修正后的语义映射导出到新目录或现有空目录 |

时间线支持播放/暂停、循环和拖动时间，拖动后暂停在选定时刻。`Bind pose` 会停止播放和重映射预览，关闭预览 IK 并清除手动表情，恢复纯绑定姿态。按 Esc 退出窗口。

打开模型后默认选择生成的 `RigPreview`（若包内存在），否则使用第一条序列；用启动参数导入 VMD 或独立动画时会选中新动作。时间线及详情面板区分程序生成、源动作与烘焙序列。动画详情提供骨骼 Translation、Quaternion、Scale 和表情曲线，悬停显示数值，点击曲线定位播放时刻；关键帧列表按可见范围绘制，适合查看烘焙后的密集采样。展开骨骼节点可以逐项读取 T/R/S 关键帧时间和值；旋转四元数在 Inspector 中按 `w, x, y, z` 显示。当前 Inspector 提供关键帧检查，不提供关键帧增删编辑。

每个表情的 `Manual override` 复选框控制是否覆盖动画曲线，拖动滑块会自动开启覆盖；开启时零值也能强制关闭该表情。取消覆盖或点击 `Reset all / 重置` 后恢复动画控制。预览 IK 目标、手动表情和当前播放时间都是查看状态；`Export revision` 保存资产数据和语义映射修正，不会把这些查看状态烘焙为新动画。VMD 导入和 `Bake sequence to target` 则显式生成新的动画序列；在动画详情点击 `Export this animation`，再通过 `Save animation` 导出选中的独立动画文件。

## 资产结构

```text
MahiroMaid/
  Package.asset.json                    包清单、版本、来源、稳定 ID、诊断和依赖引用
  Skeletons/
    model.skeleton.json                 骨骼层级、局部参考姿态、逆绑定矩阵、PMX 标志/轴/继承信息
  SkeletalMeshes/
    model.skmesh.json                    网格范围、子网格、材质、纹理、蒙皮和 Morph 元数据
    model.skmesh.bin                     顶点、索引、权重、Morph 增量和解码后的像素
  Animations/
    0_*.anim.json                       单独的动画序列资产
    1_*.anim.json
  IKRigs/
    model.ikrig.json                     humanoid 语义映射、置信度、IK 链、关节限制和识别诊断
```

每份 JSON 具有 `assetType`、`version`、`id`、`name`。骨骼网格、动画与 IK Rig 通过 `skeletonId` 引用同一骨架。清单中的 `assets` 列表可用于构造资产浏览器，`sourcePath` 仅记录来源，不是运行时加载依赖。动画序列可独立存为 `.anim.json`，不必再次复制网格和纹理。

`.skmesh.bin` 的每一个标量都以明确的小端顺序写入，未直接写 C++ struct；因此不依赖结构体 padding、GLM 对齐或编译器 ABI。CPU 顶点坐标和变换使用 double，UV/法线/切线/权重使用 float，索引使用 32 位无符号整数。RGBA8 与 RGBA32F 纹理均可原样保存，当前版本不压缩像素。

JSON 中的二进制范围分别记录 `offset`、`count`、`stride`。加载时检查文件大小、FNV-1a 64 位校验和、范围溢出、元素数量、纹理尺寸与像素数量；非空范围不允许重叠，动画文件引用和 ID 也不能重复。FNV 校验用于识别文件损坏，不是密码学签名。

## 导入与每帧预览流程

```text
源 PMX / FBX / glTF / …
  → ImportSkeletalSource
      → 几何、PBR 材质、已解码纹理
      → 骨架、绑定矩阵、逐顶点蒙皮权重
      → Morph / 源动画 / 原生 IK（依格式存在情况）
      → 人形语义识别、标准 IK 链、诊断
      → 明确标记为 generated 的预览序列
  → ValidateSkeletalAsset
  → SaveSkeletalAssetPackage
  → Package.asset.json + 分离资产 + 二进制 payload

预览器加载 Package.asset.json
  → LoadSkeletalAssetPackage（不读取 sourcePath）
  → 选择动画并计算本帧局部/全局骨骼姿态
  → 可选 IK / 重映射姿态
  → Morph 增量 + CPU 蒙皮
  → 动态更新 ModelResource 的顶点数据
  → 原有 Material + Toon Render 管线
  → RHI/OpenGL 显示模型与骨骼辅助线
```

中间资产与动画计算不需要 OpenGL 上下文，因此生成器可以独立用于离线导入流水线。预览器是资产检查工具，当前使用 CPU 求值和蒙皮，便于验证数据并减少渲染后端耦合。

## 动画序列的来源

PMX 通常存放参考姿态、骨架、蒙皮、Morph 与 IK；动作时间线一般来自独立 VMD 等文件。这个真寻资源目录没有随附动作动画，不能把骨骼或表情数量理解为已经包含动作片段。

导入 FBX/glTF 等格式时，会提取源文件实际包含的骨骼动画。生成器同时可添加绑定姿态、骨架活动/表情演示等预览序列；这些序列的 `generated` 为 `true`，`source` 记录 procedural 来源，CLI 与编辑器会明确区分。它们用于检查导入结果，不冒充源动作。

当前支持独立 VMD 动作与表情导入及重映射烘焙，不支持 VPD 姿势文件或 PMX 刚体物理仿真；清单保留源刚体与关节数量用于检查。非顶点 Morph、PMX 特有骨骼继承/变形模式的支持范围应结合导入诊断查看，不能等同于完整 MMD 播放器。

具体而言，当前预览执行顶点 Morph 和线性混合蒙皮（LBS）；SDEF/QDEF 的类型、权重及 SDEF 参数保留在中间资产中，预览按 LBS 近似。VMD 烘焙器执行已导入的 PMX append/grant、固定轴投影与有动画驱动的原生 IK，结果保存为普通局部 TRS；直接播放通用源动画时不会再自动叠加这些约束。局部轴、外部父节点和完整 MMD 分层/物理顺序仍不等同于原生 MMD 播放器。CCD 会执行显式 IK link 的局部角度限制。FBX/glTF 顶点超过四条权重时保留最大的四条并归一化，导入报告会给出提示。

## VMD 导入与独立动画导出

本机示例已生成后，可运行 `Module/Model/RunMahiroVmd.cmd`，直接载入真寻与 `Asset/Generated/Animations/MahiroTaitui.anim.json`，打开动画曲线详情。该入口播放已烘焙资产，不需要原始 VMD 目录。

将动作和表情一起自动匹配到真寻骨架：

```powershell
.\bin\skeletal_animation_import.exe `
  --target ".\Asset\Generated\MahiroMaid\Package.asset.json" `
  --vmd "C:\Users\16620\Downloads\抬抬腿_by_哈基鹿鹿鹿_8d36aca8ba85b7e747b1be0bae26388d" `
  --name "Taitui_Mahiro" --fps 30 --scale 1 `
  --output ".\Asset\Generated\Animations\MahiroTaitui.anim.json" `
  --package-output ".\Asset\Generated\MahiroTaitui"
```

`--vmd` 可以传入一个文件或目录，也可以重复指定。目录会递归扫描 `.vmd` 文件，按路径排序后合并骨骼与表情轨道；只有镜头、灯光、阴影或可见性开关的文件会报告并跳过。示例目录中的 `taitui.vmd` 是人物动作，`taitui表情.vmd` 是表情，`taitui镜头.vmd` 是镜头，不会把镜头位移当作人物位移。

VMD 骨名与表情名按 Shift-JIS 解码，坐标转换到项目的右手 Y-up 空间。骨骼平移的 X/Y/Z 和四元数旋转分别按 VMD 的 Bezier 插值采样；表情曲线按线性插值采样。匹配优先原始名、英文名和标准化别名，再使用人体语义与常见表情别名，保留匹配方法、置信度与未匹配诊断。

VMD 不包含源参考骨架、绑定姿态或身材尺寸，因此自动匹配以目标骨架为参考；`--scale` 是显式平移比例，默认 1。跨身材的脚底接触不保证精确，必须有源角色骨架时应使用后面的跨骨架重映射入口。未匹配控制骨和表情会出现在报告中，不会按相近索引强行赋值。

默认烘焙已保存的原生 IK 链、VMD IK 开关，以及 PMX 局部/非局部 append 继承增量。结果是目标骨架的普通 T/R/S 轨道与目标表情权重曲线，播放不再依赖源 VMD、IK 求解器或 append 求值；不要再叠加相同的预览 IK。模型可见性、相机/灯光、骨/组/材质 Morph、刚体/衣服/头发物理和外部父节点不写入人物动画曲线，具体限制会写入序列诊断。

`--output` 生成独立的动画资产；可选 `--package-output` 将目标角色、已有序列和新序列一起另存为完整包。两个输出使用不同路径，独立动画文件应放在 `--package-output` 之外。输出文件必须不存在或为空，包目录必须不存在或为空；已有非空资产不会覆盖。

检查独立动画，无需启动图形窗口：

```powershell
.\bin\skeletal_animation_import.exe `
  --inspect ".\Asset\Generated\Animations\MahiroTaitui.anim.json" `
  --target ".\Asset\Generated\MahiroMaid\Package.asset.json"
```

在预览器中打开已经烘焙的动画，或启动时直接导入 VMD：

```powershell
.\bin\skeletal_asset_editor.exe `
  --package ".\Asset\Generated\MahiroMaid\Package.asset.json" `
  --animation ".\Asset\Generated\Animations\MahiroTaitui.anim.json"

.\bin\skeletal_asset_editor.exe `
  --package ".\Asset\Generated\MahiroMaid\Package.asset.json" `
  --vmd "C:\Users\16620\Downloads\抬抬腿_by_哈基鹿鹿鹿_8d36aca8ba85b7e747b1be0bae26388d"
```

编辑器启动参数 `--vmd` 同样可以重复指定。`--time <seconds>` 可定位检查时刻，`--curve-bone <name>` 可按原名或英文名预选骨骼曲线。`--motion-test` 用于验证纯动画输出的跨帧 GPU 图像和顶点变形，不自动加入检查用的手动 Morph 或 IK 目标。

独立 `.anim.json` 使用与包内相同的 `AnimationSequence` schema，包含 `baked`、`sourceSkeletonId`、`source` 和 `diagnostics`；旧 version 1 序列缺少这些新字段时仍可读取。`generated` 用于区分程序生成的检查动画，`baked` 表示已经采样烘焙，两者用途不同。

独立输出还包含 `targetLayoutHash`，加载时除 `skeletonId` 外还核对目标骨名、层级、局部参考姿态及表情顺序，防止目标包修订后把同一索引应用到另一表情。它不需要源动作文件或贴图，也不会在独立动画读写时扫描、复制几何和纹理。动画 JSON 采用紧凑格式，单文件上限 256 MiB、每序列最多 200 万条关键帧；普通资产 JSON 保持 64 MiB 上限。超过限制应拆分片段或降低采样率。导出采用隔离候选文件，重新加载并验证后才发布到目标文件。

### 长动画与正式启动入口

项目根目录提供 [`StartSkeletalAssetEditor.bat`](../../StartSkeletalAssetEditor.bat)。它固定使用 Release 编辑器并从自身位置定位角色资源；启动模式和参数说明见 [START_EDITOR.md](START_EDITOR.md)。生成的 `Asset/Generated/Animations/MahiroAozoraCider.anim.json` 存在时，脚本直接载入该序列，无需再次导入源 VMD。

「あおぞらサイダー」动作的末帧为 3451，时长 115.033333 秒，30 FPS 下应保留 3,452 个采样点。真寻有 186 个骨骼和 57 个表情，旧逻辑用全部目标通道乘以采样数，838,836 超过旧 600,000 临时预算，因而错误地拒绝这段正常长度的动画。

烘焙器现在将求值工作量与实际关键帧存储分开计费。尚未发生变化的通道只暂存第一个值；首次变化时按原始采样时间补回此前全部采样，随后逐帧保留。完全恒定的参考姿态通道省略，恒定非参考值保留一个关键帧。这与原先烘焙后剔除恒定通道的结果一致，没有自动降低 FPS、截掉尾段或做有损曲线简化。VMD 和跨骨架重映射共用此收集器，四元数连续性、IK 与 PMX append 烘焙保持原语义。

保护上限集中于 `Private/animation_limits.h`：最多 250,000 个采样点、1,600 万次骨骼/表情通道求值和 200 万个实际存储关键帧。跨骨架求值同时计算源与目标规模；在分配或追加数据之前检查相应预算。动画文件的 256 MiB 和 200 万 key 限制保持不变，超过时会报告所需样本/通道或关键帧数量。

仅做动画导入或曲线检查时，可以关闭实时物理以减少额外负载：

```powershell
.\StartSkeletalAssetEditor.bat --no-cloth --no-hair
```

这仍然显示裙摆、头发与人物的普通动画蒙皮。长动作导入会执行完整 IK 和 append 求值，首次烘焙需等待；之后优先加载已保存的 `.anim.json`。

## 跨骨架重映射烘焙

已有另一模型包的动画时，可以将其普通序列重映射并导出到真寻骨架：

```powershell
.\bin\skeletal_animation_import.exe `
  --target ".\Asset\Generated\MahiroMaid\Package.asset.json" `
  --source-package "D:\MyAssets\AnotherCharacter\Package.asset.json" `
  --sequence "Walk" --name "Walk_Mahiro" --fps 30 `
  --output ".\Asset\Generated\Animations\Walk_Mahiro.anim.json"
```

`--sequence` 可以是源动画名称或从 0 开始的序号；重名时使用序号。该入口通过 `BuildRetargetMap` 对齐人体语义、参考姿态和身材比例，再按所选 FPS 采样 `RetargetPose`，并按表情名/别名匹配 Morph 曲线。输出已绑定目标 `skeletonId`，可以独立加载、播放和再次另存为完整包。骨架未映射部分保持目标参考姿态，诊断列出未映射源轨道；不会自动添加脚锁定、地面接触或物理仿真。

## IK、自动识别与重映射

`IkRigAsset` 把源骨骼索引转换为诸如 hips、spine、head、左右上臂/前臂/手、大腿/小腿/脚等语义角色。自动识别结合骨骼名称/别名、层级和参考姿态信息，输出 `confidence` 和 `method`；无法可靠识别的部位保留诊断，便于人工核对。

在 `IK Rig` 详情的 `HUMANOID SEMANTIC MAP` 中，可通过每个角色下的下拉框改选骨骼或设置 `<unmapped>`。悬停下拉框可以查看识别方法。缺少的角色可在 `ADD / REPAIR MISSING ROLE` 中选择 `Semantic role` 与 `Bone for role`，然后点击 `Assign role / repair mapping` 添加或修正；角色包括躯干、四肢、眼睛、下颌和手指分段。

手动指定保持一对一关系：若所选骨骼已被另一个角色占用，旧角色会被清除；人工映射的置信度设为 100%，方法记录为 `manual override`。修改后，编辑器根据当前角色重新建立四条自动手臂/腿链与生成的 `RigPreview`，保留源 PMX 原生约束、自定义链及真实导入动画；已经打开的重映射预览也会立即重新构建映射。通过 `Export revision` 导出新包即可保留这些修正。

IK 链包含 root、effector、目标骨骼或目标位置、由末端向根排列的 CCD links、迭代次数、每步角度与关节限制。PMX 中存在的原生 IK 与自动生成的链通过 `native` 字段区分，`enabled` 可控制求值。

跨骨架对齐使用 `BuildRetargetMap`：先对齐人体语义角色，再计算参考姿态的旋转基准修正与体型平移比例；`RetargetPose` 将源姿态应用到目标骨架。名字相同并不保证绑定轴相同，因此参考姿态修正也是映射数据的一部分。

在 `Retarget` 详情中，当前模型是目标骨架。使用 `Browse source package` 选取来源包，点击 `Build automatic mapping`，再从 `Source sequence` 选择要使用的源动画。默认优先选择第一条真实导入动画；没有真实导入动画时选择生成的 `RigPreview`，再回退到第一条序列。来源的时长、FPS 及 generated/imported 标志均会显示，勾选 `Preview retargeted motion` 后，时间线使用所选源序列的时长。可以在右侧逐项检查源骨骼到目标骨骼、置信度、平移比例和诊断；没有源动画的包只构建映射，不播放。点击 `Bake sequence to target` 将当前映射按所选采样 FPS 烘焙为新序列，自动切换到该序列，随后可在动画详情导出独立 `.anim.json`。切换目标包本地动画会退出重映射预览。

该映射是可检查、可修订的初始结果。特殊骨架、长裙/头发/装备骨、非人形角色、脚底高度和不同 A/T Pose 仍可能需要人工修正；脚接触、运动匹配和完整动画蓝图不在当前实现内。

也可以在启动时通过 `--retarget-source` 预先载入来源包，自动构建映射并启用重映射预览：

```powershell
.\bin\skeletal_asset_editor.exe `
  --package ".\Asset\Generated\MahiroMaid\Package.asset.json" `
  --retarget-source "D:\MyAssets\AnotherCharacter\Package.asset.json"
```

`--package` 是当前显示的目标角色，`--retarget-source` 是提供骨架和动画序列的另一个独立包。启动后仍可在 `Source sequence` 下拉框中切换源动画、检查匹配结果或修正目标语义角色。

## 校验与测试

```powershell
ctest --test-dir build -R "skeletal_(asset_io|runtime|import)_test|vmd_import_test|animation_(asset_io|bake)_test" --output-on-failure
```

IO 回归包含独立包 roundtrip、乱序父骨骼、double 精度、SDEF 元数据、逆绑定偏移、空默认 Morph、RGBA8/HDR 纹理、动画来源、IK 设置和稳定 ID；异常数据包含循环/越界父骨骼、非法权重、越界二进制范围、纹理尺寸不匹配、损坏 payload、不支持的版本及包外路径。

IO 测试的小型 fixture 保留在 `build/skeletal_io_fixtures/run-*` 中，方便检查失败时的原始 JSON 与 payload。

动画 IO 回归单独覆盖没有 geometry 的目标布局、源 VMD 不存在时的加载与播放、目标布局/参考姿态校验、旧 version 1 兼容、烘焙元数据、密集关键帧、重复通道和 ID、越界索引、不合法时间与四元数，以及不覆盖已有输出。对应 fixture 保留在 `build/animation_io_fixtures/run-*`。

真实窗口/GPU 冒烟验证支持保存包含编辑器 UI 的截图：

```powershell
.\bin\skeletal_asset_editor.exe `
  --package ".\Asset\Generated\MahiroMaid\Package.asset.json" `
  --smoke-test --screenshot ".\build\mahiro_asset_editor.png"
```

`--smoke-test` 隐藏窗口并运行固定帧数后退出，默认 7 帧；也可通过 `--frames 10` 指定帧数。验证会读取真实 GPU 预览纹理，检查模型细节和跨帧像素变化，同时检查动画顶点实际变形；存在非零顶点 Morph 和自动左臂 IK 链时还会开启对应预览。成功时终端输出顶点位移量与变化像素数。可同时使用 `--retarget-source` 检查另一个包的动作在目标角色上的渲染。GPU 验证需要可用的 OpenGL 图形环境。

加载器拒绝跨包路径，以及经过符号链接或 Windows reparse point/junction 的资产引用；Windows 下也拒绝尾随点/空格和 alternate data stream 路径。修改 JSON 后，仍需要满足骨架引用、权重归一化、动画时间范围等约束；改动二进制后也需要重新生成元数据中的字节数量和校验和。推荐通过 C++ 资产接口或预览器另存功能修改资产。

公共接口在 `Public/skeletal_asset.h` 与 `Public/vmd_animation.h`，存储在 `Private/skeletal_asset_io.cpp`，VMD 解析在 `Private/vmd_importer.cpp`，采样烘焙在 `Private/animation_baker.cpp`；命令行入口分别是 `Tools/skeletal_asset_import.cpp` 与 `Tools/skeletal_animation_import.cpp`。
