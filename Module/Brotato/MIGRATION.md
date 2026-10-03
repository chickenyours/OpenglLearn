# Brotato 迁移记录与后续范围

迁移基线是用户提供的 `Unity2021_Botato` 目录，主场景为 `Assets/Brotato.unity`。源目录保持只读。当前完成至第五阶段，已接入原武器六选一与唯一有效难度的选择流程；前四阶段作为战斗、表现和数值基线保留。本记录区分原脚本实际行为、场景／预制体覆盖值、迁移实现和后续工作，不以资源名称或游戏商业版本的功能推断该工程已经实现的玩法。

## 第一阶段历史：战斗与波次循环

已迁移范围：玩家移动与边界、一个基础敌人的出生／追踪／死亡、`w1` 法杖最近目标自动射击、弹丸、接触伤害、材料掉落和拾取、经验升级、死亡／复活、暂停／重启以及波次清场和继续。

第一阶段入口直接进入第一波；选择固定角色图片和单张地板，使用简化 HUD。第一阶段只实现 `w1`；其余五种武器在第二阶段继续迁移。角色属性、完整商店、随机地图、原版 UI、角色／敌人动画、粒子和音频仍待后续工作。

## 原代码到 ECS 的映射

以下源路径均相对于 Unity 项目根目录。

| 原始逻辑 | 原源文件 | 当前职责 |
| --- | --- | --- |
| 玩家移动、HP、等级、拾取、复活 | `Assets/Scripts/PlayerController.cs` | `Player` / `Transform` / `Velocity` / `Circle`、Movement / Contact / Pickup、GameModule 状态 |
| 敌人出生、追踪、命中死亡、掉落、延迟经验 | `Assets/Scripts/EnemyNPC.cs` + `Assets/AI/NpcGo.prefab` | `Enemy` 的 Spawning / Alive / Dying 阶段，Wave / Movement / Projectile |
| 最近目标、六种武器瞄准与攻击 | `Assets/Scripts/w1.cs` 至 `w6.cs` | 独立武器实体上的 `Weapon` / `Transform` / `Sprite`，WeaponSystem 与 `Private/weapons.cpp` |
| 弹丸碰撞和物理参数 | `Assets/PrefabInstance/bullet_wand - Copy.prefab`、`BulletA.prefab`、`SpawnMove.prefab` | `Projectile` / `Velocity` / `Circle` 与 ProjectileSystem，有方向胶囊扫掠 |
| 霰弹四条位置动画与寿命 | `Assets/AnimationClip/MoveGun.anim`、`Assets/Scripts/DestroyTime.cs` | `BurstPaths`、0.25 秒 smoothstep 动画曲线及 0.15 秒寿命 |
| 刷怪位置与等待时间 | `Assets/Scripts/SpawnPosition.cs` + `Assets/Brotato.unity` | SourceSpawn 坐标表和单个 spawnTimer |
| 倒计时、下一波、清场、暂停、回主页时重置 | `Assets/Scripts/GamePlayUI.cs` | WaveSystem 与 Playing / Paused / WaveComplete / Dead；SessionModule 返回主页销毁整局 |
| 掉落物外观选择 | `Assets/Scripts/ItemSetter.cs` + `Assets/PrefabInstance/Addition.prefab` | 第一阶段采用默认图片；随机外观尚未迁移 |
| 开始游戏与主页对象开关 | `Assets/Scripts/GameManager.cs` | SessionModule 的 Home / CharacterSelect / MapSelect / Run 与显式开始／返回 |
| 角色图片选择 | `Assets/Scripts/CharacterSetter.cs` | 五个真实按钮绑定角色，只替换外观，不添加角色属性 |
| 随机地图启用 | `Assets/Scripts/ContainerLogic.cs` | 五张原地图，默认每次新局随机；额外显式选择仅作预览和验证 |

所有玩法实体使用仓库已有 ECS 的组件注册、archetype/chunk 存储和系统管线。第一阶段新增的精灵批次位于 `Module/Render`，不依赖 Brotato 的组件，第二阶段继续复用。武器组件保存完整 `EntityHandle` 时发现 `entity.h` 与 `scene.h` 循环包含；第二阶段移除 `entity.h` 多余的 `scene.h` 依赖，保留 `Context` 中的 `Scene` 前向声明。这是头文件可独立包含的修复，不涉及跨场景句柄身份或其他查询问题。

## 第一阶段数值基线（历史，仍保留）

优先使用场景和预制体序列化值，不能直接照抄 C# 字段声明的默认值。例如 `EnemyNPC.SpeedMoveement` 声明为 5，实际敌人预制体为 2.3；`w1` 声明弹速 10，场景实际为 100。

| 项目 | 原工程有效值／规则 | 第一阶段 |
| --- | --- | --- |
| 玩家初始世界坐标 | `(10.8073508, -0.1775364)`，原渲染 z 不参与 2D 玩法 | 保留 xy |
| 移动边界 | x `[2.84,18.9]`，y `[-10.6,10.51]` | 保留 |
| 玩家速度 | 5 单位／秒；输入归一化 | 保留 |
| 玩家圆半径 | 0.46 | 保留 |
| 内部生命 | 150；原 UI 显示 `15/15` | 内部值保留，HUD 简化 |
| 接触伤害 | `OnCollisionEnter2D` 一次扣 10 | 一次进入重叠扣 10，持续重叠不逐帧扣血 |
| 敌人速度／半径 | 2.3／0.43 | 保留 |
| 出生预警 | 出生标先显示，0.2 秒隐藏、再 0.3 秒显示、再 1 秒启用敌人，合计 1.5 秒 | 时间和激活边界保留；外观用透明度近似 |
| 敌人生命规则 | 没有分级 HP；有效武器碰撞直接死亡 | w1 一击死亡 |
| 死亡延迟 | 1.5 秒后删除实体、加 0.02 经验 | 1.5 秒后加 2 点整数 XP |
| 升级 | 原 `Image.fillAmount == 1` 时等级加一并清空进度 | 每 100 XP 升一级，即 50 次已结算死亡 |
| 材料掉落 | `Random.Range(0,2) == 1`，概率 50% | 50%，与 XP 独立 |
| 材料拾取 | 碰撞或触发 `add` 标签后计数加 1、删除掉落物 | 圆形拾取、计数加 1 |
| 法杖 | 冷却 1 秒、最近目标范围 30、弹速 100 | 保留数值与自动瞄准 |
| 子弹圆半径／重力 | 0.22／−9.81；Rigidbody2D gravityScale 为 1 | 保留；使用固定步积分 |
| 子弹原寿命 | 无超时，命中或波次清场删除 | 新增 4 秒寿命 |
| 首波／后续波 | 20 秒，每波 +7 秒 | `20 + 7 × (wave−1)` |
| 波末 | 停止刷怪，清空 SpawnContainer，等待下一波 | 清空敌人、弹丸、材料，保留玩家 HP 与进度 |
| 复活 | HP 恢复 150，继续原场景 | 保留当前波、实体与进度并恢复 HP |

