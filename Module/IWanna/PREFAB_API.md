# I Wanna Prefab 工作流与扩展接口

本文描述当前代码（`IWANNA_WORLD_2`、`IWANNA_ROOM_2`、`IWANNA_PREFAB_1`）的实际行为，并给生物 AI、Boss 预留设计边界。标有**现有**的接口可以直接使用；标有**拟议**的接口是后续实现目标，当前 JSON 和 C++ 不识别它们。

## 一、资源、实例和运行对象

| 层次 | 标识 | 所在位置 | 生命周期 |
| --- | --- | --- | --- |
| Prefab 资源 | 文件名去掉 `.prefab.json`，如 `apple` | `Asset/IWanna/Workshop/prefabs/` | 加载世界时扫描；编辑器 F5 刷新 |
| 房间资源 | 文件名去掉 `.room.json`，如 `movement` | `Asset/IWanna/Workshop/rooms/` | 加载世界时扫描；编辑器 F5 刷新 |
| 房间实例 | `entities` 的键，如 `apple_01` | 房间 JSON | 在该房间内唯一，跨编辑保存保持稳定 |
| ECS 实体 | `ECS::EntityID` | 活动房间的 `Scene` | 房间进入时创建、切换时重建；不要写入 JSON |

`world.json` 指定目录和起始房间。房间负责放置位置、尺寸、实例覆盖值及拓扑连接；Prefab 负责同类实体的默认角色、外观、尺寸、碰撞设置和默认属性。**一个房间文件本身也是可复制的房间模板**，但房间模板和实体 Prefab 是不同层次，不共享实例化接口。

数据流：

```text
world.json + prefabs/*.prefab.json + rooms/*.room.json
  -> Content::ExpandWorld / Content::ToTiled（兼容旧 Tiled 数据）
  -> RoomCatalog::Load（校验引用、合并模板和实例数据）
  -> RoomWorld::Enter / Create（活动房间 ECS）
  -> 固定步长系统：输入、物理、动画、房间事件
  -> RoomWorld::Apply（在系统遍历结束后执行 Lua 命令）
```

入口源码：[content_format.h](Public/content_format.h)、[room_data.h](Public/room_data.h)、[room_world.h](Public/room_world.h)、[game_components.h](Public/game_components.h)、[lua_module.h](../Scripting/Public/lua_module.h)。

## 二、现有 JSON 接口

Prefab 文件名就是 Prefab ID；无需在 JSON 内重复写 ID。最小定义：

```json
{
  "format": "IWANNA_PREFAB_1",
  "role": "trigger",
  "image": "demo_white.png",
  "width": 5,
  "height": 5,
  "visible": false,
  "collide": true,
  "event": "wake"
}
```

房间实例使用单独的稳定 ID：

```json
{
  "entities": {
    "wake_zone_01": {
      "kind": "Entity",
      "prefab": "trigger",
      "position": [12.5, 6.25],
      "size": [5, 5],
      "properties": {
        "event": "wake",
        "target": "guardian_01",
        "detectionEnabled": true,
        "detectionX": 0,
        "detectionY": 0,
        "detectionW": 4,
        "detectionH": 3
      }
    }
  }
}
```

上面是片段，不是完整房间文件；完整结构、边界连接和 Lua 字段见 [Agent 数据规范](../../Asset/IWanna/Workshop/AGENT_GUIDE.md) 与 [room.schema.json](../../Asset/IWanna/Workshop/schemas/room.schema.json)。引用的 `guardian_01` 必须是同房间内真实存在的实例；示例里的触发器不会自行生成它。

| 字段 | 当前规则 |
| --- | --- |
| `format` | 必须是 `IWANNA_PREFAB_1`；改格式前须同时改扫描、加载、编辑器、Schema 和迁移 |
| `role` | 房间 Prefab 识别 `solid`、`hazard`、`checkpoint`、`trigger`、`exit`、`decoration`、`projectile`；其他 `Role` 值属于旧游戏逻辑 |
| `image` | `Asset/IWanna/images` 内的文件名，不是绝对路径；纹理图集和碰撞蒙版在运行时预加载 |
| `width`、`height` | 模板的初始世界尺寸，必须为正；放置后实例 `size` 是独立值，不跟随模板尺寸更新 |
| `visible` | 只控制绘制；隐藏的 trigger 仍可检测，检测由 `collide` 决定 |
| `opacity` | 默认 1，有限数字 0～1；可由实例 `properties.opacity` 覆盖，Lua `ctx:set_opacity(id, alpha)` 动态修改；不改变可见标记或碰撞 |
| `collide` | 省略时除 `decoration` 外默认为 true；关闭后既不作为固体，也不产生触发命中 |
| `pixelArt` | 省略时默认为 true；控制当前贴图采样模式 |
| `detectionEnabled`、`detectionX/Y/W/H` | 可在模板或实例设置；启用后以相对于实例中心的矩形**替换**原检测形状，宽高须为正 |
| `event` | 模板或实例的脚本路由名；非空时创建后发 `on_entity_spawn`，实际产生 `Touch` 时发 `on_trigger` |
| `destinationRoom`、`destinationSpawn` | `exit` 目标；房间装载时静态校验；`$self` 在导入时解析为当前房间名 |
| 其他键 | 作为模板默认值合并到 `RoomObject::properties`；目前只有标量值会送入 Lua 事件，不会自动生成 ECS 组件或行为 |

