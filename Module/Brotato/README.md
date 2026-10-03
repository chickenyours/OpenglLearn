# Brotato ECS 迁移样例

当前完成至第五阶段：五角色后选择一把起始武器，再经过唯一实际可选的难度页和地图预览开局。新增 7 张原按钮图标，保留前四阶段的六种武器、分层动画、命中粒子／飘字和 8 段音频。源码实际是武器六选一，`Weapons(1/6)` 展示文字没有对应六槽装配逻辑；本轮不以此创建装备系统。

## 构建与运行

在仓库根目录执行：

```powershell
cmake -S . -B build/brotato -G Ninja '-DCMAKE_BUILD_TYPE=Debug' '-DCMAKE_TOOLCHAIN_FILE=clang-msvc.cmake'
cmake --build build/brotato --target brotato_game brotato_game_test brotato_weapon_test brotato_session_test brotato_menu_layout_test brotato_presentation_test brotato_game_audio_test sprite_animation_test sprite_batch_geometry_test sprite_batch_runtime_test audio_module_test
.\bin\brotato_game.exe --assets .\Asset\Brotato
```

也可双击 [run_brotato.bat](../../run_brotato.bat)。脚本在 EXE 不存在时构建，已有 EXE 时直接启动；修改源码后先执行构建命令。窗口默认 1280 × 720，以 16:9 等比显示，使用现有窗口模块和 OpenGL 4.5 RHI。构建目标 `brotato_assets` 将全部资源复制到 `bin/Brotato`；运行不需要 Unity、Python 或源工程目录。

普通流程为 **主页 → 角色 → 武器 → 难度 → 地图 → 第一波**。角色为 Well-Rounded、Brawler、Crazy、Ranger、Mage，只改变外观，不添加原脚本没有实现的职业属性。武器页须点击一张卡片或用键盘选择／确认，才开放下一步；每次从主页重新进入都需要确认起始武器。难度页只提供原场景真正绑定的 `DIFFICULTY 0`，不附加生命、伤害或刷怪倍率。地图默认 RANDOM，每次新局抽取一次，允许连续抽到同一图；显式选择 MAP 1..5 仍是新增的预览与验证入口。

| 所在界面 | 按键 | 操作 |
| --- | --- | --- |
| 主页／选择页 | `Enter` | 开始选择／下一步／开局 |
| 选择页 | `W A S D`／方向键 | 移动当前选择 |
| 武器页 | `Space` | 确认当前预览武器，开放下一步 |
| 选择页 | `PageUp`／`PageDown` | 切换选项页 |
| 选择页 | `Esc` | 返回上一页；角色页返回主页 |
| 主页 | `Esc`／EXIT 按钮 | 退出 |
| 游戏 | `W A S D`／方向键 | 移动，斜向归一化 |
| 游戏 | `1` 至 `6` | 法杖、火把、Taser 激光、闪电匕首、SMG、双管霰弹枪 |
| 全部界面 | `M` | 切换静音；也可用 `--mute` 静音启动 |
| 游戏／暂停 | `P`／`Esc` | 暂停／继续 |
| 游戏及覆盖面板 | `R` | 重开第一波，保留本局角色、地图和当前武器 |
| 波次完成 | `Enter` | 下一波 |
| 死亡 | `V` | HP 恢复 150，继续当前波 |
| 暂停／死亡／波次完成 | `H`／HOME 按钮 | 返回主页 |
| 死亡／波次完成 | `Esc` | 返回主页 |

暂停、死亡和波次完成面板也有对应鼠标按钮。返回主页立即销毁本局 `GameModule`，包括玩家、武器、敌人、弹丸、掉落物、计时器和进度，角色／地图及菜单武器预览保留。再次开局采用本次在武器页确认的武器；局内数字键切换不会改写菜单的起始选择。`R` 在本局内重开仍保留当前武器。返回主页暂没有原确认框。

所有武器自动瞄准范围内最近的存活敌人。切换取消当前攻击并清除弹丸，但保留每种武器各自的剩余冷却；暂停和死亡时冷却冻结。材料计数与死亡延迟结算的经验独立。