场景依据：`Brotato.unity` 中 PlayerController 块约第 234359 行、w1 块约第 234444 行；敌人参数见 `Assets/AI/NpcGo.prefab` 约第 355 行。精确文件来源摘要另保存在资产 manifest，行号仅辅助定位。

刷怪列表共有 364 项，由 14 个 x 和 26 个 y 组合；每列的最上方 y 重复一次。迁移保留这种概率权重，没有擅自去重或改成仅沿四边刷怪：

```text
x = [18.106762, 3.106762, 17.286762, 16.356762, 15.356762, 14.106762,
     12.856762, 11.606762, 10.106762, 8.856762, 7.606762, 6.356762,
     5.356762, 4.106762]
y = [10.235486, -9.764514, -8.264514, -7.264514, -2.014514, 0.985486,
     4.735486, 7.735486, 9.485486, 8.735486, 5.985486, 6.985486,
     3.485486, 1.735486, 2.485486, -0.014514, -1.264514, -0.764514,
     -3.264514, -2.514514, -4.764514, -6.014514, -4.264514, -5.264514,
     3.985486, 10.235486]
```

`Random.Range(0,2)` 使用整数重载，原刷新等待为 0 或 1 秒，并非连续的 `[0,2)` 秒。本实现保留两种等待，零等待最早在下一模拟步生成。

## 通用行为与第一阶段差异

1. **固定步与可复现随机序列。** 模拟为 120 Hz，默认随机种子 2026，重新开始重置种子。它保留规则而不复刻 Unity 的 Update / FixedUpdate / 协程逐帧时序或随机流。单次 Advance 限制输入时间至 0.25 秒、最多执行 8 个子步，超出的整步积压会丢弃，避免卡顿后无限追帧。
2. **整数 XP。** 2／100 替代 0.02／1，避免浮点累加和 UI 精确相等判断使升级门槛漂移。XP 仍在死亡延迟完成时结算，未改成命中立即加经验。
3. **波末未兑现 XP 仍丢失。** 波末清除尚处死亡延迟的敌人，不补发该经验；这是保留原规则，不计作修复。波末未拾取材料也随清场消失。
4. **单个刷怪计时器。** 源 StartSpawning 协程永久递归，RoleNext 再调用 StartSpawningS 会叠加多个循环。迁移每局只保留一个计时器，防止刷怪速率随切波意外倍增。
5. **临时实体有界。** 法杖与 SMG 弹丸寿命上限 4 秒；霰弹按原默认 0.15 秒清除。默认敌人最多 256、弹丸最多 256、材料最多 512。达到上限时拒绝额外创建，避免原始无界临时对象增长。上限包含相应列表中的出生／死亡阶段实体。
6. **独立 2D 碰撞实现。** 第一阶段使用相对运动圆形扫掠；第二阶段扩展为有方向胶囊扫掠，圆是半长为零的特例，弹丸保留最早命中并消费一颗弹丸。玩家接触和拾取仍使用圆形重叠；没有 Box2D 的推挤、刚体分离、敌人间阻挡或完整碰撞层矩阵。因此运动轨迹和贴身战斗细节不承诺逐帧等价。
7. **第一阶段视觉近似。** 使用静态 PNG、左右翻转、简化出生闪烁和死亡姿态，暂未导入 Animator、逐帧动画、粒子、伤害飘字、拾取音效及原 UI。角色选择结果先固定为 Well-Rounded 图标；收集物固定为 Addition 默认图片；地板固定一张瓦片。武器挂点、出生标尺寸与 HUD 为新渲染中的简化布局，不是整个 Unity 层级的自动转换。
8. **独立状态机。** Playing / Paused / WaveComplete / Dead 替代全局 Time.timeScale 和 UI 激活的隐式控制。波次面板停留期间玩法暂停；第三阶段新增会话层处理主页和菜单。

原相机跟随夹紧范围为 x `[10.1,12.04]`、y `[-7,7]`。此值属于原镜头布局数据；不能据此宣称已复刻其设备适配、原 UI 安全区域和 Canvas 缩放。

## 第二阶段历史：六种武器

配置位于 [Public/weapon_definitions.h](Public/weapon_definitions.h) 的 C++ 类型化 `WeaponDefinition` 表，通过 `Config::weapons[WeaponIndex(kind)]` 访问；没有外部 JSON 加载器。第一阶段单武器配置字段已删除。所有武器共用一个独立 ECS 武器实体，保存主人和目标的代际句柄、攻击状态与各武器冷却；火把／匕首复用往返状态机，法杖／SMG 复用弹丸机制，激光与霰弹保留各自的行为。

