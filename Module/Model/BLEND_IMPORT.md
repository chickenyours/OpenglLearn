# Blender 模型导入与布料网格识别

Model 模块通过本机 Blender 将 `.blend` 转为 GLB 和来源信息，再转换为项目自己的骨骼中间资产。编辑器使用已有的 Material 材质系统和 Toon Render 管线预览；后续打开中间资产包时不再启动 Blender，也不再读取源 `.blend`。

示例源文件：

```text
C:\Users\16620\Downloads\碧蓝档案女仆爱丽丝_爱给网_aigei_com\碧蓝档案女仆爱丽丝\Alice(1).blend
```

## 正式启动入口

双击项目根目录的 [`StartAliceModel.bat`](../../StartAliceModel.bat)，或在项目目录运行：

```powershell
.\StartAliceModel.bat
```

脚本使用 `output/Release/bin/skeletal_asset_editor.exe`，加载已发布的修复包 `Asset/Generated/AliceMaid/Package.asset.json`，默认打开 **Mesh Regions / 网格分类** 面板并暂停显示 **ShoesFit**。包中的 `SourcePose` 保留作者保存帧的源表面；这个源文件没有动作动画，`SourcePose` 是捕获第 0 帧的恒定预览序列。源文件本身仍有裸足与高跟鞋不适配的问题，默认的 `ShoesFit` 使用独立修正 Morph 处理该穿戴问题。

时间轴的 **Source pose** 按钮用于恢复原始源姿态，清除手工表情覆盖并关闭额外 IK。暂停后不持续进行相同姿态的 CPU 蒙皮与顶点上传。需要检查骨架活动时，在左侧选择生成的 **RigPreview** 再点击播放。

显示帧率上限为 60 FPS，它是限速设置，不保证当前硬件和数据量下实际达到 60 FPS。脚本从自身目录定位资源，可从其他工作目录启动，也可附加编辑器参数：

```powershell
.\StartAliceModel.bat --no-cloth --no-hair
```

直接查看衣物候选网格：

```powershell
.\StartAliceModel.bat --region-category garment
```

`--region-category` 支持 `all`、`unknown`、`body`、`hair`、`garment`、`accessory`，并自动打开网格分类面板；非法分类名称会报错。

脚本只打开已经生成的资产，不自动构建或重新导入。程序或缓存包缺失时会给出处理方法并暂停；编辑器报错时返回原退出码。脚本自身的错误码为：缺少 Release 程序 `1`、缺少 Alice 包 `2`、无法进入项目目录 `3`。自动化调用可设置 `$env:SKELETAL_EDITOR_NO_PAUSE = '1'` 关闭错误暂停。

这个入口独立于真寻角色的 [`StartSkeletalAssetEditor.bat`](../../StartSkeletalAssetEditor.bat)，不会修改其默认角色、动作或物理配置。

## 首次导入与重新导入

`.blend` 导入需要本机已经安装 Blender；本项目实例使用 `C:\Program Files\Blender Foundation\Blender 4.4\blender.exe`。导入器会查找 Blender Foundation 安装目录及 PATH，也可明确指定：

```powershell
$env:BLENDER_EXECUTABLE = 'C:\Program Files\Blender Foundation\Blender 4.4\blender.exe'
```

该变量只在当前 PowerShell 及其子进程中生效，不需要修改系统环境变量。C++ 调用方也可以通过 `LoadOptions.blenderExecutable` 指定路径；桥接层同时接受 `MODEL_BLENDER_EXECUTABLE`，其环境变量优先级高于 `BLENDER_EXECUTABLE`。

在项目根目录构建 Release 导入器和编辑器：

```powershell
cmake --preset windows-release
cmake --build --preset windows-release --target skeletal_asset_import skeletal_asset_editor -j2
```

首次导入到新目录：

```powershell
.\output\Release\bin\skeletal_asset_import.exe `
  --source 'C:\Users\16620\Downloads\碧蓝档案女仆爱丽丝_爱给网_aigei_com\碧蓝档案女仆爱丽丝\Alice(1).blend' `
  --output '.\Asset\Generated\AliceMaid'
```

