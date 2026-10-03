# Lua 隐藏墙与陷阱方块

`Asset/IWanna/Examples/reactive_blocks.lua` 定义两种可复用行为。C++ 提供实体碰撞、真实接触事件、显示、透明度和速度接口；显现规则、随机弹起、下落、淡出和销毁时序都由 Lua 完成。

## 放置与属性

编辑器中放置 `hidden_wall` 或 `trap_block` 预制体，拖动和调整尺寸即可。它们是 `role: solid` 的预制体，可以与普通瓦片共同组成地形，默认尺寸为一格 2.5×2.5。隐藏墙使用正常青灰砖，陷阱块使用不同颜色的暖色边框砖以方便辨认。编辑器以幽灵轮廓显示不可见实体，方便选中、移动隐藏墙；检查器中的 `opacity` 可以预设透明度。

`hidden_wall` 初始 `collide: true`、`visible: false`。角色实际碰到它才收到 `reveal_hidden_wall` 的 `enter` 事件；首次接触显现并播放 `Block Change`，每块只播放一次。不可见不意味着无碰撞，透明度也不会改变物理判定。

`trap_block` 初始可见并可站立，事件为 `break_trap_block`。真实接触后立即关闭碰撞，播放 `Break`；本块随机向左上或右上轻弹，然后下落，在约 0.5 秒内逐渐透明并销毁。普通运动不因失去碰撞而停止，因此碎块只作为视觉效果移动，角色可以穿过它。

| 标量属性 | 默认值 | 含义 / 允许范围 |
|---|---:|---|
| `duration` | 0.5 | 淡出与清理总时长，0.05～5 秒 |
| `popMin` / `popMax` | 5 / 7 | 向上初速度随机范围，0～40；最小值不得超过最大值 |
| `driftMin` / `driftMax` | 1 / 3 | 横向速度大小随机范围，0～40；方向随机 |
| `gravity` | 45 | 向下加速度，0～200 |
| `step` | 1/60 | Lua 动画步进间隔，1/120～0.1 秒 |
| `opacity` | 1 | 初始透明度，0～1 |
| `sound` | `Break` | 触碰音效；隐藏墙默认为 `Block Change`；空字符串静音 |

参数全部是可在编辑器覆盖的实例属性。世界坐标 Y 向下，因此上弹使用负 Y 速度。默认上弹高度最多约 0.55 世界单位，约为一格的五分之一。淡出只调整 `Sprite.opacity`，不会反复切换碰撞或改变贴图蒙版。

每块在本次房间生命周期中只执行一次。死亡时销毁尚在淡出的碎块、取消 Lua 任务；重生或换房重建房间与 VM，恢复原始墙和方块。其他 Lua 规则提前销毁方块也不会让旧计时事件报错。

MyIwana 的四个房间均已接入该模块。项目示例入口 `run_block_demo.bat` 从第一房间上层西侧 X=75 的演示存档点开始。向右移动先碰到隐藏墙，墙显现后可以跳过；随后两块暖色方块触碰即碎，可观察轻弹、下落与淡出。下方保留完整安全地板，不影响原有四房间主路线。重生或重新进入房间即可还原并再次测试。

## 安装与保留旧规则

```powershell
python tools/install_iwanna_reactive_blocks.py --workshop
python tools/install_iwanna_reactive_blocks.py --project D:\Games\MyIwana --room room_01
```

`--room` 可以重复指定。工具只安装预制体、素材和 Lua 规则，不改房间 JSON 或地形；房间须使用外部 `script`。首次改写保留 `.before_reactive_blocks.bak`；已有预制体不会被覆盖。

由于运行环境没有 `require`，工具把规范模块嵌入房间脚本并保留完整旧脚本，包括已有延迟陷阱、苹果环和开发者回调。重复运行只替换自己的模块标记区段，不产生重复包装，也保留后来新增的外层脚本。

`scripts/reactive_blocks.lua` 是独立参考副本；真正执行的是各个 `scripts/room_XX.lua` 中 `BEGIN LUA REACTIVE BLOCKS MODULE` 与 `END LUA REACTIVE BLOCKS MODULE` 之间的嵌入代码。修改行为时，可编辑仓库中的规范模块后重新运行安装工具同步所需房间，或直接修改目标房间的嵌入区段。只修改参考副本不会改变游戏运行逻辑。

`Block Change.wav` 与 `Break.wav` 直接复用提供的原始音频，不转换格式；正常砖和陷阱砖都复用现有地形贴图，无额外运行依赖。

## 调度和扩展

模块返回 `handles_trigger(name)`、`handles_timer(name)`、`on_trigger(ctx,event)`、`on_timer(ctx,event)`、`on_death(ctx)` 和 `cancel_all(ctx)`。包装只截获自身事件，其余全部转交前层脚本。

动画使用 `ctx:after` 恢复任务，每次只更新速度和透明度后返回，不创建线程或阻塞循环。每项任务有一个步进计时器和一个总时长计时器；后者确保显示步进因物理帧取整而稍有延迟时，方块仍按总时长销毁。最多同时动画 16 块，保留其他系统的命令和定时器容量；超额块仍会立即关闭碰撞并销毁，不会成为永久障碍。

内部事件 `lua_reactive_block_step` 和 `lua_reactive_block_finish` 由模块保留。任务使用递增序号；死亡清理后旧事件自动成为空操作，不会作用于后来生成的其他任务。实际效果以固定物理帧为时间精度。

相关通用 API：

```lua
ctx:set_visible(id, true)     -- 显示状态，不改变碰撞
ctx:set_collision(id, false) -- 独立物理开关
ctx:set_opacity(id, 0.5)     -- 贴图透明度，不改变贴图蒙版
ctx:set_velocity(id, vx, vy)
ctx:after(seconds, event_name, task_id)
ctx:destroy(id)
```