| 脚本／选择键 | 源实现与场景覆盖 | 第二阶段实现 |
| --- | --- | --- |
| `w1.cs`／`1` | 法杖；冷却 1、检测范围 30、弹速 100、重力 −9.81、圆半径 0.22 | 保留第一阶段发射行为，配置移入武器表 |
| `w2.cs`／`2` | 火把 `torch`；冷却 1、检测范围 10.81、往返速度 56.2；Box 1.09 × 0.33，offset `(0.19,0)` | 追踪存活目标伸出、随后返回玩家当前挂点；去程／回程都可命中多个敌人 |
| `w3.cs`／`3` | 外观 `taser`；冷却 0.63、检测范围 10、分段间隔 0.02 | 六段依次伸出再收回；活目标存在时继续转向，目标死亡后保留最后朝向完成一轮 |
| `w4.cs`／`4` | 闪电匕首 `lightning_shiv`；冷却 0.72、检测范围 10.81、速度 56.2；Box 0.64 × 0.18，offset `(0.19,0)` | 复用火把往返状态机，保留较短碰撞尺寸和独立冷却 |
| `w5.cs`／`5` | SMG `smg`；冷却 0.2、检测范围 11.1、弹速 105、重力 −9.81；`BulletA.prefab` 横向 Capsule 1.02 × 0.40 | 自动发射、相对运动扫掠最早命中；约 66.7 ms 枪口效果；音频待接入 |
| `w6.cs`／`6` | 双管霰弹枪 `double_barrel_shotgun`；冷却 0.8、检测范围 10.5，生成 `SpawnMove.prefab` 四弹 | 复现四条位置曲线及 0.15 秒寿命；每颗子弹横向 Capsule 0.66 × 0.21、命中后独立消耗 |

原工程没有敌人分级生命值，上述有效武器碰撞均直接进入死亡阶段；火把音效特殊处理不等于已经存在持续燃烧伤害系统。

### 激光段的实际范围

六段是嵌套层级，最外段缩放 `0.78787`，其余缩放为 1。每段原 Box 为 `1.32 × 0.56`，缩放后为 `1.0399884 × 0.4412072`。相对武器朝向的六个中心距离为：

```text
[0.687, 1.10200028, 1.48100018, 1.96100044, 2.50900048, 2.93300049]
```

末端边界约为武器基点前方 `3.45299469`，检测范围 10 用于选目标，不代表光束有 10 单位长。源场景六段起初均未激活。原 `w3.cs` 在 `CurrentLaser == Length - 1` 时就进入熄灭分支，导致第六段从未点亮，还会递归追加协程；迁移以单个状态机完整显示六段，明确作为索引与生命周期问题修复。

### 霰弹预制体和动画证据

`SpawnMove.prefab` 根对象没有 Rigidbody2D，挂载 `BulletGun.controller → MoveGun.anim` 和 `DestroyTime`；四个 BulletB 子对象各有 SpriteRenderer、横向 CapsuleCollider2D 和 gravityScale=1 的动态刚体。它使用普通长枪弹贴图；不是火焰发射器。`w6.cs` 虽声明 `bulletSpeed=105`，发射代码只实例化预制体，没有给子弹速度赋值。

`MoveGun.anim` 全长 0.25 秒、非循环，四条局部位置曲线只有起终点且切线均为零，因此用 `u=t/0.25`、`s=u²(3−2u)` 插值：

| 子对象 | 起点 xy | 0.25 秒终点 xy | 固定局部角度 |
| --- | --- | --- | ---: |
| `BulletB` | `(0.317,-0.135)` | `(3.146,-0.796)` | −23.138° |
| `BulletB (1)` | `(0.313,0.096)` | `(3.076,0.555)` | +23.138° |
| `BulletB (2)` | `(0.228,-0.328)` | `(3.233,-1.744)` | −37.473° |
| `BulletB (3)` | `(0.233,0.238)` | `(3.148,1.519)` | +37.473° |

预制体未覆盖 `DestroyTime.DistroyTime`，脚本默认值为 0.15 秒，所以整组会在动画终点之前销毁，截止插值系数为 0.648。迁移保留这一寿命和轨迹，未擅自延长至 0.25 秒，也没有用 105 速度发射四颗直线子弹。原 Animator 每帧写局部位置与动态刚体重力同时生效，实际表现依赖 Unity 同步时序；本实现以动画曲线为准，不复刻两者相互覆盖导致的位移差异。

### 第二阶段有意差异与验证边界

- **一次装备一把。** 数字键 `1..6`、`--weapon 1..6`、`EquipWeapon` 是新的验证入口，第二阶段没有复刻起始武器选择页。第五阶段核对场景确认原页同样是六选一，早期按 `Weapons(1/6)` 文字推测的六槽装配没有真实规则依据。切换取消当前近战／光束并删除在场弹丸，保留各类冷却；重启与波次清场清除攻击状态和冷却，重启保留所选武器。
- **碰撞形状。** 圆和横向胶囊采用相对运动扫掠；火把／匕首／光束的原 Box 用相同外包尺寸的胶囊近似，圆角不等同于原矩形。扫掠在一个固定步内采用固定朝向，不是连续旋转刚体碰撞解算。近战目标死亡、代际失效或越出检测范围时转入返回阶段，防止悬空目标造成攻击卡住。
- **枪口与视觉。** 原 `BulletAnim` 的 X 从 0.094 变到 0.466，约 66.7 ms 结束，前 50 ms 不透明后淡出。当前显示简化尺寸与位置的短闪光；未建立通用 Animator、音频或粒子播放系统。武器挂点继续采用新渲染布局。
- **资产范围。** 本阶段只新增 9 张图，合计 16 张；枪弹／光束／枪口准确按原 TextureImporter 子精灵 fileID 裁切。原 7 张哈希未变，重复导入 PNG 与 manifest 逐字节一致。
- **验证。** 新增 [Test/weapon_test.cpp](Test/weapon_test.cpp)，16 组测试覆盖各类攻击、冷却、目标失效、切换、暂停／死亡、清场、容量、实例隔离和非法配置，并首先包含 `entity.h` 以覆盖头文件独立使用。2026-10-03 本轮 6 个 CTest 目标全部通过：18 组基础玩法、16 组武器回归、两个 SpriteBatch 目标以及 ForestFire／IWanna 回归，总耗时 4.40 秒。六种武器的真实 OpenGL `weapon-arena` 冒烟和截图目视检查均通过，w1..w6 捕获 ticks 分别为 `1/3/16/3/1/8`，玩法精灵数 `4/3/9/3/5/8`。正常生存流程另通过火把 720 ticks（4 次攻击／4 击杀）与双管霰弹枪 2400 ticks 波次完成（22 次攻击／17 击杀）回归。上述 GPU 日志均为一次精灵批次、`GL errors=0`；批次数不代表全部 HUD／场景总 draw call。截图和命令见 [README.md](README.md)。

