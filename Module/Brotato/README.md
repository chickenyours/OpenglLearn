# Brotato — ECS + Module 分阶段迁移

当前是用户提供的 `Unity2021_Botato` 项目的**第二阶段：六种武器行为**。在第一阶段的移动、敌人出生／追踪、死亡掉落、材料拾取、经验升级、限时波次、暂停和复活基础上，增加火把与匕首往返近战、分段激光、SMG 枪弹及双管霰弹枪四弹轨迹。玩法运行在本仓库已有的 ECS 与模块生命周期中。

当前提供一个角色外观、一种基础敌人和对应 `w1` 至 `w6` 的六种武器，每次选择一把进行验证。数字键切换是新的开发验证入口；原六槽装配 UI、角色选择、地图切换、完整菜单、角色／敌人动画和音频尚未迁移。原行为证据、已修复问题与后续范围见 [MIGRATION.md](MIGRATION.md)。

## 构建与运行

在仓库根目录使用现有 Windows / clang-cl / Ninja 工具链：

```powershell
cmake -S . -B build -G Ninja '-DCMAKE_BUILD_TYPE=Debug' '-DCMAKE_TOOLCHAIN_FILE=clang-msvc.cmake'
cmake --build build --target brotato_game brotato_game_test brotato_weapon_test sprite_batch_geometry_test sprite_batch_runtime_test
.\bin\brotato_game.exe --assets .\Asset\Brotato
```

也可双击根目录的 `run_brotato.bat`，它在找不到游戏 EXE 时先构建，再启动；已有 EXE 时直接启动。修改代码后应先执行上述构建命令或仓库现有的 `build.bat`。图形程序使用项目已有的窗口模块和 OpenGL 4.5 RHI，默认窗口为 1280 × 720，以 16:9 等比显示。`brotato_assets` 构建目标将图片与来源清单复制到 `bin/Brotato`；运行游戏不需要 Unity、Python 或原 Unity 项目目录。

| 按键 | 操作 |
| --- | --- |
| `W A S D`／方向键 | 移动，斜向归一化 |
| `1` 至 `6` | 依次选择法杖、火把、Taser 激光、闪电匕首、SMG、双管霰弹枪 |
| `P` | 暂停／继续 |
| `R` | 重新开始第一波，保留当前武器，重置玩家、进度、临时实体及各武器冷却 |
| `Enter` | 波次完成后进入下一波 |
| `V` | 死亡后恢复生命并继续当前波 |
| `Esc` | 退出 |

所有武器自动瞄准范围内最近的存活敌人，不需要攻击按键。HUD 显示当前武器、攻击状态和冷却。切换会取消当前近战／光束并清除在场弹丸，但保留各类武器各自的剩余冷却，切回同一武器不能立即免费发射；其他武器的冷却也随游戏时间递减，暂停和死亡时冻结。敌人出生预警结束后才可追踪和受击；材料计数与死亡延迟结算的经验独立。

也可用 `--weapon 1` 至 `--weapon 6` 选择启动武器，例如 `brotato_game.exe --weapon 6`。

## 验证入口

```powershell
# 无窗口玩法、精灵几何和渲染资源生命周期测试。
ctest --test-dir build -R '^(brotato_game_test|brotato_weapon_test|sprite_batch_geometry_test|sprite_batch_runtime_test)$' --output-on-failure

# 实际图形程序：推进指定数量的固定步，捕获图片后退出。
.\bin\brotato_game.exe --smoke-test --capture .\bin\brotato_smoke.png --smoke-ticks 720 --assets .\Asset\Brotato

# 固定武器验证场景，可将 --weapon 改成 1..6；截图时刻由 ticks 决定。
.\bin\brotato_game.exe --smoke-arena --weapon 3 --smoke-ticks 16 --capture .\bin\brotato_laser.png --assets .\Asset\Brotato
```

