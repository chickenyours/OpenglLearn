# 3ds Max 高模导出与布料子模型识别

当前实例来自 3ds Max 2026 已打开的 `file.max`（3D 洛丽塔女仆人物）。完整交换文件是 [`scene.fbx`](../../Asset/Model/LolitaMaid/scene.fbx)，采用二进制 FBX；相邻的 [`scene.fbx.max.json`](../../Asset/Model/LolitaMaid/scene.fbx.max.json) 保存来源对象、图层、父节点、材质、真实修改器及逐件审查的分类。`export_report.json` 记录导出参数、源文件哈希及贴图重定位。

直接运行项目根目录的 [`StartLolitaModel.bat`](../../StartLolitaModel.bat)，加载已生成的 [`LolitaMaid/Package.asset.json`](../../Asset/Generated/LolitaMaid/Package.asset.json)。默认打开网格分类面板、按身体范围构图，并暂停显示高模；60 FPS 是显示上限。本机 Intel 集成显卡的暂停预览实测为 16.06 FPS，详细条件见验证记录。

## 场景与导出

原场景包含 63 个网格、1,118,420 个顶点、1,825,716 个三角形。导出没有执行减面或网格合并，实例展开，保留对象分段、UV、法线及材质。FBX 大小 191,867,856 字节；33 张 JPG 已复制到 `Textures` 并嵌入 FBX。原贴图指向不存在的 `y:/output_wang/...`，桥接仅按同名文件在当前源模型目录中查找，遇到缺失或歧义立即报错。

导出工具 [`Tools/max_export_scene.py`](Tools/max_export_scene.py) 在 Max 内运行。它暂时将贴图引用定位到导出目录，在 `finally` 中逐项尽力恢复原引用、FBX 导出设置及原始场景 dirty 状态，不保存 `.max`，不修改几何或当前姿态。恢复失败会保留原始导出异常并报告恢复问题，场景尽可能保持 dirty，避免隐藏残留改动；此恢复逻辑用于之后的导出，不会猜测并清除本次已有场景的 dirty 标志。输出目录必须不存在或为空。可在 Max 的“脚本 → 运行脚本”运行一个调用入口：

```python
import importlib.util
from pathlib import Path
root = Path(r'C:\Users\16620\Desktop\projects\OpenglLearn')
spec = importlib.util.spec_from_file_location('model_max_export', root / 'Module/Model/Tools/max_export_scene.py')
bridge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bridge)
bridge.export_scene(root / 'Asset/Model/NewMaxExport')
```

Max 单位为毫米，FBX 导出设置为米、Y 向上。本次实例部分网格的局部顶点使用厘米量级数值，但完整实例变换还含 0.01 缩放；**应用完整实例／绑定变换后的粒子坐标已经是米**。2026-10-18 对 28 个头发和衣物网格逐一核对，缓存世界包围盒与原 Max 毫米包围盒换算到米后的最大误差约 `1.06e-7 m`。先前把局部数值解释为最终物理单位的说明已更正，诊断见 [头发与裙摆扫描](LOLITA_HAIR_SKIRT_SCAN.md)。未来为该实例生成物理资产应显式采用 `unitsPerMeter=1`，不要使用含展示背景的场景高度除以 1.6 再推算比例，也不要叠加一次 0.01 缩放。

## 导入模块

```powershell
.\output\Release\bin\skeletal_asset_import.exe `
  --source Asset/Model/LolitaMaid/scene.fbx `
  --output Asset/Generated/LolitaMaid_New
```

导入器自动读取 `<完整FBX文件名>.max.json`。当前 FBX 的完整 SHA-256 必须匹配旁车记录；对象名称必须唯一并精确映射。陈旧哈希、重名、未知对象和非法字段都会拒绝导入。没有旁车的普通 FBX 仍保留节点、父组和材质语义，执行既有保守分类。

最终中间资产保留 63 个网格、1,825,716 个三角形和 52 个材质。FBX 导入后有 5,477,148 个面角顶点；源网格的 UV、法线及面角分段导致顶点展开，不代表新增三角形或减面。最终 32 张纹理均为 RGBA8，二进制载荷为 2,042,920,580 字节。

