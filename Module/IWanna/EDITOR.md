# I Wanna 编辑器（房间资源与拓扑版）

运行根目录 `run_iwanna_editor.bat`；`build.bat` 会构建编辑器、游戏和内容校验工具。
默认编辑 `Asset/IWanna/Workshop/world.json`。`run_iwanna_showcase.bat` 使用同一份数据，保存后重启游戏即可验证。旧 `Showcase` 与 Tiled 格式继续兼容；本次迁移保留了旧文件。

## 房间资源与拓扑

瓦片绘制默认使用 Terrain 图层，按 **T** 在现有图层间切换，左侧显示当前图层。
可把开关墙放进单独的 ShotGate 图层，Lua 用 `tiles:ShotGate` 分组控制整面墙。
属性面板显示模板继承的标量值，编辑后写入实例覆盖；新的 `shot_switch`、
`ring_plate`、`ring_anchor`、`ring_apple` 可用于射击开关与苹果环。
选中压力板可调整 `spawnAnchor`、数量范围、半径、速度和存活时间，圆心位置
通过拖动圆心实例改变。一起复制压力板和圆心时自动重定向 `spawnAnchor`。

| 操作 | 快捷键 / 界面 |
| --- | --- |
| 新建空房间 | F6 / NEW，输入英文文件名 |
| 将当前房间复制为独立房间资源 | F7 / CLONE，输入新文件名 |
| 删除当前房间 | F8 / DELETE ROOM，输入当前房间 ID |
| 从磁盘刷新房间和预制体 | F5 / REFRESH |
| 查看房间拓扑、选择房间 | Tab / MAP / 顶部 ROOM；点击节点进入房间 |
| 拓扑分页 | PageUp / PageDown |
| 添加、修改或删除边界连接 | Ctrl+L / LINK |

每个 `Workshop/rooms/*.room.json` 都是独立房间预制体，包含地格、实体、出生点、连接和 Lua 规则。房间 ID 是文件名去掉 `.room.json`，文件内部不保存重复的房间 ID。直接在文件管理器中复制粘贴房间文件，再按 F5，就能看到副本；不需要手动维护 world.json 中的房间清单。实体预制体 `prefabs/*.prefab.json` 也通过 F5 扫描。

独立内容项目可由 `bin/iwanna_project.exe new <新目录>` 生成；目录中的 `edit.bat` / `run.bat` 可直接编辑和试玩。其房间使用外部 `scripts/<房间 ID>.lua`；编辑器新建和克隆这种房间时会创建独立脚本。完整结构见 [项目工作流](PROJECT_WORKFLOW.md)。

房间之间没有空间相邻关系。拓扑图仅显示逻辑连接，蓝色有向箭头表示传送门，绿色表示边界连接，右侧列出当前房间的具体出口及目标。跨页连接可在右侧查看。布局位置不影响游戏行为。

- **传送门**：放置 door，修改 `destinationRoom` 和 `destinationSpawn`。目标可以是任意房间和该房间的出生点 ID。
- **边界规则**：Ctrl+L 后输入 `right=gallery/left`，表示越过右边界进入 gallery 的 left 出生点；`top=gallery/left@10,-20` 可指定到达坐标，`bottom=gallery/@10,-20` 只指定坐标。`left=death` 立即死亡，`top=ignore` 忽略越界；`right=-` 清除显式规则。
- **同一边的多个出口**：输入 `right[-45,-25]=room_02/start`，再输入 `right[0,20]=room_03/start`，可让右边界上、下两段分别通往不同房间。`right=death` 设置其余位置死亡，同时保留已经配置的分段。左右边的区间是世界 Y，上下边是世界 X；范围含最小值、不含最大值，例如 `[-45,-25)`。区间必须在房间边长范围内，不能重叠，可以首尾相接。
- **修改与删除分段**：用完全相同的 `edge[min,max]` 修改动作或目标；`right[-45,-25]=-` 只删除这个分段。要改变范围，先删除原段再添加新段。`right=-` 清除整条边及其所有分段，恢复默认死亡。分段编辑支持撤销与重做。
- **出口预览**：画布的房间外框用红色表示死亡、绿色表示传送、黄色表示忽略，分段加粗并标注区间和目标。Tab 拓扑图显示同一条边的所有出口，以及区间以外的默认动作；未保存的配置会即时反映在当前房间预览中。
- **自引用**：数据中的目标房间可以写 `$self`，复制房间文件后自动指向副本自身。Lua `ctx:change_room` 的参数仍使用具体房间 ID。
- **单向与双向**：每条连接都是单向。需要往返时，分别配置两个方向。到达目标后速度重置；边界连接的目标出生点必须在房间内部，避免立即回传。
- **无规则边界**：四条边默认死亡。边界使用玩家中心位置判定；角落同时跨越多条边时按 left、right、top、bottom 顺序处理第一条非 ignore 规则。