`--assets <目录>` 指向下文列出的全部 16 张 PNG 所在目录；省略时优先寻找 EXE 旁的 `Brotato` 目录。`--smoke-ticks N` 在冒烟模式下生效，默认 720，范围 `0..120000`；模拟频率为 120 Hz，720 步对应 6 秒模拟时间。`--smoke-test` 使用有种子的正常生存流程，`--smoke-arena` 使用便于观察武器的固定场景并自动进入冒烟模式。死亡或波次完成会停止后续玩法步，因此长冒烟命令的实际 ticks 可能小于 N。`--capture <文件.png>` 指定截图路径；冒烟模式截图后退出，普通模式只捕获第一帧并继续运行。

`brotato_game_test` 包含 18 组用例，覆盖移动归一化与边界、出生预警、接触进入伤害、弹丸扫掠和最早命中、自动瞄准与冷却、4 秒寿命、延迟经验、材料掉落与拾取、50 次死亡结算升级、波次清场、暂停／死亡／复活、重启、多模块实例隔离、实体代际与紧密行删除、确定性刷怪、有限追帧、非法配置／时间及实体上限。图形冒烟用于验证窗口、纹理、RHI 帧提交和截图链路，不能代替持续游玩和所有未来玩法的回归。

`brotato_weapon_test` 的 [Test/weapon_test.cpp](Test/weapon_test.cpp) 包含 16 组回归，覆盖武器数据与独立实体、六种武器目标资格、往返近战、目标代际失效、激光完整周期与穿透、四条霰弹轨迹和寿命、切换清场与冷却保留、暂停／死亡、波末／重启、容量、实例隔离及非法配置。

第二阶段自动化验证（2026-10-03）：`brotato_game_test`、`brotato_weapon_test`、`sprite_batch_geometry_test`、`sprite_batch_runtime_test`、`forest_fire_test`、`iwanna_game_test` 共 6 个 CTest 目标全部通过，总耗时 4.40 秒，其中 Brotato 为 18 组基础玩法与 16 组武器回归。

六种武器均通过真实 OpenGL `weapon-arena` 冒烟，并已目视检查截图。各次日志均为 `batchDraws=1`、`GL errors=0`；这里的批次数指精灵批次，不是全部 HUD／场景的总 draw call 统计。捕获攻击不同阶段时的结果如下：

| 武器 | 模拟 ticks | 玩法精灵数 | 截图 |
| --- | ---: | ---: | --- |
| 法杖 | 1 | 4 | [w1](../../build/brotato-phase2-w1.png) |
| 火把 | 3 | 3 | [w2](../../build/brotato-phase2-w2.png) |
| Taser 激光 | 16 | 9 | [w3](../../build/brotato-phase2-w3.png) |
| 闪电匕首 | 3 | 3 | [w4](../../build/brotato-phase2-w4.png) |
| SMG | 1 | 5 | [w5](../../build/brotato-phase2-w5.png) |
| 双管霰弹枪 | 8 | 8 | [w6](../../build/brotato-phase2-w6.png) |

另外，正常生存流程 `seeded-survival` 的火把 720 ticks 冒烟记录实体 10、敌人 6、攻击 4、击杀 4、材料 0，见[生存截图](../../build/brotato-phase2-survival.png)；双管霰弹枪 2400 ticks 达到波次完成，记录实体 2、敌人 0、攻击 22、击杀 17、材料 2，见[波次完成截图](../../build/brotato-phase2-wave-complete.png)。两者同样为一次精灵批次、`GL errors=0`。截图位于本地构建目录，清理 `build` 后可使用上述命令重新生成。

第一阶段历史验证（2026-10-03）：Windows clang-cl 构建、18 组玩法用例及当时的 3 个 CTest 目标通过；GPU 冒烟在 720 ticks 后记录 9 个 ECS 实体、10 个玩法精灵、4 次发射、4 次击杀、一次批次绘制和 `GL errors=0`；2400 ticks 波次完成时记录 18 次击杀。启动脚本及中文资源／截图路径也已验证。这些数字属于第一阶段单法杖版本，不能作为第二阶段结果使用。无窗口 SpriteBatch 测试使用记录后端；真实 OpenGL 验证由游戏冒烟单独完成。

