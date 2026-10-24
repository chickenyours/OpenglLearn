# Graveyard Scene Lab

基于本机《Graveyard Keeper 2》构建的可行走场景研究。双击 `Run.cmd`，WASD 或方向键移动；沿石崖中间的坡道可走上高地。窗口左侧可暂停时间、切换日夜、调节水流和风、关闭落叶和阴影、查看真实高度颜色及像素放大镜。没有任务、背包、物品或建筑交互逻辑。

此模块实际复用 `ApplicationWindow::Window` 开窗，以 `Render::OpenglBackend + RHIResourcePool` 创建纹理、顶点缓冲、GLSL、Pipeline、std140 UniformBuffer、颜色与 Depth32F RenderTarget，并提交场景 Draw。ImGui 仅承担工具面板；原生 GL 调用仅查询诊断及导出 framebuffer 像素，不用于绕开 Render 绘制场景。

## 运行与检查

```powershell
cmake --build build --target graveyard_scene_lab --parallel 4
.\bin\graveyard_scene_lab.exe
.\bin\graveyard_scene_lab.exe --smoke-test
.\bin\graveyard_scene_lab.exe --capture-dir Module\GraveyardSceneLab\Captures\final
.\bin\graveyard_scene_lab.exe --capture-ui Module\GraveyardSceneLab\Captures\final\ui.png
.\bin\graveyard_scene_lab.exe --capture-motion Module\GraveyardSceneLab\Captures\motion
.\bin\graveyard_scene_lab.exe --capture-lighting Module\GraveyardSceneLab\Captures\lighting-v3
.\bin\graveyard_scene_lab.exe --capture-stability Module\GraveyardSceneLab\Captures\stability-v3
ctest --test-dir build -R "^graveyard_scene_lab_smoke$" --output-on-failure
```

`--capture-dir` 自动导出日、黄昏、夜景，以及水、植物、落叶、光影、高度、上下坡和前后遮挡对照；`capture-manifest.json` 固定记录每张图的时间、相机、风量、光照开关和人物坐标。`--capture-frames N` 可另外导出连续帧。`--capture-motion` 分别导出水面和风/落叶场景各 24 帧、12 FPS 的固定时间序列。`--assets MODULE_DIR` 可指定完整模块根目录。UI 捕获先预热 60 帧，再记录 120 帧，测量范围为当前机器上的编辑器、场景和呈现，见 `Evidence/performance-validation.json`，不能当成跨设备性能保证。

静态模型、其投影阴影、地表和草牌使用首次可见时创建的 RHI Static VBO 缓存；改变风、时间、光照和相机仍由 Uniform 与 Shader 动态处理。人物与落叶保持 Stream 更新。仅本演示 target 在 Debug 下开启 O2 并保留调试符号，共享 Render 和全局 Debug 配置不变。

改进版太阳阴影采用独立的 1536×1536 正交光相机，RGBA8 保存打包深度，Depth32F 附件执行真实遮挡比较，资源和绘制均经过 Render。太阳和颜色视图展开同一份 `scene.vert`，共享树风、草风与人物位置；原贴图透明度低于 .5 的片元不投影。地表、斜坡、岩壁、原模型和人物均可投影或接收，落叶粒子和光晕不投影。默认关闭旧的平面剪影，可在 `Light and shadow study` 中切换对照，并调节太阳方向、仰角、PCF、偏移和接触 AO。

光相机固定在世界中，移动观察相机不使影图失效；风和人物改变时使用当前帧时间重画影图。旧版 12Hz 更新让投影姿势落后于正在绘制的树冠，现已取消。太阳通道按原素材图集合批，实例的风相位、振幅由顶点元数据携带，避免用大量单独绘制换取同步。`--capture-lighting` 额外导出太阳阴影、软化、接触 AO、光照与后期的单项对照，以及真实影图读回的灰度图和逐张参数。屏幕接触 AO 使用场景 Depth32F，补充物体交界处的小尺度遮挡；它依赖当前屏幕可见深度，不能替代完整空间中的太阳阴影。

云影使用原 `Clouds Shadow Procedural` 的 32³ RGB 噪声数据，重排为带一像素循环边界的 272×136 图集；`Assets/cloud-noise-manifest.json` 保存原数据来源和 SHA256。通过 Render 先把固定世界范围 `(0,0)..(2000,1700)` 计算为 256² 云遮罩，再由场景采样遮罩 Alpha。迁移采用 Linear/Repeat：图集在 XY 线性过滤、相邻 Z 层手动插值，并循环体噪声坐标。原 Texture3D 的保存设置是 Point/Clamp，但 Shader 独立 inline sampler 的实际模式尚未解码，不能据此断言游戏实际采样方式。云遮罩随当前帧更新，UI 可调密度和强度。交互时风偏移累计 `(.01+wind*.15)*dt`；固定截图使用 `(.01+wind*.15)*time` 获得可复现状态，两者适用条件记录在参数清单中。默认晴日密度 .47 与环境光倍率 1.75 是迁移展示校准，原云材质密度 .643 仍可在 UI 中输入。

