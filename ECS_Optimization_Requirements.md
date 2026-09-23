# OpenglLearn ECS 框架优化需求

## 1. 项目范围

仓库：

`https://github.com/chickenyours/OpenglLearn`

目标：在保持现有 ECS、Terrain Demo、Render Pipeline 行为正确的前提下，逐步降低 ECS Runtime 开销，提高 JobSystem 工作线程利用率，并最终形成清晰、可维护的依赖驱动并行执行模型。

本需求要求 **渐进式改造**，禁止一次性重写整个 ECS。

---

## 2. 总体原则

Agent 必须遵守：

- 先阅读当前 HEAD 代码，不得仅依据本文件假设实现细节。
- 每次只完成 **一个 Task**。
- 每个 Task 必须能够独立编译、测试、审查和提交。
- 不做与当前 Task 无关的重构。
- 不进行全仓库格式化、无关重命名或目录整理。
- 优先保证 correctness，再讨论性能。
- 涉及多线程时，必须说明数据所有权、同步点和生命周期。
- 不允许通过增加新的全局 mutex 来掩盖线程安全问题。
- 不新增第四套 ECS 调度/同步机制，优先整合现有：
  - `System Reads/Writes`
  - `JobSystem`
  - `ChunkSchedule`
- Structural change（Entity/Archetype 创建销毁）必须保持在安全线程/安全阶段。
- OpenGL / RHI 操作必须继续遵守现有线程约束。
- 每完成一个 Task 后停止，不自动进入下一个 Task。

---

## 3. 每个 Task 的标准工作流程

开始修改前，Agent 先输出：

```text
Current behavior:
Potential bottleneck:
Files involved:
Concurrency/lifetime risks:
Proposed change:
Acceptance tests:
```

然后：

1. 阅读相关源码和调用链。
2. 确认问题是否真实存在。
3. 做最小范围修改。
4. 编译相关 target。
5. 运行相关测试。
6. 必要时补最小测试。
7. 总结修改和风险。
8. 停止，等待 review。

Task 完成后必须输出：

```text
Files changed:
Behavior change:
Threading model:
Tests:
Performance impact:
Remaining issues:
```

---

# 4. Task 清单

## Task 0.1 — 建立 ECS / Terrain 性能基线

### 目标

在优化前建立可重复比较的性能数据。

### 需要统计

至少包括：

- `Pipeline::Tick()` 总耗时
- `StreamingSystem` 耗时
- `GenerationSystem` 耗时
- `MeshingSystem` 耗时
- `MeshUploadSystem` 耗时
- `RenderExtractSystem` 耗时
- 每帧 Job submitted 数
- worker 实际执行 Job 数
- `WaitIdle()` 等待时间
- 每帧生成 Terrain Chunk 数
- 每帧重建 Mesh 数

### 建议输出

```text
Frame:
  ECS total          xxx us
  Generation         xxx us
  Generation wait    xxx us
  Meshing            xxx us
  Meshing wait       xxx us
  Jobs submitted     xx
  Workers used       x / N
```

### 约束

- Profiling 只在 Development/Debug 模式启用，或可配置关闭。
- Release 模式不得引入明显额外运行时成本。
- 不在本 Task 修改 ECS 调度模型。

### 验收

- 能稳定采集数据。
- 给出至少 300 帧：
  - Average
  - P95
  - Max
- 记录测试场景和机器线程数。

---

## Task 1.1 — 优化 Terrain Job 粒度

### 当前需要验证的问题

检查：

- `Terrain::System::GenerationSystem`
- `Terrain::System::MeshingSystem`
- `RunJobs`
- `ChunkQuery`
- `JobSystem`
- `JobSystemSchedule`

确认当前是否采用：

```text
1 ECS storage chunk = 1 Job
```

如果确认存在，则优化。

### 目标

让 Terrain CPU 工作能够有效利用多个 worker。

优先实现：

```text
1 terrain entity / terrain chunk = 1 Job
```

或效果等价的 range parallelism。

### 要求

- Generation 和 Meshing 都能并行。
- 两个 Job 不得写同一个 Entity。
- worker 不进行 structural modification。
- Terrain 生成结果必须与修改前一致。
- 不在本 Task 重写 Pipeline scheduler。

### 验收示例

```text
before:
8 terrain entities
1 ECS storage chunk
1 worker job

after:
8 terrain entities
8 worker jobs
```

必须通过相关 ECS/Terrain 测试和 Terrain Demo。

---

## Task 1.2 — 减少 JobSystem Task 复制

### 目标

降低 `std::function` / `Task` 在提交链路中的不必要 copy。

检查：

- `ExecuteTask`
- `Task`
- `JobSystem::Submit`
- `JobSystem::DispatchAll`
- `JobSystemSchedule::Submit`