源证据位置：`Brotato.unity` 的 w2／w3／w4 配置约第 234144／234225／234539 行，w5／w6 约第 233728／233862 行；火把和匕首 Box 尺寸约第 227740／133641 行；最外激光变换约第 136095 行。霰弹见 `SpawnMove.prefab:542` 的 Animator、`:562` 的 DestroyTime 引用、`MoveGun.anim:17` 的位置曲线和 `DestroyTime.cs:7` 默认寿命。行号辅助定位，数值和 GUID 以源文件内容为准。

## 第三阶段历史：角色、地图和会话流程

### 源场景的实际绑定

`CharacterSetter.Start` 查找按钮的 `Icon`，给 Button 注册 `CheckDone`；点击只执行 `BodyCharacter.sprite = CharacterFace.sprite`，没有任何属性变化。`Brotato.unity` 只有五个 `CharacterSetter`，按选择容器的 sibling 顺序如下。源 `BodyCharacter`（fileID `5177973264060913226`）初始 Sprite 为空；迁移采用第一项 Well-Rounded 作为可见默认，不能称其为源脚本的自动选择行为。

| 角色 | 原 Sprite | CharacterSetter 约行号 | PPU 100 下的原尺寸 |
| --- | --- | ---: | --- |
| Well-Rounded | `well_rounded_icon.asset` | 51512 | 0.7684776 × 0.8789713 |
| Brawler | `brawler_icon.asset` | 162517 | 0.7684776 × 0.8789713 |
| Crazy | `crazy_icon.asset` | 7029 | 0.7684776 × 0.8789713 |
| Ranger | `ranger_icon.asset` | 93612 | 0.7784776 × 0.8789713 |
| Mage | `mage_icon.asset` | 208363 | 0.8692388 × 0.9197325 |

`ContainerLogic.OnEnable` 先关闭 `Maps` 全部对象，再启用 `Maps[Random.Range(0, Maps.Length)]`。场景数组有五项，顺序如下；没有按钮 onClick 指向这些地图对象，因此源工程是随机开图，没有显式地图选择界面。

| 原对象／新预览名 | 地面颜色 RGB | 装饰图集 | 原对象约行号 |
| --- | --- | --- | ---: |
| `MapZEro`／MAP 1 | 0.47058827, 0.4039216, 0.34509805 | `tiles_1` | 68032 |
| `MapOne`／MAP 2 | 0.34509805, 0.30588236, 0.25882354 | `tiles_5` | 224990 |
| `MapTwo`／MAP 3 | 0.47450984, 0.3921569, 0.3803922 | `tiles_4` | 28679 |
| `MapThree`／MAP 4 | 0.3921569, 0.3921569, 0.3921569 | `tiles_3` | 147746 |
| `MapFour`／MAP 5 | 0.3137255, 0.3647059, 0.36078432 | `tiles_2` | 49154 |

每图主地板都引用 `tiles_outline.asset`（GUID `ad3689d846db58d41baf8d71f6d3d90b`），使用 Tiled SpriteRenderer，局部尺寸 22 × 17.5、sortingOrder −10。完整父级变换将本地 X 映到世界 Y、本地 Y 映到世界 X，所以世界尺寸为 17.5 × 22，原 pivot 世界位置为 `(10.8333327, 0)`。生成数据在此基础上补偿 Sprite pivot 到矩形中心；平铺九宫格保留左／下／右／上边框 `0.8097325 / 0.7095709 / 0.8997322 / 0.89973236` 单位，绘制角度 +π/2、Y 翻转。

五图各有 50 个只有 Transform 和 SpriteRenderer 的装饰，共 250 个，没有障碍碰撞或地图规则脚本。导入保留坐标、PPU、缩放、旋转、颜色、镜像、pivot 与排序。MapOne 的两处装饰、MapTwo 的三处装饰直接指向 `Texture2D/tiles_5.png`／`tiles_4.png` 的 fileID 21300000；对应 meta 为 `spriteMode: 1`、没有子 Sprite，因此实际显示整张 192 × 256 图集。迁移按原异常引用保留，不猜测应换成哪个切片。所有精确源引用在 [scene_manifest.json](../../Asset/Brotato/scene_manifest.json) 中。

### 原 UI 事件链与当前改组

源流程为 `HomeUI/StartBtn` 显示 StartGame 面板，角色 NextBtn 切到武器页，武器 NextBtn 切到难度页，难度 NextBtn 调用 `GameManager.StartGame`、启用 Container（触发随机地图）、调用 `StartSpawningS` 并复位各选择页显隐。上述主要绑定分别在场景约 25338、10150、87139、179384 行。已审查难度按钮仅更改展示对象，没有生命／伤害／刷新倍率计算。

原暂停键与 Resume 都调用 `GamePlayUI.Pause` 切换 `CheckPause` 和 `Time.timeScale`。MainMenu 先打开 BackHome 确认框，Yes 再调用 `BackHome`；死亡 Give up 同样返回，Revive 调用 `PlayerController.Revive`，只恢复 HP 并继续原局。暂停页 `Btn-Restar` 的 onClick 为空。波次面板的多处按钮只调 `RoleNext`，没有购买或升级属性计算。

原 `BackHome` 重置波数／计时至 1／20 秒，HP 150、等级／XP／材料为 0，玩家局部位置为 `(1.399667, -1.303347, -26.44369)`；清除 AI、Flash 和 SpawnContainer 子对象，停用 Container，关闭游戏相关面板，显示主页并恢复 timeScale=1。它不清空 CharacterSetter 写入的 Sprite，也没有明确复位所选武器激活状态。

本阶段建立 `SessionModule` 管理 `Home → CharacterSelect → MapSelect → Run`。菜单不拥有战斗 Scene，不推进或积累模拟时间；开始时创建新 `GameModule`，失败时保留选择页且不消费地图随机序列。返回销毁整局，旧实体、组件指针、句柄与计时器不能跨局使用。角色和地图选择保留；随机地图有独立随机流，不扰动刷怪／掉落序列，允许连续抽到同图。

