# I WANNA BE THE KING — ECS + Module 移植

## 编译、运行

在 OpenglLearn 根目录运行 `build.bat`，然后运行 `run_iwanna.bat`（或 `bin/iwanna_game.exe`）。
默认窗口为 **1920×1080**，16:9 等比显示。地形现使用规则 TileMap，编辑方法见 [瓦片地图与像素素材](../../Asset/IWanna/TILEMAP.md)。

另有独立的 **房间资源 + Lua 演示**：运行根目录 `run_iwanna_showcase.bat`，体验移动测试、陷阱和事件展示房间。当前数据位于 `Asset/IWanna/Workshop`，房间使用稳定对象 ID、会话存档和安全阶段命令队列；制作方法见 [编辑器说明](EDITOR.md) 与 [Prefab 接口](PREFAB_API.md)。原启动脚本仍进入旧关卡。
要建立完全独立的内容项目，运行 `bin/iwanna_project.exe new <新目录>`；生成的目录有 Lua、房间、预制体、素材及游戏/编辑器启动脚本。内容开发无需重新编译，见 [项目工作流](PROJECT_WORKFLOW.md)。
`build.bat` 可从其他工作目录调用；配置或编译失败会返回非零退出码。

- A / D：左右移动；空格或 J：轻点小跳、按住跳得更高，松开提前收短。触地起跳后可再空中跳一次；直接落空时仅有一次空中补跳。方向键也可左右移动。
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
| jumpSpeed | 50 | 首跳向上初速度，填写正数 |
| doubleJumpSpeed | 35 | 二段跳向上初速度，替换当时的竖直速度 |
| gravity | 150 | 常规向下加速度，世界单位/秒² |
| jumpHoldSeconds | 0.22 | 每次起跳后，按住跳跃键可延长上升的时间窗口（秒） |
| jumpHoldGravityScale | 0.45 | 按住期间上升重力的倍率；越小越容易跳高 |
| jumpReleaseMultiplier | 0.55 | 提前松键时保留的上升速度比例；越小短跳越低 |
| maxFallSpeed | 65 | 最大下落速度 |
| fixedHz | 120 | 每秒物理更新次数 |
| maxSubstepDistance | 0.025 | 每个碰撞子步的最大相对移动距离 |

轻点跳跃键时，松键会在上升阶段削减一次速度；按住则在 `jumpHoldSeconds` 内使用较低的上升重力，时间到后恢复正常重力。二段跳重新开启同样的时间窗口。只有触地会补满两次跳跃额度；走出平台、从空中出生或下落时，地面跳额度立即失效。当前推荐值下，首跳轻点约为 2–3 世界单位，按满约为 14 世界单位；具体高度受固定步长和接触面影响。这个设计参考[《泰拉瑞亚》官方维基描述的跳跃持续时间与松键提前结束机制](https://terraria.wiki.gg/wiki/Movement_speed)，并非复制其像素速度。提高 `jumpHoldSeconds` 或降低 `jumpHoldGravityScale` 会增大长短跳差异；降低 `jumpReleaseMultiplier` 会让短跳更低。

`player` 可调整显示尺寸、动画帧数和整段动画时长。当前帧画布为 128×96，世界尺寸 4.8×3.6，脚底对齐。实际运行以你编辑的 JSON 为准；旧 JSON 没有三个新字段时使用表中的默认值。

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
bin/iwanna_route_replay.exe path/to/project/world.json path/to/project/CAMPAIGN_ROUTE_CHECK.json
```

测试覆盖蒙版透明空洞、亚像素重叠、翻转/旋转/非均匀缩放、动画切帧、位集边界、平地移动不升空、推荐首跳高度、顶头停止、最大落速，以及提取数量、二段跳/第三跳拒绝、高速落地、苹果机关、消失平台、存档、重复死亡、复活重置、通关死亡轨迹、短输入保留、音频解码和模块重启。
接触回归另外覆盖：双向跨越高低地砖接缝、左右墙面按键/无按键下滑、浅层嵌入恢复、靠墙移动不爬升、不恢复空中跳跃次数、离开平台不悬浮、披风区域无伤害及身体命中陷阱。轨迹回归使用固定物理测试参数，不强制 JSON 保持历史推荐值。
GPU 冒烟使用实际 ApplicationWindow + Render，等待帧完成后在渲染线程读取截图并检查 GL 错误。
`iwanna_route_replay` 对项目路线证据逐段执行真实 ECS 物理跳跃，同时验证 Lua 压板、定时关闭和直冲苹果陷阱；作者修改房间后可重新生成证据并重放。
Windows 已构建运行；整个宿主项目仍使用原有 Windows 工具链/库，未宣称整个游戏已在其他系统通过构建。

性能对比、测试口径与原始 CSV 见 [Benchmarks/README.md](Benchmarks/README.md)。

## Prefab 接口

房间游戏新增 Z / K 按住连射，参数位于 `gameplay.json` 的 `player.shooting`。
`ShotReceiver` 触发 Lua `on_hit`，`set_group_enabled` 可移除整层墙格；
`spawn_radial` / `spawn_radial_at` 可在世界坐标或圆心实例处随机生成等角度弹环。
`Lifetime` 自动清理子弹和临时陷阱，重生与切换房间恢复资源默认状态。
`iwanna_combat_test` 覆盖薄墙阻挡、射击开关、连射限速、方向、苹果环与重置；
追加项目路径参数可验证 MyIwana 实际机关的开门通行和撤退路线。

Prefab 的资源、实例、ECS 生命周期和后续生物 AI / Boss 扩展契约见 [PREFAB_API.md](PREFAB_API.md)。
