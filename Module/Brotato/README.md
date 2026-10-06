# Brotato ECS 迁移样例

当前完成第十一阶段第一批敌人和刷怪改进（2026-10-05）：八类常规敌人、两类强化变体、分波编队、突袭事件、怪物成长和更强的 Boss。保留五角色、三档难度、六波战役、三类武器组合、四种词条、十种物品、六槽同时攻击、四级武器、合成／出售与随机升级。普通启动默认使用这套规则，`--source-rules` 保留源规则回归。新增玩法采用明确的新设计，美术来自原工程，不宣称恢复了商业版本或源工程未实现的规则。

十五个玩法 System 直接查询 ECS 组件，战斗实体创建／删除在固定步结束后通过通用命令缓冲提交。新增 `SpawnDirector` 组件保存刷新计时、波内阶段、事件、编队及独立随机流，由 `EncounterSystem` 查询驱动。Boss 行为由 `BossBrain` 管理，目标进度和结算由玩家的 `RunProgress` 管理；角色由 `CharacterProfile` 管理，本局难度由玩家的 `RunRules` 管理；物品是带完整拥有者句柄的 `OwnedItem` 实体，燃烧／减速是带完整目标句柄的 `TimedStatus` 实体。属性由升级记录、物品和装备查询重新计算，商店事务在固定步之间提交。六种武器继续使用源场景的物理根、精灵 pivot、发射点、初始反射姿态、动画和音频。

## 怪物、刷新与强化的源码核对

`tools/audit_brotato_enemies.py` 扫描场景／预制体与脚本 GUID，生成 `build/brotato-enemy-source-audit.json`，包含源行和文件哈希。该工程有 26 个游戏脚本、25 张 `40001..40025` 怪物 Sprite，实际只有 `NpcGo.prefab` 绑定 `EnemyNPC`，身体使用 `40001`。主场景只有一个 `SpawnPosition`，引用这一个预制体及 364 个位置。原逻辑是整数 0／1 秒等待、追踪玩家、接触扣 10 HP、武器命中死亡、延迟经验／掉落；切波增加七秒时长，没有怪物属性成长或分波敌人表。这些已通过源规则保留。原刷新协程在停刷后仍递归运行，切波又启动新的链，存在累积刷新缺陷；迁移使用单一生命周期，由本批正式分波规则管理密度。图片不代表已实现的 AI，当前接入九张怪物原图，剩余十六张仍待设计行为。

本批解决旧扩展模式每波重复十六项序列、怪物属性固定的问题。`ExpandedGameplay` 开启 encounters；历史固定场景可以显式关闭它。

| 波次 | 新引入的威胁 | 基础编队／间隔 | 主要突袭 |
| --- | --- | --- | --- |
| 1 | 普通、快怪 | 2 / 2.0 秒 | 虫群 SWARM |
| 2 | 重甲、射手 | 2 / 1.5 秒 | 对侧射手／重甲 CROSSFIRE |
| 3 | 精英、冲锋、强化变体 | 3 / 1.8 秒 | 多侧冲锋 STAMPEDE |
| 4 | 治疗者、召唤者 | 3 / 1.6 秒 | 辅助加重甲 REINFORCEMENTS |
| 5 | 全类型混合 | 4 / 1.7 秒 | 交叉火力 |
| 6 | 全类型混合、最终 Boss | 4 / 1.5 秒 | 多侧冲锋 |

前三成时间间隔 ×1.15，最后四分之一 ×.8；加入 ±10% 的独立随机变化，再乘难度刷新倍率。第七秒开始四秒突袭，每十三秒再次触发，主要突袭与虫群交替。编队沿边缘排列，交叉火力选相对两侧、冲锋选多侧；与玩家至少相距四单位，极小自定义场地选择最远候选。保留 1.5 秒出生预警。容量不足整组跳过，不积累补刷；Boss 先预留容量。

普通怪生命倍率六波依次为 100／140／200／280／380／500%，接触与敌弹伤害 100／108／116／124／132／140%，移速每波 +3.5%；再叠加难度。出生时保存 wave／伤害／速度和强化身份，不因后续出生改变既有怪物。第三波起出现青色 SWIFT（生命 ×1.5、速度 ×1.3）和金色 BULWARK（生命 ×2.5、速度 ×.85、伤害 ×1.2）；体积 ×1.15，冲锋预警使用实际速度。

治疗者 .75 秒预警后恢复五单位内存活普通同伴最大生命的四分之一，最少 1；不治疗自己、Boss、尸体或范围外目标。冷却 2.5 秒。召唤者一秒预警后召唤两只快怪，冷却 4.5 秒；每名召唤者最多六名未死亡子怪，以完整 generation 句柄关联。子怪保留出生预警，但没有 XP／材料奖励，靠近玩家三单位以内不召唤。击杀或击退辅助怪可以取消当前预警。治疗圈绿色、召唤圈紫色；界面显示突袭与强化身份。

Boss 在本波 35% 时间入场，最长等待十二秒；标准第三波 105 HP、末波 405 HP，再乘难度，伤害／速度同步成长。仍必须在波末前真实击杀。源规则和显式关闭 encounters 的历史模式保留旧刷新、固定属性及 Boss 入场行为。

验证命令：

```powershell
python tools/audit_brotato_enemies.py --source "<Unity2021_Botato目录>"
cmake --build build/brotato --target brotato_game brotato_encounters_test
ctest --test-dir build/brotato -R brotato_encounters_test --output-on-failure
bin/brotato_game.exe --smoke-test --smoke-gameplay encounter-reinforcements --map 3 --capture build/reinforcements.ppm
bin/brotato_game.exe --smoke-test --smoke-gameplay healer
bin/brotato_game.exe --smoke-test --smoke-gameplay summoner-pack
bin/brotato_game.exe --smoke-test --smoke-gameplay champions
bin/brotato_game.exe --smoke-test --smoke-gameplay campaign --weapon 5 --map 3
bin/brotato_game.exe --smoke-test --smoke-gameplay campaign-focus --weapon 1 --map 3
```

本批 23 项相关测试通过，encounters 九组验证独立 ECS 系统、外部 archetype、出生安全、容量、治疗／召唤／强化、冻结与源规则。五角色×三难度的 SMG 固定绕场路线为 14 次通关、一次 Mage／DANGER 2 第四波死亡；其余三个实际应用六波场景和默认法杖的 Boss 接近／环绕路线通关。法杖的纯绕场路线会在第三波漏掉限时 Boss；需要主动接近并躲避 Boss，系统不会自动优先锁定目标。实际日志和逐项核对见 MIGRATION；这些路线验证不能覆盖所有构筑平衡。

## 角色属性与三档难度

以下是本项目新增规则，不来自源脚本的职业实现。默认 Well-Rounded／STANDARD 保留上一批基准。角色奖金与升级、物品相加后统一限幅；不会因切波或重新计算反复叠加。

| 角色 | 默认起始 HP | 特长 | 代价 |
| --- | ---: | --- | --- |
| Well-Rounded | 150 | 通用起始规则，适合任意武器 | 无额外加成／惩罚 |
| Brawler | 180 | 火把／匕首伤害 +2，护甲 +2 | 移动速度 −10% |
| Crazy | 130 | 暴击率 +15 个百分点、攻速 +10%、移速 +15% | 最大生命 −20 |
| Ranger | 125 | 法杖／Taser／SMG／霰弹伤害 +1，拾取半径 +25% | 最大生命 −25，近战没有伤害奖金 |
| Mage | 140 | 法杖／火把命中点燃，燃烧每跳伤害 +2 | 最大生命 −10、攻速 −10% |

武器特长在等级基础伤害之后加算，不乘武器等级；例如 Brawler 的二级火把为 `ceil(3 × 1.5) + 2 = 7`，再加升级／物品伤害。Mage 奖金只加入元素武器的完整攻击快照，可与元素组合、燃烧词条和香肠叠加，仍受燃烧上限约束。已经发射的弹丸和生成的状态保留旧快照，来源职业变化或退休不追溯修改。Crazy 暴击复用每把武器的独立随机流，恢复频率仍保留原攻击动画时长。菜单预览显示所选角色的起始伤害与恢复时间。

| 难度（参数） | 普通敌人 HP | 接触／敌弹伤害 | 敌人移速（含冲锋） | 刷怪间隔 | Boss HP | 波间治疗 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| STANDARD（0） | 100% | 100% | 100% | 100% | 100% | 最大生命 25% |
| DANGER 1（1） | 120% | 115% | 105% | 90% | 120% | 最大生命 20% |
| DANGER 2（2） | 145% | 135% | 110% | 80% | 145% | 最大生命 15% |

HP／伤害／治疗用整数向上取整，零伤害仍为零，正值上限 100000；倍率在敌人创建或攻击生成时应用一次。普通和 Boss 冲锋的显示预警长度跟随实际移速，燃烧／减速继续作用。间隔倍率不额外抽取随机数，最短保持一个固定步；容量限制照常执行，不以难度突破实体上限。难度不修改波次时长、攻击动画、XP 单价、掉落概率、商店价格或升级／商店随机流。更高难度刷出更多敌人，也可能产生更多 XP；这一批交付明确可玩的规则，后续还需继续调节构筑与成长平衡。