以下是有意的迁移差异：

- 第三阶段将角色／武器／难度原流程改组为角色／地图预览流程，武器选择和难度页当时尚未迁移，六种武器使用数字键或初始 `--weapon` 验证。第五阶段恢复武器／难度流程并确认起始武器是六选一；源工程未实现六槽装配。
- 默认保持源随机地图，MAP 1..5 显式选择是新增验证入口，不增加障碍、地图属性或新规则。
- 鼠标和键盘共享新布局与命中区域。原面板美术、4 秒启动等待和返回确认框尚未复刻；当前返回按钮直接结束本局。
- `R` 是原空 Restart 按钮的新增可用操作：在本局内重开，保留角色、实际地图和当前武器，重置进度／冷却。回主页再开局则采用配置初始武器，不依赖上一局残留激活对象；随机地图在新局重新抽取。
- `GamePlayUI.BackHome` 的隐式对象清理由整局所有权替代；复活仍保留原局、波次和进度。源升级／商店显示不自动转化为属性系统。

### 引擎、资源和验证

第三阶段新增可复用的 `SpriteBatch2D::SpriteRegion` 与 `BuildNineSlice`：使用图集内 UV 区域绘制平铺九宫格，地图战场和菜单预览复用同一套 250 装饰数据。角色和地图目录是类型化 C++ 表；`tools/brotato_scene.py` 从源场景可重现生成 `content_catalog.h` 和 `map_layout_data.h`，主导入器同时输出图片及来源清单，不需要运行时 Unity 解析器。

本阶段新增 44 张 PNG，合计 60 张；旧 16 张 SHA-256 不变。重复导入的 60 PNG、2 manifest、2 header 共 64 文件逐字节一致。六个 CTest 目标已通过（2026-10-03）：四个无窗口 Brotato 目标运行耗时 2.25 秒，两个 SpriteBatch 目标另一次运行耗时 0.84 秒，包括 18 组基础玩法、16 组武器、15 组会话、菜单坐标和渲染几何／资源生命周期检查。会话检查覆盖反复进出、暂停／死亡／波末返回、选择限制、启动失败不留半局、实例隔离与随机流独立性。真实 OpenGL 的八个菜单／生命周期场景，以及五角色／五地图与武器 1..5 配对的各 720 ticks 冒烟均通过并已目视检查。`cycle` 还通过真实按钮命中路径连续开局／返回三次。最后以 Mage、MAP 5 和双管霰弹枪跑满 2400 ticks 到波次完成，记录实体 2、敌人 0、攻击 22、击杀 17、材料 2；主页最终排版也重新检查。所有 GPU 场景 `GL errors=0`，精灵批次为 1，不能据此推断全程序总 draw call。验证入口和截图见 README。

## 第四阶段历史：分层动画、命中特效和音频

### 绑定证据与显示层级

玩家 `PlayerController._anim` 指向场景 Animator `5083329934025401204`，其 Controller GUID 为 `52ce4a6dc678d8a4092a98364c11ee3e`。敌人 `NpcGo.prefab` 的 `_anim` 指向 `4613876897586908232`，Controller GUID 为 `0b304e466342c36438da8fb8714e5759`。导入器首先校验这两条脚本引用，再解析对应 state 的实际 Clip GUID；六个导入片段均没有 Sprite 帧替换。

| 导入名 | 实际 Clip／绑定 | 长度／循环 | 主要显示行为 |
| --- | --- | --- | --- |
| `PlayerIdle` | Player controller Idle → `Idle_Player.anim` | 1 秒，循环 | 身体组 y 在 .25／.2／.25 间呼吸；缩放 1,1 → 1.15,.85 → 1,1 |
| `PlayerMove` | Player controller Move → `Move_Player.anim` | .6 秒，循环 | 身体组上下与压扁伸展，两条腿独立位置／角度，右腿负 X 缩放 |
| `EnemyMove` | Monster_All 的 Idle 与 Move 都指向 `Idle_Monsters.anim` | 1 秒，循环 | 与玩家待机相同的整体呼吸变换，没有独立腿 |
| `EnemyDeath` | Monster_All Death → `Death_Monsters.anim` | .5 秒，不循环 | 角度 0 → −360°、缩放 1 → 0；结束姿态保持 |
| `MuzzleFlash` | 场景 Shoot controller 实际开火状态 → `BulletAnim.anim` | .06666667 秒，不循环 | X .094 → .466，前 .05 秒 alpha=1，最后一段 Hermite 淡出 |
| `DamageText` | NpcGo Value Animator → Value controller AddIt → `AddIt.anim` | .48333332 秒，不循环 | 锚点从 (−.039,−.758) 移至 (−.118,.194)，到达后保持 |

`PlayerMove` 的身体组 y 关键点为 `.25, .16, .3, .22, .16, .3, .25`，每隔 .1 秒一项；XY 缩放为 `(1,1), (1.2,.8), (.9,1.1), (1,1), (1.2,.8), (.9,1.1), (1,1)`。这些和腿部曲线包含非零切线，迁移保存左右切线并作逐段 Hermite 插值，不能用一条正弦或全段 smoothstep 代替。角度按源数值连续插值，不将死亡旋转折叠为最短路径。

两个源 Controller 的 state、AnyState 和 Entry transition 列表都为空，脚本直接调用 `Animator.Play`，没有 CrossFade。本轮在 Idle／Move／Death 切换时重置对应 Clip 时间并硬切姿态，不需要为这些绑定新增过渡混合；Unity 的逐帧调用时序由 120 Hz 固定步替代。

玩家显示有 9 个拓扑排序节点：Animator 根、`Player_Body_All`、Mark、Body、Body/Itemes、Shadow、Player_Legs、Leg_r、Leg_L。两腿父节点位于身体组下，位置 `(0,.064)`、缩放 `1.25`，因而同时继承身体的呼吸／移动缩放。Body 的固定图是 `potato1.asset`；Shadow 也使用它，颜色黑、alpha `.4509804`，局部 y `−.326`、Y 缩放 `−.3`。Mark 是 `highlight.asset`，颜色 `(.44705883,1,.9490196,.4509804)`。