输出目录必须不存在或为空。已有缓存需要保留；重新导入时选择新的目录，例如 `Asset/Generated/AliceMaid_r2`，然后通过编辑器的 **Open package** 打开新包。保存时先写入独立候选目录，重新读取并校验通过后才发布；不会覆盖非空资产目录。

检查已生成的包：

```powershell
.\output\Release\bin\skeletal_asset_import.exe `
  --inspect '.\Asset\Generated\AliceMaid\Package.asset.json'
```

也可以在编辑器顶部使用 **Import source** 选择 `.blend`，检查后通过 **Export revision** 保存到新的包目录。直接导入源文件会等待后台 Blender 转换；日常预览优先使用缓存包。

## 鞋子与女仆装贴合修复

本次修复针对导入后鞋子、女仆装和身体明显错位的问题。修复同时处理参考姿态、实际变形权重和修改器顺序，保留源模型已有的衣物形状：

1. **恢复作者保存的姿态。** Alice 的 410 根源骨骼中，有 47 根在保存帧上的姿态相对静止骨架发生了变化。直接显示 `BindPose` 会改变源文件中已有的相对位置。导入器把求值后的源骨骼世界矩阵转换为本地 TRS，保存为 `SourcePose`；原始静止骨架、逆绑定矩阵和动画绑定标识继续保留。该过程忠实保留源表面，也保留源文件本来存在的穿戴问题。
2. **筛选真正参与蒙皮的顶点组。** 只有对应 Blender 骨骼且 `use_deform` 开启的组参与皮肤形变。`onepiece` 和 `underdress` 各有 24 个非变形控制组，需要从导出蒙皮中排除；这些组的作者来源信息仍留在旁车文件中。导出保留完整骨架，控制骨仍可用于层级与动画。
3. **保留全部有效变形影响。** 不再把每顶点权重截为四项。本例 Blender 求值顶点诊断最多有 26 项有效骨骼影响；实际进入三角形渲染资产的顶点最多有 25 项。完整权重用于源表面拟合、引擎 CPU 蒙皮及依赖蒙皮的模块；四项数据仅作为兼容摘要。导出沿用 Blender glTF 对极小权重的筛选阈值 `0.0001`，并记录该阈值。
4. **按源顺序保留修改器表面。** 对 `ARMATURE → SUBSURF / SOLIDIFY` 等顺序，先求值作者当前姿态下的原始修改器链，再使用完整权重把结果逆蒙皮回原始绑定空间。Basis 和每个相对 Shape Key 使用一致的拓扑和处理流程；播放 `SourcePose` 后恢复该帧的衣物表面。单纯关闭 Armature 再细分不能保持同样的贴合结果。

未应用穿戴适配的源还原包默认选择并暂停 `SourcePose`；**Bind pose** 用于检查原始静止骨架。`RigPreview` 以源姿态为基线叠加检查动作，保留未参与检查动作的骨骼和表情默认值。含独立适配姿态的包需要另外核对适配后的预览基线，不能用生成的检查动作代替源动画。

旧缓存包没有这些修复数据，仅更新编辑器无法重建丢失的权重和源表面，需要重新导入 `.blend` 并生成新包。使用上方新目录流程保留旧包，在 **Open package** 打开新版本。

这次修复恢复的是源文件捕获帧的几何与蒙皮一致性。其他外部动作仍需正确重映射，衣物分类仍不会自动生成或启用布料物理；后续模拟与碰撞配置继续使用独立 ClothAsset 流程。

### 源表面还原的实际验证

修复后的完整资产包含 57 个材质 section、907,718 个顶点、1,525,050 个三角形、443 个骨架及场景节点、136 条按 section 保存的 Morph 记录和 13 张纹理。14 个网格保存完整蒙皮列表，共 3,284,808 对骨骼影响。

重新读取最终穿戴适配包并选择 `SourcePose` 后，31 个源网格对象全部通过原 Blender 求值表面的双向最近点比较，共检查 1,673,132 个方向性样本，超限样本为 0。容差为 `0.000924896407` 源单位，各对象最大误差如下，详见 [最终包源表面对比日志](../../build/alice_shoesfit_source_reference.log)：

| 比较范围 | 最大位置误差（源单位） |
|---|---:|
| 全部 31 个对象 | `0.0000422545187` |
| 鞋子 `shoes` | `0.0000108516854` |
| 身体 `body` | `0.000011254431` |
| 丝袜 `stockings_R` | `0.0000102312956` |

参考点云仅包含原求值网格中被多边形引用的顶点。丝袜的 4 个孤立点经 Blender 直接检查确认没有任何多边形引用，保留在源数据和完整拟合计算中；它们不作为三角形表面的比较样本。旁车明确记录排除数量和顶点编号，比较没有放宽容差或跳过对象。依据见 [孤立点检查](../../build/alice_stockings_loose_vertices.json)。

[12 项相关回归](../../build/alice_fit_corrective_tests.log)和[最终实际缓存包检查](../../build/alice_shoesfit_asset_test.log)已通过。源表面对比证明原始几何在 `SourcePose` 下继续保留，穿戴效果另由适配姿态的实际渲染检查验证；这些结果不代表动画播放性能已经改善。

### 独立鞋部穿戴适配

未经穿戴修正的 `SourcePose` 中，鞋部近景可见脚趾从鞋底前下方伸出。对原文件的只读检查确认：全场景驱动器为 0，Foot 与 Toe 骨骼均无约束，相关脚趾骨的 `use_deform` 为开启状态。因此本例的剩余失配存在于读取的源场景，并非关闭嵌入脚本漏执行鞋部驱动器。检查依据见 [驱动器与脚部约束审计](../../build/alice_fit_driver_audit.json)。

穿戴适配的正式配置路径为 [`Asset/ImportProfiles/AliceMaid.shoesfit.json`](../../Asset/ImportProfiles/AliceMaid.shoesfit.json)，通过通用 `ApplySkeletalFitProfile` 为资产增加派生的 Corrective Morph。最终选择的配置仅对 `body` 的网格 section 26 增加 **21,150 个顶点修正**，`boneCorrections` 为空。足部沿鞋底方向连续压缩，并平滑调整前掌的长度和宽度，踝部保持固定。只调整趾骨不能覆盖主要由 Foot 骨骼驱动的前掌区域，因此使用独立表面修正。

源丝袜已经具有适配高跟鞋的脚型；此次保持全部丝袜顶点原形，不对身体和丝袜套用同一组位移。源身体前掌和丝袜在鞋底方向上的范围明显不同，对两者同时抬升会把丝袜推到鞋面上方，产生额外的足背叠层。最终配置的丝袜修正数量为 0。

配置的 `morphDeltas` 每项包含 `mesh`、`vertex`、`expectedPosition`、`positionDelta`、`normalDelta`，用于定位原顶点、检查预期绑定坐标并生成位置及法线增量。原网格、蒙皮权重、绑定矩阵和源文件继续保留，适配作为额外 Morph 保存。`SourcePose` 中该修正的权重为 0，`ShoesFit` 恒定序列将权重设为 1；`RigPreview` 以 `ShoesFit` 为基线叠加检查动作。编辑器对含该序列的包默认选择 `ShoesFit` 并暂停，时间轴的 **Shoes fit** 与 **Source pose** 按钮用于切换适配结果和作者原始姿态。

可从已忠实还原的源缓存生成新的适配包，无需重复启动 Blender：

```powershell
.\output\Release\bin\skeletal_asset_import.exe `
  --inspect '.\Asset\Generated\AliceMaid_FitFix_20261016\Package.asset.json' `
  --fit-profile '.\Asset\ImportProfiles\AliceMaid.shoesfit.json' `
  --output '.\Asset\Generated\AliceMaid_ShoesFit_New'
```