新建、复制、切换、刷新前需要先保存或撤销当前修改。删除会移入房间目录中的 `.trash`，不会永久抹除文件；其他房间的门或边界仍引用它时会提示具体来源，先修改/删除这些连接并保存。删除起始房间时，会选取一个剩余房间及其出生点作为新起点。房间文件操作不进入对象撤销历史，可从 `.trash` 恢复后刷新。Lua 中动态拼接的房间引用无法静态校验。

## 磁性套索和预制体复制

## 摄像机

按 `C` 或点击右侧检查器标题切换到房间摄像机设置；点击数值编辑，`Ctrl+S` 保存。粉色框显示摄像机可见范围，半透明暗区表示画面外；跟随模式以房间默认出生点作为编辑器预览中心。运行时跟随实际角色，房间切换时读取目标房间的摄像机配置。

编辑器可直接启动当前房间试玩。`F9` 或顶部 `PLAY` 从默认出生点启动；`F10` 输入房间内已有的出生点 ID；`F11` 后在画布上点击，从该世界坐标生成角色，`Esc` 取消选点。试玩前自动保存并校验未保存的房间改动，再启动独立游戏窗口；保存失败或出生点不存在时会在编辑器状态栏提示，不会启动旧版本。多次试玩可并行打开游戏窗口，关闭试玩窗口不会关闭编辑器。`F11` 的自选位置需要在房间范围内；角色死亡后会按房间出生点/存档点的常规规则重生。

`mode=fixed` 时 `x/y` 是舞台中心；`mode=follow` 时 `offsetX/offsetY` 是相对角色的偏移。`zoom` 越大画面越近；`followSpeed=0` 表示立即跟随，正数表示平滑跟随；`clampToRoom=true` 让视野尽量留在房间内。若视野宽度或高度超过房间，对应方向会保持在房间中心，因此要看到水平跟随效果，应提高 `zoom` 或关闭边界约束。未设置 camera 的旧房间默认使用居中、完整展示房间的固定视角。

Lua 的 `ctx:camera_fixed(x, y, zoom)` 和 `ctx:camera_follow(offsetX, offsetY, zoom, followSpeed)` 可在事件回调中切换视角；跟随速度参数可省略，沿用房间设置。运行时修改仅作用于当前进入的房间实例，重新进入或重生会重新读取房间配置。

- `1` 选择模式：点击对象拖动，右下黄色方块缩放。空白处拖动或 Shift+拖动进行矩形套索，完全落入框中的对象组成选择组；拖动选中对象移动整组。
- `M` 切换磁性吸附，默认开启。移动时根据屏幕距离，将对象中心/边缘对齐到瓦片网格线或附近预制体的中心/边缘；组移动以主选对象为对齐基准，保持组内相对位置。
- 按住 Alt 临时禁用吸附，保留任意浮点坐标。放置预制体时也会吸附。
- `Ctrl+D` 复制选中对象或选择组，默认偏移一个格子；`Ctrl+拖动` 从原位置产生副本后拖动。
- 副本获得唯一 uid，保留尺寸、检测框、属性和事件。组内 `target` / `targetId` 指向同组对象时会重定向到副本；其他字符串以及 Lua 源码不会猜测性改写。
- Delete 删除选中对象或组；Ctrl+Z 撤销，Ctrl+Y 重做。 Ctrl+S 保存。

## 原有编辑功能