第四阶段进一步纠正第三阶段的简化：五个 `CharacterSetter.BodyCharacter` 实际都指向 **Body/Itemes** 的 SpriteRenderer `5177973264060913226`，不是固定 Body。本轮保留 Body 原土豆底图，把选中的角色 Icon 放到 Itemes；该子节点位置 `(−.0010128,.018993)`、缩放 `(−.7974548,.7974548)`，阴影不随角色图替换。每个角色使用自己的 Sprite 尺寸和 pivot，Mage 的 pivot 与其他角色不同。源 Itemes 初始为空，迁移仍采用 Well-Rounded 为默认可见外观。该行为由生成器对五个真实引用逐项校验。

`PlayerController.FixedUpdate` 原本只在 Vertical 非零时播放 Move，纯横向移动仍播放 Idle。本轮修正为任何实际移动都播放 Move；显示动画不反向写入碰撞、位置、生命或随机数。源脚本玩家死亡只冻结 `Time.timeScale`、显示死亡 UI，不播放 Player controller 的 Death，故未因存在 `Death_Player.anim` 就自行加入玩家死亡动画。

### 命中特效与时间规则

出生标仍按 `EnemyNPC.LoadingEnem` 的时间显示：`[0,.2)` 可见，`[.2,.5)` 隐藏，`[.5,1.5)` 可见，到 1.5 秒启用身体与碰撞。敌人死亡身体动画 .5 秒结束后不可见，但实体继续等待至 1.5 秒才发 XP；可见尸体的缩放结束不能提前结算经验。

`NpcGo.PrefabParticle` GUID `68797e6c0aa224340870701e3a501331` 指向 `DirectionalHitParticles.prefab`。UVModule 使用 `particle_9.asset`，并非按粒子材质的空 MainTex 推测图片。一次发射 6 个，寿命 .5 秒，速度随机 1..8，尺寸随机 .1...28，颜色 `(.8018868,.19290671,.1970778,1)`；归一化大小曲线由 `(0,1,0,0)` 与 `(1,0,−2,−2)` 两个 Hermite 点定义，即 `1−t²`。本轮保留这些值，以固定 +X、±7° 的二维窄锥模拟原 3D 发射方向；不声称完整实现 Unity ParticleSystem 的三维形状与深度采样。

飘字 Value 的父 Canvas 直接位于 NpcGo 根下，不继承 Monster_All 的死亡缩放。Canvas 的 local Z 为 `.972`，经 NpcGo 根 X 轴 −90° 旋转后成为世界 Y 偏移；结合 RectTransform 的 anchored X `−.003`，最终文本位置为敌人坐标 + `(−.003,.972)` + AddIt 采样位置。因此出生文字位于敌人原点上方约 .214 单位，而非脚下 .758 单位。生成目录保存这项父级位移。

源粒子预制体没有删除脚本，非循环播放结束会留下空对象。本轮 Effect 实体在显示寿命到达时回收。装饰飘字在每次有效击杀触发，显示白色随机 `+1..8`、字号 .25；它不是实际伤害、XP 或材料。源 AddIt 没有淡出，位置结束后保持，直到关联死亡延迟完成。本轮效果采用独立确定性散列，不消耗玩法刷怪／掉落随机流。

ECS 新增 `ActorAnimation`、`Effect` 与末尾 `PresentationSystem`。默认最多 512 个 Effect；外部音频消费的值事件队列最多 256 条，容量满时记录丢弃计数，避免消费者不轮询造成无限增长。事件捕获发生时位置和代际句柄，不让音频保存组件地址。重开、波次清场、回主页和 Shutdown 均处理特效与事件生命周期。

### 音频绑定与模块改进

只导入场景／活动预制体已绑定的音频，完整 fileID、GUID、哈希与时长见 [audio_manifest.json](../../Asset/Brotato/audio_manifest.json)。

| 事件 | 音频文件 | 源有效增益 |
| --- | --- | ---: |
| 法杖弹丸生成 | `Audio/wand.wav` | .732 |
| SMG 开火 | `Audio/gun.wav` | .504 |
| 双管霰弹开火 | `Audio/burst.wav` | .504 |
| 有效命中／敌人死亡 | `Audio/enemy_death.wav` | .618；火把覆写为 .3 |
| 火把命中额外燃烧 | `Audio/fire_death.wav` | 1 |
| 材料拾取 | `Audio/material.wav` | 1 |
| 已接入的菜单操作 | `Audio/button.wav` | .765 |
| 场景持续背景音乐 | `Audio/music.wav` | .594，循环 |

源音乐设置面板父级初始 inactive，所以首次进入时采用 AudioSource 序列化增益 `.594`；不能把尚未触发的 `SliderShow.OnEnable` 首次 PlayerPrefs 默认 `.75` 当作初始值。音量设置页尚未迁移。7 段 WAV 原样复制，音乐 Ogg 用本地 ffmpeg 解码为 PCM16 WAV，保持采样率和声道，运行时复用现有 `AudioModule`。

火把、匕首、激光没有实际发射 AudioSource，保持无发射音。资源目录虽然存在受伤／升级／波末声音，活动脚本没有对应绑定，本轮不凭文件名新增播放。`PlayerHurt` 仍可产生有界值事件，但音频桥不为它臆造音效。

法杖声在原项目中附着于弹丸 AudioSource，弹丸提前命中销毁会一起截断播放；本轮把它作为发射事件的短声音，允许原 .268 秒 WAV 播放完成。这是解耦音效所有权后的差异，不宣称逐样本复现 Unity 实际运行声音。

音频模块增加按 voice 暂停／恢复并保留播放游标。Brotato 桥最多保留 32 个玩法短声音；暂停冻结这些声音，UI 与音乐继续。死亡、重开、切波和回主页清除短声音。源 `Time.timeScale=0` 本身并不会暂停 AudioSource，因此短音效冻结与生命周期清理是迁移改进；复活不重播已清除的旧击杀音。`M` 与 `--mute` 是新增静音入口。

### 通用渲染能力与可重现导入