角色外观现在按真实源层级组合：固定土豆身体、角色图片叠加层、两条独立腿、阴影与高亮标记。动画只改变显示姿态，不改变碰撞圆；横向和纵向实际移动都播放步行动画。敌人命中后旋转缩小，0.5 秒后身体不可见，仍在原 1.5 秒死亡延迟结束时结算经验。白色 `+1..8` 是源脚本的随机装饰数字，不代表伤害、经验或材料。

背景音乐在主页和战斗中连续播放；暂停冻结当前玩法短音效，菜单音和音乐继续。死亡、重开、切波和回主页清理玩法音效，避免旧局声音残留。火把、匕首和激光没有源发射声音绑定，保持无开火音效；火把命中有额外燃烧声。

启动参数 `--character 1..5`、`--map 1..5|random` 和 `--weapon 1..6` 设置初始预览，不跳过普通模式的主页或武器确认。例如：

```powershell
.\bin\brotato_game.exe --character 5 --map 3 --weapon 6
```

## 验证入口

```powershell
ctest --test-dir build/brotato -R '^(brotato_game_test|brotato_weapon_test|brotato_session_test|brotato_menu_layout_test|brotato_presentation_test|brotato_game_audio_test|sprite_animation_test|sprite_batch_geometry_test|sprite_batch_runtime_test|audio_module_test)$' --output-on-failure

# 有种子的正常生存流程，自动开局并捕获。
.\bin\brotato_game.exe --smoke-test --smoke-ticks 720 --capture .\build\brotato-survival.png

# 无随机刷怪的固定武器场景。
.\bin\brotato_game.exe --smoke-arena --weapon 3 --smoke-ticks 16 --capture .\build\brotato-laser.png

# 菜单、覆盖面板和往返生命周期场景。
.\bin\brotato_game.exe --smoke-menu characters --character 5 --capture .\build\brotato-characters.png
.\bin\brotato_game.exe --smoke-menu weapon-unselected --capture .\build\brotato-weapon-unselected.png
.\bin\brotato_game.exe --smoke-menu weapons --weapon 6 --capture .\build\brotato-weapons.png
.\bin\brotato_game.exe --smoke-menu difficulty --capture .\build\brotato-difficulty.png
.\bin\brotato_game.exe --smoke-menu maps --map 4 --capture .\build\brotato-maps.png
.\bin\brotato_game.exe --smoke-menu cycle --capture .\build\brotato-cycle.png

# 固定的动画／特效场景；离线混音可单独留档。
.\bin\brotato_game.exe --smoke-presentation move --character 5 --capture .\build\brotato-move.png
.\bin\brotato_game.exe --smoke-presentation death --capture .\build\brotato-death.png --capture-audio .\build\brotato-death.wav
```

`--smoke-presentation` 可选 `idle`、`move`、`spawn`、`death`、`muzzle`、`pause`，用于固定姿态／特效生命周期检查；它与其他固定冒烟场景互斥。`--capture-audio <文件.wav>` 只用于冒烟模式，输出离线混音；它验证混音数据，不等同于听音设备的实际输出。普通运行使用音频模块输出。Brotato 使用固定构建目录 `build/brotato`，可执行文件仍输出到 `bin`。

`--smoke-menu` 可选 `home`、`characters`、`weapon-unselected`、`weapons`、`difficulty`、`maps`、`pause`、`dead`、`wave-complete`、`return-home`、`cycle`。未选择场景还检查鼠标下一步与 Enter 都不能绕过门槛；`cycle` 通过真实按钮命中路径反复完成整套选择／开局／返回。这些固定场景不属于正常生存成绩，不能与 `--smoke-arena` 同用。`--smoke-ticks N` 用于战斗冒烟，范围 `0..120000`，默认 720，120 Hz 下为 6 秒模拟时间；死亡或波次完成会停止玩法步。普通模式 `--capture` 仅捕获第一帧并继续运行，冒烟模式捕获后退出。`--assets <目录>` 可指定资源目录，省略时优先寻找 EXE 旁的 `Brotato`。

第五阶段已验证（2026-10-03）：上述 10 个 CTest 目标全部通过，耗时 10.98 秒；会话测试增至 18 组，覆盖六种起始武器互斥、武器确认门槛、返回重选、唯一难度无倍率、前后导航、菜单无模拟、启动失败事务性和音频新局边界。旧战斗、表现、音频和通用渲染回归继续通过。