输出目录必须不存在或为空。源模型导入入口也支持在 `--source`、`--output` 参数旁附加 `--fit-profile`。配置与具体源资产和顶点布局对应，不能把 Alice 配置直接套用于其他模型或经过不同拓扑处理的包。

这种修正不是源文件原本存在的动画或布料模拟。外部动作如果没有包含对应的修正 Morph 曲线，需要显式启用该 Morph 或把修正烘焙进动作；不能据此声称所有外部动作已经自动适配或完全不会穿模。

选定配置的实际 GPU 鞋部近景已通过检查：脚掌和趾尖不再从鞋底伸出，足背叠圈消失。派生包包含 137 条按 section 保存的 Morph 记录和 4 条预览序列，保留原始源几何与 `SourcePose`；相关 [12 项回归测试](../../build/alice_fit_corrective_tests.log)已通过，包含 `fit_profile_test`。数学检查未发现新增退化三角形或反向三角形，最小局部变形 Jacobian 行列式约为 `0.164`；依据见 [适配报告](../../build/alice_forefoot_fit_r6_report.json)。报告保留局部射线检查的覆盖范围与剩余偏差，不将鞋外壳凸包内判定等同于全部鞋腔或所有姿态零穿透。

![选定配置的 GPU 鞋部近景](../../build/alice_shoesfit_r6_shoes_preview.png)

