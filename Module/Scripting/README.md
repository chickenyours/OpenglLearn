# LuaModule

独立 `IModule` 实现，宿主通过 `Load / Call / Tick / TakeCommands` 使用，不依赖 GLFW、OpenGL 或 IWanna ECS 类型。Lua 5.4.9 从 `third_party` 静态编译，顶层工程启用 C 与 C++。

每个活动房间一个 VM。开放基础、table、string、math、utf8 库；不开放 io/os/package/debug，也不提供文件加载或原生库加载接口。事件和上下文的构造、函数调用均在 protected call 中执行；每次调用限制 100000 条 VM 指令，VM 分配上限 8 MiB。宿主队列限制 256 命令、128 计时器和 4096 once key，字符串长度也有限制。这用于限制关卡脚本错误，不宣称是运行任意恶意代码的完整安全边界。

`on_timer` 使用宿主传入的固定 dt，与渲染帧率无关。命令只是数据，由 IWanna 的 RoomWorld 在物理系统退出后处理。发生 Lua 错误后停用该 VM 的后续回调，重新加载脚本可恢复。关闭 VM 会释放计时器、命令与 once 状态。

`ctx:set_rotation("spike_13", 90)` 在运行时设置当前房间实例的角度（度，正值在屏幕坐标中顺时针，范围 -3600 到 3600）。它同时改变绘制和陷阱的蒙版/检测框判定，目标为房间内稳定实体 ID；重新进入房间会恢复资源中的角度。

角色基准宽高由 `gameplay.json` 的 `player.width/height` 设置，`player.scale` 的 `x/y` 独立乘上基准宽高，默认都是 0.75。Lua 可用 `ctx:set_player_scale(scaleX, scaleY)` 改变相对比例（各 0.1–4），或用 `ctx:set_player_size(width, height)` 指定世界单位的绝对宽高（各 0.1–40）。两者都实时更新贴图与角色内矩形碰撞体，保持碰撞体脚底位置不变。死亡后按 R 重生或重新进入房间时先恢复配置尺寸，再执行该房间的 `on_enter` Lua 回调。若结果小于碰撞皮肤允许的尺寸，或新碰撞体与墙、地格重叠，宿主会拒绝修改并报告脚本命令错误，角色保持原尺寸和位置。角色碰到地刺等陷阱仍按新尺寸实时判定。

摄像机命令由 IWanna 宿主执行：`ctx:camera_fixed(x, y, zoom)` 将视角固定到世界坐标，`ctx:camera_follow(offsetX, offsetY, zoom, followSpeed)` 跟随玩家并应用偏移。`zoom` 可省略，默认 1；`followSpeed` 可省略，沿用当前房间设置，传 0 则立即跟随。缩放范围 0.05–8，速度范围 0–60。视角是否限制在房间内由房间资源的 `camera.clampToRoom` 决定；Lua 修改在重新进入房间时重置。

房间数据接口、示例 Lua 与制作步骤见 `Asset/IWanna/Showcase/README.md`。

`local x, y = ctx:get_position("entity_id")` 查询当前房间实体的实时中心位置，
不存在时返回 `nil`。这是只读查询，不暴露 ECS 指针；读取已经提交的场景状态，
本回调刚排入队列的 `spawn`/`destroy` 要等命令执行后才能从查询中看到。

IWanna 宿主支持实例初始化回调 `on_entity_spawn(ctx,event)`。对象的 `event` 非空时，
`event.name` 使用该路由名、`id` 为稳定实例 ID、`phase` 为 `spawn`；`properties`
包含合并后的模板/实例标量，并用实时 `positionX/Y`、`sizeX/Y`、`rotation` 覆盖同名值。
房间完整创建、位置查询就绪、`on_enter`（重置时加 `on_reset`）的命令提交后才分派；
动态 `spawn` 或径向生成也会分派，同批尺寸和旋转命令先执行。此时 `get_position`
能直接查询初始化对象，适合根据编辑器尺寸和角度生成视觉、碰撞或逻辑子实体。

```lua
return {
  on_entity_spawn = function(ctx, event)
    if event.name ~= "my_controller" then return end
    local p = event.properties
    local child = event.id .. ":hazard"
    ctx:spawn("ordinary_hazard", child, p.positionX, p.positionY)
    ctx:set_size(child, p.sizeX, p.sizeY)
    ctx:set_rotation(child, p.rotation)
    ctx:set_visible(event.id, false)
  end
}
```

示例 `ordinary_hazard` 模板的 `event` 应为空，控制器自己保留独立检测框。
每个初始化回调完成后提交其命令，再处理下一对象；已删除实例的待初始化事件会移除。
每次宿主提交最多执行 4096 条命令，以及 256 个产生后续命令的初始化批次，防止递归
生成失控；超过限额会报告明确错误。单个 Lua 待提交队列仍限制为 256 条命令。
回调不提供时是空操作；死亡和重置分别沿用 `on_death` 清理、新 VM 重新初始化。