解析优先级是 **实例 `properties` > Prefab JSON 默认值 > C++ 默认值**。`id` 来自房间 `entities` 的键；`position`、`size`、`rotation` 来自实例顶层字段，省略实例 `rotation` 时采用 Prefab 顶层默认角度。角度单位为度，绕实体中心旋转，正值在屏幕坐标中顺时针；检测框偏移随之旋转。模板的 `role`、`image`、`pixelArt` 是资源定义，不应靠实例的同名自定义属性改变。实例属性保持字符串、有限数字或布尔值；房间原生格式会拒绝数组、对象和 null 实例属性。

当前检测语义：玩家使用内矩形；`trigger`/`exit` 默认用整个实体矩形；`hazard`/普通 `solid` 默认按贴图蒙版；设置 `detectionEnabled=true` 后改用自定义矩形。`hazard` 命中会由物理系统直接致死，现有触发回调不提供敌方受伤/玩家攻击事件。

隐藏墙和触碰碎落方块使用普通 `solid` + `event` + Lua，不增加专用角色类型。真实实体接触产生 `on_trigger`；试探移动不会产生接触事件。脚本在触碰的固定步长结束时关闭碰撞，后续弹起和淡出不再阻挡角色。预制体参数与接入示例见 [REACTIVE_BLOCKS.md](REACTIVE_BLOCKS.md)。

[伸缩尖刺与随机飞刺](SPIKE_TRAPS.md) 使用 `trigger` 控制器配合动态 `hazard`，完全由 Lua 组合现有位置查询、速度、尺寸、旋转、碰撞和定时接口实现；不增加运行时 C++ 角色或分支。

### 房间边界与分层出口（现有）

房间的 `boundaries` 定义越界规则，独立于实体 Prefab 和房间之间的空间关系。每条边的 `action` 是默认动作，`segments` 可按越界位置覆盖默认动作。例如两个楼层通过同一条右边界前往不同房间：

```json
{
  "boundaries": {
    "right": {
      "action": "death",
      "segments": [
        {"range": [-45, -25], "action": "transfer", "room": "room_02", "spawn": "start"},
        {"range": [0, 20], "action": "transfer", "room": "room_03", "spawn": "start"}
      ]
    }
  }
}
```

`left/right` 的 `range` 使用世界 Y，`top/bottom` 使用世界 X，含下限、不含上限。区间须位于房间对应边内、长度为正且互不重叠；可以首尾相接。匹配时使用玩家中心位置，未匹配到分段时执行默认动作，未配置的边默认死亡。分段也可以使用 `death` 或 `ignore`。`transfer` 支持 `room`、`spawn` 和可选 `position: [x,y]`，目标 room 可写 `$self`；到达点必须位于目标房间内。分段内不能继续嵌套分段。

编辑器 Ctrl+L 支持 `right[-45,-25]=room_02/start`，保存时保留全部分段并校验引用；画布与拓扑图显示各出口。内容工具的 `inspect` 逐段输出 `source`、`range` 和目标。C++ 使用 `RoomDefinition::BoundaryTarget` 复用动作与目标字段，`BoundarySegment` 添加范围，`BoundaryRule` 添加分段列表；运行层不依赖房间 ID、楼层数量或出口用途。

## 三、现有 C++ / Lua 接口及调用时序

**内容层**

```cpp
Json::Value Content::ExpandWorld(const std::filesystem::path& world);
Json::Value Content::ToTiled(const Json::Value& room);
Json::Value Content::FromTiled(const Json::Value& map);
RoomCatalog RoomCatalog::Load(const std::filesystem::path& world);
```

