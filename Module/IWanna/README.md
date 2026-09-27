# I WANNA BE THE KING — ECS + Module 移植

## 编译、运行

在 OpenglLearn 根目录运行 `build.bat`，然后运行 `run_iwanna.bat`（或 `bin/iwanna_game.exe`）。
默认窗口为 **1920×1080**，16:9 等比显示。地形现使用规则 TileMap，编辑方法见 [瓦片地图与像素素材](../../Asset/IWanna/TILEMAP.md)。
`build.bat` 可从其他工作目录调用；配置或编译失败会返回非零退出码。

- A / D：左右移动；J：跳跃，可二段跳。方向键、空格也可使用。
- R：从最近存档点复活，同时复位苹果陷阱和消失平台；通关后仍可重试。
- Esc：退出。失去焦点时不读取游戏按键。
- `--mute`：关闭音频设备；`--assets <目录>`：加载另一份关卡资源目录。
- `--config <文件.json>`：指定玩法配置；`--export-masks <目录>`：导出实际参与判定的逐帧蒙版后退出，不创建窗口。
- `--smoke-test --capture <文件.png>`：三帧 RHI/GPU 验证，截图后退出。
- 窗口标题每秒显示实际完成帧的 FPS。
- `--benchmark 90 --metrics <文件.csv>`：完整物理与渲染回放基准，自动关闭垂直同步；加 `--benchmark-vsync` 可保留正常垂直同步。

资源通过 CMake 的 `iwanna_assets` 目标复制到 `bin/IWanna`。运行不依赖原 funcode 目录、其 DLL 或 Python。

## 调整移动、跳跃与重力

编辑 [Asset/IWanna/gameplay.json](../../Asset/IWanna/gameplay.json)，重启 `run_iwanna.bat` 即可生效，无需重新编译。该启动脚本显式读取源目录配置；直接启动 EXE 默认读取 `bin/IWanna/gameplay.json`。配置错误会报告字段名并停止启动。

| physics 字段 | 推荐值 | 作用 |
| --- | ---: | --- |
| runSpeed | 18 | 水平速度，世界单位/秒 |
| jumpSpeed | 56 | 首跳向上初速度，填写正数 |
| doubleJumpSpeed | 52 | 二段跳向上初速度，替换当时的竖直速度 |
| gravity | 100 | 向下加速度，世界单位/秒² |
| maxFallSpeed | 65 | 最大下落速度 |
| fixedHz | 120 | 每秒物理更新次数 |
| maxSubstepDistance | 0.025 | 每个碰撞子步的最大相对移动距离 |

首跳理论高度约 `jumpSpeed² / (2 × gravity)`，推荐值约 15.68，离地到顶点约 0.56 秒；接近顶点时二段跳可累计上升约 29.2。固定步长积分的实际高度略低。推荐值兼顾原关卡约 27.5 的高平台落差，单次高度显著低于旧参数的 93.75。增加重力会缩短滞空时间；降低跳跃速度会降低高度，但过低会使原关卡的高平台无法到达。

`player` 可调整显示尺寸、动画帧数和整段动画时长。当前帧画布为 128×96，世界尺寸 4.8×3.6，脚底对齐。上表是历史推荐值，实际运行以你编辑的 JSON 为准；此次接触求解修改保留了当前首跳 50、二段跳 35、重力 150。

人物现在使用固定的身体内矩形，与动画帧和朝向无关，披风、武器不参与判定。`collision.playerBody` 相对于完整贴图尺寸定义，人物缩放时判定框同步缩放：

| 字段 | 默认值 | 含义 |
| --- | ---: | --- |
| widthRatio | 0.28 | 矩形宽度占贴图宽度的比例，当前约 1.344 世界单位 |
| heightRatio | 0.72 | 矩形高度占贴图高度的比例，当前约 2.592 世界单位 |
| offsetXRatio | 0 | 中心相对贴图中心的水平偏移比例 |
| offsetYRatio | 0.04 | 中心竖直偏移比例，正数向下 |

`collision.contact` 控制移动容错，所有距离均为世界单位：

| 字段 | 默认值 | 含义 |
| --- | ---: | --- |
| skin | 0.02 | 地形接触余量，避免浮点误差导致两轴互相卡住 |
| stepHeight | 0.12 | 有地面支撑时允许跨越的细小台阶；不在空中抬升人物 |
| groundSnap | 0.06 | 原本着地时向下贴地的最大距离；离开平台后正常下落 |
| recoveryDistance | 0.35 | 轻微嵌入地形时寻找脱离位置的最大距离 |
| wallSlide | 0.10 | 空中贴墙遇到小凸沿时允许横向让位的最大距离 |