此模型没有作者骨骼绑定、作者动画序列或 Cloth 修改器。导入日志中的 264 个“Bones”实际上是用于刚性变换的场景节点，不是已完成的角色骨骼绑定；作者动画数为 0、作者 Cloth 数为 0。生成的 BindPose/RigPreview 等检查序列也不应当作原作者动作。没有作者动画的 FBX 默认暂停预览，避免持续重算高模蒙皮。

## Max Physical 材质与大资产保存

源 Max Physical 材质的 cutout 灰度按连续透明度导入，合入基础色 Alpha，并使用现有 **Blend** 流水线，保留头发卡片和蕾丝的细小覆盖；灰度语义见 [Autodesk Physical Material 文档](https://help.autodesk.com/cloudhelp/2026/ENU/3DSMax-Lighting-Shading/files/GUID-911073ED-9A73-482A-A274-6DED542F6DA3.htm)。仅在识别到对应 Max Physical 原始字段时应用该回退；粗糙度标量根据作者的 `roughness_inv` 标志解释，反转时使用 `1 - roughness`，不会盲目反转已映射的粗糙度贴图。**现有 Blend 材质不参与阴影投射**，因此透明发片和蕾丝的地面影子仍受这一管线限制。复杂 Arnold／Max 贴图网络不保证与离线渲染完全一致，无法完整转换的字段保留导入警告。

8 位、Alpha 为 255 的基础色与 8 位灰度透明度合成，可无损保存为 RGBA8；浮点来源保留浮点精度。最终包只保留 PBR 八个纹理槽实际引用的图像，并重映射索引，移除已经被合成结果替代的未用原色图／遮罩副本。源 FBX 和导出的 33 张 JPG 保持完整，不做降采样。

保存使用 64 KiB 流式缓冲与递增 FNV；加载使用同等缓存和最多 256 KiB 的块校验索引，不再同时分配完整二进制副本。保存也不再复制全部蒙皮、表情和动画。仍保留原有 v1 字节格式、2 GiB 载荷上限、候选包完整重载验证、范围／有限值／重叠检查、原子发布与失败清理；块校验会拒绝初次校验后发生变化的实际读取数据。

## 衣物分类

对象和材质原名均为编号，单靠名字无法可靠判断全部衣物。本次对全部 63 个隔离网格逐件查看，并结合原始位置核对，产生明确的外部分类标注。它与通用名称识别分开记录在证据中：`userOverride=true` 表示明确标注；不是声称命名算法自动理解了编号。可重用标注见 [`LolitaMaid.mesh-regions.json`](../../Asset/ImportProfiles/LolitaMaid.mesh-regions.json)，只能用于哈希匹配的该次导出。

分类为：19 个 Garment、19 个 Body、9 个 Hair、16 个 Accessory；展示背景、底座、硬质鞋底、鞋扣和纽扣没有纳入衣物。

| 源对象编号 `3DXY_geometry_` | 衣物构件 |
|---|---|
| 005、016 | 头顶蝴蝶结、头饰织物褶边与飘带 |
| 011、013、028 | 衣领、领口垂带、胸前蝴蝶结 |
| 018、019、026 | 两道裙边蕾丝、腰部褶边 |
| 020、044、045 | 内衬裙片、上层裙摆、下层裙摆 |
| 022、053、056、061 | 门襟、腰身、胸衣蕾丝、长袖上衣 |
| 030、032、041、042 | 靴／袜筒前侧结饰与褶边、长筒覆盖层、踝部结饰 |

030、032、041 的外形可识别为服装构件，但灰模不能确认其为纺织物、皮革还是较硬的鞋靴材料；标注证据保留了这一限制。**Garment 是衣物候选，不表示已经具有布料物理资源。** 源场景无 Cloth 修改器；当前也未自动生成可动区域、固定点、代理网格或碰撞体。

可视核对图：[001–021](../../build/max_export_20261017/mesh_inventory/mesh_inventory_01.png)、[022–042](../../build/max_export_20261017/mesh_inventory/mesh_inventory_02.png)、[043–063](../../build/max_export_20261017/mesh_inventory/mesh_inventory_03.png)。图片中的各对象独立缩放；判断实际大小和位置应使用旁边 JSON 的原始世界包围盒。

在编辑器打开 **Mesh Regions / 网格分类**，选择 **Garment** 可仅显示 19 个候选网格。搜索支持原始对象号、材质和分类证据中的中文标签；选中后用 **Isolate selected section** 单独检查，再点击 **Frame visible**，按当前可见网格重新构图。**Show all** 恢复全部类别。展开 **Recognition evidence** 查看标签与来源，修改后用 **Export revision** 保存。

也可直接以衣物筛选和衣物范围构图启动：

```powershell
.\StartLolitaModel.bat --region-category garment --frame-region garment
```

`--region-category` 控制可见分类，`--frame-region` 控制取景范围；两项可以独立设置。衣物分类和取景都不会自动启动布料物理。

## 双精度兼容性

实际 FBX 揭示了 Assimp 6.0.5 的材质校验问题：UV 变换编码为 5 个 double、40 字节，旧 validator 仅接受 Float 类型。本项目通过 [`PatchAssimp605MaterialDouble.cmake`](../../cmake/PatchAssimp605MaterialDouble.cmake) 修正编码类型校验和旧显式模板属性写入，保留 `aiProcess_ValidateDataStructure`。

固定 FetchContent 源码自动应用补丁；显式提供的外部源码只检查，需使用者对自己的可写 6.0.5 源码运行该脚本。补丁核对版本、官方文件完整哈希及精确幂等状态，无法识别的修改不会被覆盖。

## 验证记录

| 验证 | 当前结果 |
|---|---|
| 最终材质、精度、Max 元数据、资产 IO、运行时及服装相关回归 | [final_blend_tests.log](../../build/max_export_20261017/final_blend_tests.log)：12 项全部通过，21.83 秒；含连续 Alpha、历史 v1 字节兼容、跨块纹理／蒙皮、反序范围读取及截断检查 |
| 完整 FBX 导入和候选包重载后保存 | [streaming_import.log](../../build/max_export_20261017/streaming_import.log)：成功发布 `Asset/Generated/LolitaMaid/Package.asset.json`；上述网格、材质及分类数量取自实际导入 |
| 最终缓存独立加载 | [final_cache_inspect.log](../../build/max_export_20261017/final_cache_inspect.log)：原生加载器完整校验通过；35 Opaque、17 Blend，几何和纹理二进制载荷未改动 |
| 最终 GPU 全身／衣物筛选预览 | 正式启动脚本两次均退出 0；[全身预览](../../build/max_export_20261017/lolita_full_preview.png) 显示全部 63 个网格，头发与蕾丝可见；[衣物预览](../../build/max_export_20261017/lolita_garment_preview.png) 仅显示 19 个 Garment，日志见 [final_full_preview.log](../../build/max_export_20261017/final_full_preview.log)／[final_garment_preview.log](../../build/max_export_20261017/final_garment_preview.log) |
| 正式启动脚本与帧率测量 | [final_benchmark.log](../../build/max_export_20261017/final_benchmark.log)、[lolita_benchmark.json](../../build/max_export_20261017/lolita_benchmark.json)：Release、可见窗口、885×749、Intel RaptorLake-S Mobile Graphics、关闭垂直同步、上限 60 FPS；排除初始化与 60 帧预热后测量 120 帧，平均 16.06 FPS、帧时间中位数 55.79 ms／P95 75.09 ms；稳定后 CPU 蒙皮及上传均为 0 ms，GPU 场景跨度均值 57.58 ms |

本次性能数据仅代表该高模的暂停预览。模型没有物理资产，求解步数为 0；不能将该结果作为布料、头发或角色动画性能。未修改系统 GPU 偏好或电源策略。

原始 `.max` 文件 SHA-256 再次核对未变：`37a0ddc65e913e6ba1cc0a229b65c725f1507f6dcc09b9ed6f7aa129a85d5a44`，记录见 [final_source_integrity.json](../../build/max_export_20261017/final_source_integrity.json)。最终 17 个 cutout 材质的模式更新保留了原 JSON 备份，变更记录见 [opacity_blend_cache_update.json](../../build/max_export_20261017/opacity_blend_cache_update.json)；几何、纹理与二进制 FNV 不变，并由最终缓存加载再次校验。
