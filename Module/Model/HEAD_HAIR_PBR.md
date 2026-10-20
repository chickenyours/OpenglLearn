# 头部与长披发 PBR 预览

这是头发物理研究前的静态准备视图。运行项目根目录的 [`StartLolitaHeadHairPBR.bat`](../../StartLolitaHeadHairPBR.bat)，加载独立的 [`LolitaHeadHair`](../../Asset/Generated/LolitaHeadHair/Package.asset.json) 包。原全身模型和原启动入口保留。

## 模型范围

该包实际只保存并加载以下 23 个源网格及它们引用的材质、贴图，不是加载全身后隐藏绘制：

- 14 个头颈／面部构件：002、003、004、006、009、010、012、017、031、033、040、050、059、060，包含眼部各层、双耳、嘴唇、牙齿和口腔。
- 9 个头发子层：047、048、049、051、052、054、055、057、058。保留内层帽壳、刘海、侧发和长披发；不保留蝴蝶结、头饰、衣服、四肢、身体和展示背景。

031／033 的世界高度处在眼部范围，属于眼缘细条／眼线等层；007／008 是手脚附近细节，未混入头部。选取名单与源包身份、载荷哈希绑定在 [`LolitaMaid.head-hair.json`](../../Asset/ImportProfiles/LolitaMaid.head-hair.json)。

| 内容 | 独立头发预览包 |
|---|---:|
| 网格 | 23 |
| 原网格顶点 | 671,593 |
| 面角展开渲染顶点 | 2,861,088 |
| 三角形 | 953,696 |
| 材质 | 21 |
| 运行时贴图 | 11，保留原分辨率和像素 |
| 二进制载荷 | 1,079,834,920 字节，原全身包为 2,042,920,580 字节 |

原始顶点、索引、完整蒙皮数据和贴图范围按字节复制，没有减面、图像降采样或精度转换。原 264 个场景节点和对应刚性绑定保留，以维持原始变换关系；这不代表模型已经具备人物骨骼或毛发骨链。完整实例变换后的米制坐标也保持不变，未来物理应使用 `unitsPerMeter=1`，不能根据头部高度推算一个 1.6 m 人物比例。当前没有作者头发物理资源。

## 显示与操作

入口默认使用现有 Material／Render 模块的 **PBR** 管线：中性环境、主光／补光／轮廓方向点光、连续粗糙度高光、ACES 与 FXAA。保留导入基础色、粗糙度和透明发丝；PBR 后处理完成后的画面在 GPU 内复制到编辑器视口，不逐帧回读 CPU。

角膜／眼部湿润层有一项明确的预览材质例外：源 `3DXY_Material_51` 的 Max Physical 原始参数为 `transparency=1`，旧导入结果却是不透明黑色，遮住了虹膜。本包通过 profile 中的 `materialOverrides` 将它替换为低 Alpha 的透明薄膜（白色底色、Alpha 0.06、粗糙度 0.08、Blend）。这只作用于派生头部预览，原全身包和 `.max` 均不改写；它是现有 PBR 的透明近似，不等同于作者折射和吸收效果。几何与纹理字节仍保持原样，变更记录见 [eye_preview_override.json](../../build/lolita_head_hair_20261018/eye_preview_override.json)。

本视图采用低开销配置：不启用 GI、平面反射、动态阴影、SSAO、Bloom、描边或地面，默认暂停动画与物理。显示上限为 **30 FPS**，失焦沿用编辑器的后台节流；并非保证所有显卡达到该帧率。当前仍是各向同性 PBR，尚未增加专用毛发各向异性高光、皮肤 SSS 或 Strand/Groom 渲染。

