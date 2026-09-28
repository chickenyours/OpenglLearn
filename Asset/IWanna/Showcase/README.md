# I Wanna 房间与机关展示

根目录 `build.bat` 编译后运行 `run_iwanna_showcase.bat`。独立程序为 `bin/iwanna_showcase.exe`，原来的 `run_iwanna.bat` 仍运行旧关卡。

默认 1920×1080，沿用项目的 ApplicationWindow、Render/RHI、ECS 与音频模块。
A/D 或方向键移动，J 或空格二段跳，R 从本房间存档点复位，Esc 退出。门是自动触发出口，不需要额外按键。

## 三个连通房间

```text
movement 移动测试  ⇄  traps 机关演示  ⇄  gallery 事件展示
       ↑                                      │
       └──────────── 右侧门循环返回 ────────────┘
```

1. **MOVEMENT LAB**：平地接缝、阶梯、贴墙下落与二段跳；右侧门进入机关房。蓝晶石是存档点，激活后变绿。
2. **TRAP LAB**：橙色感应带释放苹果；跨过感应带后可存档；踏上桥后 0.65 秒桥消失；坑底和出口前有地刺。R 恢复苹果和桥，并清空计时器与一次性触发标记。
3. **EVENT GALLERY**：踩感应带生成一个无伤害光球，移动 2 秒后销毁，演示脚本命令安全地增删 ECS 实体。右侧门循环返回第一房间，左侧门返回机关房。

房间存档在**本次程序运行期间**跨房间保留，退出程序后不写磁盘。进入房间从门对应入口出现，死亡或 R 才回到该房间最近存档点；没有存档时回到本次入口。每次进入或重置房间都会重建机关和 Lua VM，因此上一房间的计时器不会泄漏到新房间。

## 用 Tiled 制作房间

直接用 Tiled 打开 `showcase.tiled-project`，或打开 `rooms/*.tmj`。不需要把 Tiled 安装在游戏运行机器上。

- `world.json`：房间稳定 ID、地图路径、Lua 路径、入口、标题、预制体定义。
- `rooms/*.tmj`：标准 Tiled JSON 地图，`Terrain` 是格子层，`Objects` 是对象层。
- `rooms/terrain.tsj`：外部瓦片集；`rooms/editor` 是供编辑器显示的 32×32 瓦片。每个 tile 的 `runtimeImage` 属性指向游戏图片。
- `scripts/*.lua`：每房间一个返回回调表的脚本。

建议先复制一个房间 `.tmj` 和 Lua 文件，在 `world.json.rooms` 添加新的 `id/map/script/title/hint`，然后修改出口。不要使用生成脚本做日常保存：`tools/build_iwanna_showcase.py` 会覆盖示例地图。

当前导入器支持有限、正交、正方形网格；tile layer 使用无压缩的数值 JSON `data` 数组，外部 tileset 位于地图目录或其子目录。暂不支持无限地图、压缩层、组层、瓦片翻转、对象旋转和偏移瓦片层；遇到不支持的数据会报错。地图属性 `worldTileSize/originX/originY` 定义像素到世界坐标转换，当前为 `2.5/-75/-42.5`，编辑器一格 32 像素。

对象 Class 和自定义属性：

| Class | 属性 | 含义 |
| --- | --- | --- |
| Spawn | `uid` | 点对象，定义稳定的房间入口 ID |
| Entity | `uid`, `prefab` | 矩形对象，位置与尺寸就是运行时对象范围 |
| Entity | `event` | 可选，触发区进入/离开或实体接触时发送给 Lua |
| Entity（door） | `destinationRoom`, `destinationSpawn` | 目标房间 ID 与 Spawn ID |
| Label | `text`, `textScale` | 点对象，用于屏幕中的说明文字；当前字形支持英文字母与数字 |

`uid` 在房间内唯一；`player` 保留给玩家。复制对象后修改 `uid`，**不要依赖 Tiled 的数字对象 ID 或对象数组顺序**。出口、脚本都引用稳定 ID。地形实体具有 `TileCell` 行列与材质组件；对象中心按 Tiled 矩形左上角加半尺寸转换。

