# AI / Agent 内容编辑规范

Prefab 的完整工作流、现有 C++ / Lua 接口和生物 AI / Boss 扩展契约见 [PREFAB_API.md](../../../Module/IWanna/PREFAB_API.md)。

## 入口与文件边界

编辑 `Asset/IWanna/Workshop`，而不是 `bin/IWanna` 的构建副本。

- `world.json`：格式 IWANNA_WORLD_2；起始房间/出生点、roomDirectory、prefabDirectory。
- `rooms/<room-id>.room.json`：独立房间预制体，格式 IWANNA_ROOM_2。
- `prefabs/<prefab-id>.prefab.json`：实体模板，格式 IWANNA_PREFAB_1。
- `schemas/*.schema.json`：JSON Schema 2020-12，描述结构。文件与引用是否存在、重复 ID、GID 是否有效等，由 C++ 校验工具检查。

资源 ID 取完整文件名移除对应后缀，区分大小写。推荐 `[a-z0-9_-]+`；复制文件即产生新资源，不要向文件内部添加需要全局唯一的房间 ID。仅扫描目录第一层；`.trash`、备份和临时文件不会加载。

实体键在房间内唯一；`player` 保留。实体重排不改变脚本引用。房间文件复制保留内部实体 ID，因此房间内的 Lua 仍可以按原 ID 找到目标。引用其他房间保持原目标；需要复制后指向自身时，门或边界的目标房间使用 `$self`。

房间根级可选 `camera`，例如 `"camera":{"mode":"follow","offset":[0,0],"zoom":2,"followSpeed":8,"clampToRoom":true}`。固定舞台视角使用 `"mode":"fixed","position":[0,0]`；未填写时默认固定视角并自动适配整间房。运行时 Lua 可调用 `ctx:camera_fixed(x,y,zoom)` 或 `ctx:camera_follow(offsetX,offsetY,zoom,followSpeed)`。摄像机坐标、尺寸与房间实体统一使用世界单位。

## 房间格式

下面是结构示例（tiles 每行长度必须等于 width，行数等于 height；图层可以有多个）：

```json
{
  "format": "IWANNA_ROOM_2",
  "title": "MY ROOM",
  "hint": "TRY THE PORTAL",
  "grid": {"width": 4, "height": 4, "tileSize": 2.5, "origin": [-5, -5]},
  "palette": {"1": "terrain_01.png"},
  "tileLayers": {"Terrain": [[0,0,0,0],[0,0,0,0],[1,1,1,1],[1,1,1,1]]},
  "entities": {
    "left": {"kind":"Spawn", "position":[-2.5,-2.5], "properties":{}},
    "apple_01": {
      "kind":"Entity", "prefab":"apple", "position":[2,-2], "size":[3,3], "rotation":90,
      "properties":{"event":"drop", "speed":20, "detectionEnabled":true, "detectionX":0, "detectionY":0, "detectionW":2, "detectionH":2}
    }
  },
  "boundaries": {"right":{"action":"transfer","room":"gallery","spawn":"left","position":[-10,5]}, "bottom":{"action":"death"}, "top":{"action":"ignore"}},
  "scriptLua": "return {}"
}
```

坐标全部为世界单位，x 向右、y 向下；Entity 的 position 是中心，size 是宽高，rotation 是绕中心的角度（度，正值顺时针，范围 ±3600）。省略实例 rotation 时继承 Prefab 顶层 rotation，默认 0。贴图、蒙版和局部检测框一同旋转；Lua `ctx:set_rotation("apple_01",90)` 可实时修改。Spawn 与 Label 是点。GID 0 为空格，其余整数引用 palette；图片文件位于共用 `IWanna/images`。房间复制共用图片资源，不把图片重复打包。

kind=Entity 必须引用已存在 prefab；kind=Label 的 properties 包含 text，可选 textScale；kind=Spawn 的实体键就是出生点 ID。

properties 是键值对象，值支持字符串、数字、布尔值。保留字段：event、visible、collide、destinationRoom、destinationSpawn、detectionEnabled/X/Y/W/H。其他自定义标量在 Lua 触发事件中以 event.properties 暴露。模板默认值与实例 properties 合并，实例优先。未知的房间/实体元数据字段会在编辑器保存时保留，可将说明放在 metadata 中；不要把结构化对象放进运行时 properties。

房间规则使用 `scriptLua` 内嵌源码，或用 `script` 指向项目根目录下的独立 `.lua` 文件，二者不能同时出现。两种脚本都必须返回回调表。独立内容项目推荐 `"script":"scripts/room_01.lua"`，开发者直接编辑 Lua 后重启游戏即可生效；复制房间 JSON 时也要复制脚本或明确共享脚本。脚本可用 API 和限制继承 `Module/Scripting/README.md`、`Asset/IWanna/Showcase/README.md`。

## 预制体格式

```json
{
  "format":"IWANNA_PREFAB_1",
  "role":"hazard",
  "image":"demo_apple.png",
  "width":3,
  "height":3,
  "pixelArt":true,
  "visible":true,
  "collide":true,
  "speed":20
}
```

role 支持 solid/hazard/checkpoint/trigger/exit/decoration。预制体默认检测矩形可设置与实例相同的 detection 字段。实例 size 是放置时确定的尺寸；以后修改模板 width/height 不会强行修改已有实例尺寸。

直接复制为 `apple_fast.prefab.json` 后改 speed，编辑器 F5 即出现新模板。预制体属性在各实例加载时合并；修改图片后 F5 会重建编辑器图集。

## 拓扑与删除

`boundaries` 的键只允许 left/right/top/bottom；每条规则的 action 为 `transfer`、`death` 或 `ignore`。transfer 指定目标 `room` 和 `spawn` 或 `position`（也可同时指定；position 是目标房间内的角色中心坐标）；`room` 可以写 `$self`。没有显式规则的边界默认死亡。旧版 `connections` 仍按 transfer 读取，`boundaries` 同名规则优先；新增内容使用 `boundaries`。不通过房间坐标或目录排序推断邻居。出口 prefab role=exit，目标由实例 properties 指定。

删除文件之前检查其他房间的 boundaries、旧 connections 和 exit 实例；还需检查脚本中的 ctx:change_room 字符串（动态脚本引用无法静态推断）。删除起始房间需更新 world.json 的 startRoom/startSpawn。编辑器内删除会检查静态入边并保留文件到 .trash；Agent 操作建议同样移入 .trash。

## 建议的编辑流程

1. 先读取 world.json、目标房间以及用到的模板。保留不相关字段。
2. 用 JSON 工具只修改目标实体、指定格子或连接。已有实体 ID 不随意更改；添加时使用语义明确的唯一键。
3. 复制文件生成房间副本，或复制实体对象到新键。复制成组对象时仅显式改写需要指向副本的 target/targetId；不要批量替换任意 Lua 字符串。
4. 执行 `bin/iwanna_content.exe validate Asset/IWanna/Workshop/world.json`，检查退出码及 JSON 的 ok/error。
5. `inspect` 输出同样经过校验的拓扑、出生点和预制体清单，可用于下一步选择目标。
6. 编辑器先保存/撤销未保存内容，再 F5；游戏重新启动加载新数据。

校验不代替实际游玩测试：它不证明关卡可通关，也不执行全部触发条件。Lua 加载执行受现有指令预算保护。