完整的 **Lua 生成苹果环** 示例在 `Asset/IWanna/Examples/apple_ring.lua`，
MyIwana 第一关已使用同一代码。脚本自己调用 `math.random`、`math.cos/sin`
计算数量、位置和速度，再逐个调用 `spawn`、`set_velocity`，并用 `after`
与 `on_timer`/`destroy` 控制清理。这条路径没有调用 C++ 的径向生成接口。
`ring.spawn(ctx,prefix,centerX,centerY,options)` 还可以作为 Lua 局部函数重复调用；
示例将数量限制为 1–64，给其他脚本的命令和计时器留出空间。
生成对象用 `options.spawnPrefab` 指定，默认 `ring_apple`；不要使用
`event.properties.prefab`，它是发出事件的压力板自身模板 ID。
`spawn_radial` 系列仍可用于选择宿主算法的其他房间，两种方式可共存。

房间运行层增加了弹体受击事件 `on_hit(ctx,event)`：`event.id` 为被击中的实例，
`event.name` 为其 `event` 属性，`phase` 为 `hit`，`properties.projectileId` 为弹体 ID。
目标须设置 `receivesShots=true` 且启用碰撞；受击检测沿用贴图蒙版或可视检测框。
每个目标每个固定步长合并一次受击；一次性机关再用 `ctx:once`。
`on_trigger` 的进入事件、`on_hit`、`on_entity_spawn` 都提供实时
`properties.positionX/positionY`、`sizeX/sizeY`、`rotation`。接触退出事件沿用资源位置。

```lua
ctx:set_group_enabled("tiles:ShotGate", false) -- 图层名为 ShotGate 的所有墙格
ctx:set_group_enabled("boss_barrier", false) -- 实例的 group 属性
ctx:spawn_radial("ring_apple", "burst", 20, -5, {
  minCount=12, maxCount=24, radius=2.5, speed=15, lifetime=2.4, phase=0
})
ctx:spawn_radial_at("ring_apple", "burst", "ring_center_01", {
  minCount=12, maxCount=24, radius=2.5, speed=15, lifetime=2.4
})
```

径向生成随机选择包含两端的数量，再按等角度布置实体并赋予朝外的速度；
`spawn_radial_at` 在执行命令时读取圆心实例位置，适合图形编辑器放置与拖动圆心。
圆心可为任意当前房间实体。数量范围 1–128，半径 0–100，速度 0–500，
存活时间 0.05–60 秒，phase 为角度（顺时针，默认 0）。配置表可省略，
默认依次为 12、24、1.5、15、3、0。生成 ID 为 `prefix:批次序号:索引`。
生命周期到期后删除实体，房间重置或切换时立即清理。普通 Prefab 也可设置
`despawnAfter`（秒）获得自动清理。压力板的 `lifetime` 只作为生成环的参数，
不会让尚未踩到的机关自行消失。不存在的圆心、Prefab 或分组会报告脚本错误。

动画效果与临时伤害可使用以下通用命令（目标是当前房间中已生成的非玩家实体）：

```lua
ctx:set_visible(id, true)       -- 只控制显示
ctx:set_opacity(id, 0.5)        -- 只控制透明度，有限数字 [0,1]
ctx:set_collision(id, false)   -- 只控制碰撞，不改变显示或角色类别
ctx:set_size(id, 12, 12)        -- 世界单位；每个轴 (0,1000]
ctx:restart_animation(id)      -- 从第一个配置帧重新播放
```

`set_size` 更新 Transform；贴图蒙版自动随尺寸缩放，显式 `detectionW/H` 仍为绝对世界尺寸。
`set_enabled` 保持原有“显示和碰撞一起恢复/禁用”的语义。若效果模板 `collide=false`，
应使用 `set_collision(id,true)` 开启临时伤害。命令按顺序在物理步结束后提交，
因此可以在同一回调中 `spawn` 后设置尺寸、旋转、显示和碰撞。

[延迟爆炸与激光](../IWanna/DELAYED_TRAPS.md) 使用 Lua 阶段任务加 `after` 实现非阻塞调度；
宿主没有爆炸或激光专用分支。警告、伤害窗口、视觉尾段、冷却以及死亡取消均可修改 Lua。

[隐藏墙与陷阱方块](../IWanna/REACTIVE_BLOCKS.md) 继续复用 `on_trigger`、`set_collision`、
`set_velocity`、`set_opacity` 和 `after`。禁用碰撞后速度仍会推进位置，因此 Lua 可以实现
触碰即失去碰撞、随机轻弹、重力下落和 0.5 秒淡出，无需新增专用 C++ 方块行为。