角色组件缺失或非法时使用通用基准，不继承主玩家职业。RunRules 缺失或非法时使用 STANDARD；正常菜单及启动配置拒绝无效选择。`profiles` 在 `expanded && builds` 模式启用，暴击和燃烧仍需要相应 traits 机制。源模式以及关闭 profiles 的历史模式只有一个难度选项，`--source-rules --difficulty 1/2` 会明确拒绝。重开保留本局角色／难度，清空升级、购物和结果；主页保留选项预览，新局重新创建组件。

## 六波战役与 Boss

普通模式共六波，时长依次为 20／27／34／41／48／55 秒，合计 225 秒战斗时间；升级、暂停和商店停留不计入。第一至第五波完成后进入商店，按难度免费恢复最大生命的 25%／20%／15%（向上取整，不超过缺失生命），面板显示本次 HEAL。第三波和第六波各生成一个 GUARDIAN：必须在本波倒计时结束前击杀，提前击杀仍需生存至波末。末波成功直接进入 VICTORY，不再生成商店或发放波间材料。

下表为开启 encounters 后的 STANDARD 数值；较高难度再叠加难度倍率。关闭 encounters 的历史基准仍为 70／180 HP、15／20 接触伤害。

| Boss | HP | 接触伤害 | 延迟结算 XP |
| --- | ---: | ---: | ---: |
| 第三波 GUARDIAN | 105 | 18 | 12 |
| 第六波 FINAL GUARDIAN | 405 | 28 | 24 |

Boss 按扇形齐射、环形齐射、冲锋循环攻击。预警开始时锁定方向，预警期间停止移动；恢复后以基础敌人速度的 .65 倍接近玩家。扇形为五发、环形为八发，速度分别为 7／5.5；冲锋速度 9，持续 .55 秒。生命降至一半后永久狂暴，扇形增至七发、环形十二发、冲锋速度 12，预警及恢复时间缩短。狂暴不会因治疗撤销。Boss 接受燃烧／减速，只接受 10% 击退，预警不被击退打断。攻击整组预留敌弹容量，不足时整组取消并正常进入恢复。

玩家死亡或未在限时内击杀 Boss 均进入 DEFEAT；同一步击杀 Boss 并死亡仍判失败。失败没有复活或下一波。胜负面板保存本局波次、击杀、Boss 数、等级、生命、材料、装备／物品数及战斗时长，冻结模拟并清理战斗实体与短音效。`R`／`Enter` 重开，`H`／`Esc` 返回主页。源规则及显式关闭 `campaign` 的历史模式保留原死亡／复活和无限后续波次。

Boss 使用原项目 `40020.asset` 的图片，名称、技能、数值和完整局规则均为新增设计。角色主动技能、更多 Boss 与构筑平衡、永久成长／存档和奖励稀有度仍待后续实现。

## 当前可玩的成长规则

下表为第一波、无强化变体、STANDARD 的基础属性；后续波次按前文成长规则缩放。

| 敌人 | HP | 移速倍率 | 接触伤害 | 延迟结算 XP |
| --- | ---: | ---: | ---: | ---: |
| 普通 NORMAL | 2 | 1 | 10 | 2 |
| 快速 RUNNER | 1 | 1.65 | 7 | 3 |
| 重甲 ARMORED | 6 | .65 | 20 | 6 |
| 远程 SHOOTER | 3 | .85 | 7 | 4 |
| 冲锋 CHARGER | 4 | .9 | 15 | 5 |
| 精英 ELITE | 12 | .8 | 25 | 10 |
| 治疗者 HEALER | 5 | .75 | 6 | 5 |
| 召唤者 SUMMONER | 8 | .5 | 8 | 7 |

八类敌人通过分波编队和突袭混合刷出；重甲和精英没有额外减伤公式，分别只接受 35%／20% 的击退强度。怪物使用原项目中恢复的图片。法杖／火把／激光／匕首／SMG／霰弹每次有效命中的基础伤害为 `2/3/1/2/1/1`。近战往返和激光各段在一次攻击中只伤害同一个敌人一次；下一次攻击可以再次命中，霰弹各弹丸独立结算。弹丸出生时保存伤害，不受之后的属性变化影响。玩家受伤后有 .75 秒保护，持续接触在保护结束后可再次造成伤害。

远程敌人在距离 3.2..5.2 内保持位置，攻击前显示 .45 秒瞄准线；冲锋敌人显示 .65 秒路径预警，再以速度 10 沿锁定方向冲刺 .55 秒，结束后停留 .6 秒。精英预警 .7 秒后同时发射三发扇形弹丸并冲刺 .45 秒，随后停留 .8 秒。瞄准在预警开始时锁定，玩家可以横向躲避；冲锋接触使用相对运动扫掠，避免高速跨过玩家而漏掉碰撞。

敌方弹丸速度 7、寿命 3 秒，默认远程／精英伤害为 6／8，只碰撞玩家，不伤害同阵营。独立 `HostileProjectile` 保存完整来源句柄，拥有者死亡后在途弹丸仍继续；切换玩家武器不清除敌弹。默认容量 128，同时生成请求也计入预留，精英三发齐射在容量不足时整组拒绝。暂停、升级、切波和重开都遵守现有生命周期。

六武器击退强度依次为 `4/9/0/6/2/5`，冲量持续 .12 秒、每固定步衰减至 90%。击退能打断非精英敌人的攻击预警；精英预警不被打断。法杖命中还产生半径 1.15 的爆炸，按敌人碰撞圆与范围的相交结算完整攻击伤害，直接目标只扣一次，不递归引发爆炸。尸体不重复结算 XP／材料，特效容量不足不影响伤害。黄色瞄准线、红色冲锋路径和橙色爆炸圈来自 ECS 值快照，使用通用精灵批次的 Line／Ring 绘制；动画圈表示冲击效果，不是逐帧碰撞边界。

当前等级从 0 开始，下一级所需 XP 为 `8 + 4 × 当前等级`，上限 4008。经验仍在 1.5 秒死亡延迟结束时结算；波末清场会丢弃尚未结算的尸体。每级从六类奖励中抽取三个不同选项：伤害 `+1`、攻击频率倍率 `+.20`、移动速度倍率 `+.15`、最大生命 `+10`、护甲 `+1`、拾取半径倍率 `+.15`。一次得到多个等级时逐次选择，当前选项不会在等待期间变化。奖励随机流独立于刷怪和掉落。

升级暂停战斗、波次计时、动画、特效、回血与玩法短音效，音乐继续。属性按基础值累加后统一限幅：伤害加成 10000、攻速倍率 .25..5、移速倍率 .25..3、最大 HP 100000、护甲 20、拾取倍率 4、回血 20 HP/s。护甲从每次玩家受伤中扣除，正伤害最低仍为 1；不会减免零伤害。最大生命增长时增加相同数量的当前 HP，重复计算不会再次治疗，死亡状态不会被物品复活。攻速缩短恢复时间，保留源攻击动画时长。

## 波间商店与物品

波次结束清理敌人、弹丸、状态和掉落后进入四格商店。商品允许重复出现，可按卡片购买、锁定或刷新。购买立即应用属性；十种物品效果是新增设计，图标来自原工程对应的 UI Sprite。

| 物品 | 每份效果 | 基础价格（材料） |
| --- | --- | ---: |
| LENS 镜片 | 所有武器伤害 +1 | 6 |
| COFFEE 咖啡 | 攻击频率倍率 +.15 | 7 |
| BEANIE 毛帽 | 移速倍率 +.10 | 6 |
| LEATHER VEST 皮背心 | 护甲 +1 | 8 |
| PLANT 植物 | 回血 +.5 HP/s、拾取半径倍率 +.25 | 8 |
| CAKE 蛋糕 | 最大 HP +10，同时恢复增加的 HP | 7 |
| BANDANA 头巾 | 弹丸额外穿透一个目标 | 12 |
| BAT 蝙蝠 | 直接命中吸血 +5% | 10 |
| SUNGLASSES 太阳镜 | 暴击率 +10% | 10 |
| SCARED SAUSAGE 香肠 | 所有直接命中点燃，燃烧每跳伤害 +1 | 12 |

每种物品最多累计 99 份，同类购买优先增加已有实体的堆叠数量。默认全场景物品实体容量为 128，其他 archetype 中的 `OwnedItem` 也计入容量和属性。余额上限 1000000。规则如下：

| 行为 | 当前规则 |
| --- | --- |
| 通关材料 | `15 + min(3 × (波次 − 1), 1000)`，加到已有余额 |
| 商品价格 | 基础价格 + `min(进入商店次数 / 2, 50)`，次数从 1 开始，整数除法 |
| 刷新费用 | 本次商店首次 2，随后每次 +1，最高 32；下一波重新从 2 开始 |
| 锁定 | 未售商品的类型和价格跨刷新、跨波次保留；购买后解除锁定 |
| 失败操作 | 余额不足、商品已售、堆叠／容量已满或非法价格时不扣款；四格全锁时刷新不扣款，也不消耗随机序列 |

商店冻结模拟、倒计时、动画和回血，清理战斗短音效，音乐与菜单音继续。离开商店的输入不会补跑停留时间。下一波保留生命、等级、XP、升级、物品、余额及锁定；`R` 重开或返回主页后新开局清空。源规则／历史模式的 `V` 复活保留物品并恢复到成长后的最大生命，普通战役不提供复活。