`Render::Animation` 是不依赖 Brotato 的纯曲线／Pose 采样接口，支持节点化位置、缩放、连续角度和透明度。生成目录记录源 bind pose，采样只覆盖绑定通道。精灵批次新增 `SpriteAffine`，使用二维仿射基向量保留父级非均匀缩放、子节点旋转产生的斜切，避免将角色分层近似成角度相加与尺寸相乘。源几何矩阵只用于显示，不改变原碰撞圆。

新资源是两腿、固定身体／阴影、高亮与命中粒子共 5 PNG，总图片数 65；导入同时生成六条 Clip、9／3 个角色／敌人层级节点、5 个角色 pivot，以及 [presentation_manifest.json](../../Asset/Brotato/presentation_manifest.json)。重复导入的 5 PNG、manifest、header 共 7 文件逐字节一致，原 60 PNG 的 SHA-256 均未变；音频的 8 WAV 与 manifest 也重复导入逐字节一致（同一 ffmpeg 版本）。源码保持只读，展示／音频导入器都拒绝输出到源目录内；完整原 UI、起始武器选择、玩家 Smoke 发射器和其余真正有逻辑的内容不计入第四阶段完成范围。

第四阶段验证完成（2026-10-03）：10 个相关 CTest 目标全部通过，耗时 8.56 秒，包括 18 组基础玩法、16 组武器、16 组会话、18 组表现、6 组音频桥，以及菜单布局、三个渲染目标和通用音频模块。飘字 Canvas 位移的最后修正后，重新编译并单独复验 18 组表现测试，通过耗时 .81 秒；通过实际 `run_brotato.bat` 再跑 death 场景，输出 `GL errors=0`、7 个效果实体和 13 个玩法精灵，截图／离线 WAV 已更新。

六个固定表现 GPU 场景、三轮菜单往返、2400 ticks 完整波次及静音场景共 9 个入口通过。9 份离线 WAV 均为 48 kHz／双声道／PCM16，静音文件全零，其余有非零混音数据。WinMM 的 48 kHz 与 44.1 kHz 静音设备流启停也通过；这证明设备路径可打开关闭，不作为人工听音结论。完整入口及截图链接见 README。前三阶段测试结果继续作为历史，不代替本轮验证。

## 第五阶段当前：起始武器与难度选择

### 场景绑定修正六槽假设

`Assets/Scripts` 不存在 `WeaponSetter`。真实起始武器行为来自 `Brotato.unity` 的 Button persistent onClick：`Canvas/StartGame/DownUIWeapons/ScroolView/Scroll View/Viewport/Content` 下有 6 个 Button，各自对 `Container/PlayerGo/ws/w1` 至 `w6` 调用一组 `SetActive`，恰好所选的 1 个为 true，另外 5 个为 false。全场景针对这 6 个武器根的按钮调用共 36 条，全部来自这 6 个按钮；没有商店或其他按钮再启用额外武器。

对应 Button component fileID 顺序为 `1966261189`、`1902595707`、`1307450011`、`232146656`、`1994513103`、`1721886298`。它们依次启用 w1 法杖、w2 火把、w3 Taser、w4 闪电匕首、w5 SMG、w6 双管霰弹，并切换 WP1..WP6 详情、6 个 Select 高亮及 NextBtn。重复点击仍是同一组互斥状态，不增加数量。`Weapons(1/6)` 是展示文本，不能据此推断背包、重复武器、六个挂点、出售或合成；这些规则在该源工程里均没有实现。

原 w1..w6 初始全部 inactive。武器页 NextBtn（component `766271798`）初始 inactive，点击某个武器才启用；Next 的动作进入难度页，并隐藏武器页的 6 个高亮和 Next 自身，但不关闭已选武器。BackBtnOneTwo／BackBtnOneThree 仅变更页面显隐，不重置战斗武器激活状态。

难度内容下只有 Obj 带 Button（component `1250121155`），其 Icon 绑定 `Assets/Sprite/diff_0.asset`。其他 6 个图标对象没有 Button，不应补成多个可选难度。该按钮只改变 UI GameObject 的显隐，4 个目标还是空引用，既不修改生命、伤害、刷新也不设置倍率。唯一 Select 和 NextBtn（component `1701212947`）初始都是 active，可以直接继续。详情仅有 `Please Select`，没有有效难度名称；迁移中的 `DIFFICULTY 0` 是依据资源名给出的可读标签。

难度 Next 依次播放点击音、调用 `GameManager.StartGame`、启用 Container（触发随机地图）、调用 `SpawnPosition.StartSpawningS`，然后复位选择页显隐。所有精确 fileID、sceneLine、方法、参数、目标路径及初始 active 状态见 [selection_manifest.json](../../Asset/Brotato/selection_manifest.json)。

### 会话、界面和可重现资源

会话现在按 `Home → CharacterSelect → WeaponSelect → DifficultySelect → MapSelect → Run` 工作。武器选择只写入下一局配置，菜单中仍不创建战斗 Scene。进入新一轮选择时，预览可以保留，但必须点击武器卡、用方向键切换或按 Space 确认后才能继续；鼠标和 Enter 都遵守相同门槛。返回前页保留本轮确认，回主页／Shutdown 清除确认；`--weapon` 只设置预览，不绕过门槛。

新局采用菜单确认的武器，局内 `1..6` 测试切换不改变下一局菜单选择；`R` 仍保留当前战斗武器重开。开始失败保留当前页与选择，不创建半局或消费地图随机序列。菜单、战斗音频和回主页清理由原会话生命周期继续管理。

以下界面差异保留为迁移设计：地图页仍是源工程之外的预览／验证入口；角色页继续采用第三阶段的默认角色预选，而原 Character Next 初始 inactive；回退到武器页保留可见高亮和确认，不复刻源 Next 清掉高亮后状态不一致的表现。难度页保留唯一实际选择及原数值，不移植指向 Char1 的错误展示调用或空引用。完整菜单版式、角色属性和商店规则不计入本轮完成。