角色仍使用身体内矩形，地形/危险物继续使用贴图蒙版；Trigger 与 Exit 使用编辑器中的矩形区域，与显示图片透明度无关。

## Lua 规则接口

每个脚本必须 `return { ... }`，回调可省略：

| 回调 | 事件字段 |
| --- | --- |
| `on_enter(ctx, event)` | `name` 房间 ID，`id` 入口 ID |
| `on_trigger(ctx, event)` | `name` 是对象的 event 属性，`id` 是 uid，`phase` 为 enter / exit |
| `on_checkpoint(ctx, event)` | `name/id` 是存档点 uid |
| `on_timer(ctx, event)` | `name` 是预约事件名，`id` 是预约目标 |
| `on_death(ctx, event)` | `name` 房间 ID，`id` 为 player |
| `on_reset(ctx, event)` | 在新 VM 的 on_enter 后调用 |

```lua
return {
    on_trigger = function(ctx, event)
        if event.phase == "enter" and event.name == "drop_apple"
           and ctx:once("apple") then
            ctx:set_velocity("apple_01", 0, 32)
            ctx:after(1.5, "hide_apple", "apple_01")
        end
    end,
    on_timer = function(ctx, event)
        ctx:set_enabled(event.id, false)
    end
}
```

可用方法：

- `ctx:once(key)`：本次房间实例只返回一次 true；重置时清空。
- `ctx:after(seconds, eventName, targetId)`：按固定物理时间预约 on_timer；死亡时暂停，重置/换房清空。
- `ctx:set_velocity(uid, vx, vy)`：设置运动速度，单位为世界单位/秒。
- `ctx:set_enabled(uid, bool)`：同时控制显示和该对象原本允许的碰撞。
- `ctx:spawn(prefabId, newUid, x, y)`：生成预制体；新 uid 必须唯一。
- `ctx:destroy(uid)`：真正删除实体并刷新 ECS 视图。
- `ctx:sound(name)`、`ctx:message(text)`：音效和底部状态提示。
- `ctx:change_room(roomId, spawnId)`、`ctx:complete()`：请求换房或完成关卡。

Lua 不持有组件裸指针，也不直接删除实体；命令在 `Pipeline::Tick` 完成后依次执行。房间更换发生在命令处理之后。脚本命令不能修改或销毁 player，人物输入、重力与碰撞由 C++ 系统处理。

Lua 指令和内存分配有上限；脚本错误会显示提示并输出控制台。脚本执行异常时丢弃待提交命令，修复 Lua 后按 R 可重新加载。无效目标等 C++ 命令错误会停止本批后续命令，已执行命令不回滚。目标房间 Lua 加载失败时保留当前房间。

## 修改与运行

`run_iwanna_showcase.bat` 直接读取源目录，所以改 Lua 后 R 复位即可应用；改地图、world.json、预制体或图片后重启演示，不用重新编译。直接运行 bin 中的 EXE 默认读取已复制到 `bin/IWanna` 的资源，需要先 build.bat 同步。

```powershell
bin/iwanna_showcase.exe --room traps --spawn left
ctest --test-dir build --output-on-failure
bin/iwanna_showcase.exe --smoke-tour --mute --capture bin/showcase_tour.png
```

`--smoke-tour` 在一个窗口和同一图集中依次切换三个房间，完成 9 帧 GPU 验证。`iwanna_room_test` 验证实际输入通关第一房间、双向出口、存档、延时陷阱、复位、生成/销毁、重复换房、稳定 ID、错误连接与脚本失败隔离。

原 `tilemap.json + level.txt` 关卡继续兼容；新房间使用 Tiled + uid，不使用 sourceIndex。新增依赖仅为项目内静态 Lua 5.4.9，来源与校验见 `third_party/README.md`。未引入 sol2 或新的窗口、图形、音频平台库。