商店同时售六种武器和十种被动物品。武器装备、等级、词条、组合加成、经济及 Boss 是新增设计；奖励稀有度和永久成长尚未实现。

## 武器装备与合成

开局有一把一级武器，位于第一个装备格。最多同时装备六把，允许重复类型；每把都是独立 `Weapon` 实体，自动寻敌、攻击、冷却与命中去重相互独立。数字键在战斗中只选择查看的装备格，所有装备持续攻击，不会免费取得未购买的武器。第一个挂点保留源几何，其他五个在玩家旁偏移，物理发射点、近战归位和显示使用相同挂点。

| 等级 | 武器基础伤害倍率（向上取整后加玩家伤害） | 冷却倍率 | 基础价格倍率 | 颜色 |
| --- | ---: | ---: | ---: | --- |
| 1 | 1 | 1 | 1 | 白 |
| 2 | 1.5 | .9 | 2 | 绿 |
| 3 | 2 | .8 | 4 | 蓝 |
| 4 | 2.5 | .7 | 8 | 紫 |

六武器基础价格依次为法杖 10、火把 10、Taser 12、闪电匕首 10、SMG 14、霰弹 12；商店再加本次访问的价格增量。每个新商品有 50% 概率为武器、50% 为物品，同类中等概率抽取。武器等级分布按进入商店次数决定：

| 商店次数 | 一级 | 二级 | 三级 | 四级 |
| --- | ---: | ---: | ---: | ---: |
| 1 | 100% | 0 | 0 | 0 |
| 2–4 | 65% | 35% | 0 | 0 |
| 5–8 | 65% | 20% | 15% | 0 |
| 9 起 | 65% | 20% | 10% | 5% |

购买武器占用第一个空格，六格已满时须先合成或出售。默认整个 Scene 的武器实体上限 128，其他拥有者和 archetype 的武器也计入总容量。购买成功创建完整实体后才扣款；失败不扣款或消耗随机序列。锁定武器保存类型、等级和旧价格，跨刷新／波次保留。

商店按 `Tab` 进入装备页，选择一格后按 `U` 合成：找到同拥有者、同类型、同等级的另一把，保留选中的实体与格位、删除另一把并升一级，无材料费用，四级不能继续合成。不自动连锁合成；没有匹配时不改变装备。`X` 出售选中的武器，获得该等级基础价格的一半，至少保留一把。出售当前查看的武器会重新选择有效装备，不留下失效句柄。

切波和复活保留所有武器与等级，波间清理全部攻击状态及在途弹丸。重开只保留当前查看武器的类型，恢复一把一级武器，清空其他装备及本局成长／材料；回主页新开局使用菜单确认的起始武器。升级、暂停、死亡和商店冻结所有武器，切换查看装备不会重置冷却或清除在途攻击。

## 武器类别、词条与命中效果

每两把同类别装备为该类别的武器增加一档加成，按实体数量计数，最高三档。合成会少一把装备，类别阈值立即重算；等级不提高类别计数。装备页显示当前数量和加成。

| 类别 | 武器 | 每两把的加成 |
| --- | --- | --- |
| ELEMENTAL 元素 | 法杖、火把 | 燃烧每跳伤害 +1，并点燃直接命中的敌人 |
| PRECISION 精准 | Taser、闪电匕首 | 暴击率 +10% |
| BALLISTIC 弹道 | SMG、双管霰弹 | 攻击频率 +10% |

| 词条 | 规则 |
| --- | --- |
| BURNING | 燃烧每跳伤害 +1，三级／四级为 +2 |
| CHILLING | 减速 1.5 秒，一级减少 35%，每级再增加 5%，最高 50%；包括冲锋速度 |
| PIERCING | 额外穿透一个目标，三级／四级为两个；只出现在法杖、SMG、霰弹 |
| VAMPIRIC | 直接命中吸血 +10% |

武器商品词条抽取为普通 60%、四词条各 10%；不能使用穿透的武器将这 10% 转为普通。带词条商品加价 3 材料，锁定保留词条。合成只要求同类型／同等级，保留选中武器的词条；出售仍返还等级基础价的一半，不返还词条溢价。换波保留词条与每把武器的暴击随机流，重开恢复无词条起始装备。

暴击总概率最高 60%，整次攻击伤害翻倍，最高 100000；霰弹四发共用一次判定和完整伤害快照。随机流独立于刷怪、掉落、升级和商店；容量不足导致没有发射时不抽取。普通伤害、暴击、燃烧、减速、穿透和吸血在攻击开始时一并保存，飞行途中改属性或删除来源武器不会改变它们。

燃烧持续 2 秒，每 .5 秒结算一次，完整持续时间共四跳；重复命中刷新持续时间，保留最强伤害与已累计的脉冲时间，快速攻击不会不断推迟伤害。减速保留最强倍率。每个目标同种状态只保留一个实体，默认全 Scene 最多 256 个，包括其他 archetype；同一步请求预留容量并在回放时合并重复，达到容量后拒绝额外状态请求，直接伤害仍正常结算。状态目标退休或死亡后清理，旧句柄不会绑定到复用 ID 的新敌人；来源退休不取消燃烧。

燃烧不暴击、不吸血、不爆炸、不再次触发状态，击杀仍走正常掉落和延迟 XP。范围爆炸只造成伤害／击退，不触发状态或吸血。吸血只按直接命中实际扣除的 HP 计算，最高 50%，小数累计，不能用过量伤害治疗、超过最大生命或复活；满血后清空余数。额外穿透最高三个目标，按扫掠命中先后和实体 ID 确定顺序，每个弹丸对同一目标只结算一次。

暂停、升级、死亡和商店冻结状态；切波／重开／返回主页清理全部状态。敌人血条上方显示橙色 BURN、蓝色 SLOW。上述规则只在 `expanded && builds && arsenal && traits` 全部开启时启用，历史阶段可以单独关闭 traits，源模式不受影响。

## 构建与运行

在仓库根目录执行：

```powershell
cmake -S . -B build/brotato -G Ninja '-DCMAKE_BUILD_TYPE=Debug' '-DCMAKE_TOOLCHAIN_FILE=clang-msvc.cmake'
cmake --build build/brotato --target brotato_game brotato_game_test brotato_weapon_test brotato_weapon_geometry_test brotato_growth_test brotato_enemy_combat_test brotato_build_shop_test brotato_arsenal_test brotato_traits_test brotato_campaign_test brotato_profiles_test brotato_session_test brotato_menu_layout_test brotato_presentation_test brotato_game_audio_test brotato_ecs_architecture_test ecs_world_operations_test sprite_animation_test sprite_batch_geometry_test sprite_batch_runtime_test audio_module_test iwanna_game_test iwanna_entity_lifecycle_test
.\bin\brotato_game.exe --assets .\Asset\Brotato
```

也可双击 [run_brotato.bat](../../run_brotato.bat)。脚本在 EXE 不存在时构建，已有 EXE 时直接启动；修改源码后先执行构建命令。窗口默认 1280 × 720，以 16:9 等比显示，使用现有窗口模块和 OpenGL 4.5 RHI。构建目标 `brotato_assets` 将全部资源复制到 `bin/Brotato`；运行不需要 Unity、Python 或源工程目录。

普通流程为 **主页 → 角色 → 武器 → 难度 → 地图 → 第一波**。角色为 Well-Rounded、Brawler、Crazy、Ranger、Mage，普通模式使用本项目明确设计的起始属性及武器特长；源规则模式仍只改变外观。武器页须点击一张卡片或用键盘选择／确认，才开放下一步；每次从主页重新进入都需要确认起始武器。普通模式难度页提供 STANDARD／DANGER 1／DANGER 2，并显示实际倍率；源规则页仍只提供原场景真正绑定的 `DIFFICULTY 0`。地图默认 RANDOM，每次新局抽取一次，允许连续抽到同一图；显式选择 MAP 1..5 仍是新增的预览与验证入口。