地表、角色和模型使用关闭混合的实心裁切通道，只有叶粒、光晕和旧剪影使用透明混合。原模型水平底面、岩帽与地表可能共面；运行时给它们分配稳定的微小 clip-depth 层序，保持真实世界坐标和脚下高度不动，避免等深时随提交顺序换面。`--capture-stability DIR` 验证冻结风水的重复帧、正反 opaque 提交顺序、整像素相机平移后重投影的误差，以及 60 帧动态风的太阳/云时间同步；结果同时写入 `Evidence/stability-validation.json`。

当前完整太阳通道为 23 次 RHI Draw；动态风的 60 帧验证中，太阳与云遮罩各更新 60 次，相对颜色视图时间延迟为零。本机验证中，13 帧冻结效果与相同像素对齐相机的图像变化为零；反序提交 opaque 模型和地表的图像变化也为零。整数相机平移的全图重投影仍包含遮挡轮廓、屏幕 AO 与暗角差异，报告保留实际误差，不把移动镜头称作逐像素不变。正常构建与启动检查日志为 `Evidence/normal-build-v3.log`、`Evidence/run-smoke-v3.log`，连续帧日志为 `Evidence/stability-v3.log`。

四种真实地表图块为草、干草土、阴影草和黄褐土，采用 192 世界单位对应 192 原像素的周期，并在世界范围中作稀疏混合。素材来源见 `Assets/terrain-v3-manifest.json`。叶粒按八种原纹理合成八个持续复用的 Stream 缓冲，每片叶子的旋转、位移和明暗相位独立保留；透明叶片互相重叠时的混合顺序有少量变化。固定场景纹理按视图绑定一次，避免每片模型重复进行后端设备限制查询。

水现在独立绘制：先绘制包含 25 像素深河床的 opaque 颜色和 Depth32F，再通过 RHI 全屏复制颜色和真实深度，最后在河道内绘制 Y=0 水面。水读取 opaque 颜色/深度形成吸收与折射，读取 640×360 的镜像场景 RT 形成岸边景物反射；水和透明粒子不会递归进入反射。原作证据指向 reflection probe 立方体；这里的平面反射属于新增迁移方案。`--capture-lighting` 含水深、折射、反射独立对照、`opaque-only.png`、`water-depth-debug.png` 和 `reflection-only.png`，UI 可分别关闭。

四种草/花/芦苇纹理的 Alpha 只有 0 和 255，且草分支不使用单物体随机参数，因此按素材合成四组草牌提交；保留原逐物体可见性判断，只有可见索引集合改变时才更新组缓冲。第一轮缓存与草牌合批曾通过全部 22 张固定参数 GPU 截图的逐像素一致检查，见 `Evidence/static-cache-image-comparison.json` 与 `Evidence/plant-batch-image-comparison.json`。它们记录历史性能优化，不代表第二轮光影与旧图或原游戏逐像素一致。

## 原作证据与迁移范围

`Assets/provenance.json` 记录原始 Mesh/Texture/CAB/pathID、原层级矩阵和重新打包的纹理图集。11 个模型包含原树、灌木、完整教堂、墓碑和墓边框、灯、花床、木桥；保留原三角形、UV、法线与顶点色风权重。`Tools/prepare_scene_assets.py` 可从 `E:/Asset/GraveyardKeeper2Scene` 的三份提取清单重新打包。只重排图集及坐标，不修改游戏安装目录。

原作相机 X 角为 53.130104 度，投影系数 .6/.8。这里地图南向 Z 对应 Unity 的负 Z，因此屏幕向下量为 `.8*Zsouth-.6*Y`，真实深度为 `-(.6*Zsouth+.8*Y)`。高度来自地表与连续坡道网格，移动时检查脚下支撑高度，树和人物由 GPU 深度遮挡。相机和角色画面位置作像素对齐。原作树风使用 `COLOR.r^3`；草使用根部为零的 `pow(UV.y,p)`，并保留嵌套正弦世界相位和共享 Simplex 阵风；人物四方向帧在同一个画布与脚点上切换，避免每帧裁剪造成体型缩放。

场景布局、坡道和简化地表由本模块重新编排。水保留原绿通道噪声纹理、`n²+clamp(n⁴)` 主波、两路反向次波和 Voronoi 岸泡的子集；吸收、屏幕折射、平面反射和深度颜色属于迁移实现。树冠采用恢复的值噪声微扰公式，但使用现有树层的单层近似。日照、环境光和背光采用恢复的昼夜 preset 关键颜色；太阳影图、屏幕接触 AO、云影和光晕由本模块实现，没有完整搬入 Unity 的级联阴影、屏幕草形变场或原场景存档。

昼夜预览从原作的中午、黄昏、夜间三个节点取值，滑杆按夜→黄昏→中午插值，不是原归一化时间，也没有完整迁移 14 节点的全天插值规则。树冠恢复红通道双阈值的切线法线分档；Back Light 取太阳反方向。局部灯使用三维距离衰减与 N·L，颜色后期为恢复参数的轻量近似。

`--smoke-test` 验证资源、24 组 GPU 状态、真实坡道行走、崖边高度限制、相机跟随、单项效果像素差、人物前后遮挡和逆绘制顺序下的 Depth32F 遮挡。结果见 `Evidence/gpu-validation.json`。第三轮逐环节效果图、官方截图对照与通俗说明见 `Report/index.html`，第一轮基础报告保留为 `Report/foundation.html`。本机最新核显预览实测约 19 FPS，性能尚属技术样板水平。