提高 `stepHeight` / `wallSlide` 会增加边缘容错，不宜设得过大。脱离重叠只修正位置并清除朝向障碍的速度，不添加上升速度；超出恢复范围的深度嵌入不会被任意传送出墙。

地刺、其他陷阱和地形仍使用 alpha 蒙版，与人物内矩形求交。`collision.alphaThreshold` 默认 128（1–255）；提高阈值会忽略更多半透明边缘。地形接触使用 `skin` 容差，危险物判定使用完整内矩形。

## 原项目分析及提取

原版逻辑集中在 `SourceCode/Src/Main.cpp`；`LessonX.cpp` 基本是空模板。
`Bin/game/data/levels/level.t2d` 保存场景，`managed/datablocks.cs` 保存图像映射及动画帧。
没有链接、执行或移植 funcode/Torque 引擎 DLL。

`Asset/IWanna/reference` 保留原始代码、关卡和 datablocks，保持原始字节和编码。
`Asset/IWanna/manifest.json` 记录来源、角色数量和每个提取文件的 SHA-256。
资源来自用户提供的项目，不对原素材重新声明授权。

提取结果为 212 个可渲染实体、16 张实际引用图片、6 个 WAV：
（这是原始提取统计；当前运行将 129 个自由摆放地形对象替换为 946 个规则瓦片，初始共 1029 个 ECS 实体。）
129 块地形、53 个危险物、4 个存档精灵（其中 1 个是原编辑器外围参考）、2 块消失平台、1 个终点，及装饰和角色模板。
关卡中的重复 `cao`/`di`/`ci` 名称各自创建不同 ECS EntityID，不因同名覆盖。
无图像配置的旧角色和挂载辅助对象不作为第二个玩家；最终的 `man` 动画精灵是玩家。
原生文字对象由算法生成的抗锯齿笔画字形 HUD 替代，保留死亡计数和操作提示，不引入字体库。

重新提取（仅开发时需要 Python 标准库）：

```powershell
python tools/extract_funcode.py "<funcode项目根目录>"
```

原代码的移动速度 40、跳跃初速度 -75、重力参数 30 仅作来源记录；当前使用上表推荐参数。保留两次跳跃、存档、死亡位置、终点、两块一起消失的平台，及三处苹果机关。
复活点初值为原源码的 `(-71.351, 23.764)`；第一次入场位置取关卡中的玩家位置。
三处机关目标与速度从旧代码转换为数据：`cao1 → ciapple1 (-100,0)`、`cao2 → ciapple2 (0,50)`、`cao3 → ciapple3 (0,50)`。

刻意修正旧版全局变量带来的状态泄漏：复活清零速度/跳跃次数、所有机关可再次触发、左右同时按下抵消、死亡只计一次、死亡后不继续操控隐藏角色。
固定步长和碰撞求解是独立实现，不声称逐帧复刻旧引擎内部物理误差。旧版在画面外的开始/结束图像保留数据；通关使用可见 HUD，并显示历史死亡位置。

## 模块结构与扩展

```text
ApplicationWindowModule ─ 原有窗口、GLFW输入和 OpenGL context
RenderModule / RHIDevice ─ 原有渲染线程、纹理、网格、UBO、帧命令
  └ SpriteRenderer      ─ 精灵贴图、动画 UV、内置字形和 16:9 等比视口
IWanna::GameModule      ─ IModule 生命周期、关卡加载、固定步长
  └ 现有 ECS::Core::Scene + ECS::System::Pipeline
      AnimationSystem → InputSystem → PhysicsSystem → Extract()
AudioModule             ─ WAV → 浮点混音/重采样 → Output 平台后端
```

所有游戏实体存储在项目原有 ECS 的 archetype/chunk 中，组件注册使用 `REGISTER_COMPONENT`。
组件为 Transform、Motion、Sprite、Collider、Behavior、Player。系统声明组件访问，通过 Context 注入 GameModule；没有另建一套 ECS。
游戏逻辑不包含 GLFW/OpenGL/Windows API，单元测试不创建窗口。声音经 `playSound` 事件回调与 AudioModule 解耦。
渲染使用既有 RHI 的图集、流式网格和管线，将精灵与 HUD 按原始透明混合顺序合成一批，每帧一次 DrawIndexed。上传回调明确保证网格更新完成后再提交帧，避免 RHI 的资源队列和帧队列交错时读取旧数据。
旧贴图使用线性/三线性采样，新瓦片、告示牌及存档点采用独立的最近邻像素采样；动画帧独立打包、外扩 8 像素，采样 mip 层限制在 0–2 以免图集串色。字形采用距离场 alpha 和屏幕空间导数进行抗锯齿。
图集上限 4096×4096，超出会报告错误；扩大素材集时可扩展为多图集批次。
纹理解码使用项目已存在的 stb_image；没有新增图片、字体、物理库。