`ExpandWorld` 扫描两个资源目录；`ToTiled` 是给现有编辑器和导入器使用的内存适配形式，磁盘上的新房间仍保存为 `IWANNA_ROOM_2`。`RoomCatalog` 保存 `prefabs[id]`、`rooms[id]`；`RoomObject` 是合并后的运行时定义，包含模板 ID、稳定实例 ID、`Transform`、`Sprite`、碰撞数据、`Role` 和 `Json::Value properties`。

**编辑层**（[EditorDocument](Public/editor_document.h)）

```cpp
void EditorDocument::Open(const std::filesystem::path& world);
void EditorDocument::Place(const std::string& prefabId, glm::vec2 position);
void EditorDocument::Duplicate(glm::vec2 offset);
void EditorDocument::SetField(const std::string& key, const std::string& value);
void EditorDocument::Save();
void EditorDocument::Refresh();
```

`Place` 使用当前模板尺寸，`Duplicate` 操作当前选择组，`SetField` 操作当前选中实例；这些方法管理编辑文档及撤销历史，不是游戏运行时生成接口。Prefab 文件本身由外部文件编辑器修改，`Refresh` 重新扫描并校验磁盘内容；有未保存的房间修改时会拒绝刷新。预览用 `Effective` 合并模板和实例，保存回房间 JSON，而不是把展开后的模板字段复制进每个实例。

**运行层**

```cpp
void RoomWorld::Load(const std::filesystem::path& world);
void RoomWorld::RequestRoom(const std::string& room, const std::string& spawn);
ECS::EntityID RoomWorld::Find(const std::string& stableId) const;
void GameModule::FixedTick(Input input);
```

进入房间时 `RoomWorld::Create` 给每个实例创建 ECS 实体，注册并填充 `Transform`、`Motion`、`Sprite`、`Collider`、`Behavior`、`Player`、`TileCell`、`Camera`、`ShotReceiver`、`Lifetime`；目前所有这些实体共用同一种 archetype。`Behavior::stableId` 是房间内的 JSON 键，`ECS::EntityID` 只在当前场景有效。切换房间会重建 `Scene` 与房间 Lua VM；现有跨房间状态主要是运行期间的存档点状态。

一次固定步长的关键顺序是：`RoomWorld::BeginTick` → 射击准备与实体创建 → ECS 动画/输入/物理/弹体系统 → `RoomWorld::EndTick` 中处理计时器、接触与受击事件 → `RoomWorld::Apply` 执行命令 → 清理已命中/过期实体 → 处理待切换房间。脚本不应在 ECS 遍历中直接增删实体；运行时按这个顺序处理结构变化并刷新 `EntityView`。

**脚本层**

```lua
return {
  on_trigger = function(ctx, event)
    if event.name == "wake" and event.phase == "enter" then
      local target = event.properties.target or "guardian_01"
      ctx:set_enabled(target, true)
    end
  end
}
```

`event` 当前含 `name`（事件名）、`id`（实例稳定 ID）、`phase`（创建为 `spawn`、接触为 `enter`/`exit`、受击为 `hit`）及合并后的 `properties`。房间 VM 支持 `on_enter`、`on_reset`、`on_entity_spawn`、`on_death`、`on_checkpoint`、`on_trigger`、`on_hit`、`on_timer` 等由宿主调用的回调；无每个 Prefab 自带的独立 VM。可用命令见 [LuaModule](../Scripting/README.md)：`set_velocity`、`set_enabled`、`set_rotation(id,degrees)`、`spawn(prefabId,newId,x,y)`、`destroy`、`sound`、`message`、`change_room`、`complete`、`after`、`once`。

`on_entity_spawn(ctx,event)` 为所有 `event` 非空的存活实例提供初始化入口。进入房间时，先创建整间房和角色、安装位置查询、调用 `on_enter`（重置时再调用 `on_reset`），提交这些回调命令后，才初始化实例。动态 `spawn` 与径向生成同样收到此事件；同批 `set_size`/`set_rotation` 先提交，因此 `properties.positionX/Y`、`sizeX/Y`、`rotation` 是分派时的实时变换，会覆盖同名自定义标量。初始化可以生成子实体，命令通过有界队列执行；不要为伤害子实体复制控制器的 `event`，以免递归生成。已在初始化前删除的实例不会回调，死亡清理不会重新初始化；重新进入房间使用新 VM 再初始化。旧脚本省略该回调即可保持原行为。单次提交最多执行 4096 条命令、256 个产生后续命令的初始化批次，超限会报告脚本错误；每个 Lua 待提交队列原有的 256 命令上限仍然适用。

完整用例见 [十种 Lua 变形尖刺](MORPH_SPIKES.md)：初始化回调把可编辑的近身检测控制器与真实蒙版伤害子实体组合起来，变换和阶段顺序由 Lua 数据表定义，编辑器复制实例即可自动接入行为。