[全身预览](../../build/alice_shoesfit_full_preview.png)与[侧面预览](../../build/alice_shoesfit_side_preview.png)也已通过实际 GPU 检查；对应[全身日志](../../build/alice_shoesfit_full_preview.log)、[侧面日志](../../build/alice_shoesfit_side_preview.log)保留运行结果。[动作预览](../../build/alice_shoesfit_motion_preview.png)检查到最大顶点位移 `0.620732` 源单位、29,990 个视口像素变化，详见[动作检查日志](../../build/alice_shoesfit_motion_preview.log)。这验证了修正姿态与生成的骨架检查动作可以渲染，不替代外部动画的穿戴适配与碰撞验证。

| 验收项 | 当前记录 |
|---|---|
| 穿戴适配配置 | 仅身体 21,150 个顶点增量；丝袜修正数量 0 |
| 正面鞋部近景 | GPU 检查通过，见上图 |
| 全身、侧面及动作预览 | GPU 检查通过，见上方图像及日志 |
| 派生包的 SourcePose 源表面对比及独立缓存检查 | 31 个源对象全部通过；缓存检查 0 失败 |
| 适配姿态、SourcePose 对照及 RigPreview | 检查通过；原姿态和独立修正分别保留 |
| 正式 `AliceMaid` 缓存发布 | 已发布到 `Asset/Generated/AliceMaid`，启动脚本默认 `ShoesFit` |
| 修复版本运行性能 | 正式 `.bat` 暂停预览实测 32.31 FPS；稳定后 CPU 蒙皮、上传均为 0 ms，详见下方新测量 |

原缓存保存在 `build/AliceMaid_before_fit_fix_20261016`，可用于回看或恢复历史版本。正式启动使用修复后的缓存，不需要重复读取 `.blend`。

正式 `.bat` 在发布后完成独立性能测量：Release、完整模型、`ShoesFit` 暂停、Intel RaptorLake-S Mobile 集显、关闭 VSync、60 FPS 上限；120 帧排除前 30 帧，测量期间不截图。90 帧耗时 2.785 秒，平均 **32.31 FPS**，平均帧时间 30.95 ms，P95 85.74 ms；CPU 蒙皮和网格上传均为 0 ms，进程 CPU 约占整机 0.82%。这说明暂停后复用已生成的表面，不代表实时动画或物理达到同样性能；下方旧动画测量仍只作历史记录。依据见 [启动及性能日志](../../build/alice_shoesfit_launcher_benchmark.log)和[性能数据](../../build/alice_shoesfit_static_benchmark.json)。

## 导入流程与来源保留

```text
Alice(1).blend（只读）
  → 本机 Blender 后台进程
      → scene.glb：可交换的几何、骨架、蒙皮、Morph、材质和动画
      → source.blend.json：对象、集合、材质槽、顶点组及修改器来源证据
      → source_pose_reference.bin：源姿态表面校验点云（转换缓存）
      → blender.log：转换日志
  → 既有骨骼模型导入器
  → 按材质 section 识别 Body / Hair / Garment / Accessory / Unknown
  → 校验并生成 SkeletalAssetPackage
  → Material + Toon Render 预览
```

