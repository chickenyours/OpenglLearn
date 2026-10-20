# 骨骼资产编辑器正式启动入口

头发研究准备视图使用 [`StartLolitaHeadHairPBR.bat`](../../StartLolitaHeadHairPBR.bat)：独立加载头部与长披发包，PBR 中性布光，默认暂停动画与物理，30 FPS 显示上限。旋转、缩放、PBR／Toon 切换和重建方法见 [HEAD_HAIR_PBR.md](HEAD_HAIR_PBR.md)。

双击项目根目录的 [`StartSkeletalAssetEditor.bat`](../../StartSkeletalAssetEditor.bat)，或在项目目录的 PowerShell 中运行：

```powershell
.\StartSkeletalAssetEditor.bat
```

脚本始终使用 `output/Release/bin/skeletal_asset_editor.exe`，不会自动切换到旧版 Debug 程序。它从自身目录定位项目资源，因此可从其他工作目录启动，项目路径也可以包含空格和中文。

3ds Max 洛丽塔女仆高模使用 [`StartLolitaModel.bat`](../../StartLolitaModel.bat)，加载 `Asset/Generated/LolitaMaid/Package.asset.json`，默认打开网格分类面板、按身体范围构图并暂停显示。只查看衣物时运行：

```powershell
.\StartLolitaModel.bat --region-category garment --frame-region garment
```

在 **Mesh Regions / 网格分类** 中切换类别或 **Isolate selected section** 后，点击 **Frame visible** 可按当前可见网格重新构图；**Show all** 恢复全部类别。该高模有 63 个网格、5,477,148 个面角展开顶点、1,825,716 个三角形、52 个材质、32 张 RGBA8 纹理，二进制载荷为 2,042,920,580 字节。264 个场景节点不代表作者角色绑定；作者动作和 Cloth 均为 0，因此此入口用于静态资产检查，不会自动模拟衣物。

当前分类为 19 个衣物、19 个身体、9 个头发和 16 个配件；030、032、041 三个衣物构件的具体材料类型仍保留待确认。Max Physical 连续 cutout 使用 Blend，现有管线中的 Blend 不投影。无损纹理合成／未用图像裁剪及 64 KiB 流式 IO 已用于这次保存；最终 12 项相关回归、完整 FBX 导入和缓存重载、全身／19 件衣物筛选 GPU 预览均已通过。本机 Intel 集成显卡暂停预览实测 16.06 FPS，稳定后 CPU 蒙皮与上传均为 0 ms。导出流程、粗糙度反转、来源证据及验证日志见 [MAX_IMPORT.md](MAX_IMPORT.md)。

女仆爱丽丝 `.blend` 示例使用独立入口 [`StartAliceModel.bat`](../../StartAliceModel.bat)，加载已发布的修复包 `Asset/Generated/AliceMaid/Package.asset.json`，打开网格分类面板并默认暂停在 **ShoesFit**。包中的 **SourcePose** 保留作者保存帧的实际表面，时间轴的 **Source pose** 按钮可恢复原始源姿态。源资源本身存在脚趾与高跟鞋不适配，独立的穿戴修正与忠实源还原分别保留。

鞋部穿戴适配使用 [`AliceMaid.shoesfit.json`](../../Asset/ImportProfiles/AliceMaid.shoesfit.json) 配置增加独立的 Corrective Morph，只修正身体足部的 21,150 个顶点，保持原丝袜脚型，不覆盖原始网格、权重或绑定。含 **ShoesFit** 序列的修复包默认暂停在该穿戴姿态，Morph 权重为 1；**Source pose** 恢复原始源姿态，修正权重为 0，**Shoes fit** 可重新启用适配结果。`RigPreview` 以 `ShoesFit` 为基线叠加骨架检查动作。

正面鞋部近景、[全身预览](../../build/alice_shoesfit_full_preview.png)、[侧面预览](../../build/alice_shoesfit_side_preview.png)及动作 GPU 检查均已通过；脚掌、趾尖不再从鞋底伸出，足背叠圈消失。12 项相关回归、最终包 31 个源对象的 `SourcePose` 表面对比和独立缓存检查已通过，修复缓存已正式发布；原缓存保存在 `build/AliceMaid_before_fit_fix_20261016`。正式 `.bat` 暂停预览实测 32.31 FPS，稳定后 CPU 蒙皮和网格上传均为 0 ms；该测量不代表实时动画或物理性能。其他外部动作如果没有对应修正 Morph 曲线，需要显式启用修正或重新烘焙，当前适配不保证所有动作自动消除穿模。验证日志与 `--fit-profile` 生成流程见 [BLEND_IMPORT.md](BLEND_IMPORT.md)。

默认加载 `Asset/Generated/MahiroCloth/Character.asset.json` 中的真寻角色、独立裙摆和 `Hair_Realtime30.hair.json` 头发配置，**裙摆和头发物理均开启**。显示帧率上限为 60 FPS；该上限不代表物理求解能达到 60 Hz。联合模拟时后台工作器会协调物理时钟，30 Hz 配置不构成动态求解速度保证。当前物理仍比较耗时，仅导入、重映射或检查动画时建议运行：

```powershell
.\StartSkeletalAssetEditor.bat --no-cloth --no-hair
```

这仍显示人物、裙摆和头发的动画蒙皮。只关闭裙摆物理、保留头发惯性时使用 `--no-cloth`。

