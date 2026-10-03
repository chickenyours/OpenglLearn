# I Wanna 内容项目工作流

引擎、ECS、渲染、音频、物理和 Lua 宿主保留在 `OpenglLearn`；每个游戏项目是独立的数据目录。改房间、贴图、预制体、物理数值或 Lua 规则后重启游戏即可生效，不需要重新编译 C++。只有新增引擎级组件、系统或 Lua 原生命令时才需要改代码和编译运行时。

先用 `build.bat` 构建一次运行时，然后生成项目：

```powershell
./bin/iwanna_project.exe new C:/Games/MyIwana
```

目标目录必须不存在，工具不会覆盖已有项目。Windows 下双击生成的 `edit.bat` 打开关卡编辑器，`run.bat` 运行游戏。生成器在其他平台构建后提供 `edit.sh` / `run.sh`。启动脚本记住生成时的运行时目录；搬动运行时后设置 `IWANNA_RUNTIME_DIR` 指向新的 `bin` 目录。项目数据本身没有平台路径依赖。

```text
MyIwana/
  world.json                 起始房间、出生点、房间/预制体目录
  gameplay.json              角色贴图、移动、跳跃、碰撞参数
  rooms/room_01.room.json    瓦片、实体实例、相机、边界规则
  scripts/room_01.lua        房间回调和动态玩法
  prefabs/*.prefab.json      可复用实体模板
  images/                    本项目的贴图
  audio/                     本项目的音效
  schemas/                   内容 JSON Schema
  run.bat / edit.bat         开发启动入口（其他平台为 .sh）
```

编辑器 F5 重新扫描房间和预制体文件，F9 从当前房间试玩。每个 `rooms/<id>.room.json` 可以写 `"script":"scripts/<id>.lua"`；路径相对项目根目录。原有 `scriptLua` 内嵌写法仍可用，同一房间只能选一种。编辑器中新建或克隆外置脚本房间时，会同时生成独立的 `scripts/<新房间 ID>.lua`。直接在文件管理器复制房间 JSON 时，应同步复制脚本并改 `script` 路径，否则两个房间会共用同一个 Lua 文件。

数据职责保持清楚：Prefab 给出类型默认值，房间实例覆盖位置、尺寸、旋转和属性；房间 `boundaries` 决定四条边的传送、死亡或忽略，传送目标可指定房间内坐标；Lua 处理计时、机关、生成和运行时变化。Lua 命令在固定步逻辑结束后提交，不能直接持有 ECS 指针。房间切换会重建场景和该房间的 Lua VM，运行时改动会重置；存档点状态由宿主管理。未来的生物 AI、Boss 和新事件可沿用这一数据/命令边界扩展。

发布或交给其他开发者前运行：

```powershell
./bin/iwanna_content.exe validate C:/Games/MyIwana/world.json
```

接口细节见 [Prefab API](PREFAB_API.md)、[编辑器](EDITOR.md)和 [Agent 数据规范](../../Asset/IWanna/Workshop/AGENT_GUIDE.md)。