`spawn` 在 **本次系统运行结束后** 用模板默认值创建实体，传入的位置为世界坐标。它目前没有实例属性覆盖参数；如需带血量、阵营等初始值，不能靠现有 `spawn` 表达。`destroy` 同样延迟执行；旧的 `ECS::EntityID`、`EntityView` 指针和房间切换前的引用都不可跨这类结构变化保存。脚本命令按提交顺序执行；当前实现没有整批事务回滚，前面的命令成功后，后面的失败仍可能留下已发生的变更。

## 四、制作、复制和验证工作流

1. 从 `prefabs/` 复制一个同类模板并重命名为新的资源 ID；填写 `format`、`role`、`image`、`width`、`height`，以及需要的默认标量属性。共享规则放模板，单个关卡的差异放房间实例的 `properties`。
2. 在编辑器 F5 刷新目录；选模板放置，或在房间 `entities` 中加入 `kind=Entity`、`prefab`、中心 `position`、`size`、唯一实例键。贴图也须放到 `Asset/IWanna/images`。编辑器 Ctrl+D / Ctrl+拖动复制实例时会生成新 ID，并只重定向组内 `target`、`targetId` 引用。
3. 将联动逻辑写进所属房间的 `scriptLua`。复制房间文件会复制该脚本；`$self` 只支持房间数据里的目标字段，Lua 字符串中的目标 ID 仍需人工检查。
4. 运行 `bin/iwanna_content.exe validate Asset/IWanna/Workshop/world.json`；退出码 0 且 JSON 中 `ok=true` 表示数据、静态房间引用和 Lua 装载通过。`inspect` 输出房间拓扑、出生点与 Prefab 列表。校验器**不检查玩法可通关、图片像素内容或所有动态脚本引用**。
5. 保存编辑器修改，重启游戏进行实际碰撞、事件、房间切换与重置测试。编辑器保存会验证并保留 `.editor.bak`；编辑器 F5 只刷新编辑器目录，不对已运行的游戏热替换 ECS/脚本状态。

## 已实现：射击受击与径向陷阱

现代房间运行层支持 Z / K 按住连射。`gameplay.json` 的 `player.shooting`
配置启用开关、贴图、子弹宽高、速度、发射间隔、存活时间和枪口相对位置。
角色的 Lua XY 缩放会同步影响枪口位置。子弹是 `Role::Projectile`，不伤害发射者；
沿移动路径检测实体墙和 `ShotReceiver`，贴图蒙版与可视检测框继续有效。
命中和超时清理都在 ECS 遍历结束后提交。

| 默认属性 | 效果 |
| --- | --- |
| `receivesShots: true` | 创建启用的 `ShotReceiver`，被子弹命中时向房间 Lua 发出 `on_hit` |
| `event: "open_gate"` | 作为 `on_hit` 的 `event.name`；接触事件仍走 `on_trigger` |
| `group: "barrier"` | 分组启用/隐藏实例，便于开关、Boss 门等联动 |
| `despawnAfter: 2.4` | 添加 `Lifetime`，到期安全删除；不填写则无限 |

瓦片按图层自动加入 `tiles:图层名` 分组，如 `tiles:ShotGate`。
关闭组会同时隐藏图像并停用碰撞，重新进入房间恢复资源默认值。
图形编辑器按 T 切换绘制图层，默认编辑 Terrain；属性面板显示模板继承值，
修改时写入实例覆盖。复制压力板与圆心的选区会重定向组内 `spawnAnchor`。

模板 `shot_switch`、`ring_plate`、`ring_anchor`、`ring_apple` 可直接放置。
压力板的 `spawnAnchor` 指向任意圆心实例，拖动圆心即可改生成位置；
`minCount/maxCount`、`radius`、`speed`、`lifetime` 都是可编辑标量。
MyIwana 的业务范例现在由 Lua 计算整个环，使用 `get_position`、`spawn`、
`set_velocity`、`after`、`destroy` 通用接口；可复用源码在
`Asset/IWanna/Examples/apple_ring.lua`，集成版见 `scripts/room_01.lua`。
数量、半径、相位、生成分布与清理逻辑都可直接改脚本，无须改 C++。
宿主 `spawn_radial_at` 与世界坐标 `spawn_radial` 仍保留；精确签名和限制
见 [LuaModule](../Scripting/README.md)。
这套接口尚未包含血量/伤害/阵营结算，后续 Boss 可在受击事件上扩展。

### 动画预制体与延迟伤害

