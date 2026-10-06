# ECS 查询与结构变更

`Scene` 拥有实体和 archetype/chunk 存储。系统通过 `ChunkQuery` 按组件组合查询，常规更新直接访问 chunk 内的组件数组；玩家归属、攻击目标等实体关系使用完整 `EntityHandle` 做存活检查后再随机访问。

```cpp
using Moving = ECS::Core::ChunkQuery<
    ECS::Core::Require<Position, Velocity>,
    ECS::Core::Optional<>, ECS::Core::Exclude<>>;
Moving moving;
moving.Refresh(scene);
ECS::Core::CommandBuffer commands(scene);
for (auto chunk : moving) {
    auto* positions = chunk.Get<Position>();
    const auto* velocities = chunk.Get<Velocity>();
    for (std::size_t row = 0; row < chunk.count; ++row) {
        positions[row].value += velocities[row].value * dt;
        if (ShouldRetire(positions[row])) commands.Destroy(chunk.Entity(row, scene));
    }
}
commands.Playback(); // 所有借用行及组件引用已结束使用。
```

`ChunkQuery::Count()` 返回匹配 archetype 当前的实体总数，不是 chunk 数量。`RefreshIfNeeded(scene)` 在场景改变或 archetype 版本变化时重建匹配集。Optional 缺失是正常匹配结果，对应指针为空。`ChunkView::Entity(row, scene)` 校验行边界和 archetype 所属关系，返回当前完整代际句柄；不要从裸 ID 自行构造用于删除的句柄。

`Scene::TryGetComponent<T>(handle)` 校验完整 generation 后返回借用组件指针，实体失效或缺少该组件时返回 `nullptr`，不输出错误日志。适合目标／拥有者关系与可选表现组件；批量模拟仍应使用查询数组，避免逐实体随机访问。

`ChunkView::TryGet<T>(row)` 访问查询已声明的 Required／Optional 组件行，缺少可选组件或 `row >= count` 时返回 `nullptr`。它不支持 AnyOf 的 variant 访问，不查询 Scene，也不验证视图在结构变更之后仍有效；返回值仍遵守同一借用期限。Brotato 的 MovementSystem 用它读取可选 CombatStats／EnemyBrain 和更新 Knockback，让基础与扩展 archetype 共用移动查询。常规连续数据循环仍可一次 `Get<T>()` 后遍历数组。

```cpp
if (auto* impulse = chunk.TryGet<Knockback>(row)) {
    position += impulse->velocity * dt;
}
```

`CommandBuffer` 绑定一个必须比它活得更久的 `Scene`，仅在主线程记录和回放。`Destroy(handle)` 保存完整句柄；`Create(archetype, initializer)` 的初始化回调接收 `Scene&` 和新句柄。回调应捕获初始化值或稳定资源，不能捕获查询中的组件地址。回放总是先销毁、后按记录顺序创建，重复／过期删除不会销毁复用 ID 后的新实体，无效 archetype 创建会跳过。

初始化回调抛异常时，当前新实体回滚，剩余命令及下一批命令清空，原异常向上传递。之前成功执行的命令保留，因此回放不是整批事务。回调内允许记录下一批命令，但不允许递归 `Playback()`；`Clear()` 清空待执行批次。缓冲本身不提供游戏容量限制，Brotato 在生成请求层预留弹丸和效果容量。

查询视图、组件指针和引用不能跨越任何结构变更；在 `CreateEntity`、`DeleteEntity` 或命令回放后重新取得。`Scene::GetEntityHandle(id)` 只用于将当前场景已知存活 ID 转为句柄，不能把旧 ID 当成旧实体的身份。现有 `EntityHandle` 尚不包含全局场景身份，禁止跨 Scene 使用。

`System::Reads/Writes` 是访问声明。当前 `Pipeline` 按依赖顺序串行执行，未实现基于这些声明的自动并行，也不会给查询访问自动加锁。游戏资源、随机流和命令缓冲仍需在引入并行前单独声明和隔离。

验证入口：`ecs_world_operations_test` 覆盖跨 chunk 查询、Required／Optional 行与越界访问、删除重排、代际句柄、跨 Scene 查询刷新与行归属、延迟可见边界、失败清理和递归回放；`brotato_ecs_architecture_test` 验证实际玩法自动接纳不同 archetype 的实体。`brotato_enemy_combat_test` 进一步验证独立 AI、外部敌方弹丸 archetype、同一步预留容量和弹丸来源退休后的 generation 复用。

组件可以包含需要构造／析构的成员；现有 FixedChunkArray 使用类型化构造、交换和销毁，不按裸字节复制组件。`brotato_growth_test` 用包含 `std::vector<EntityHandle>` 的真实 Weapon 组件验证删除重排和继续攻击，并检查旧目标 generation 被回收后新实体仍可命中。该 vector 只保存一次攻击的目标关系；世界成员仍由 Scene／查询管理。该测试同时运行独立 Health／Growth 系统，证明新成长逻辑不依赖应用模块回调。