12 次真实 OpenGL 验证均通过，`GL errors=0`：未确认武器页、已确认武器页、难度页、角色页、完整三轮 `cycle`、2400 ticks 波次，以及六武器各 24 ticks 固定战斗。`weapon-unselected` 检查点击／Enter 都不能绕过门槛；`cycle` 通过实际按钮命中检查角色↔武器↔难度↔地图回退、三次开局／暂停返回，结束时没有活动 GameModule。六种武器各自有 1 次攻击、1 次击杀；Mage + MAP 5 + 双管霰弹跑完 2400 ticks，记录攻击 22、击杀 17、材料 2、效果 0，与第四阶段战斗结果一致。

已目视检查[未选择武器](../../build/brotato-phase5-weapon-unselected.png)、[原武器图标](../../build/brotato-phase5-weapons.png)、[难度页](../../build/brotato-phase5-difficulty.png)、角色页长按钮、Taser／霰弹战斗及[波次完成](../../build/brotato-phase5-wave.png)。`cycle` 和完整波次的离线 WAV 分别为 24000／960000 帧，均是 48 kHz／双声道／PCM16 且包含非零混音。截图／音频位于 `build/brotato-phase5-*`，构建目录清理后可用上述入口重新生成。

第四阶段历史验证（2026-10-03）：10 个相关 CTest 目标全部通过，耗时 8.56 秒。包含 18 组基础玩法、16 组武器、16 组会话、18 组表现、6 组音频桥，以及菜单布局、Hermite 动画、精灵几何／资源生命周期和通用音频测试。覆盖分层仿射／负缩放、角色叠加层 pivot、出生显隐、死亡曲线与延迟经验独立、效果容量、飘字随拥有者删除、暂停冻结、声音清场、随机流独立和事件容量。

第四阶段六个固定表现 GPU 场景 `idle/move/spawn/death/muzzle/pause`、三轮 `cycle`、2400 ticks 完整波次和静音场景均通过。9 份离线 WAV 均为 48 kHz／双声道／PCM16，静音文件全零，其余文件有非零混音数据。WinMM 的 48 kHz 和 44.1 kHz 静音设备流启停通过；这验证设备路径，不替代人工听音。

最后补齐飘字 Canvas 的源父位移后，重新编译并复验 18 组表现测试，耗时 .81 秒；通过实际启动脚本运行 Mage + MAP 1 的 death 场景，20 ticks 后有 7 个效果实体、13 个玩法精灵，`GL errors=0`。[最终命中画面](../../build/brotato-phase4-death.png)和[离线音频](../../build/brotato-phase4-death.wav)已重新生成。构建目录可清理，以上入口用于重新生成验证资料。

第三阶段历史验证（2026-10-03）：六个 CTest 目标通过，其中四个无窗口 Brotato 目标耗时 2.25 秒、两个 SpriteBatch 目标另一次运行耗时 0.84 秒。包括 18 组基础玩法、16 组武器、15 组会话回归、菜单布局坐标，以及 UV 子区域／九宫格几何和渲染资源生命周期测试。会话测试覆盖菜单不推进模拟、选择资格、开始失败事务性、反复返回／重进、多会话隔离、随机地图可达与确定性、显式地图不消费随机序列、选图不扰动战斗随机流。真实 OpenGL 的八个菜单／生命周期场景全部通过并已目视检查：home、characters、maps、pause、dead、wave-complete、return-home、cycle。五个角色与五张地图分别配对武器 1..5，各运行 720 ticks 的图形冒烟也通过并已目视检查。

最终补充验证：`cycle` 通过实际按钮命中路径连续完成三次开局／返回。Mage + MAP 5 + 双管霰弹枪的正常生存冒烟达到 2400 ticks 波次完成，记录实体 2、敌人 0、攻击 22、击杀 17、材料 2、玩法精灵 2，见[波次完成截图](../../build/brotato-phase3-survival-wave.png)。主页最终排版也已重新捕获检查。上述 GPU 场景均为 `GL errors=0`，精灵批次 `batchDraws=1`；这是精灵批次数，不是全程序总 draw call。截图中的深色地板按相对清屏颜色检查前景，避免将原深色美术误判为空白。