桥接脚本为 [`Tools/blend_export.py`](Tools/blend_export.py)。导入器以 `--background --factory-startup --disable-autoexec` 启动 Blender，打开源文件时关闭其嵌入 Python 脚本执行，不调用保存源文件操作。转换结果放入项目 `build/blend_import_cache/blend-*` 的独立目录，转换失败也保留日志供检查。后台进程限制为两条 Blender 工作线程并降低调度优先级；启动预览缓存包不会重复产生转换负载。

导出在 Blender 进程内的副本上处理受支持的修改器，包括 `ARMATURE`、`MIRROR`、`SUBSURF`、`SOLIDIFY`，以及三角化、边分离和受支持的法线处理。处理按源修改器的渲染启用状态和原始顺序求值，沿用已有细分设置，对 Basis 和每个相对 Shape Key 分别执行同一套处理，并核对顶点与连接拓扑一致，再重建 Morph。绑定空间几何由捕获的源姿态表面逆蒙皮得到。因此能够保留镜像后的完整模型、修改器后的衣物贴合和表情，避免直接应用修改器时丢失 Shape Key。绝对时间 Shape Key 或导致各表情拓扑不一致的处理会明确报错，不能静默截断。

捕获源表面时保留 Armature 在修改器链中的实际作用；构造绑定几何后恢复导出蒙皮，让骨骼继续驱动模型。名称含 `outline` 的 Solidify 描边层由引擎描边替代，避免生成重复外壳；普通 Solidify 保留在源表面求值中。Cloth 模拟和其他未支持的动态处理不转换为引擎物理资源，未支持项保留来源诊断。骨骼约束、驱动器的已求值结果可以体现在捕获帧中，其运行逻辑不在引擎内执行。这些转换只改变后台场景副本，不保存或改写原 `.blend`。

桥接选择当前源场景中可见且可渲染的网格及其骨架，保留修改器求值后的完整几何，不自动减面。本例首版引擎数据约 91 万顶点，显著大于原 `.blend` 属于展开镜像、细分和 Morph 数据后的预期结果。蒙皮支持每顶点完整影响列表，旁车文件记录最大影响数、超过四项的顶点数量、极小权重阈值和源姿态拟合残差。

来源旁车文件记录原始修改器类型、启用状态、Cloth 固定组和参数，以及物理缓存的存在信息。这些是检查和后续资产生成的依据，不是可直接执行的引擎物理资源。

中间资产包使用现有目录结构：

```text
Asset/Generated/AliceMaid/
  Package.asset.json                 包信息、meshRegions 分类与来源证据
  SkeletalMeshes/model.skmesh.json   网格、材质、纹理、蒙皮与 Morph 元数据
  SkeletalMeshes/model.skmesh.bin    几何、权重、Morph 增量和解码后的贴图像素
  Skeletons/model.skeleton.json     骨骼与绑定姿态
  IKRigs/model.ikrig.json            语义映射与 IK 信息
  Animations/*.anim.json            源动画或标明 generated 的预览序列
```

`source.blend.json`、GLB 和 `source_pose_reference.bin` 留在转换缓存中供追溯。旁车文件的 `sourcePose` 记录捕获帧、帧率、源骨骼世界矩阵及对象独立的 Morph 默认值；`sourcePoseReference` 与各对象的 `sourceReference` 指向参考点云的字节范围。点云使用小端 `float32`、引擎 Y-up 世界坐标，用于把重新读取的引擎资产表面与原 Blender 求值表面进行核对。该文件属于导入验证数据，不是布料模拟资产，也不是日常启动必需资源。

最终包已包含预览所需的数据，包含生成的恒定 `SourcePose`；源路径只记录来源。完整资产格式及骨骼/动画预览操作见 [SKELETAL_ASSETS.md](SKELETAL_ASSETS.md)。

### 静态模型入口的源姿态快照