地刺、刺球、苹果、地形及其他触发物的贴图 alpha 生成逐帧位蒙版，启动时缓存，绘制与碰撞共用动画帧、缩放、旋转、翻转。玩家由独立内矩形表示，不再使用角色像素蒙版。AABB 只做粗筛；窄相检测内矩形与不透明像素面积相交，不把透明空洞算成实体，也不依赖屏幕分辨率采样。轴对齐时使用位集区间查询，旋转时对候选像素矩形进行 SAT 求交。旧关卡的多边形字段保留以兼容格式，不再参与游戏判定。

物理先进行有限距离的重叠恢复，再按 X/Y 分轴解算，碰撞时二分查找接触位置，只清除对应轴速度。使用接触余量、着地微台阶修正、贴地探测和空中横向让位处理格子接缝与墙面错位，保留切向速度，不通过斜面法线生成向上速度。120 Hz 固定更新配合按玩家与机关相对速度细分的子步降低穿透风险；这是离散子步检测，并非任意薄障碍下的连续碰撞保证。新增极薄或高速机关时需相应减小 `maxSubstepDistance`。

蒙版预览位于 [Asset/IWanna/generated_masks](../../Asset/IWanna/generated_masks)，白色为不透明像素，黑色为空。角色蒙版保留供素材检查，玩家实际使用内矩形。改图或改阈值后可以重新导出：

```powershell
bin/iwanna_game.exe --assets Asset/IWanna --config Asset/IWanna/gameplay.json --export-masks Asset/IWanna/generated_masks
```

用户提供的角色图、裁切参数、透明化算法和来源校验记录见 [角色素材说明](../../Asset/IWanna/player/README.md)。没有添加平台依赖；JSON 使用项目现有 JsonCpp，图像使用现有 stb。
游戏模块缓存 ECS 组件视图，并在创建实体后重建，关闭时释放。若以后增加删除/迁移实体的 API，也必须在结构变化后重建视图。
主循环使用 Render 回调队列的条件变量通知，不再靠短暂 sleep 轮询；窗口最小化时仍使用窗口事件等待。

关卡格式 `level.txt` 第一行 `IWANNA_LEVEL_1`，随后每行字段：

```text
"name" "image" "role" x y width height rotation layer flipX flipY collide
columns rows cellWidth cellHeight animationDuration "frame sequence"
polygonPointCount x0 y0 ... "trigger target name" velocityX velocityY
```

上面的换行仅为说明，实际每个实体占一行。名称和图片使用双引号。
较大的 layer 先绘制。坐标保持原关卡 X 向右、Y 向下，当前视区为 `[-75,75] × [-42.1875,42.1875]`。
新增地形编辑 `tilemap.json` 的格子层；危险物/存档点继续使用对象数据。新增苹果式机关只需设置目标名称及速度，不需更改 C++ 条件分支。
新增行为可扩展 Role 与独立 ECS System，再注册到 Pipeline；新增可调物理参数在 Rules 中维护。
当前消失平台属于同一组；若新增多组，可增加 Group 组件并在行为系统按组触发。

## 验证

```powershell
ctest --test-dir build --output-on-failure
bin/iwanna_game.exe --smoke-test --capture bin/iwanna_smoke.png
bin/render_scheduling_test.exe
bin/iwanna_game.exe --benchmark 90 --mute --metrics bin/iwanna_metrics.csv
```

测试覆盖蒙版透明空洞、亚像素重叠、翻转/旋转/非均匀缩放、动画切帧、位集边界、平地移动不升空、推荐首跳高度、顶头停止、最大落速，以及提取数量、二段跳/第三跳拒绝、高速落地、苹果机关、消失平台、存档、重复死亡、复活重置、通关死亡轨迹、短输入保留、音频解码和模块重启。
接触回归另外覆盖：双向跨越高低地砖接缝、左右墙面按键/无按键下滑、浅层嵌入恢复、靠墙移动不爬升、不恢复空中跳跃次数、离开平台不悬浮、披风区域无伤害及身体命中陷阱。轨迹回归使用固定物理测试参数，不强制 JSON 保持历史推荐值。
GPU 冒烟使用实际 ApplicationWindow + Render，等待帧完成后在渲染线程读取截图并检查 GL 错误。
Windows 已构建运行；整个宿主项目仍使用原有 Windows 工具链/库，未宣称整个游戏已在其他系统通过构建。

性能对比、测试口径与原始 CSV 见 [Benchmarks/README.md](Benchmarks/README.md)。