第二阶段历史验证：六个 CTest 目标通过，18 组基础玩法及 16 组武器回归；六种武器实际 OpenGL 截图、火把 720 ticks 生存和霰弹 2400 ticks 波次完成通过，`GL errors=0`。这些是第二阶段版本结果，不替代第三阶段验证。历史截图位于 `build/brotato-phase2-*.png`，清理构建目录后可用上述入口重新生成。

## 模块结构与生命周期

| 文件 | 职责 |
| --- | --- |
| [Public/session_module.h](Public/session_module.h)、[Private/session_module.cpp](Private/session_module.cpp) | 主页／选择／开局状态及单局 `GameModule` 所有权 |
| [Public/game_module.h](Public/game_module.h)、[Private/game_module.cpp](Private/game_module.cpp) | 单局 ECS 生命周期、固定步、玩法与值快照 |
| [Public/game_components.h](Public/game_components.h) | 玩家、武器、敌人、弹丸与材料组件及输入／配置 |
| [Public/animation_catalog.h](Public/animation_catalog.h)、[Private/presentation.cpp](Private/presentation.cpp) | 导入生成的分层姿态／曲线，固定步采样、命中粒子和飘字 |
| [Public/game_events.h](Public/game_events.h)、[Private/game_audio.cpp](Private/game_audio.cpp) | 有界玩法值事件，事件驱动的音频播放与局生命周期 |
| [Public/weapon_definitions.h](Public/weapon_definitions.h)、[Private/weapons.cpp](Private/weapons.cpp) | 六武器类型化数据和攻击状态机 |
| [Public/combat_geometry.h](Public/combat_geometry.h) | 相对运动圆形／有方向胶囊扫掠 |
| [Public/content_catalog.h](Public/content_catalog.h)、[Public/map_layout_data.h](Public/map_layout_data.h) | 导入生成的角色／地图目录和 250 个原装饰；不手工编辑 |
| [Public/selection_catalog.h](Public/selection_catalog.h) | 六个实际武器按钮与唯一难度的图标目录、源尺寸；不手工编辑 |
| [Public/map_presentation.h](Public/map_presentation.h) | 同一地图数据绘制战场和选择预览，地板采用平铺九宫格 |
| [Public/menu_layout.h](Public/menu_layout.h) | 菜单布局、视口与鼠标坐标换算 |
| [Systems/game_systems.h](Systems/game_systems.h) | `Wave → Movement → Weapon → Projectile → Contact → Pickup → Presentation` 管线 |
| [../Render/Public/Sprite/sprite_batch.h](../Render/Public/Sprite/sprite_batch.h)、[../Render/Public/Sprite/sprite_nine_slice.h](../Render/Public/Sprite/sprite_nine_slice.h) | 通用精灵批次、旋转／翻转、UV 子区域和九宫格平铺 |
| [../Render/Public/Sprite/sprite_animation.h](../Render/Public/Sprite/sprite_animation.h) | 通用 Hermite 标量曲线／二维姿态采样；仿射精灵批次保留父级缩放造成的斜切 |
| [../Audio/Public/audio_module.h](../Audio/Public/audio_module.h) | 复用 PCM 解码与混音，新增按声音暂停／继续，保留播放游标 |
| `main.cpp` | 窗口、输入、模块组合、菜单／HUD 和真实图形冒烟 |

会话不在菜单中创建战斗 Scene，也不积累菜单时间。玩法实体存于已有 `ECS::Core::Scene` 的 archetype/chunk，系统由 `Pipeline` 调度；结构变化在系统阶段结束后提交。渲染消费值快照，不保存组件指针。地图装饰是无行为的渲染数据，原场景也没有给它们碰撞组件。

`ActorAnimation` 保存本地显示姿态，层级合成采用完整二维仿射变换。命中粒子和飘字是独立 Effect archetype，默认上限 512；每次命中最多生成 6 个粒子与一个数字，额外请求达到上限后拒绝。效果参数使用独立确定性散列，不消费刷怪／掉落随机流。外部音频只消费捕获位置等值数据，待取事件上限 256；音频桥最多保留 32 个玩法短声音，均有清理和溢出诊断。

`Game()`、`Get`、`Scene`、实体句柄和生成接口仅在主线程、模拟步之间使用；组件引用不能跨结构变化保存，旧局的任何指针／句柄不能带入下一局。当前实体句柄不携带全局场景身份。`Config::weapons` 是 C++ 类型化表；本阶段未引入运行时玩法 JSON 加载器。

