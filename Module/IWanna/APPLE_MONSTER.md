# Lua 苹果妖怪陷阱

`apple_monster_trap` 被玩家进入检测框后，在玩家附近随机召唤一个大苹果。大苹果围绕玩家选择随机目标并游荡，默认每 1.4 秒瞄准玩家当时的位置发射一颗小苹果。小苹果之后保持直线运动，使用苹果贴图蒙版碰撞，碰到角色即死亡。

玩家 Z / K 射击：每颗命中的子弹扣 1 点血，默认总血量为 5。第五次命中后，大苹果、血条及它发出的剩余小苹果一起清除。大苹果本体负责受击，触碰它本身不致死。白框、红底、黄色剩余生命的像素血条跟随大苹果移动；每次受击减少五分之一并短暂闪烁。

## 试玩与放置

MyIwana 的 `run_apple_monster_demo.bat` 进入独立战斗展示房。向右走到中间的压力板触发，按住 Z / K 射击，左右移动和跳跃躲避小苹果。两侧平台提供不同射击高度，右侧门返回项目的起始房间。

编辑器 F5 刷新后放置 `apple_monster_trap`，可以调整检测框、移动范围和射击参数。每个触发器在一次房间加载中只召唤一次；死亡重生或重新进入房间会恢复。新建房间的 Lua 需要先安装该模块，原 MyIwana 房间已接入。

```json
"apple_summoner": {
  "kind": "Entity",
  "prefab": "apple_monster_trap",
  "position": [0, 19.55],
  "size": [2.5, 0.9],
  "properties": {
    "health": 5,
    "shotInterval": 1.4,
    "shotSpeed": 20,
    "arenaMinX": -40,
    "arenaMaxX": 40,
    "arenaMinY": -24,
    "arenaMaxY": 16.5
  }
}
```

## 参数

| 属性 | 默认值 | 含义 |
|---|---:|---|
| `health` | 5 | 整数生命值，1～50；每颗子弹始终扣 1 |
| `monsterSize` | 6 | 大苹果宽高，2～12 |
| `wanderSpeed` | 6 | 最大移动速度，0～20 |
| `wanderRadius` | 14 | 玩家左右方向的游荡半径，6～30 |
| `wanderHeight` | 3 | 游荡目标的上下变化幅度，0～12 |
| `hoverOffset` | 3 | 目标相对玩家向上偏移，0～20 |
| `retargetSeconds` | 1.1 | 重新随机选择目标的间隔，0.25～5 秒 |
| `spawnRadiusMin/Max` | 12 / 18 | 初次召唤的水平椭圆范围，需 min ≤ max |
| `arenaMinX/MaxX/MinY/MaxY` | -970 / 970 / -970 / 970 | 大苹果中心移动边界；展示房有明确覆盖值 |
| `firstShotDelay` | 1 | 召唤后首次射击等待，0.25～10 秒 |
| `shotInterval` | 1.4 | 后续发射间隔，0.4～10 秒 |
| `shotSpeed` | 20 | 小苹果速度，1～50 |
| `shotSize` | 1.4 | 小苹果宽高，0.5～3 |
| `shotLifetime` | 4 | 小苹果存活秒数，0.25～6 |
| `barWidth/barHeight/barOffset` | 8 / 0.65 / 4.5 | 血条宽高与向上偏移 |
| `seed` | 0 | 0 使用随机序列；正整数为可重复的独立随机序列 |
| `spawnSound/shotSound` | `apple` | 召唤和发射音效，空字符串静音 |
| `hitSound/deathSound` | `Block Change` / `Break` | 受伤、击败音效 |
| `detectionX/Y/W/H` | 0 / -1.2 / 4 / 3 | 现有检测框，可在编辑器调整 |

移动采用 Lua 目标寻向，不进行墙体寻路。设定中心边界时应按大苹果半径向内预留空间，选择连通的开放战斗区域。小苹果按直线飞行，并在超时或超出战斗区域外侧 8 单位后清理。发射点位于大苹果外侧，玩家过于贴近时跳过本次发射，避免小苹果直接生成在角色体内。

## 代码与接口

运行规则全部在 `Asset/IWanna/Examples/apple_monster.lua`：随机召唤、30 Hz 游荡更新、瞄准方向计算、固定速度弹体、Lua 血量、血条位置与尺寸、受伤闪烁和清理。使用既有 `get_position`、`spawn`、`set_velocity`、`set_size`、`set_opacity`、`destroy`、`sound`、`after`、`on_entity_spawn` 和 `on_hit` 接口，没有增加新的平台依赖。

运行时唯一的 C++ 行为修正是保留同一固定步长中所有不同子弹的受击事件：相同子弹在多个检测采样中仍然去重，两个不同子弹同时命中会分别扣血。一次性开关可以继续用 `ctx:once`。

大苹果模板 `apple_monster_actor` 设置 `receivesShots:true`、`event:"apple_monster_actor"`。小苹果模板 `apple_monster_shot` 为 `hazard` 且不接收玩家子弹。三个 `apple_health_*` 模板为无碰撞装饰；其纯色像素纹理由 Python 标准库生成，无需下载素材。

每房最多 4 个活动妖怪、所有妖怪合计最多 32 个小苹果。触发时先保留生成槽位，同一帧大量触发也不能越过上限；超过上限的触发器保持未消耗，可离开检测框再进入重试。每个大苹果使用一个更新计时器，小苹果清理合并进该更新；HP、子弹命中去重和所有实体引用只保存稳定 ID。击败、外部销毁、玩家死亡和换房都会取消其任务并清理所属实体。

## 安装与更新

```powershell
python tools/install_iwanna_apple_monster.py --workshop
python tools/install_iwanna_apple_monster.py --project D:/Games/MyIwana --room room_01 --room room_02
python tools/add_iwanna_apple_monster_demo.py --project D:/Games/MyIwana
```

安装工具保留旧房间回调，只创建缺少的预制体，首次修改脚本前保存 `.before_apple_monster.bak`。重复安装只替换自己的模块区块。展示创建器检测到已有同名房间或脚本时拒绝覆盖。

实际运行代码嵌入每个房间脚本的 `BEGIN LUA APPLE MONSTER MODULE` / `END LUA APPLE MONSTER MODULE` 区块。项目中的 `scripts/apple_monster.lua` 为参考副本，不会被 `require` 自动加载；修改规范代码后重新安装，或者直接修改房间嵌入区块。