Brotato 构筑使用 `Query<OwnedItem>` 跨 archetype 汇总物品，先确认完整拥有者句柄存活，再按 owner 分组。BuildSystem 从升级记录和物品数量派生 CombatStats，不保留物品实体列表，也不重复累计加成。商店购买在固定步之间处理：复制商品值并结束组件借用，创建或增加物品堆叠后重新取得玩家／商店组件，再扣款。源记录和派生属性的区分、结构变更前结束借用、变更后重新访问都遵守现有接口约定，本批没有新增引擎 API。`brotato_build_shop_test` 验证外部 archetype、其他拥有者、旧 generation、删除重排、容量／事务、随机隔离及独立 BuildSystem。

六槽装备同样由武器实体上的 `equipmentSlot/tier` 表示；查询产生临时装备快照，当前查看武器的句柄不决定 WeaponSystem 的成员范围。合成／出售先结束查询并复制需要的值，删除后再用完整句柄访问保留实体或修复 UI 焦点，不跨删除重排保存组件地址。`brotato_arsenal_test` 覆盖其他 archetype 的合成、其他拥有者和旧 generation 排除、删除焦点后的恢复，以及同一步多个武器的弹丸容量预留；不同近战实体各自持有本次攻击关系，不会共享命中去重状态。

Brotato 的燃烧／减速使用独立 TimedStatus 实体保存目标关系；StatusSystem 跨 archetype 查询，并用 Scene 验证完整目标句柄，目标编号复用不会转移状态。玩家装备／物品查询派生类别和属性，Projectile 保存本次攻击的完整快照和命中关系 vector。状态创建回调保存值，查询刷新并合并同一步重复后结束借用，再删除未使用的新实体；默认容量在游戏请求层预留并在回放时复查。`brotato_traits_test` 覆盖独立系统、外部状态 archetype、旧 generation、来源退休、冻结／清场、非平凡弹丸组件重排与随机隔离。本批复用已有 ECS 接口，不增加游戏专用引擎 API。

Brotato 的 Boss 使用 BossBrain 组件和独立 BossAISystem，查询要求 BossBrain／Enemy／Health／Transform；常规 EnemyAI 排除这类行为，Movement 使用可选 BossBrain 决定移动。玩家 RunProgress 保存目标和捕获一次的 RunResult，RunSystem 根据查询到的本波 objective Boss 的真实死亡状态判胜。活 Boss 被删除或编号被复用不满足击杀目标，生成失败不提前标记目标出生。请求层继续用通用 CommandBuffer 预留整组敌弹和 Boss 容量，终局查询清场覆盖不同 archetype。

`brotato_campaign_test` 在没有 GameModule 的 Context／Pipeline 中运行 BossAI 与 Run，外部 Boss archetype 没有旧 EnemyBrain 也能攻击和结算；另检查容量重试、目标代际、冻结、状态相互作用、结果值生命周期与普通数值六波。该批再次验证现有查询／命令接口，没有引入 Boss 或战役专用引擎 API。系统依赖仍串行，不能据此宣称已有自动并行或完成通用性能优化。

角色规则由拥有者的 CharacterProfile 组件确定，BuildSystem 跨 archetype 派生 CombatStats 的武器伤害／元素燃烧及一般属性；缺失组件按通用基准处理，不从主玩家选择给外部拥有者套用职业。RunRules 保存本局难度，创建和攻击请求按其值缩放一次，移动在每步读取一次速度倍率，预警提取使用同一规则。基础 Config 不被倍率覆写，重开没有二次缩放；RunResult 捕获实际组件身份，UI 读取值快照。

`brotato_profiles_test` 在独立 Context／Pipeline 中运行实际 BuildSystem，检查不同拥有者、可选角色组件缺失、退休 generation 复用、派生值清除和重复计算。实际攻击、状态、成长购买与菜单事务另有检查，五角色×三难度完整六波使用正常数据与真实购物。该批复用现有查询和安全随机访问，没有为了角色或难度添加引擎专用 API。


## Brotato 的分波刷新与辅助怪

第十一阶段把新刷怪安排放在 `SpawnDirector` 组件，由第十五个 `EncounterSystem` 查询驱动。计时、波内压力、事件、成功／跳过编队及随机数都属于实体状态；`GameWorld` 的旧计时器只服务 source／历史规则。系统读取场上 `Enemy` 和同一步预留，整组请求走通用 `CommandBuffer`，容量不足消耗本次机会，不保存补刷队列。暂停、成长选择和商店冻结同一时间线。创建回调只捕获值数据，不带出借用行指针。

敌人组件保存出生波次、伤害／速度成长和强化身份。治疗系统通过 `Enemy + Health + Transform` 查询同伴，召唤子怪通过完整来源句柄关联、限制未死亡数量；子怪没有经验／材料奖励。表现层读取快照和 Ring／Text，没有创建窗口侧 AI 或系统实体列表。`brotato_encounters_test` 在没有 `GameModule` 的 Context／Pipeline 中运行导演，并检查外部 archetype、命令提交边界、共享容量、随机流与来源 generation。该批复用已有通用 ECS 接口。