| 所在界面 | 按键 | 操作 |
| --- | --- | --- |
| 主页／选择页 | `Enter` | 开始选择／下一步／开局 |
| 选择页 | `W A S D`／方向键 | 移动当前选择 |
| 武器页 | `Space` | 确认当前预览武器，开放下一步 |
| 选择页 | `PageUp`／`PageDown` | 切换选项页 |
| 选择页 | `Esc` | 返回上一页；角色页返回主页 |
| 主页 | `Esc`／EXIT 按钮 | 退出 |
| 游戏 | `W A S D`／方向键 | 移动，斜向归一化 |
| 游戏 | `1` 至 `6` | 查看对应装备格；源规则模式仍切换六种武器 |
| 升级面板 | `1` 至 `3`／鼠标点击 | 选择当前对应卡片的奖励 |
| 升级面板 | `A D`／左右方向键、`Enter` | 移动焦点并确认；选择完成前保持暂停 |
| 商店 | `1` 至 `4`／鼠标点击 | 购买对应商品 |
| 商店 | `Shift` + `1` 至 `4`／LOCK 按钮 | 锁定／解锁对应未售商品 |
| 商店 | `A D`／左右方向键、`Space` | 移动购买焦点并确认购买 |
| 商店 | `F`／REROLL 按钮 | 付费刷新未锁商品 |
| 商店 | `Enter`／NEXT WAVE 按钮 | 开始下一波 |
| 商店 | `Tab`／页签按钮 | 切换商品与装备页 |
| 商店装备页 | `1` 至 `6`／鼠标／左右方向键 | 选择要操作的装备格 |
| 商店装备页 | `U`／MERGE 按钮 | 合并同类型同级武器，升一级 |
| 商店装备页 | `X`／SELL 按钮 | 出售选中的武器；至少保留一把 |
| 全部界面 | `M` | 切换静音；也可用 `--mute` 静音启动 |
| 游戏／暂停 | `P`／`Esc` | 暂停／继续 |
| 游戏及覆盖面板 | `R` | 重开第一波，保留本局角色、地图和当前武器 |
| 波次完成 | `Enter` | 下一波 |
| 源规则／历史模式死亡 | `V` | 恢复当前最大 HP，继续当前波 |
| 胜利／失败结算 | `R`／`Enter`／NEW RUN 按钮 | 重开第一波，清空本局构筑和结果 |
| 暂停／死亡／波次完成／升级／商店／结算 | `H`／HOME 按钮 | 返回主页 |
| 死亡／波次完成／商店／结算 | `Esc` | 返回主页 |

暂停、死亡和波次完成面板也有对应鼠标按钮。返回主页立即销毁本局 `GameModule`，包括玩家、武器、敌人、弹丸、掉落物、计时器和进度，角色／难度／地图及菜单武器预览保留。再次开局采用本次在武器页确认的武器；局内数字键切换不会改写菜单的起始选择。`R` 在本局内重开仍保留当前武器。返回主页暂没有原确认框。

所有武器自动瞄准范围内最近的存活敌人。普通模式查看另一装备不取消攻击，所有装备独立冷却；源规则和历史单武器模式切换会取消当前攻击并清除玩家弹丸，保留敌方弹丸及六类型各自的冷却。暂停、死亡、升级和商店冻结冷却，升级与商店内禁用战斗焦点切换，商店装备选择只决定事务目标。材料计数与死亡延迟结算的经验独立。武器选择页从会话配置读取实际伤害和击退／爆炸效果。

武器物理根与图片中心分开：法杖根相对玩家为 `(.014,-.029)`，其他五种在玩家原点；显示再叠加各自子节点偏移、旋转和 Sprite pivot。SMG 发射点是 `(.516,-.049)`，霰弹是 `(.628,.053)`，法杖保留额外子旋转。源初始世界姿态中的 XY 反射保留，自动瞄准赋世界 Z 旋转后移除；火把／匕首归位恢复初始姿态。玩家身体翻转不影响兄弟节点下的武器。激光六段的显示与命中都从同一源根计算。

角色外观现在按真实源层级组合：固定土豆身体、角色图片叠加层、两条独立腿、阴影与高亮标记。动画只改变显示姿态，不改变碰撞圆；横向和纵向实际移动都播放步行动画。敌人死亡后旋转缩小，0.5 秒后身体不可见，仍在原 1.5 秒死亡延迟结束时结算经验。新玩法显示敌人名称、血条、受击闪色及橙色实际扣血数字；仅 `--source-rules` 显示源脚本的白色随机装饰 `+1..8`。

背景音乐在主页和战斗中连续播放；暂停冻结当前玩法短音效，菜单音和音乐继续。死亡、重开、切波和回主页清理玩法音效，避免旧局声音残留。火把、匕首和激光没有源发射声音绑定，保持无开火音效；火把命中有额外燃烧声。

启动参数 `--character 1..5`、`--difficulty 0..2`、`--map 1..5|random` 和 `--weapon 1..6` 设置初始预览，不跳过普通模式的主页或武器确认。例如：

```powershell
.\bin\brotato_game.exe --character 5 --difficulty 1 --map 3 --weapon 6
```

## 验证入口