`StaticModelLoader::Load("模型.blend")` 使用同一只读转换桥接，并自动启用内部 `--static-pose` 分支。它把作者保存帧的骨骼姿态和默认 Shape Key 值求值为普通静态网格，再交给现有 Material 与 Render 系统。鞋子、女仆装与身体因而使用源姿态下的同一套表面，不会把供蒙皮使用的绑定网格直接作为最终静态外观。

该静态快照不包含可播放骨架、表情曲线或动画；需要这些数据时继续使用 `ImportSkeletalSource` 和骨骼资产编辑器。静态分支也会求值没有骨架、没有修改器但具有非零默认 Shape Key 的网格，避免关闭 Morph 导出后丢失其默认外观。

直接 `.blend` 静态加载只还原源文件，并不自动套用独立 `ShoesFit` 配置；Alice 源文件已有的裸足与高跟鞋失配仍需在生成穿戴适配资产时单独处理。

可通过独立静态预览程序检查直接 `.blend` 加载：

```powershell
.\output\Release\bin\model_viewer.exe --model "C:\Users\16620\Downloads\碧蓝档案女仆爱丽丝_爱给网_aigei_com\碧蓝档案女仆爱丽丝\Alice(1).blend" --toon
```

静态和骨骼转换均保留当前保存帧作为对象的基准变换，防止 glTF 导出器把对象切回第 0 帧后改变服装相对位置。骨骼导入仍保留原始骨骼静止姿态与可用的源动作；源文件没有动作序列时，捕获的 `SourcePose` 仍只是恒定预览，不会因使用保存帧而生成动作。

### 完整蒙皮权重格式

SkeletalMesh v1 的每个网格可选保存两段二进制范围，缺少时继续按旧四项数据读取：

| 字段 | 二进制布局 | 含义 |
|---|---|---|
| `fullInfluenceOffsets` | 小端 `uint32`，stride 4 | 长度为顶点数加一；顶点 `v` 的影响范围为 `[offset[v], offset[v+1])` |
| `fullInfluences` | 小端 `uint32 joint` + `float32 weight`，stride 8 | 完整骨骼索引和归一化权重 |

两字段必须同时存在，首偏移为零，末偏移等于影响总数，每顶点范围非空。权重必须有限且为正、和接近一，骨骼索引必须有效，同一顶点不可出现重复骨骼。读取同时校验范围、重叠、元素预算和包校验和。原 `skin` 四项记录继续保存，在存在完整列表时作为兼容摘要使用。

