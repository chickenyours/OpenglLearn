# Lua 延迟爆炸与激光

延迟和伤害窗口由 `Asset/IWanna/Examples/delayed_traps.lua` 定义。C++ 只提供实体生成、碰撞开关、显示开关、缩放、动画重播和定时回调；业务流程没有写入 C++，也没有创建线程。

每个陷阱是一项 Lua 任务，只挂一个 `ctx:after` 定时器。定时事件恢复下一阶段后马上返回，角色、物理、其他陷阱继续运行。不同压力板可以并发；同一压力板在警告、效果播放和冷却期间忽略重复进入。每房间最多同时运行 24 项任务，为其他规则保留宿主命令和定时器容量。

## 安装与放置

```powershell
python tools/install_iwanna_delayed_traps.py --project D:\Games\MyIwana --room room_01
```

可重复 `--room` 参数。工具读取房间的外部 `script`，保留其全部回调和已有的苹果环、开关包装，在外层加入分派。重复运行只更新自己的包装，不叠加嵌套；改写文件前保留一次 `.before_delayed_traps.bak`。已有预制体不覆盖。工具不修改房间 JSON 或地形。

安装得到 `delayed_explosion_plate`、`delayed_laser_plate`、`effect_anchor`、`effect_warning`、`explosion_effect`、`laser_effect`，以及可独立复用的 `scripts/delayed_traps.lua`。动画及音频资源由资源构建工具提供，运行时不依赖素材转换工具。

当前房间解释器没有文件模块 `require`，安装器将模块内嵌在实际房间脚本的
`BEGIN LUA DELAYED TRAPS MODULE` 标记之间。直接编辑 `scripts/room_01.lua` 内该段即可改变当前房间逻辑，
重启游戏生效，无需编译。`scripts/delayed_traps.lua` 是独立复用副本，不会被房间自动加载；
如需统一更新多个房间，修改仓库 `Asset/IWanna/Examples/delayed_traps.lua` 后重新执行安装命令。

MyIwana 已在第一房间上层西侧放置两种示例。运行 `D:/Games/MyIwana/run_trap_demo.bat`，
或在编辑器选择第一房间、F10 输入 `trap_demo` 出生点即可体验。踩板后停在安全处等待，
再通过警告区域；直接冲入激活后的效果会死亡。主路线、两层出口和原有苹果机关保留。
`DELAYED_TRAPS_INPUT_CHECK.json` 记录了这两种等待通行与直冲死亡的真实按键回放。

在编辑器中放置压力板和 `effect_anchor`，将压力板 `effectAnchor` 设置为锚点的实例 ID。锚点只是位置标记，可以放在任意位置；警告开始时记录其中心位置，警告图与实际效果使用同一位置。激光素材默认竖直，可用 `effectRotation` 旋转。结构字段 `rotation` 表示压力板自身的角度，`effectRotation` 表示生成效果的角度。

## 属性

所有时间单位为秒；尺寸和位置为世界单位。预制体的标量默认值会出现在编辑器实例属性中，可覆盖。

| 属性 | 爆炸默认值 | 激光默认值 | 含义 |
|---|---:|---:|---|
| `event` | `delayed_explosion` | `delayed_laser` | 接收进入事件 |
| `effectAnchor` | `explosion_anchor` | `laser_anchor` | 必须指向实际存在的锚点实例 |
| `effectPrefab` | `explosion_effect` | `laser_effect` | 生成的效果模板 |
| `delay` | 0.8 | 0.8 | 警告持续时间 |
| `damageStart` | 0.08 | 0.1 | 从效果出现到开启碰撞的等待时间 |
| `damageDuration` | 0.2 | 0.8 | 碰撞实际开启时长 |
| `visualDuration` | 1.0 | 1.1 | 效果出现到销毁的总时长 |
| `cooldown` | 1.0 | 1.0 | 效果销毁后再次允许触发的等待时间 |
| `effectWidth` | 8 | 3.2 | 效果、警告的宽度 |
| `effectHeight` | 8 | 8 | 效果、警告的高度 |
| `effectRotation` | 0 | 0 | 旋转角度 |
| `sound` | `trap_explosion` | `trap_laser` | 效果出现时播放；空字符串静音 |

爆炸只在短窗口造成伤害，推荐 `damageDuration` 在 0.08～0.25 秒。前后仍可显示动画，但碰撞关闭。激光在伤害阶段内循环播放动画。`visualDuration` 必须不小于 `damageStart + damageDuration`，否则脚本报告配置错误。

流程为：进入压力板 → 警告 → 生成无伤害效果 → 开启碰撞 → 关闭碰撞 → 销毁视觉 → 冷却 → 允许下次进入。冷却结束后需要离开并重新进入压力板；站在原地不会不断触发。

调度以固定物理帧为精度，零秒等待也在下一次定时事件处理中恢复。多个阶段的实际显示时长可能多出少量物理帧，但不会阻塞主循环。

## 生命周期与扩展接口

模块返回 `on_trigger(ctx, event)`、`on_timer(ctx, event)`、`on_death(ctx)`，以及分派查询 `handles_trigger(name)` / `handles_timer(name)`。其他房间回调由安装包装转交原脚本。

`cancel_all(ctx)` 会销毁仍存在的警告和效果，并在销毁前关闭效果碰撞；延迟到达的旧事件成为空操作。死亡时调用它；重生和换房会重建房间 VM，因此旧任务、定时器和实体一起释放。其他 Lua 规则提前销毁效果也不会使后续任务报错。

内部定时事件为 `lua_delayed_trap_task`。任务使用递增编号，实体 ID 为 `delayed:<编号>:warning` 和 `delayed:<编号>:effect`，不依赖房间名或坐标。请将此前缀和事件名保留给此模块。

新增延迟能力可以向 `defaults` 和 `phases` 添加配置或阶段，复用现有调度。图集动画使用预制体结构字段：

```json
"animation": { "columns": 6, "rows": 4, "duration": 1.0, "loop": false }
```

动画每格的透明度参与当前帧蒙版检测。效果模板默认 `collide: false`，仅由任务短暂开启，直接放置效果也不会留下永久伤害区。压力板碰撞检测框仍由编辑器的 `detectionEnabled` 等属性独立设置。

编辑器会循环预览动画（单次动画结束后暂停 0.5 秒再预览），不运行陷阱逻辑。
选中压力板可看到到锚点的连线及旋转后的效果外框；实际伤害仍按该时刻的透明蒙版检测。
同时复制压力板和锚点时，`effectAnchor` 自动指向复制后的锚点。

资源构建：`python tools/build_iwanna_delayed_trap_assets.py --project D:/Games/MyIwana`。
源爆炸包中的 PNG 层离线合成为 24 帧 6×4 图集，激光总贴图裁为 8 帧循环图集；
源哈希、裁剪范围和顺序保存在 `images/trap_animations.json`。已有 ffmpeg 仅在离线转换音效时使用，
游戏继续读取现有音频模块支持的 PCM WAV；没有新增运行时平台依赖。

回归入口：`ctest --test-dir build -R iwanna_delayed_traps_test --output-on-failure`。
实际示例回放：`bin/iwanna_route_replay.exe D:/Games/MyIwana D:/Games/MyIwana/DELAYED_TRAPS_INPUT_CHECK.json`。
GPU 阶段截图可在 `--smoke-test` 后加 `--smoke-ticks 120`，先推进真实物理与 Lua 120 步再截图；
如 `--room room_01 --spawn trap_demo --position 12 -26.1 --smoke-test --smoke-ticks 120 --capture boom.png`。