如果 `Asset/Generated/Animations/MahiroAozoraCider.anim.json` 存在，启动时会自动加载并播放本次导入的「あおぞらサイダー」动画序列，无需重新烘焙。该动画文件不存在时，脚本会明确提示，并显示角色包已有的动画；仍可在窗口内导入其他 VMD。

## 导入和查看动画

启动后使用工具栏的 **Import VMD**，选择动作 `.vmd` 或包含动作与表情 VMD 的文件夹，设置名称与采样 FPS，然后点击 **Match and bake animation**。导入完成后，在左侧 Animation Sequences 中选择动作，使用时间轴播放或暂停，并在动画面板保存生成的动画序列资产。

也可以把编辑器参数直接附加在启动命令后面。例如打开已经导出的动画序列：

```powershell
.\StartSkeletalAssetEditor.bat --animation "Asset/Generated/Animations/MahiroTaitui.anim.json" --no-cloth --no-hair
```

需要直接导入其他动作时，把路径替换成实际存在的文件或目录；含空格、中文的路径应保留引号：

```powershell
.\StartSkeletalAssetEditor.bat --vmd "D:\Motions\动作目录" --no-cloth --no-hair
```

命令行 VMD 导入在窗口显示前完成；长动作可能需要等待。反复播放同一动作时，优先从编辑器保存并加载 `.anim.json`，避免每次启动都重新烘焙。

## 本次长动画验证

「あおぞらサイダー」已导入为 `Asset/Generated/Animations/MahiroAozoraCider.anim.json`：115.033333 秒、30 FPS、3,452 个采样点，包含 56 条非空骨骼轨道和 9 条表情曲线，共 224,380 个关键帧，文件约 23.3 MiB。57 条源骨骼轨道均匹配；未输出的恒定参考姿态通道不影响播放。5 条未匹配的源表情保留在资产诊断中，未按索引强行套用。

已验证原目录导入、独立动画导出及重新读取，也从项目外的工作目录通过本 `.bat` 再次导入 VMD 并运行到第 112 秒附近，GPU 图像及动画顶点变化检查通过。预览图为 `build/aozora_editor_import_preview.png`。复核日志为 `build/aozora_vmd_import.log`、`build/aozora_animation_inspect.log` 和 `build/aozora_editor_import.log`。

长动画处理按真实存储量计费，保留原始 FPS 和尾帧；仍有采样数、求值工作量及文件大小保护。具体修复和边界见 [SKELETAL_ASSETS.md](SKELETAL_ASSETS.md#长动画与正式启动入口)。

## 缺少程序或资源

若 Release 程序缺失，在项目根目录执行：

```powershell
cmake --preset windows-release
cmake --build --preset windows-release --target skeletal_asset_editor -j2
```

角色与裙摆资产的生成方法见 [CLOTH.md](CLOTH.md)，头发资产及 30 Hz 配置的生成方法见 [HAIR.md](HAIR.md)。脚本不会自动下载资源、改写源模型或运行构建。

脚本等待编辑器退出，并返回编辑器的退出码。缺少程序、角色、头发资源及项目目录不可用时分别返回 `1`、`2`、`3`、`4`。错误时会暂停，便于双击启动时查看原因；自动化运行可先设置 `$env:SKELETAL_EDITOR_NO_PAUSE = '1'` 关闭错误暂停。

### 皮肤材质参考预览与中键平移

视口内按住鼠标中键拖拽可平移相机中心；RMB 旋转，滚轮缩放。
手动平移会解除骨骼中心追踪，Frame model / Focus selected bone 可重新构图。

杰洛特原分辨率模型与皮肤配置位于 `E:\Asset\Witcher3Save1`。
运行 `PreviewGeraltSkin.cmd` 查看面部近景，`PreviewWitcher3Save1.cmd` 查看全身。
首次导入高分辨率模型可能需要约一分钟或更久。

```powershell
output/Release/bin/skeletal_asset_editor.exe --source E:/Asset/Witcher3Save1/Geralt_CurrentSave_Editor.glb --pipeline pbr --skin-profile E:/Asset/Witcher3Save1/Skin/GeraltSkin.profile.json --bind-pose --focus-bone head --distance 1 --yaw 3.32 --no-cloth --no-hair --max-fps 30
```

`--skin on|off` 切换皮肤响应；`--lighting studio|side|back` 选择布光。
右侧 Skin / lighting 提供相同开关和原始材质参数。选择灯光预设时按当前
相机方向布置灯光，之后旋转相机不会带动灯光。配置仅按精确材质名应用，
未加载配置时保持标准 PBR。关闭 Skin 仍保留 Ambient.G 的正确粗糙度基线。

脸配置使用从本机游戏编译 shader 恢复的 RGB 漫反射核、微法线和
直射/环境反射分别的 F0 与粗糙度；控制图 R/G/B 分别为 detail mask、
roughness、specular input。它不是 AO/厚度图，法线 Alpha 也不是脸粗糙度。
完整游戏环境、阴影透射和色调映射尚未逐帧匹配，厚度透射默认关闭。
配置的 `source` 保存证据而非运行时 shader 参数；无控制图时的通用 RGB wrap
和双 GGX 仅为编辑器近似。相关数学、原图和证据在 Asset 的 Skin 子目录。