```powershell
ctest --test-dir build/brotato -R '^(brotato_.*|ecs_world_operations_test|sprite_animation_test|sprite_batch_geometry_test|sprite_batch_runtime_test|audio_module_test|iwanna_game_test|iwanna_entity_lifecycle_test)$' --output-on-failure

# 第二批角色／难度详情与完整局画面。
.\bin\brotato_game.exe --smoke-menu characters --character 2 --capture .\build\brotato-profiles-brawler-menu.png
.\bin\brotato_game.exe --smoke-menu difficulty --difficulty 2 --capture .\build\brotato-profiles-danger2.png
.\bin\brotato_game.exe --smoke-gameplay campaign --character 5 --weapon 5 --map 3 --capture .\build\brotato-profiles-mage-campaign.png
.\bin\brotato_game.exe --smoke-gameplay campaign --character 4 --difficulty 2 --weapon 5 --map 3 --capture .\build\brotato-profiles-ranger-danger2-campaign.png

# 第十阶段：完整普通六波流程，SMG 起始、正常升级和赚取材料购物。
.\bin\brotato_game.exe --smoke-gameplay campaign --weapon 5 --map 3 --capture .\build\brotato-boss-campaign.png

# 短机制夹具：Boss 预警／齐射、胜负、超时、重开和返回主页。
foreach ($scene in @('boss-fan','boss-ring','boss-charge','boss-enrage','boss-volley','run-victory','run-defeat','boss-timeout','run-restart','run-home')) {
    .\bin\brotato_game.exe --smoke-gameplay $scene --capture ".\build\brotato-boss-$scene.png"
}

# 第九阶段词条、组合和状态场景。
.\bin\brotato_game.exe --smoke-gameplay traits-shop --capture .\build\brotato-traits-traits-shop.png
.\bin\brotato_game.exe --smoke-gameplay traits-build --capture .\build\brotato-traits-traits-build.png
.\bin\brotato_game.exe --smoke-gameplay families --capture .\build\brotato-traits-families.png
.\bin\brotato_game.exe --smoke-gameplay burn --capture .\build\brotato-traits-burn.png
.\bin\brotato_game.exe --smoke-gameplay slow --capture .\build\brotato-traits-slow.png
.\bin\brotato_game.exe --smoke-gameplay pierce --capture .\build\brotato-traits-pierce.png
.\bin\brotato_game.exe --smoke-gameplay lifesteal --capture .\build\brotato-traits-lifesteal.png

# 有种子的正常生存流程，自动开局并捕获。
.\bin\brotato_game.exe --smoke-test --smoke-ticks 720 --capture .\build\brotato-survival.png

# 六敌人、瞄准／冲锋预警、躲避、精英齐射、范围命中与击退。
.\bin\brotato_game.exe --smoke-gameplay enemies --capture .\build\brotato-enemy-enemies.png
.\bin\brotato_game.exe --smoke-gameplay ranged --capture .\build\brotato-enemy-ranged.png
.\bin\brotato_game.exe --smoke-gameplay ranged-fire --capture .\build\brotato-enemy-ranged-fire.png
.\bin\brotato_game.exe --smoke-gameplay charge --capture .\build\brotato-enemy-charge.png
.\bin\brotato_game.exe --smoke-gameplay charge-dodge --capture .\build\brotato-enemy-charge-dodge.png
.\bin\brotato_game.exe --smoke-gameplay elite --capture .\build\brotato-enemy-elite.png
.\bin\brotato_game.exe --smoke-gameplay blast --capture .\build\brotato-enemy-blast.png
.\bin\brotato_game.exe --smoke-gameplay knockback --capture .\build\brotato-enemy-knockback.png

# 新奖励池、商店购买／锁定／刷新／跨波次，以及六种物品属性。
.\bin\brotato_game.exe --smoke-gameplay weapon-shop --capture .\build\brotato-arsenal-weapon-shop.png
.\bin\brotato_game.exe --smoke-gameplay weapon-buy --capture .\build\brotato-arsenal-weapon-buy.png
.\bin\brotato_game.exe --smoke-gameplay weapon-merge --capture .\build\brotato-arsenal-weapon-merge.png
.\bin\brotato_game.exe --smoke-gameplay weapon-sell --capture .\build\brotato-arsenal-weapon-sell.png
.\bin\brotato_game.exe --smoke-gameplay arsenal --capture .\build\brotato-arsenal-arsenal.png

# 第一批物品商店的兼容夹具，关闭多武器构筑。
.\bin\brotato_game.exe --smoke-gameplay shop --capture .\build\brotato-build-shop.png
.\bin\brotato_game.exe --smoke-gameplay shop-buy --capture .\build\brotato-build-shop-buy.png
.\bin\brotato_game.exe --smoke-gameplay shop-lock --capture .\build\brotato-build-shop-lock.png
.\bin\brotato_game.exe --smoke-gameplay shop-cycle --capture .\build\brotato-build-shop-cycle.png
.\bin\brotato_game.exe --smoke-gameplay build --capture .\build\brotato-build-build.png
.\bin\brotato_game.exe --smoke-gameplay upgrade --capture .\build\brotato-build-upgrade.png

# 第七阶段固定三奖励的兼容回归，以及非致命命中。
.\bin\brotato_game.exe --smoke-gameplay armored-hit --capture .\build\brotato-growth-armored-hit.png
.\bin\brotato_game.exe --smoke-gameplay growth-cycle --capture .\build\brotato-growth-growth-cycle.png

# 自动选择仅用于战斗验证，普通游戏由玩家选择。
.\bin\brotato_game.exe --smoke-test --weapon 5 --auto-upgrade --smoke-ticks 2400 --capture .\build\brotato-build-natural-shop.png
.\bin\brotato_game.exe --smoke-test --weapon 6 --character 5 --map 5 --auto-upgrade --smoke-ticks 2400 --capture .\build\brotato-growth-wave-burst.png

# 源规则回归。
.\bin\brotato_game.exe --source-rules --smoke-test --weapon 6 --character 5 --map 5 --smoke-ticks 2400 --capture .\build\brotato-growth-source-wave.png

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

第十阶段第二批验证（2026-10-05）：22 个相关 CTest 全部通过，167.61 秒。新增 `brotato_profiles_test` 八组检查覆盖角色起始值／重复计算／冻结、实际近战／弹丸伤害、职业燃烧与暴击快照、独立 BuildSystem 与外部 archetype／拥有者 generation、缺失组件的基准、难度生命／敌弹／移动／预警／刷怪／治疗、成长购物／重开、菜单选择与失败事务、源规则兼容，以及完整 5×3 组合。

十五种角色与难度组合均以 SMG 起始、默认生命、实际奖励、自然商品和赚取材料完成六波，225 秒击杀两个 Boss。普通路线保持上一批结果；全记录见 `build/brotato-profiles-natural-results.json`。只验证本种子及路线，不将其作为全部起始武器与构筑平衡的保证。

26 个真实 OpenGL 场景全部通过，GL errors=0、batchDraws=1。已目视核对[角色优势／代价](../../build/brotato-profiles-brawler-menu.png)、[Mage 元素规则](../../build/brotato-profiles-mage-menu.png)、[三档难度徽章和倍率](../../build/brotato-profiles-danger2.png)、[职业武器预览](../../build/brotato-profiles-brawler-weapon.png)及三条完整局结果。完整自然流程如下，均未注入额外生命／材料／库存／伤害，CPU 与实际应用结果一致：

| 实际应用完整局 | 最终 HP | 等级 | 击杀／Boss | 职业效果观察 |
| --- | ---: | ---: | ---: | --- |
| [Mage／STANDARD](../../build/brotato-profiles-mage-campaign.png) | 190/190 | 25 | 395／2 | 12 次燃烧结算 |
| [Crazy／DANGER 1](../../build/brotato-profiles-crazy-campaign.png) | 179/180 | 26 | 438／2 | 135 次暴击攻击 |
| [Ranger／DANGER 2](../../build/brotato-profiles-ranger-danger2-campaign.png) | 175/175 | 28 | 467／2 | 四远程类型伤害奖金实际参与 |

图形记录包含十三个菜单／角色／难度／源 UI 检查、上述三完整局和十个兼容检查，见 `build/brotato-profiles-gpu-results.json`。短难度敌人／冲锋场景仍是安静的展示夹具，不计作自然生存成绩。源模式六武器和霰弹／Mage／MAP 5 满波兼容，后者仍为 22 攻击／17 击杀／材料 2／HP 20／XP 32。没有新增美术资产或渲染后端，图片仍为 88；新徽章由通用 Ring／Text 绘制。

第十阶段第一批历史验证（2026-10-05）：21 个相关 CTest 全部通过，50.90 秒。新增 `brotato_campaign_test` 九组检查覆盖目标生命周期、暂停／升级冻结、扇形／环形／锁定冲锋、半血狂暴与实际减速、整组弹丸容量、Boss 容量耗尽后重试、目标删除／generation 复用不判胜、六波胜利／无末波商店、同帧死亡优先、超时失败、结果值冻结与清场／重开、源规则／无限历史模式，以及独立系统和外部 archetype。音频桥增至九组，验证胜负页面清理短音效、音乐继续和重开不恢复旧声音。

正常生命、实际奖励和赚取材料购物的 SMG 路线完成全部六波：225 秒、396 击杀、两个 Boss、25 级、六把武器、四件物品、材料 11、HP 200/200。无窗口测试与真实应用得到相同结果；仅证明该路线及选择可以完成整局，不代表所有起始武器或随机种子的平衡。26 个真实 OpenGL 检查全部通过，`GL errors=0`、`batchDraws=1`，包含完整六波、十个新 Boss／结算夹具及十五个兼容场景。源霰弹／Mage／MAP 5 满波保持 22 攻击、17 击杀、材料 2、HP 20、XP 32。静止 SMG 首波 HP 从 127 恢复至 150，商店正确显示 HEAL +23。

已目视检查[完整局胜利](../../build/brotato-boss-campaign.png)、[扇形预警](../../build/brotato-boss-boss-fan.png)、[环形预警](../../build/brotato-boss-boss-ring.png)、[冲锋预警](../../build/brotato-boss-boss-charge.png)、[狂暴](../../build/brotato-boss-boss-enrage.png)、[实际齐射](../../build/brotato-boss-boss-volley.png)、[死亡失败](../../build/brotato-boss-run-defeat.png)、[超时失败](../../build/brotato-boss-boss-timeout.png)、[重开](../../build/brotato-boss-run-restart.png)、[返回主页](../../build/brotato-boss-run-home.png)与波间补给画面。日志及命令记录于 `build/brotato-boss-gpu-results.json`。`campaign` 场景保留普通配置、路线移动、实际升级和自然商品；其余十个新短夹具关闭常规伤害／移动或缩短到一波，并在胜负场景注入实际弹丸伤害，只验证机制／界面，不算自然通关成绩。精灵批次数不等于全部渲染 draw call。

第九阶段第三批历史验证（2026-10-05）：20 个相关 CTest 目标全部通过，31.15 秒。新增 `brotato_traits_test` 九组检查覆盖类别／物品重算、燃烧四跳与刷新、状态跨 archetype／代际／容量／冻结、减速移动与冲锋、实际伤害吸血与非递归效果、穿透扫掠与组件重排、霰弹完整快照／独立暴击随机流、词条商品／锁定／合成／重开及自然两波。

普通移动使用真实升级奖励、自然刷出的商品和赚取的材料完成两波，HP 26/160、49 击杀、材料 24、两把武器；这条路线没有产生燃烧或暴击，不用它代替机制测试。固定机制与商店场景有明确夹具数值，通过普通系统和购买按钮执行，独立验证新效果。

23 个实际 OpenGL 检查全部通过，GL errors=0、batchDraws=1，含七个新场景、六武器源回归、两条源满波、自然 SMG、菜单往返与旧玩法。源霰弹／Mage／MAP 5 的 2400 ticks 基线保持 22 次攻击、17 击杀、材料 2、HP 20、XP 32。已检查[词条商店](../../build/brotato-traits-traits-shop.png)、[十物品构筑](../../build/brotato-traits-traits-build.png)、[三类武器组合](../../build/brotato-traits-families.png)、[燃烧](../../build/brotato-traits-burn.png)、[减速](../../build/brotato-traits-slow.png)和[穿透](../../build/brotato-traits-pierce.png)。结果与日志见 `build/brotato-traits-gpu-results.json`。

第九阶段第二批历史验证（2026-10-05）：19 个相关 CTest 目标全部通过，最终耗时 19.68 秒。新增 `brotato_arsenal_test` 的 7 组检查覆盖六格购买与扣款、重复操作、等级合成／出售／焦点恢复、外部 archetype 与拥有者代际、同一步多武器发射及霰弹整组容量、各等级实际伤害快照和冷却、同类型近战各自去重、混合商品／等级分布／锁定／源模式，以及普通数值的连续两波。

自然移动路线仅使用实际奖励和赚取的材料，在首波购买自然刷出的第二把武器，并完成第二波：HP 26/160、49 击杀、材料 27、两把武器、75 次攻击。未修改自然流程的钱包、库存、生命或刷怪数值；结果只验证该路线和选择。5 个新 GPU 夹具会设置钱包／库存，以检查购买六实体、合成释放格位、出售与六武器同时攻击，不计作自然生存成绩。

第二批 20 个真实 OpenGL 场景全部通过，均为 `GL errors=0`、`batchDraws=1`：5 个新装备场景、源模式六武器与完整波次、普通 SMG 生存、主页／菜单往返、旧商店／物品夹具、精英／爆炸及随机升级。源规则满波仍为 22 次攻击、17 击杀、材料 2、HP 20、XP 32；静止 SMG 自动选择到达首波商店，HP 127/150、等级 5、XP 2。已目视检查[混合商店](../../build/brotato-arsenal-weapon-shop.png)、[六格装备](../../build/brotato-arsenal-weapon-buy.png)、[合成升级](../../build/brotato-arsenal-weapon-merge.png)、[出售](../../build/brotato-arsenal-weapon-sell.png)、[同时攻击](../../build/brotato-arsenal-arsenal.png)。渲染继续复用现有图标和精灵批次，没有新增资产或渲染后端。

第九阶段第一批历史验证（2026-10-05）：18 个相关 CTest 目标全部通过，最终耗时 21.46 秒，包含共享 ECS 的 IWanna 回归。新增 `brotato_build_shop_test` 的 9 组检查覆盖奖励唯一性／确定性／连续选择、跨 archetype 物品与失效拥有者、属性重算不重复加成、实际护甲／拾取／攻击／回血、购买扣款与重复点击、堆叠和容量限制、逐卡锁定与刷新失败不消费随机流、跨波次／重开／复活／源模式、独立 BuildSystem，以及普通数值移动完成两波。音频检查增至 8 组，确认商店清理战斗尾音且音乐继续。

本轮 21 次真实 OpenGL 检查全部通过，均为 `GL errors=0`、`batchDraws=1`：6 个商店／构筑／奖励场景、源模式六武器和完整波次、2 个普通成长场景、主页与菜单往返、六敌人／精英／范围爆炸及旧成长流程。已目视核对[四格商店](../../build/brotato-build-shop.png)、[六物品属性](../../build/brotato-build-build.png)、[随机升级](../../build/brotato-build-upgrade.png)、[第二波保留物品与锁定](../../build/brotato-build-shop-cycle.png)。六物品固定场景通过真实购买入口得到 6 个物品堆叠实体，HP 110/160；其钱包和库存为专门设置的流程夹具，不属于自然战斗成绩。

普通数值移动检查使用实际击杀成长、赚取的材料和商店购买：法杖两波后 HP 36/170、42 击杀、材料 23、2 个物品堆叠；霰弹两波后 HP 49/160、33 击杀、材料 24、2 个物品堆叠，均再次进入商店。静止 SMG 自动选择完成首波并进入商店，HP 127/150、等级 5、XP 2/28、材料 15；静止 Mage／MAP 5／霰弹仍在波末前死亡。结果只验证指定路线和选择，不代表全面平衡。源规则满波保持 17 击杀、材料 2、HP 20、XP 32，见[源规则回归截图](../../build/brotato-build-source-wave.png)。

第八阶段历史验证（2026-10-05）：17 个相关 CTest 目标全部通过，最终耗时 17.16 秒。新增 10 组战斗检查覆盖锁定瞄准、冲锋躲避与恢复、精英三发轨迹、弹丸来源退休与 generation 复用、阵营／高速扫掠／保护时间、外部 archetype 与同一步容量、击退抗性和打断、范围边界与唯一结算、预警生命周期、独立 EnemyAISystem 和自然六敌人混合。ECS 查询新增 Required／Optional 行访问的越界与重排检查；渲染验证 Line／Ring 有效几何、单批次和资源清理。旧成长、音频和共享 ECS 的 IWanna 回归继续通过。

29 次真实 OpenGL 检查均为 `GL errors=0`、`batchDraws=1`：8 个新机制固定场景、6 个自然生存／成长场景、源模式六武器与完整波次、主页／菜单往返，以及修正武器详情后的 5 次菜单复验。新机制画面已目视检查：[六敌人](../../build/brotato-enemy-enemies.png)、[冲锋预警](../../build/brotato-enemy-charge.png)、[精英齐射](../../build/brotato-enemy-elite.png)、[法杖爆炸](../../build/brotato-enemy-blast.png)、[武器效果详情](../../build/brotato-enemy-menu-weapon-1.png)。

自然法杖首次升级仍在 tick 846。自动成长的静止 SMG 完成首波：49 次攻击、28 击杀、HP 150、等级 5／XP 12；静止法杖在 tick 1780 死亡，Mage／MAP 5／霰弹在 tick 2148 死亡。移动集成检查采用普通数值和刷怪：法杖轮选成长，两波后 HP 28／37 击杀／29 次敌方齐射／28 次冲锋；霰弹选择伤害成长，两波后 HP 31／33 击杀／28 次齐射／16 次冲锋。它们验证确定路线和成长选择可完成两波，不代表所有武器与路线的平衡结论。源规则满波仍为 22 次攻击、17 击杀、材料 2、HP 20、XP 32。

第七阶段历史验证（2026-10-05）：16 个相关 CTest 目标全部通过，耗时 12.59 秒。新增 10 组成长检查覆盖三敌人数值、多次命中／XP、伤害快照与其他拥有者、特效容量、每次攻击去重、旧 generation／非平凡组件重排、升级冻结与防重复领取、连续升级及真实属性效果、切波／重开／死亡优先级、接触保护和自然刷怪，以及无需 GameModule 的独立 Health／Growth 系统。音频桥新增升级暂停／恢复检查，7 组离线音频检查通过。

22 次真实 OpenGL 场景通过，均为 `GL errors=0`：4 个固定成长场景、4 个正常生存场景、新旧规则各 6 武器以及源规则完整波次和菜单往返。新玩法自然触发首次升级于 tick 846（约 7.05 秒）；自动成长的 Mage／MAP 5／霰弹完成 2400 ticks，攻击 23、命中 25、击杀 10、HP 5、等级 2、XP 10；激光完成首波为等级 4、击杀 26。静止法杖场景在 tick 2308 死亡，属于真实战斗结果。源规则满波仍为攻击 22、击杀 17、材料 2、HP 20、XP 32，与第六阶段一致。已目视检查[三敌人](../../build/brotato-growth-enemies.png)、[自然升级面板](../../build/brotato-growth-natural-upgrade.png)、[重甲命中](../../build/brotato-growth-armored-hit.png)、[三次成长后](../../build/brotato-growth-growth-cycle.png)和[首波完成](../../build/brotato-growth-wave-burst.png)。

`--smoke-gameplay` 的固定场景使用默认法杖、关闭自动攻击／随机刷怪，并缩短尸体等待以直接验证流程；新敌人的专用弹速／冲锋速度继续生效，`charge-dodge` 提供横向移动输入。`upgrade/armored-hit/growth-cycle/blast/knockback` 按默认法杖验证，其他武器不适用这些固定命中断言。商店场景缩短波次；`shop/shop-buy/shop-lock/shop-cycle/build` 保留第一批物品商店夹具，明确关闭多武器模式。`build` 另设置钱包和商品以购买全部六种物品；`weapon-shop/weapon-buy/weapon-merge/weapon-sell/arsenal` 设置商品与钱包，分别验证混合库存、六实体购买、合成、出售和同时攻击。`growth-cycle` 明确关闭新构筑，保留第七阶段固定三奖励的兼容断言，`upgrade` 使用当前随机池。它们不属于正常生存成绩。`--auto-upgrade` 按等级轮流选取当前面板的第 1／2／3 个选项，仅允许战斗冒烟；固定菜单、表现和玩法场景不接受它。无自动选择的正常冒烟到升级面板时冻结模拟，进入商店后不自动购买。

第六阶段历史验证（2026-10-04）：15 个相关 CTest 目标均通过，包含 Brotato、ECS、渲染、音频和共享 ECS 的两个 IWanna 目标。新增 5 组几何回归覆盖六根／pivot／反射、三种远程武器四个朝向的发射点与霰弹轨迹、枪口父变换、近战归位和激光最远命中边界。方向和速度分别校验，避免角度浮点误差经弹速放大后误报。

15 个真实 OpenGL 场景通过，均为 `GL errors=0`：六武器各 24 ticks、法杖初始姿态、SMG／霰弹开火首帧、完整六段激光、三轮菜单往返、2400 ticks 满波及 `move/death/pause`。已目视检查 [SMG 开火](../../build/brotato-geometry-smg-fire.png)、[霰弹开火](../../build/brotato-geometry-burst-fire.png)、[六段激光](../../build/brotato-geometry-laser-full.png) 和 [波次完成](../../build/brotato-geometry-wave.png)。满波为攻击 22、击杀 17、材料 2、HP 20、XP 32，最后一个敌人在 tick 2334 被击杀，清场前还余约 .958 秒死亡延迟，未结算它的 2 XP。几何校准改变命中时间，因此该经验值不再沿用旧 XP 34 基线；死亡延迟规则未改。冒烟日志现在直接输出 HP／等级／XP，截图与日志在 `build/brotato-geometry-*`。

ECS 架构修正历史验证（2026-10-04）：14 个相关 CTest 目标全部通过，耗时 15.33 秒，包含旧 Brotato／渲染／音频回归及两个共享 ECS 的 IWanna 目标。新增 6 组玩法架构测试：外部 archetype 的移动／动画／提取／清场、外部弹丸与拾取生命周期、容量与武器查询、无 GameModule 的独立 System、外部特效、缺少动画／速度组件的敌人。新增 9 组 ECS 测试覆盖查询行、generation、跨场景刷新、可选组件访问、命令可见边界、失效命令与异常清理。

11 次真实 OpenGL 场景通过，均为 `GL errors=0`：六武器各 24 ticks、菜单三轮 `cycle`、2400 ticks 满波及 `move/death/pause`。完整波次仍为攻击 22、击杀 17、材料 2、HP 20、XP 34，清场后仅剩玩家与武器，与第五阶段基线一致。死亡粒子／飘字和满波画面已目视检查，截图位于 `build/brotato-ecs-*.png`。这验证当前行为，不是性能提升或自动并行的证明。

第五阶段历史验证（2026-10-03）：当时 10 个 CTest 目标全部通过，耗时 10.98 秒；会话测试增至 18 组，覆盖六种起始武器互斥、武器确认门槛、返回重选、唯一难度无倍率、前后导航、菜单无模拟、启动失败事务性和音频新局边界。旧战斗、表现、音频和通用渲染回归继续通过。

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
| [Public/game_module.h](Public/game_module.h)、[Private/game_module.cpp](Private/game_module.cpp) | 单局生命周期、System 装配、固定步和对外接口 |
| [Public/game_world.h](Public/game_world.h)、[Private/game_world.cpp](Private/game_world.cpp) | 注入系统的配置／运行数据、生成工厂、容量及查询清场；没有对象分类列表和更新方法 |
| [Systems/simulation_systems.cpp](Systems/simulation_systems.cpp) | 波次、移动、弹丸、接触和拾取系统直接处理组件数组 |
| [Private/render_extract.cpp](Private/render_extract.cpp) | 查询显示组件、提取值快照并排序，不借用跨帧组件指针 |
| [Public/game_components.h](Public/game_components.h) | 玩家、武器、敌人、弹丸与材料组件及输入／配置 |
| [Private/combat.cpp](Private/combat.cpp)、[Public/expanded_gameplay.h](Public/expanded_gameplay.h) | 统一扣血、Health／Growth 系统、升级选择和新规则平衡配置 |
| [Private/enemy_ai.cpp](Private/enemy_ai.cpp)、[Public/enemy_behavior.h](Public/enemy_behavior.h) | 查询 EnemyBrain，处理瞄准／冲锋／恢复，独立敌方弹丸请求和静态行为配置 |
| [Public/enemy_art_catalog.h](Public/enemy_art_catalog.h) | 五个新常规敌人及 Boss 原图的尺寸／pivot；由只读导入器生成 |
| [Private/campaign.cpp](Private/campaign.cpp)、[Test/campaign_test.cpp](Test/campaign_test.cpp) | 独立 BossAI／Run 系统、六波目标、结果值与战役回归 |
| [Public/run_definitions.h](Public/run_definitions.h)、[Private/run_profiles.cpp](Private/run_profiles.cpp)、[Test/profiles_test.cpp](Test/profiles_test.cpp) | 角色／难度类型化规则、ECS 身份读取、值缩放与 5×3 完整局检查 |
| [Public/animation_catalog.h](Public/animation_catalog.h)、[Private/presentation.cpp](Private/presentation.cpp) | 导入生成的分层姿态／曲线，固定步采样、命中粒子和飘字 |
| [Public/game_events.h](Public/game_events.h)、[Private/game_audio.cpp](Private/game_audio.cpp) | 有界玩法值事件，事件驱动的音频播放与局生命周期 |
| [Public/weapon_definitions.h](Public/weapon_definitions.h)、[Private/weapons.cpp](Private/weapons.cpp) | 六武器类型化数据和攻击状态机 |
| [Public/weapon_geometry_catalog.h](Public/weapon_geometry_catalog.h)、[Public/weapon_geometry.h](Public/weapon_geometry.h) | 导入生成的六武器根、子精灵／发射／枪口几何、六束光和四条弹道；世界变换辅助 |
| [Public/combat_geometry.h](Public/combat_geometry.h) | 相对运动圆形／有方向胶囊扫掠 |
| [Public/content_catalog.h](Public/content_catalog.h)、[Public/map_layout_data.h](Public/map_layout_data.h) | 导入生成的角色／地图目录和 250 个原装饰；不手工编辑 |
| [Public/selection_catalog.h](Public/selection_catalog.h) | 六个实际武器按钮与唯一难度的图标目录、源尺寸；不手工编辑 |
| [Public/map_presentation.h](Public/map_presentation.h) | 同一地图数据绘制战场和选择预览，地板采用平铺九宫格 |
| [Public/menu_layout.h](Public/menu_layout.h) | 菜单布局、视口与鼠标坐标换算 |
| [Systems/game_systems.h](Systems/game_systems.h) | `Wave → Build → Health → Status → EnemyAI → BossAI → Movement → Weapon → Projectile → Contact → Pickup → Presentation → Growth → Run` 管线 |
| [Public/build_definitions.h](Public/build_definitions.h)、[Private/builds.cpp](Private/builds.cpp) | 六奖励／十物品数据、BuildSystem、商店事务与构筑值快照 |
| [Public/item_art_catalog.h](Public/item_art_catalog.h) | 十张原物品 UI 图标的生成尺寸目录；不手工编辑 |
| [Public/trait_definitions.h](Public/trait_definitions.h)、[Private/traits.cpp](Private/traits.cpp)、[Test/traits_test.cpp](Test/traits_test.cpp) | 类别／词条数据、独立状态系统、完整攻击快照及机制回归 |
| [Private/arsenal.cpp](Private/arsenal.cpp)、[Test/arsenal_test.cpp](Test/arsenal_test.cpp) | 装备查询／值快照、四级数值、购买／合成／出售事务及多武器回归 |
| [../Render/Public/Sprite/sprite_batch.h](../Render/Public/Sprite/sprite_batch.h)、[../Render/README_SPRITE.md](../Render/README_SPRITE.md) | 通用精灵批次、旋转／翻转、UV 子区域、九宫格平铺与 Line／Ring 预警几何 |
| [../Render/Public/Sprite/sprite_animation.h](../Render/Public/Sprite/sprite_animation.h) | 通用 Hermite 标量曲线／二维姿态采样；仿射精灵批次保留父级缩放造成的斜切 |
| [../Audio/Public/audio_module.h](../Audio/Public/audio_module.h) | 复用 PCM 解码与混音，新增按声音暂停／继续，保留播放游标 |
| `main.cpp` | 窗口、输入、模块组合、菜单／HUD 和真实图形冒烟 |

会话不在菜单中创建战斗 Scene，也不积累菜单时间。`GameModule` 装配 `Pipeline` 并注入纯数据 `GameWorld`，系统不再回调模块。实体是否参加某个系统由 `ChunkQuery` 的组件组合决定；新建的其他 archetype 只要满足组合，也会参与模拟和提取。常规遍历直接读写组件数组，实体关系仍允许代际检查后的随机访问。地图装饰是无行为的渲染数据，原场景也没有给它们碰撞组件。

每个固定步结束时，通用 `CommandBuffer` 先提交实体删除和普通创建，再提交效果创建；所有组件借用在回放前结束。波次结束丢弃未提交生成并查询清场。容量统计同样来自查询，武器与效果生成包含未提交预留量。引擎接口、回放异常约定和当前串行限制见 [ECS 使用说明](../../Engine/include/engine/ECS/README.md)。

构筑的权威数据为 `Growth::upgrades` 和查询得到的 `OwnedItem` 堆叠；`CombatStats` 是派生属性，`BuildBase` 保存基础最大生命及小数回血余量。BuildSystem 逐玩家重算，不将加成反复写回基础值。物品拥有者必须通过完整 generation 检查，复用 ID 的新玩家不会继承旧物品；不同 archetype 的物品和不同拥有者独立生效。购买先结束所有组件借用，再在固定步之间创建或增加堆叠，成功后才扣款并标记已售。商店存量、锁定、刷新次数和随机流都位于玩家的 `Shop` 组件；窗口只发送操作并读取值快照，没有背包实体列表。

`Weapon::equipmentSlot/tier` 是装备格与等级的权威，装备快照按组件查询重建；`GameWorld::weapon/selected` 只表示当前查看的武器关系，WeaponSystem 始终查询并更新全部武器。合成与出售使用完整句柄和查询结束后的值复制，在固定步之间删除，随后重新取得保留实体／玩家组件；删除重排不会使旧地址继续被使用。购买容量跨 archetype，其他拥有者不参与主玩家的合成与装备显示。每把武器将等级伤害在攻击开始时存入 Damage，已生成的弹丸继续使用其出生快照。

`ActorAnimation` 保存本地显示姿态，层级合成采用完整二维仿射变换。命中粒子和飘字是独立 Effect archetype，默认上限 512；每次命中最多生成 6 个粒子与一个数字，额外请求达到上限后拒绝。效果参数使用独立确定性散列，不消费刷怪／掉落随机流。外部音频只消费捕获位置等值数据，待取事件上限 256；音频桥最多保留 32 个玩法短声音，均有清理和溢出诊断。

`Game()`、`Get`、`Scene`、实体句柄和生成接口仅在主线程、模拟步之间使用；组件引用不能跨结构变化保存，旧局的任何指针／句柄不能带入下一局。当前实体句柄不携带全局场景身份。`Config::weapons` 是 C++ 类型化表；本阶段未引入运行时玩法 JSON 加载器。

生命的唯一权威是 `Health`，旧 `Player::health` 已移除。外部敌人 archetype 若要参加碰撞与武器寻敌，需包含 `Enemy/Transform/Circle/Health`；弹丸需包含 `Projectile/Transform/Velocity/Sprite/Circle/Damage`。外部武器需有 `Weapon/Transform/Sprite/Damage`，自己的 owner 决定伤害和攻速；没有 CombatStats 的 owner 使用基础数值。Health、CombatStats、Growth 和 Damage 属于实体组件，不在窗口或 GameModule 中复制一份。`Weapon::hitTargets` 仅是当前攻击的目标关系，保留完整代际句柄并清理失效关系，不承担世界成员管理；其 `std::vector` 在 ECS 删除重排时的构造、交换和析构已通过实际武器回归。当前运行仍为单个可操作玩家。

常规 AI 查询只要求 `Enemy/Transform/EnemyBrain`，遇到 BossBrain 则交由独立 BossAISystem。BossAI 查询 `BossBrain/Enemy/Health/Transform`，不要求旧 EnemyBrain，外部 archetype 也可直接参加。移动通过 `ChunkView::TryGet<T>(row)` 访问可选 EnemyBrain／BossBrain／Knockback；没有这些附加组件的外部敌人继续采用基础移动。敌方弹丸使用 `HostileProjectile/Transform/Velocity/Sprite/Circle/Damage` 独立查询，容量和清场也接纳其他 archetype。预警和爆炸提取复制位置、方向与进度，不保存组件借用跨帧。

RunProgress 是玩家组件，保存本波目标和完成标记；RunSystem 查询本波 objective Boss 的真实死亡状态后判定胜负，删除活 Boss 或复用其编号不算击杀。Boss 创建成功后才标记已出生；容量不足时下一步重试，Boss 请求先于常规敌人。结果是 ECS 内的 RunResult 值，终态捕获一次，窗口只读取快照。终态不回放未提交生成并查询退休战斗实体，装备／物品保留供结果显示，重开重新初始化进度。该批复用已有查询和命令缓冲，没有添加游戏专用引擎 API。

角色／难度的开局选择复制到玩家 CharacterProfile／RunRules 组件，BuildSystem 查询每个拥有者的角色，CombatStats 保存派生武器伤害和元素燃烧奖金；不能从全局所选角色给外部玩家追加属性。难度系统辅助函数读取主玩家本局规则，生成工厂和波次／移动／敌人攻击系统消费它，配置中的生命／伤害仍保留原始基础值，重开不再次缩放。结果捕获实际组件身份；UI 仅展示快照。新 archetype 或无窗口 Pipeline 无需应用回调即可参与角色构筑，本批没有添加游戏专用 ECS API。

## 原始美术与可重现导入

现有 88 张 PNG：前两阶段 16 张、第三阶段 44 张、第四阶段两腿／基础身体与阴影／高亮／命中粒子 5 张，第五阶段再导入 6 张武器选择图标和 `diff_0` 难度图标，第七阶段两张敌人原图、第八阶段三张敌人原图、第九阶段十张物品 UI 图标，第十阶段一张 Boss 原图。基础身体和阴影共用图片。原场景将 `tiles_4.png`／`tiles_5.png` 整图当作单 Sprite 的 5 处引用按原样保留，不自行猜测替代子图。前三阶段资产见 [manifest.json](../../Asset/Brotato/manifest.json)，地图绑定见 [scene_manifest.json](../../Asset/Brotato/scene_manifest.json)，动画／分层和音频来源分别见 [presentation_manifest.json](../../Asset/Brotato/presentation_manifest.json)、[audio_manifest.json](../../Asset/Brotato/audio_manifest.json)，选择事件和图标见 [selection_manifest.json](../../Asset/Brotato/selection_manifest.json)，物品来源见 [build_content_manifest.json](../../Asset/Brotato/build_content_manifest.json)。

```powershell
$brotatoSource = 'C:\Users\16620\Downloads\Unity2021_土豆兄弟_Botato_爱给网_aigei_com\Unity2021_土豆兄弟_Botato\Unity2021_Botato\Unity2021_Botato'
python tools/import_brotato_assets.py --source $brotatoSource
python tools/import_brotato_presentation.py --source $brotatoSource
python tools/import_brotato_audio.py --source $brotatoSource
python tools/import_brotato_selection.py --source $brotatoSource
python tools/import_brotato_weapon_geometry.py --source $brotatoSource
python tools/import_brotato_enemy_art.py --source $brotatoSource
python tools/import_brotato_build_art.py --source $brotatoSource