当前 V1 已支持 `animation` 结构：`columns`、`rows`、`cellWidth`、`cellHeight`、
`frames`（行优先帧索引数组）、`duration`（完整序列秒数）、`loop`。
省略 `frames` 使用全部格子，`loop=false` 停在最后一帧；渲染和碰撞读取同一当前帧。
帧尺寸可省略并按图像整除；图集字段在 prefab 文件中定义，实例仍用标量属性覆盖玩法参数。

`ctx:set_collision` 与 `ctx:set_visible` 分别控制判定和显示，`set_size` 调整世界尺寸，
`restart_animation` 重播动画。[延迟爆炸与激光](DELAYED_TRAPS.md) 是完整示例：
`delayed_explosion_plate` / `delayed_laser_plate` 通过 `effectAnchor` 引用任意位置锚点，
Lua 用非阻塞阶段任务控制短暂碰撞窗口；重复触发、并发、死亡与换房清理由该 Lua 模块负责。

## 五、生物 AI / Boss 的拟议扩展契约

现有 `IWANNA_PREFAB_1` 仍供普通瓦片对象、陷阱和触发器使用。**以下 `IWANNA_PREFAB_2` 仅是待实现的数据草案，直接放进当前 `prefabs/` 会被加载器拒绝。** 建议让 `role` 只做碰撞/阵营等高层分类，具体能力由可组合组件声明；不要为每种敌人或 Boss 阶段继续增加 `Role` 分支。

```json
{
  "format": "IWANNA_PREFAB_2",
  "role": "actor",
  "components": {
    "Transform": {"size": [4, 4]},
    "Sprite": {"image": "guardian.png", "animation": "idle"},
    "Collider": {"shape": "mask", "enabled": true},
    "Health": {"max": 100, "contactDamage": 1},
    "Brain": {"behavior": "patrol_guard", "sightRadius": 18},
    "BossPhases": {"asset": "guardian_phases.json"}
  }
}
```

拟议的 C++ 边界：

```cpp
// 拟议接口；当前仓库中不存在这些类型。
PrefabDefinition PrefabRegistry::Load(prefabId);
ValidationResult PrefabRegistry::Validate(definition, componentSchemas);
EntityHandle PrefabFactory::Instantiate(roomId, stableId, prefabId,
                                        transform, typedOverrides);
void CommandQueue::Spawn(roomId, stableId, prefabId, transform, typedOverrides);
void CombatEvents::Emit(DamageEvent{sourceId, targetId, amount, hitboxId});
```

实现顺序建议如下，各阶段须保持旧资源可加载：

1. **注册与解析**：抽出 `PrefabRegistry` 和组件工厂，按版本解析 Prefab；组件键有明确 Schema、默认值、覆盖规则和版本迁移。旧 V1 走适配器映射为组件，编辑器与运行时共用校验器。将结构化的 AI 参数放组件配置，而不是挤进 V1 标量属性。
2. **实例化**：`PrefabFactory` 把资源定义与房间实例覆盖合并，在一个明确的创建点生成 ECS 组件。`stableId` 保持房间内唯一；动态生成对象的 ID 由调用者提供并做冲突检查。支持带类型检查的 `spawn` 覆盖值。结构增删继续在固定步长结束时提交。
3. **生物**：加入 `Health`、`Faction`、`Brain`、`Perception` 等组件及系统。AI 决策用固定步长，先读世界快照、产生命令，再统一应用；碰撞区域分为实体碰撞、受击框和攻击框。`Role::Hazard` 的接触即死不能承担一般伤害结算。
4. **Boss**：Boss 由正常生物组件加阶段状态/攻击模式组成。阶段切换基于血量、计时或事件，攻击模式引用可复用的弹体/陷阱 Prefab。定义 `DamageEvent`、`PhaseChanged`、`Defeated` 的时序、去重与一次性触发；弹体生成、销毁和击杀奖励走同一命令队列。跨房间返回、死亡重置、存档恢复的 Boss 状态要显式声明策略。
5. **编辑器与测试**：编辑器按组件 Schema 提供类型化属性面板、枚举/资源引用和检测框可视化；校验器检查组件版本与资源依赖。测试至少覆盖复制实例、覆盖优先级、AI 固定步长可复现、受击/无敌帧、阶段切换、房间重进及存档恢复。图集预加载策略要包含 Boss 阶段与弹体资源。

拟议组件、事件和 API 在实现前均不构成当前内容格式承诺。新增能力时应先更新版本化数据契约与迁移，再接运行时、编辑器、校验器和测试，避免一份 Prefab 在各处有不同解释。