## 模块结构

| 位置 | 职责 |
| --- | --- |
| [Public/game_components.h](Public/game_components.h) | ECS 组件、输入、状态、配置和渲染快照 |
| [Public/weapon_definitions.h](Public/weapon_definitions.h) | 六种武器的类型化定义、原激光段位置、霰弹轨迹数据 |
| [Public/combat_geometry.h](Public/combat_geometry.h) | 圆形与有方向胶囊的相对运动扫掠 |
| [Public/game_module.h](Public/game_module.h) | `IModule` 生命周期、固定步、测试／编辑器调用接口 |
| [Private/game_module.cpp](Private/game_module.cpp) | 创建实体、玩法实现、延迟提交结构变化、提取值快照 |
| [Private/weapons.cpp](Private/weapons.cpp) | 最近目标、武器切换、独立冷却、往返近战和分段光束状态机 |
| [Systems/game_systems.h](Systems/game_systems.h) | `Wave → Movement → Weapon → Projectile → Contact → Pickup` 系统及访问声明 |
| [../Render/Public/Sprite/sprite_batch.h](../Render/Public/Sprite/sprite_batch.h) | 可复用的 `Render::SpriteBatch2D`，提供贴图、翻转、旋转、矩形与文字批次 |
| `main.cpp` | 窗口输入、游戏模块与渲染模块的组合、HUD、冒烟入口 |
| [Test/game_test.cpp](Test/game_test.cpp) | 不创建窗口的玩法回归 |
| [Test/weapon_test.cpp](Test/weapon_test.cpp) | 第二阶段武器行为与生命周期回归 |

玩家、独立武器、敌人、弹丸和材料均为现有 `ECS::Core::Scene` 中的 archetype 实体；系统通过 `ECS::System::Pipeline` 执行。武器实体持有主人／目标的代际句柄、攻击阶段和各类冷却。删除与生成在系统阶段结束后提交，结构变化之后重新取得组件地址。渲染消费 `DrawSprite` 值快照，不持有 ECS 组件指针，也不在渲染线程推进玩法。

`Get`、`Scene`、`PlayerEntity`、`WeaponEntity`、`EquipWeapon`、`SpawnEnemy`、`SpawnProjectile`、`SpawnPickup` 等接口仅供主线程在模拟步之间调用。句柄只应在创建它的 GameModule 实例与本次 Startup 生命周期内使用，不能跨实例、Shutdown / 再 Startup 传递；当前句柄不携带全局场景身份。`Get` 返回的组件引用不能跨实体创建、删除或下一个模拟步保存。

本阶段继续使用现有 archetype 与 pipeline，并修正一处 ECS 头文件依赖：独立武器组件保存完整 `EntityHandle` 时暴露了 `entity.h → scene.h → entity.h` 循环包含，现删除 `entity.h` 中多余的 `scene.h` 包含，使用 `Context` 已提供的 `Scene` 前向声明；返回 `Scene*` 无需完整定义。武器测试首先包含 `entity.h` 来覆盖这一编译边界。此修复不改变句柄的跨场景身份限制，也不代表其他查询问题已经解决。渲染沿用第一阶段加入的 `SpriteBatch2D`，消费旋转武器、光束段和枪口闪光；其 `render_sprite2d` 目标由 Module 层独立建立，Brotato 仅链接使用，不拥有该通用目标。

`Config::weapons` 当前是 C++ 类型化数据表，默认定义在 `weapon_definitions.h`，使用 `config.weapons[WeaponIndex(WeaponKind::Gun)]` 等方式配置；第一阶段的单武器配置字段已删除。本阶段没有外部玩法 JSON 加载器。后续敌人、地图与外部配置应扩展此边界，避免将具体游戏规则放进通用渲染或 ECS 内核。

## 原始美术与可重现导入

当前共裁剪原图中的 16 张精灵：保留第一阶段 7 张，增加第二阶段 9 张，未复制全部图集或插件。导入时需要 Python 与 Pillow，运行时不需要。