# 同时将图片、清单和生成头文件写到构建目录，便于比较。
python tools/import_brotato_assets.py --source $brotatoSource --output .\build\brotato_assets --catalog-output .\build\brotato_catalog
```

图片导入依赖 Python 与 Pillow；音频导入另需本地 `ffmpeg`，可用 `--ffmpeg <路径>` 指定。7 段源 WAV 按字节复制，1 段 Ogg 背景音乐解码为 PCM16 WAV，运行时不需要新增压缩音频解码器。`tools/brotato_scene.py` 只读解析 Unity 场景真实绑定、父级变换、Sprite pivot、PPU、颜色和排序；图片导入器按 `.asset` 或 TextureImporter 引用裁剪，保留 RGBA，不重绘和缩放。展示导入器从实际脚本 `_anim`、Controller state 和 Clip 引用生成 `animation_catalog.h`，保留每个关键点的左右切线；没有把 Unity 整个项目复制进引擎。

第三阶段两次导入的 60 PNG、2 个 manifest、2 个头文件共 64 文件逐字节一致；第四阶段展示导入的 5 PNG、1 manifest、1 header 也逐字节一致，旧 60 PNG 哈希未变。第五阶段选择导入的 7 PNG、manifest、header 共 9 文件重复生成逐字节一致，之前 65 PNG 未变，71 个源文件哈希已核对；选择导入器拒绝把图片或目录输出到源工程内。清单保存源路径、GUID、Sprite 矩形、像素单位、图集哈希与输出哈希，可追溯到只读原工程。

第六阶段几何导入不写图片，仅输出 [weapon_geometry_manifest.json](../../Asset/Brotato/weapon_geometry_manifest.json) 和生成头文件；两文件重复生成逐字节一致，72 张旧 PNG 与 40 个引用源输入哈希未变。清单记录脚本绑定、源 fileID／行、旋转根、射击点、Sprite pivot、PPU 和 prefab／动画路径。导入器拒绝资源目录或头文件目录落入 Unity 源工程，遇到当前不能表达的根缩放／投影或弹道曲线会报错。

第八阶段敌人导入扩展为五图、清单、头文件共 7 文件，重复生成逐字节一致，旧 74 PNG 与 12 个唯一源输入哈希未变，两种源目录输出保护检查正确拒绝。当时共 77 PNG，敌方弹丸复用原弹丸图片并着橙色。来源见 [enemy_content_manifest.json](../../Asset/Brotato/enemy_content_manifest.json)。

第九阶段物品导入读取 `lens_icon/coffee_icon/beanie_icon/leather_vest_icon/plant_icon/cake_icon`，输出六 PNG、来源清单和头文件共 8 文件，重复生成逐字节一致。旧 77 PNG、14 个唯一源输入哈希未变；图片输出及头文件输出落入源目录的两种检查均正确拒绝。使用完整 UI 图标，不将穿戴附属图片误作商店图。清单标明原图来源及新增设计的规则边界，图片总数 83。

## 迁移边界

`--source-rules` 的主要战斗数值保留：玩家速度 5、敌人速度 2.3、生命 150、接触进入伤害 10、出生／死亡延迟 1.5 秒、首波 20 秒、后续每波 +7 秒。模拟使用 120 Hz 固定步、整数 XP、单个刷怪计时器及实体上限；有效武器命中直接使基础敌人死亡。法杖／SMG 弹丸有新增的 4 秒上限，霰弹保留原动画轨迹和 0.15 秒寿命。

胶囊近似原火把／匕首／光束 BoxCollider，未复刻 Box2D 推挤和完整接触解算；激光修正原第六段未激活的问题。命中粒子采用原数量、寿命、颜色和大小曲线，方向是源固定 +X 窄锥的二维投影，未引入通用 3D 粒子系统。原角色属性、难度倍率、六槽装配和完整商店规则在已审查源码中没有对应实现。第七至十阶段的敌人、成长、攻击效果、混合商店、武器四等级／合成、六槽装配、Boss、完整局结算、角色属性及难度倍率均为新设计；当前仍未覆盖商业版本的完整武器词条、套装效果与经济规则。完整菜单版式、确认框、玩家烟尘、音量设置页和剩余有行为证据的内容仍待迁移。具体源依据和差异见 [MIGRATION.md](MIGRATION.md)。

第七阶段敌人导入的两图、清单、头文件共 4 文件重复生成逐字节一致，旧 72 PNG 和 6 个唯一源输入哈希未变，图片／目录两种源工程输出保护检查均正确拒绝。来源见 [enemy_content_manifest.json](../../Asset/Brotato/enemy_content_manifest.json)。

第九阶段第三批物品导入新增 `bandana_icon/bat_icon/sunglasses_icon/scared_sausage_icon`，现有十物品 PNG／清单／目录共 12 文件重复生成逐字节一致，22 个唯一源输入哈希与原有 83 PNG 未变；源目录图片输出、头文件输出两种保护均拒绝写入。该批总计 87 PNG。

第十阶段只读敌人导入新增 `40020.asset` 的 Boss 图，六敌人 PNG／来源清单／尺寸目录共 8 输出再次生成逐字节一致。旧 87 PNG 与 14 个唯一源输入哈希未变，源目录图片输出及目录头文件输出两种保护均拒绝写入；当时总计 88 PNG。

第十一阶段读取 `40007/40008.asset` 为治疗者／召唤者的原图，八敌人 PNG／清单／目录共 10 输出重复生成逐字节一致，18 个唯一源输入及本批之前 88 PNG 未变。两个源目录输出保护检查均拒绝写入。当前总计 90 PNG，运行资产已复制至 `bin/Brotato` 并逐一核对相等。