导入器使用唯一的导出节点别名避免重名导致动画与骨骼绑定混淆，`meshRegions` 仍保留原 Blender 对象、网格和材质名称。全部 57 个材质 section 都已映射回来源。按 [glTF 蒙皮约定](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#skins)，骨骼网格实例与绑定变换会规范化，忽略蒙皮网格节点自身的变换；源关节 TRS 与逆绑定矩阵保留，最终世界空间动画结果不变，静态模型和骨骼预览使用一致的坐标空间。

## Alice 实例基础数据（首版记录）

以下统计和截图来自本次贴合修复前的首次导入，作为历史对照保留；新包增加了 `SourcePose`、完整权重和源表面拟合数据。它们不能替代修复版本的源表面对比验证。

| 项目 | 首版结果 |
|---|---|
| 源网格对象 | 31 |
| 引擎材质 section | 57，全部成功映射来源 |
| 顶点 / 三角形 | 907,379 / 1,525,050 |
| 骨架与场景节点 | 源骨架 410 根骨骼；引擎骨架保留共 443 个关节及场景节点 |
| Morph | 136 条按 section 保存的 Morph 记录，不代表 136 个不同的作者表情 |
| 内嵌贴图 | 13 张，最大 6144 × 6144，保留完整分辨率 |
| 动画 | 源动作 0 条；程序生成 `BindPose`、`RigPreview` 两条预览序列 |
| Body / Hair / Garment | 18 / 7 / 11 个 section |
| Accessory / Unknown | 15 / 6 个 section |
| 待核对项 | 17 项：11 个衣物候选及 6 个未知 section |
| 作者 Cloth 修改器 | 0 |

6144² RGBA 贴图包含 150,994,944 个颜色通道，超过普通网格的元素计数上限。资产读取现按受检纹理尺寸与二进制字节预算校验贴图，保留完整像素；网格、Morph 的元素限制以及 payload 越界、重叠、校验和检查仍生效。

### 历史验证与性能（2026-10-16，贴合修复前）

下述测试、性能数值和预览图对应首版缓存，已被本次导入贴合修复取代。首版静止蒙皮漂移只比较引擎自身的静止网格与绑定姿态，不包含原 Blender 求值表面的核对，不能据此判断服装是否准确还原。修复后的测试新增源表面参考点云对比；其实际运行数据应以新验证日志为准，当前不把旧性能结果写作修复版性能。

Release 构建完成，8 项相关测试通过：glTF 材质、衣物识别、Blender 来源映射、服装拆分、资产读写、骨骼运行时、源模型导入和真实 Alice 缓存包。实际包的静止蒙皮最大漂移为 `3.6744e-6` 源单位；面部 Morph 以权重 1 播放时产生实际顶点位移。源 `.blend` 的 SHA-256 在导入前后保持不变。

完整模型静态预览在 Intel RaptorLake-S Mobile 集显上实测 **33.52 FPS**：Release、885×749 视口、关闭 VSync、60 FPS 上限、90 帧中排除前 30 帧预热；最后一帧保存截图。稳定后的 CPU 蒙皮和网格上传均为 0 ms，进程 CPU 用量约占整机 0.76%。这组数据针对暂停的 `BindPose`。当前保留完整几何和贴图，没有自动降低资产分辨率。

同一机器的完整模型实时 `RigPreview` 播放实测 **10.07 FPS**：60 帧排除前 10 帧，测量期间不截图。平均 CPU 蒙皮 `40.76 ms`、网格上传 `54.47 ms`，仍是需要后续优化的播放瓶颈；主线程约占一个逻辑核心的 `94.42%`，进程占整机约 `4.43%`。因此任务管理器整机 CPU 比例低，不能说明这条串行预览路径还有单核余量。动画冒烟截图包含额外检查，图中约 6 FPS 不用作实时播放基准。静态分类预览与完整密度动画的性能分别记录，不能据此宣称动画已达 30/60 FPS。

衣物动画 GPU 检查也已通过，实际筛选出 11 / 57 个 section；12 帧测试观察到顶点位移和 2,949 个视口像素变化。下方衣物截图处于 `RigPreview` 播放状态，不代表布料物理模拟。

验证记录：[导入](../../build/alice_final_import.log)、[7 项回归](../../build/alice_gltf_skin_tests.log)、[真实缓存包](../../build/alice_cached_asset_final_test.log)、[静态预览](../../build/alice_static_preview.log)、[静态性能数据](../../build/alice_static_benchmark.json)、[动画性能数据](../../build/alice_animation_benchmark.json)、[衣物动画预览](../../build/alice_garment_preview.log)。

完整模型预览：

![Alice 完整模型预览](../../build/alice_full_preview.png)

衣物分类预览：

![Alice 衣物候选网格预览](../../build/alice_garment_preview.png)

## 如何区别布料、身体与头发

识别单位是渲染网格的**材质 section**。同一个 Blender 对象可能同时包含皮肤、衣物、头发和金属装饰，不能因为对象名是 `Body` 或对象有 Cloth 修改器，就把全部三角形归入身体或布料。

识别器结合材质名称、对象和网格数据名、集合、修改器以及该 section 的实际骨骼权重给出候选分类，并保留每条证据。材质信息比混合对象名称更具体；对象顶点组名称的存在本身不代表每个 section 都受该组影响。证据冲突或不足时保留 `Unknown`，而不是强制归为衣服。

每条 `meshRegions` 记录包含：

| 字段 | 含义 |
|---|---|
| `mesh`、`kind` | 目标材质 section 索引及分类：`unknown`、`body`、`hair`、`garment`、`accessory` |
| `confidence`、`requiresReview` | 识别置信度及是否需要人工确认；置信度不是物理参数 |
| `authoredCloth`、`clothPinGroup` | 源对象是否有 Cloth 修改器，以及源固定组名称；修改器关闭时仍保留其作者信息 |
| `sourceObject`、`sourceMesh`、`sourceMaterial` | Blender 对象、网格数据和材质来源 |
| `collections`、`vertexGroups`、`modifiers`、`evidence` | 来源集合、顶点组、修改器摘要及识别依据 |
| `userOverride` | 当前分类是否经用户手动修正 |

在左侧打开 **Mesh Regions / 网格分类**，按以下步骤检查：

1. 用 **Category preview** 选择 `All sections`、`Unknown`、`Body`、`Hair`、`Garment` 或 `Accessory`，查看对应网格。
2. 在 section 列表中点选网格，勾选 **Isolate selected section** 单独查看；用 **Show all** 恢复全模型。
3. 展开 **Recognition evidence**、**Source collections**、**Source modifiers**、**Source vertex groups**，检查分类依据与来源。
4. 对误判项修改 **Manual category**。操作会设置手动覆盖并标记待核对，检查后勾选 **Reviewed / 已核对确认**。
5. 使用顶部 **Export revision**，指定新目录，点击 **Generate assets** 保存。

手动覆盖与分类证据写入包清单，重新打开导出的包后继续保留；仅在窗口内修改而未导出，不会改写已有包。分类修改不改变网格二进制、骨架或动画绑定标识，旧版缺少 `meshRegions` 的包仍可加载。

本例来源扫描有 31 个网格对象，包含 `onepiece`、`apron`、`apron1`、`underdress`、`stockings` 等衣物对象；没有发现作者设置的 `CLOTH` 修改器。衣服仍可以被识别为 `Garment` 候选，但不能据此声称源文件自带可复用的布料模拟资产。

## 分类与物理模拟的边界

`Garment` 表示衣物网格候选，**不会自动启用布料物理**。`authoredCloth` 也只记录 Blender 作者信息；Blender Cloth 参数、修改器缓存和引擎 ClothAsset 不同，不能直接互换。

后续需要明确选择可动衣物区域、固定顶点、模拟代理网格和身体碰撞体，再生成项目的独立服装包、ClothAsset 与角色装配清单。这样才能沿用既有换装与裙摆模拟流程，避免把丝袜、胸前布料、硬装饰等全部当成自由裙摆。现有独立服装资产流程见 [CLOTH.md](CLOTH.md)。头发分类同样不会自动生成头发惯性资源，相关生成流程见 [HAIR.md](HAIR.md)。

## 材质与导出限制

Blender 的任意节点组、Shader to RGB、程序纹理和自定义二次元材质不能直接在本引擎执行。常规 Principled PBR 节点沿用 glTF 材质导出路径；对于其他二次元节点图，桥接从有效输出追踪可提取的底色贴图、显式 RGB 颜色与乘色、UV/Mapping 和透明信息，生成可交换的 Principled 材质作为数据载体，再交给现有 Material 系统和 Toon Render 使用。Shader to RGB 链路会尽量保留其 Diffuse 等输入中的底色，光照分段由引擎处理，不将源光照烘入模型底色。使用的贴图和近似方式记录在来源旁车文件与诊断中。

这种转换保留可提取的材质输入，不能完整执行复杂节点图，不保证与 Blender 原渲染逐像素一致。

需要精准还原特殊高光、阴影遮罩或节点混合时，应在作者工具中把结果烘焙为贴图，并提供明确的材质语义。缺失外部贴图、未导出的对象和不支持的特性会出现在转换旁车文件、`blender.log` 或资产导入诊断中。骨骼动画、约束、驱动器及物理也应结合导入报告判断支持范围，不能把 `.blend` 能被打开理解为完整 Blender 场景执行器。

若提示无法找到 Blender，检查安装路径或在当前终端设置 `BLENDER_EXECUTABLE` 后重试；若转换失败，先查看报错提供的 `build/blend_import_cache/blend-*/blender.log`；若只有预览材质异常，检查来源贴图和节点材质近似诊断。启动缓存包不需要 Blender，重新导入 `.blend` 才需要它。