```powershell
$brotatoSource = 'C:\Users\16620\Downloads\Unity2021_土豆兄弟_Botato_爱给网_aigei_com\Unity2021_土豆兄弟_Botato\Unity2021_Botato\Unity2021_Botato'
python tools/import_brotato_assets.py --source $brotatoSource

# 可选：输出到另一目录，再用 --assets 指定该目录。
python tools/import_brotato_assets.py --source $brotatoSource --output .\build\brotato_assets
```

导入器只读源项目。从 `Assets/Sprite/*.asset` 解析 Unity Sprite 矩形与纹理 GUID；枪弹、激光段和枪口闪光则按场景／预制体直接引用的 TextureImporter 内部 fileID 读取 `Assets/Texture2D/*.png.meta` 子精灵。转换左下角坐标并向外取整裁剪，保留 RGBA，不缩放、不重绘。`Asset/Brotato/manifest.json` 保存每张图的源相对路径、GUID、内部 fileID（适用时）、原矩形、实际裁剪区域、pivot、像素单位和源文件／输出文件 SHA-256。16 张图及 manifest 重复导入已核对为逐字节一致，原 7 张输出的哈希保持不变。

| 游戏图片 | 原 Sprite | 输出尺寸 |
| --- | --- | ---: |
| `player.png` | `well_rounded_icon.asset`，选用带脸的角色图标 | 77 × 88 |
| `enemy.png` | `40001.asset`，基础紫色独眼敌人 | 77 × 90 |
| `weapon.png` | `wand.asset` | 109 × 24 |
| `bullet.png` | `bullet_wand.asset` | 48 × 49 |
| `material.png` | `harvesting_icon.asset`，原掉落预制体默认图片 | 68 × 72 |
| `floor.png` | `tiles_1_0.asset` | 64 × 64 |
| `spawn.png` | `entity_birth.asset` | 214 × 211 |
| `weapon_torch.png` | `torch.asset` | 100 × 29 |
| `weapon_laser.png` | `taser.asset` | 59 × 39 |
| `weapon_knife.png` | `lightning_shiv.asset` | 71 × 22 |
| `weapon_gun.png` | `smg.asset` | 69 × 47 |
| `weapon_burst.png` | `double_barrel_shotgun.asset` | 91 × 36 |
| `projectile_gun.png` | projectiles 图集，fileID `-1909389404` | 102 × 40 |
| `projectile_burst.png` | 同一子精灵，按用途独立命名 | 102 × 40 |
| `laser_segment.png` | projectiles 图集，fileID `1101925732` | 132 × 56 |
| `muzzle_flash.png` | projectiles 图集，fileID `904847731` | 100 × 96 |

## 本阶段实现边界

保留原场景的主要数值，包括玩家速度 5、敌人速度 2.3、内部生命 150、接触伤害 10、法杖冷却 1 秒／范围 30／弹速 100、弹丸重力 −9.81、1.5 秒出生与死亡延迟、20 秒首波及每波增加 7 秒。

迁移实现使用 120 Hz 固定步、整数经验、单个刷怪计时器和实体上限。法杖／SMG 弹丸上限寿命 4 秒；霰弹按原动画的四条轨迹运动，在原默认寿命 0.15 秒截止。原霰弹动画全长为 0.25 秒，未把源码中未使用的速度 105 当成发射速度，也未复制 Animator 与 Rigidbody2D 同时写位置的相互覆盖。

命中使用有方向胶囊的相对运动扫掠（半长为零时退化为圆）；原火把、匕首与光束的 BoxCollider2D 以相同外包尺寸的胶囊近似，圆角处不同。玩家接触和拾取使用圆形重叠，没有复刻 Unity Box2D 的阻挡、分离和完整接触解算。激光修正了原第六段未激活的索引错误；活目标存在时跟踪方向，目标死亡后保持最后方向完成一轮。枪口显示约 66.7 毫秒的简化位移／淡出，尚无音频。角色／敌人动画、粒子与原面板布局仍待迁移。具体差异见 [MIGRATION.md](MIGRATION.md)。