### 建议方向

支持 move：

```cpp
Task(ExecuteTask func)
    : executeFunc_(std::move(func)) {}
```

必要时增加：

```cpp
Submit(Task&&)
Submit(ExecuteTask&&)
SubmitBatch(...)
```

### 约束

- 保持现有 API 兼容，除非有充分理由。
- 不在本 Task 修改 worker 调度策略。

### 验收

- JobSystem 测试全部通过。
- Terrain Demo 正常。
- 说明减少了哪些 copy/move。

---

## Task 1.3 — Worker 数量配置化

### 当前需要验证

检查是否存在固定：

```cpp
jobSchedule_.Start(8);
```

### 目标

- 默认根据 `std::thread::hardware_concurrency()` 决定 worker 数。
- 合理保留主线程资源。
- 支持显式配置覆盖。

### 约束

策略集中管理，不允许多个模块自行决定线程数。

### 验收

- 不同硬件线程数下行为合理。
- 可配置覆盖默认值。
- JobSystem 测试通过。

---

# 5. ECS 热路径优化

## Task 2.1 — 移除 ECS 热路径 ObjectWeakPtr mutex

### 当前需要验证

检查：

```text
EntitySceneInfo
ObjectWeakPtr<ArchType>
Scene::GetActiveComponent<T>
EntityComponentHandle<T>
```

以及：

```text
ObjectPtr / ObjectWeakPtr
```

是否在 `Get()/lock()/operator->` 中使用 mutex。

### 目标

Entity → Component 高频路径不应因 Archetype 引用而进入 mutex。

优先考虑：

```cpp
struct EntitySceneInfo {
    ArchType* archtype = nullptr;
    uint32_t denseIndex = 0;
    uint32_t generation = 1;
    bool alive = false;
};
```

或稳定 Archetype ID。

### 要求

- 保留 Entity generation 校验。
- 保证 Archetype 生命周期安全。
- 不得制造悬空指针。
- Structural change 规则必须明确。
- 不要为了替换 ObjectWeakPtr 再增加新的 per-access mutex。

### 验收

- `Scene::GetActiveComponent<T>()` 正确。
- Entity delete/recycle 正确。
- Archetype destruction 正确。
- ECS 测试通过。

---

## Task 2.2 — 将 denseIndex 放入 EntitySceneInfo

### 目标

将：

```text
EntityID
 -> Scene entity info
 -> ArchType
 -> unordered_map<EntityID, denseIndex>
 -> Component
```

优化为：

```text
EntityID
 -> Scene entity info
 -> ArchType + denseIndex
 -> Component
```

### 要求

swap-remove 时必须更新被搬移 Entity 的 `denseIndex`。

评估 `ArchType::entityID2Unit_` 是否还能删除。

如果保留，必须说明仍有哪些必要用途。

### 验收

- 创建、删除、swap-remove 后 denseIndex 始终正确。
- Entity recycle generation 正确。
- Component 查询不再依赖 hash lookup（若本 Task 设计目标达成）。

---

## Task 2.3 — CreateEntity 单实体 Fast Path

### 当前需要验证

确认 `CreateEntity()` 是否通过：

```text
CreateEntities(..., 1)
```

并产生临时 vector。

### 目标

单实体创建不进行不必要动态容器分配。

### 验收

- API 行为不变。
- 测试通过。
- 单实体创建路径不构造临时 `vector<EntityID>` / `vector<EntityHandle>`。

---

# 6. Query 优化

## Task 3.1 — 缓存 Component Array Pointer

### 当前需要验证

确认 Query 是否已经缓存匹配 Archetype，但每个 ChunkView 构造仍执行：

```text
type_index
unordered_map lookup
TryCastActiveComponentArray<T>()
```

### 目标

在 Query Refresh 阶段缓存：

```cpp
MatchedArchetype {
    ArchType* archtype;
    // cached component-array pointers...
};
```

Chunk iterator 仅执行轻量 pointer/chunk index 操作。

### 要求

- `Require`
- `Optional`
- `Exclude`
- `AnyOf`

语义不得改变。

### 验收

- Query hot path 不再重复进行 `unordered_map<type_index,...>` lookup。
- Query tests 全部通过。

---

# 7. System Scheduler 改进

> 进入本阶段前，应先完成并 review 前面低风险任务。

## Task 4.1 — 使用 Reads/Writes 构建冲突关系

### 目标

让 `System::Reads<T>() / Writes<T>()` 真正参与 Pipeline Build。

冲突规则：

```text
Read A  + Read A   -> compatible
Read A  + Write A  -> conflict
Write A + Read A   -> conflict
Write A + Write A  -> conflict
无共同组件         -> compatible
```

同时保留：

```cpp
RunBefore<A, B>()
```