## 原始美术与可重现导入

现有 72 张 PNG：前两阶段 16 张、第三阶段 44 张、第四阶段两腿／基础身体与阴影／高亮／命中粒子 5 张，第五阶段再导入 6 张武器选择图标和 `diff_0` 难度图标。基础身体和阴影共用图片。原场景将 `tiles_4.png`／`tiles_5.png` 整图当作单 Sprite 的 5 处引用按原样保留，不自行猜测替代子图。前三阶段资产见 [manifest.json](../../Asset/Brotato/manifest.json)，地图绑定见 [scene_manifest.json](../../Asset/Brotato/scene_manifest.json)，动画／分层和音频来源分别见 [presentation_manifest.json](../../Asset/Brotato/presentation_manifest.json)、[audio_manifest.json](../../Asset/Brotato/audio_manifest.json)，选择事件和新图标见 [selection_manifest.json](../../Asset/Brotato/selection_manifest.json)。

```powershell
$brotatoSource = 'C:\Users\16620\Downloads\Unity2021_土豆兄弟_Botato_爱给网_aigei_com\Unity2021_土豆兄弟_Botato\Unity2021_Botato\Unity2021_Botato'
python tools/import_brotato_assets.py --source $brotatoSource
python tools/import_brotato_presentation.py --source $brotatoSource
python tools/import_brotato_audio.py --source $brotatoSource
python tools/import_brotato_selection.py --source $brotatoSource

# 同时将图片、清单和生成头文件写到构建目录，便于比较。
python tools/import_brotato_assets.py --source $brotatoSource --output .\build\brotato_assets --catalog-output .\build\brotato_catalog
```

图片导入依赖 Python 与 Pillow；音频导入另需本地 `ffmpeg`，可用 `--ffmpeg <路径>` 指定。7 段源 WAV 按字节复制，1 段 Ogg 背景音乐解码为 PCM16 WAV，运行时不需要新增压缩音频解码器。`tools/brotato_scene.py` 只读解析 Unity 场景真实绑定、父级变换、Sprite pivot、PPU、颜色和排序；图片导入器按 `.asset` 或 TextureImporter 引用裁剪，保留 RGBA，不重绘和缩放。展示导入器从实际脚本 `_anim`、Controller state 和 Clip 引用生成 `animation_catalog.h`，保留每个关键点的左右切线；没有把 Unity 整个项目复制进引擎。

第三阶段两次导入的 60 PNG、2 个 manifest、2 个头文件共 64 文件逐字节一致；第四阶段展示导入的 5 PNG、1 manifest、1 header 也逐字节一致，旧 60 PNG 哈希未变。第五阶段选择导入的 7 PNG、manifest、header 共 9 文件重复生成逐字节一致，之前 65 PNG 未变，71 个源文件哈希已核对；选择导入器拒绝把图片或目录输出到源工程内。清单保存源路径、GUID、Sprite 矩形、像素单位、图集哈希与输出哈希，可追溯到只读原工程。

## 迁移边界

主要战斗数值保留：玩家速度 5、敌人速度 2.3、生命 150、接触进入伤害 10、出生／死亡延迟 1.5 秒、首波 20 秒、后续每波 +7 秒。模拟使用 120 Hz 固定步、整数 XP、单个刷怪计时器及实体上限；有效武器命中直接使基础敌人死亡。法杖／SMG 弹丸有新增的 4 秒上限，霰弹保留原动画轨迹和 0.15 秒寿命。

胶囊近似原火把／匕首／光束 BoxCollider，未复刻 Box2D 推挤和完整接触解算；激光修正原第六段未激活的问题。命中粒子采用原数量、寿命、颜色和大小曲线，方向是源固定 +X 窄锥的二维投影，未引入通用 3D 粒子系统。原角色属性、难度倍率、六槽装配和完整商店规则在已审查源码中没有对应实现，本阶段不编造这些规则。武器物理挂点／精灵中心／发射点的精确分离、完整菜单版式、确认框、玩家烟尘、音量设置页和剩余有行为证据的内容仍待迁移。具体源依据和差异见 [MIGRATION.md](MIGRATION.md)。