- 左侧瓦片：左键连续绘制，右键擦除，自动对齐网格；当前绘制第一个地形层。瓦片多于 16 种时，在瓦片区滚轮翻页。原生房间的 `terrain_<主题>_00..15.png` 系列会根据上下左右露出的边自动替换本格及相邻四格贴图；`top=1, right=2, bottom=4, left=8`，`_alt` 系列可与同主题基础系列混用。瓦片只改变外观，不改变非零地格的实体碰撞。可运行 `python tools/build_iwanna_terrain_variants.py` 重新生成 moss、ember、azure 三套原生像素贴图；运行游戏不依赖 Python。
- 左侧预制体列表：选择后点击画布放置；列表区域滚轮翻页。`3` 重新放置当前预制体。
- 中键平移，画布滚轮缩放。
- 右侧点击属性输入，Enter 应用，Esc 取消；滚轮查看更多属性。`ADD PROPERTY` 输入 `speed=12.5`、`enabled=true` 或 `target=apple_01`。
- `DETECTION BOX` 或 `4` 编辑独立矩形，拖动框移动、拖动右下角缩放。detectionX/Y 相对实体中心，detectionW/H 是世界尺寸。detectionEnabled=false 恢复原判定。
- 检查器的 `rotation` 以度为单位围绕实体中心旋转，正值按屏幕坐标顺时针。实体贴图、蒙版、独立检测框和编辑器轮廓一起旋转；检测框偏移使用实体局部坐标。
- `visible=false` 的预制体在编辑器中以半透明贴图和紫色轮廓显示，便于选择隐藏墙；游戏仍遵循其隐藏状态。`opacity`（0～1）只控制贴图透明度，独立于 `collide`。隐藏墙和触碰碎落方块的 Lua 接入与参数见 [REACTIVE_BLOCKS.md](REACTIVE_BLOCKS.md)。
- 矩形覆盖仅影响启用它的实例，其余陷阱仍使用蒙版；绿色是触发/检测区域，黄色是当前选中框。
- 实例属性可在 Lua `on_trigger(ctx,event)` 中通过 `event.properties.speed` 等读取。
- `floor_spike_trap`、`left_spike_trap` 和 `flying_spike_trap` 的触发范围使用现有检测框，可直接拖动或修改 `detectionX/Y/W/H`。`orientation` 控制尖刺攻击方向，`effectAnchor="$self"` 表示以机关自身为基座；使用外部锚点时填写其稳定 ID。伸缩时序、长度和随机飞刺轨道参数见 [SPIKE_TRAPS.md](SPIKE_TRAPS.md)。

使用项目原有英文字符渲染，界面和输入框支持 ASCII；显示字形转大写但保存保留大小写。磁盘扫描及房间 ID 使用 UTF-8，但非 ASCII 名称在当前界面中无法完整显示，推荐英文文件名。

保存时验证整张拓扑、稳定 ID、地图尺寸和检测框，不通过则不覆盖原文件；正常保存留有 `.editor.bak`。刷新失败保留原来的编辑文档。编辑器未保存的修改会阻止刷新，避免被外部文件改动覆盖。

## AI / Agent 工作流

数据规范和示例见 `Asset/IWanna/Workshop/AGENT_GUIDE.md`，JSON Schema 位于同目录 `schemas`。

Prefab 从资源文件到 ECS 的接口、复制与实例覆盖规则，以及未来生物 AI / Boss 的扩展设计见 [PREFAB_API.md](PREFAB_API.md)。

```
bin\iwanna_content.exe validate Asset\IWanna\Workshop\world.json
bin\iwanna_content.exe inspect Asset\IWanna\Workshop\world.json
```

无窗口工具输出 JSON，成功退出码 0，失败退出码 1。检查房间、预制体、出生点、门/边界目标以及 Lua 加载错误；输出拓扑及资源清单。每个分段作为独立的 `kind: "boundary"` 链接输出，`source` 是边名，`range: [min,max]` 是半开世界坐标范围；没有 `range` 的记录是该边的默认动作。Schema 供外部编辑器使用，工具本身执行 C++ 语义校验，不需要 Python、Lua 安装或额外库。

旧关卡需要迁移时，可以使用 `tools/migrate_iwanna_world.py`，输出到**新的目录**，不会覆盖已有目标。