作为显式依赖。

### 本 Task 只做

- dependency/conflict 分析
- DAG 构建
- deterministic execution order
- cycle detection

本 Task 可以继续顺序执行 System。

### 禁止

本 Task 不得：

- 重写 JobSystem
- 删除 ChunkSchedule
- 修改 Entity storage
- 修改 Query storage
- 实现 work stealing
- 修改 Renderer
- 修改 Terrain generator

### 必测

```text
Read/Read
Read/Write
Write/Read
Write/Write
无冲突
RunBefore
多依赖
cycle detection
```

---

## Task 4.2 — 依赖驱动 System 并行执行

### 目标

允许互不冲突、依赖已满足的 System 并行执行。

从：

```cpp
for(system : executionOrder)
    system.Tick();
```

逐步演进为依赖驱动调度。

### 要求

- Phase 语义保留。
- `RunBefore` 保留。
- Component conflict 必须阻止非法并发。
- downstream System 在依赖完成后自动 ready。
- 不得在每个 System 后默认执行全局 `WaitIdle()`。

### 验收

必须有并发测试和稳定顺序测试。

---

## Task 4.3 — 引入 TaskHandle / Fence / Dependency

### 目标

逐步替换：

```cpp
DispatchAll();
WaitIdle();
```

支持：

```text
Submit
WhenAll
SubmitAfter
Fence
```

或效果等价机制。

Terrain 最终希望形成：

```text
Generate A -> Mesh A -> Upload A
Generate B -> Mesh B -> Upload B
Generate C -> Mesh C -> Upload C
```

而不是：

```text
Generate ALL
    |
global barrier
    |
Mesh ALL
```

### 约束

- 不要求一次完成所有 Terrain pipeline 异步化。
- 先建立通用依赖机制并测试。

---

# 8. 并发模型收敛

## Task 5.1 — 评估 ChunkSchedule 职责

### 背景

当前需要评估三套机制的重叠：

```text
System Reads/Writes
ChunkSchedule
JobSystem
```

### 目标

明确：

- 哪一层负责静态 System 依赖。
- 哪一层负责 worker 执行。
- 哪些场景真的需要 runtime chunk locking。

### 工作方式

先搜索全部 `ChunkSchedule` 实际调用方。

不要直接删除。

输出设计结论，然后做最小必要改造。

### 重点评估

每个 `ArchChunkMeta` 常驻：

```text
mutex
reader condition_variable
writer condition_variable
reader/writer counters
```

是否仍有必要。

---

# 9. 工程结构清理

## Task 6.1 — Production / Test 代码分离

检查并整理：

```text
Engine/tests/job_system.cpp
Engine/tests/job_system_schedule.cpp
Engine/tests/scene.cpp
Engine/tests/archtype_instance.cpp
Engine/tests/archtype_manager.cpp
...
```

如果这些实际上是 production implementation，应迁移到：

```text
Engine/src/ECS/
```

测试保留：

```text
Engine/tests/
```

CMake 不应要求 Terrain executable 手动链接 ECS `tests/*.cpp` 实现文件。

### 注意

这是最后阶段任务。

不要在性能架构改造期间同时进行大规模目录迁移。

---

## Task 6.2 — 清理旧代码

搜索并确认无引用后，处理：

```text
Engine/include/engine/ECS/System_old/
Engine/include/engine/Scene_old/
component_loader_registry copy.h
```

不得未经引用检查直接删除。

---

# 10. 最终验收标准

完成整个 Roadmap 后至少满足：

1. 原 ECS tests 全部通过。
2. Terrain pipeline tests 全部通过。
3. Terrain render demo 正常运行。
4. 无已知数据竞争。
5. Entity handle / generation 无回归。
6. Archetype 生命周期无回归。
7. Terrain 生成结果保持一致。
8. 给出优化前后 benchmark。
9. 各阶段保持独立、可 review。
10. 不存在为性能优化而引入的大范围无关重构。

最终性能报告至少包括：

```text
Before:
- Main thread frame:
- ECS Pipeline:
- Generation:
- Generation wait:
- Meshing:
- Meshing wait:
- Jobs submitted:
- Worker utilization:

After:
- Main thread frame:
- ECS Pipeline:
- Generation:
- Generation wait:
- Meshing:
- Meshing wait:
- Jobs submitted:
- Worker utilization:
```

---

# 11. Agent 当前起始任务

Agent 第一次执行本需求文件时：

**只执行 `Task 0.1 — 建立 ECS / Terrain 性能基线`。**

完成后停止。

不要自动进入 `Task 1.1`。

如果当前仓库已经存在等价 profiling 基础设施，则：

1. 审核其覆盖范围。
2. 补齐本需求要求的缺失指标。
3. 给出基线数据。
4. 停止等待 review。