Windows 预览器导出 `NvOptimusEnablement=1`，按 [NVIDIA Optimus 官方机制](https://developer.download.nvidia.com/devzone/devcenter/gamegraphics/files/OptimusRenderingPolicies.pdf)请求高性能显卡，不改写系统 GPU 或电源设置；系统及用户应用配置仍可优先于该请求。本机实际已选择 RTX 4060，最终测量见下文。启动默认按精简包自身中心和高度取景，`--distance` 仍可调整距离；**Frame model** 则按当前网格包围范围重新适配。

- 在视口上方的 **PBR／Toon** 下拉菜单切换管线；右键拖动旋转，滚轮缩放，**Frame model** 重新取景。
- 网格分类中选择 **Hair** 可单独查看头发，选择 **Body** 可单独查看头部；**Show all** 恢复组合。
- `--no-hair` 在此入口仅关闭头发物理，不隐藏头发网格；`--no-cloth` 关闭布料物理。

```powershell
# 默认：头部与全部头发，PBR，30 FPS 上限
.\StartLolitaHeadHairPBR.bat

# 后侧观察长披发
.\StartLolitaHeadHairPBR.bat --yaw 2.8

# 仅观察头发，或调整帧率上限
.\StartLolitaHeadHairPBR.bat --region-category hair
.\StartLolitaHeadHairPBR.bat --max-fps 60

# 同一精简包使用卡通预览
.\StartLolitaHeadHairPBR.bat --pipeline toon
```

## 重建资产

首次开发已生成独立包；正常启动无需再提取。需要重新生成时，使用任意可用 Python 3.10+，输出必须为不存在的新目录：

```powershell
python Module/Model/Tools/extract_preview_subset.py `
  --package Asset/Generated/LolitaMaid `
  --profile Asset/ImportProfiles/LolitaMaid.head-hair.json `
  --output Asset/Generated/LolitaHeadHair_New

.\output\Release\bin\skeletal_asset_import.exe `
  --inspect Asset/Generated/LolitaHeadHair_New/Package.asset.json
```

工具按 64 KiB 分块复制选中的载荷区间，重新计算完整新包 FNV，稳定重映射材质、八个纹理槽、实例和分类；表情与表情轨道也按保留网格过滤。它保留骨架、清除不适用的外部动画布局哈希，并在源文件尺寸／时间戳变化或目标已存在时拒绝发布。原包不改写。默认不重新扫描原 2 GB 包的全文件校验和，该限制会记录在新包诊断中；可额外使用 `--verify-source`，但无论如何都应以原生检查器验证最终派生包。

缺少 Release 程序时：

```powershell
cmake --build --preset windows-release --target skeletal_asset_editor skeletal_asset_import -j2
```

### 启动时误报资产缺失

2026-10-18 修复了一次 Windows 目录权限问题：旧版提取工具使用 `tempfile.mkdtemp` 创建候选目录，本机 Python 运行时在 Windows 上为它设置私有权限。候选目录重命名为正式资产目录后仍保留该权限，导致生成账户可以读取，桌面登录账户却无法访问，CMD 的 `if exist` 因而误报文件不存在。

现有 `LolitaHeadHair` 目录已恢复对 `Asset/Generated` 的权限继承，未重写模型或贴图。提取工具现改为使用随机唯一名称和普通目录创建，继承输出父目录权限后再原子发布；不再将私有临时目录直接发布为用户资产。Python 3.12.14 下 10 项工具测试通过，其中 Windows 实测确认发布后的目录、子目录和二进制文件均未保护 DACL 继承。启动项采用脚本目录下的绝对路径，失败提示同时列出完整资产路径和实际启动脚本路径，并明确文件可能缺失或不可访问。

## 验证

验证日志与画面保存在 `build/lolita_head_hair_20261018/`：

| 检查 | 实际结果 |
|---|---|
| 最终程序构建 | [gpu_hint_build.log](../../build/lolita_head_hair_20261018/gpu_hint_build.log)：Release 成功；只出现原有 GLFW 头文件大小写警告 |
| 子集工具测试 | [subset_tests.log](../../build/lolita_head_hair_20261018/subset_tests.log)：8 项通过，含区间拷贝、材质／表情引用重映射、跨缓冲和浮点纹理保留、材质覆盖校验与失败回滚、无覆盖发布 |
| 模块基础回归 | [native_tests.log](../../build/lolita_head_hair_20261018/native_tests.log)：`model_lifecycle_test`、`skeletal_asset_io_test` 全部通过 |
| 原生完整加载 | [inspect.log](../../build/lolita_head_hair_20261018/inspect.log)：新包完整载荷哈希、几何、蒙皮、材质和引用验证通过，确认 23 网格／21 材质／11 贴图 |
| 与源数据逐范围核对 | [byte_verification.json](../../build/lolita_head_hair_20261018/byte_verification.json)：103 个区间、1,079,834,912 个数据字节均与源包相同；其余 8 字节为不变载荷头，除声明的眼部薄膜覆盖外材质及骨架核对通过 |
| 正式启动项 PBR 画面 | [正面](../../build/lolita_head_hair_20261018/head_hair_pbr_front.png)、[侧后方](../../build/lolita_head_hair_20261018/head_hair_pbr_back.png)，启动日志为 [pbr_front.log](../../build/lolita_head_hair_20261018/pbr_front.log)／[pbr_back.log](../../build/lolita_head_hair_20261018/pbr_back.log) |
| Windows 桌面账户启动 | 恢复派生资产目录继承权限后，以登录账户 `16620` 从 Desktop 工作目录调用绝对路径启动项，90 帧后正常退出；[日志](../../build/lolita_head_hair_20261018/launch_access_fixed.log)确认 NVIDIA、23/23 网格、PBR，[画面](../../build/lolita_head_hair_20261018/launch_access_fixed.png)显示约 29 FPS；[权限记录](../../build/lolita_head_hair_20261018/launch_access_permissions.json)确认 13 个文件／目录均继承权限；确认测试程序已退出 |
| 卡通兼容性 | [toon_compatibility.log](../../build/lolita_head_hair_20261018/toon_compatibility.log)、[Toon 画面](../../build/lolita_head_hair_20261018/head_hair_toon.png)：同一包使用 `--pipeline toon` 验证 |
| 独立无截图性能采样 | [pbr_benchmark.json](../../build/lolita_head_hair_20261018/pbr_benchmark.json)、[日志](../../build/lolita_head_hair_20261018/pbr_benchmark.log)：RTX 4060 Laptop GPU、885×743、PBR、30 FPS 上限、关闭 VSync；排除初始化与 60 帧预热后测量 120 帧，**29.58 FPS**，帧时间中位数 33.63 ms、P95 33.91 ms、最大 43.48 ms；CPU 蒙皮／上传均为 0 ms，GPU 场景跨度均值约 4.21 ms |

该性能结果针对暂停的准备视图，包含帧率限制等待，不表示未来物理能达到同样帧率，也不是无上限 GPU 吞吐测试。采样期间另有游戏使用独显，未关闭或更改它。核显路径的独立无截图采样曾出现最长约 10 s 停顿，完整记录保留在 [pbr_benchmark_intel.json](../../build/lolita_head_hair_20261018/pbr_benchmark_intel.json)；改为实际 RTX 4060 后，上述最终采样未再出现长停顿。更早含末帧截图的采样另存为 `pbr_benchmark_initial.json`，未当作稳定性能结果。

后续头发物理所需的代理、发根固定、碰撞和高模变形绑定见 [头发／裙摆扫描报告](LOLITA_HAIR_SKIRT_SCAN.md)；本次准备阶段没有新增求解器。