武器 UI 原图与战斗精灵不同。本轮从按钮真实引用导入 `wand_icon`、`torch_icon`、`taser_icon`、`lightning_shiv_icon`、`smg_icon`、`double_barrel_shotgun_icon` 及 `diff_0`，新增 7 PNG，总数 72。生成 `selection_catalog.h` 保存顺序、武器类型、图名和原 PPU 尺寸，菜单按比例缩放。`tools/import_brotato_selection.py` 校验六组互斥绑定与唯一难度，然后输出图像和来源清单；7 PNG、manifest、header 共 9 文件重复生成逐字节一致，旧 65 PNG 未变，71 个源文件哈希已核对，两个源目录输出保护测试均正确拒绝。

第五阶段验证完成（2026-10-03）：10 个相关 CTest 目标全部通过，耗时 10.98 秒，会话测试增至 18 组。12 次真实 OpenGL 场景全部通过且 `GL errors=0`，包括新增 3 个选择页场景、角色页、三轮菜单往返、6 武器各 24 ticks 战斗及完整 2400 ticks 波次。未选择页对鼠标／Enter 统一门控，往返场景验证全部前后导航并最终释放 GameModule。6 武器各攻击 1 次、击杀 1 个；Mage + MAP 5 + 双管霰弹满波攻击 22、击杀 17、材料 2、效果 0，与第四阶段战斗结果一致。新选择页、长按钮、Taser／霰弹和满波画面已目视检查；命令与截图见 README。

### 已发现但尚未修正的武器几何差异

当前战斗仍沿用第二阶段统一的 `player + direction × .55` 挂点近似，混用了物理瞄准根、精灵中心和弹丸发射点。本轮没有在选择流程改动中重写这一套几何，下一步应拆开源脚本根、rotator、Sprite pivot 和射击点，并逐武器回归。

`Brotato.unity` 中 w1 WeaponRotator 约第 225045 行，世界原点相对玩家为 `(.014,-.029)`；w2（227699）、w3（235019）、w4（133584）、w5（153247）、w6（204970）的对应旋转／近战根都在玩家原点。子 Sprite 的局部 pivot x 通常为 `.22`，火把／Taser 的 y 为 `.05`，霰弹为 `.056`，还必须结合 `Assets/Sprite/*.asset` 的 `m_Pivot` 换算矩形中心，不能把子层偏移直接当显示中心。

SMG Point（约 217684 行）相对其旋转根累计为 `(.516,-.049)`；霰弹 Point（83834）为 `(.628,.053)`；法杖 Point（225061）约为 `(.22,0) + rotate((.473,0),1.6401°)`。`w1.cs`／`w5.cs` 的 Update 以脚本 owner 位置瞄准，Shoot 再以 WeaponRotator 到目标的方向赋弹速。这些差异有实际源依据，后续应分离碰撞／显示／发射变换，而不是创建没有依据的六个装备槽。

## 展示资源与空实现的边界

`Assets/Scripts` 只有 26 个自定义脚本；大量文件属于纹理、预制体、动画、FairyGUI、Spine 示例和其他插件。文件数量不能作为完整游戏系统已经实现的证据。

- `DataController.cs`、`BooleanManager.cs` 只有空 Start / Update；`LaserManager.cs` 是空类。
- `CharacterSetter.cs` 只将按钮 Icon 的 Sprite 赋给角色，没有应用角色 UI 中写出的生命、速度、收获等修正。后续角色数值需要明确的数据与规则依据，不能当成已有属性系统移植。
- `UIManager.cs` 读取 `PlayerPrefs.GetInt("Cash")` 展示余额；已审查脚本未发现完整购买扣款、刷新、锁定商品、背包、合成或物品属性结算。商店图片和面板存在不等于经济系统已完成。
- `ItemSetter.cs` 随机选择掉落物图片，不提供物品属性或奖励表；`DestroyTime.cs` 只提供延迟删除。其他敌人、Boss、建筑和大量武器美术不能自动算成已实现的 AI、技能或战斗系统。
- 源 UI 存在按钮绑定和对象显隐流程；第三阶段已核对开始、返回、暂停、死亡与波次按钮，其余页面仍须逐项审查，不能仅靠脚本文件名推断行为。`CompoenentManager.cs` 是 4 秒后显示主页的加载流程，非资源管理系统。

## 后续里程碑与验收

| 阶段 | 交付范围 | 完成依据 |
| --- | --- | --- |
| 2：武器行为（已完成切片） | C++ 类型化武器表、独立武器实体、w2 / w4 往返近战、w3 六段激光、w5 枪、w6 四弹动画；新增选择验证入口 | 6 个 CTest 目标、六武器实际 GPU 截图、火把生存与霰弹波次完成回归通过。起始武器选择页当时尚未迁移；统一挂点近似仍待修正 |
| 3：角色与地图流程（已完成切片） | 五角色外观、五原地图默认随机、显式预览选择、开始／返回主页、暂停／死亡／波次 UI | 场景绑定及 250 装饰可重现导入；15 组会话回归、布局坐标测试和旧玩法回归通过；渲染验证结果见 README |
| 4：动画与音频（已完成切片） | 源分层 Transform 曲线、完整二维仿射显示、敌人出生／死亡表现、命中粒子／飘字、六条 Clip 和八段实际绑定音频 | 有界 ECS 特效／事件，Hermite 采样与音频暂停／清理；历史验证见 README。玩家 Smoke 和完整 UI／音量页尚剩余 |
| 5：起始武器与难度选择（当前） | 恢复六选一起始武器／唯一实际难度／选择门槛，7 个原按钮图标及绑定目录 | 校验 36 条互斥调用，保留原数值，避免由展示文字推断六槽装配；本轮会话／界面验证见 README |
| 后续：其余有逻辑依据的内容 | 修正武器脚本根／精灵 pivot／射击点分离，继续玩家 Smoke 与剩余实际场景行为 | 只有具备行为证据和对应回归的条目才标记“已迁移”；缺失系统先明确新设计再实现 |
| 持续：ECS 与渲染 | 安全的结构变化、查询与生命周期、精灵批次、渲染线程资源管理和容量诊断 | 引擎能力由实际玩法需要推动；与原有 IWanna 等模块共存并执行相关回归 |

资源重现命令、构建、控制键和测试入口见 [README.md](README.md)。每阶段结束应更新本文件中对应范围、已知差异与实际验证结果，而不是将整个资产目录一次复制后视作玩法完成。
