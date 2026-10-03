# ForestFire：ECS 森林火焰元胞自动机

模块沿用项目的 `Public / Private / Systems / Test` 目录组织，复用引擎的 `ECS::Core::Scene`、`ECS::System::Pipeline` 和 `IModule` 生命周期。模拟库不依赖窗口或 OpenGL；可视化通过 `Module/Render` 的 RHI 资源与帧命令接口完成。

## 构建与运行

在项目根目录使用现有 Windows / clang-cl 工具链：

```powershell
cmake -S . -B build -G Ninja '-DCMAKE_TOOLCHAIN_FILE=clang-msvc.cmake'
cmake --build build --target forest_fire_demo forest_fire_test
ctest --test-dir build -R '^forest_fire_test$' --output-on-failure
.\bin\forest_fire_demo.exe
```

图形演示需要项目现有 GLFW、GLAD 库和 OpenGL 4.5 上下文。主程序是 `Test/main.cpp`，无窗口逻辑测试是 `Test/forest_fire_test.cpp`。

```powershell
# 创建隐藏窗口，执行有限帧数后自动退出，用于图形初始化和帧提交验证。
.\bin\forest_fire_demo.exe --smoke-test

# 指定网格、种子和拓扑，从暂停状态开始。
.\bin\forest_fire_demo.exe --width 120 --height 80 --seed 42 --wrap --four-neighbors --paused

# 图形自检并保存最后一帧（PPM）。
.\bin\forest_fire_demo.exe --capture build/forest_fire.ppm
```

| 目标 | 用途 |
| --- | --- |
| `forest_fire_module` | ECS 模拟静态库，可供其他模块复用 |
| `forest_fire_render` | 基于 Render RHI 的网格与信息面板渲染 |
| `forest_fire_demo` | 可交互演示，入口位于 Test 目录 |
| `forest_fire_test` | 无窗口规则、边界和生命周期测试 |

演示操作：

| 输入 | 操作 |
| --- | --- |
| `Space` | 暂停／继续 |
| `N` | 暂停并推进一步 |
| `R` | 用相同种子重新生成森林和初始火源 |
| `+` / `-` | 调节模拟速度 |
| 鼠标左键拖动 | 点燃树木 |
| 鼠标右键拖动 | 种树 |
| 鼠标中键拖动 | 清为空地，绘制防火带 |
| `Shift` + 鼠标 | 扩大编辑笔刷 |
| `Esc` | 退出 |

演示在失去窗口焦点时停止推进模拟；命令行 `--help` 显示可用参数。演示网格每边限制为 512，核心模拟每边最多 2048，总元胞数最多 1,048,576。

## 模型

每个网格位置对应一个 ECS 实体，状态为 `Empty`（空地）、`Tree`（树木）或 `Burning`（燃烧）。每次固定步长遵循以下规则：

1. 空地以 `growthProbability` 概率长出树木。
2. 树木对每个不同的燃烧邻居分别以 `spreadProbability` 判定传播；未被传播点燃时，以 `lightningProbability` 概率自行起火。环绕后重复的邻居只计算一次，元胞不会成为自己的邻居。
3. 燃烧元胞倒计时，耗尽 `burnTicks` 后成为空地。

下一状态基于整张网格的当前状态计算，再统一提交。因此新起火的树木在下一步才会继续传播，刚长出的树木也不会在同一步起火。所有概率都是**每个模拟步**的概率；改变步长会改变每秒的事件频率。

`SimulationConfig` 默认网格为 160 × 100，初始树木密度 0.65，固定步长 0.05 秒。支持四邻域或八邻域、有限边界或环绕边界、可配置传播概率与燃烧时长，以及用于复现的随机种子。模拟本身只生成树木和空地；演示程序额外设置初始火源。

## 使用模拟库

```cpp
#include "ForestFire/module.h"

ForestFire::SimulationConfig config;
config.width = 80;
config.height = 60;
config.seed = 1337;
config.neighborhood = ForestFire::Neighborhood::Eight;
config.boundary = ForestFire::BoundaryMode::Finite;

ForestFire::ForestModule forest(config);
if (!forest.Startup()) {
    // forest.Error() 包含配置或初始化错误。
    return;
}
forest.SetCell(40, 30, ForestFire::CellState::Burning);
forest.Advance(1.0 / 60.0);
const auto cells = forest.Snapshot(); // 按 y * width + x 排列的值拷贝。
forest.Shutdown();
```

`Advance(seconds)` 按固定步长驱动管线，并限制单次追帧数量；负数和非有限时间不会推进模拟。暂停时不积攒时间，`FixedTick()` 可用于暂停后的单步执行。`Reset()` 以当前种子重建初态，`Reset(seed)` 指定新种子。`SetCell` 可编辑空地、树木和火源，`Ignite` 仅点燃树木。`Stats()` 提供模拟步数和三种状态的数量。

## 扩展位置

- `Public/forest_components.h`：元胞组件、配置、统计与渲染快照数据。
- `Public/forest_module.h`、`Private/forest_module.cpp`：模块生命周期、实体映射、编辑和固定步长驱动。
- `Systems/forest_systems.h`：同步规则计算与提交系统。
- `Public/forest_renderer.h`、`Private/forest_renderer.cpp`：将快照转为 RHI 网格；模拟状态不会依赖 GPU 资源。
- `Test/main.cpp`：窗口、输入、模拟及渲染的组合入口。

基础组件为 `CellPosition`、`Cell`（当前状态）和 `NextCell`（待提交状态）。默认管线按 `EvaluateSystem → CommitSystem` 执行。

在 `Startup()` 前通过 `AddCellComponent<T>(registrationKey)` 给所有元胞添加自定义组件，通过 `AddSystem<T>()` 注册自定义 ECS 系统，通过 `RunBefore<A, B>()` 声明同阶段的执行顺序。例如，为湿度规则预留位置：

```cpp
struct Moisture : ForestFire::ForestComponent<Moisture> {
    float value = 0.5f;
};

// MoistureSystem 继承 ECS::System::System，使用 Update 阶段。
// OnTick 中读取 Moisture / Cell，修改 NextCell；湿润树木可阻止起火。
forest.AddCellComponent<Moisture>("forest_fire_moisture");
forest.AddSystem<MoistureSystem>();
forest.RunBefore<ForestFire::EvaluateSystem, MoistureSystem>();
forest.RunBefore<MoistureSystem, ForestFire::CommitSystem>();
// 然后调用 forest.Startup()。
```

系统从 `Context` 获取 `ForestModule` 服务，通过 `Scene()` 和 `EntityAt(x, y)` 访问实体及组件。`OnStart()` 可初始化自定义组件；观察最终状态可使用 `PostUpdate` 阶段。`Reset()` 保留元胞实体 ID 和自定义组件数据，仅重置基础状态、随机序列、时间与统计；自定义环境需要自行决定重置方式。`Shutdown()` 后再 `Startup()` 会重新创建所有实体。

扩展需保留网格实体及其基础组件、坐标对应关系，并遵守“读当前状态、写下一状态、统一提交”的时序。注册系统和修改管线顺序必须在模块停止时进行；生命周期回调不应抛出异常，`OnStart()` 用 `false` 报告失败。系统内部直接操作组件，不重入模块的启停、编辑或步进接口。模拟 API 在主线程调用，渲染器只消费快照。随机种子复现要求相同配置、编辑操作与模拟步序列。

渲染器以异步完成回调连接资源创建、网格上传和帧提交，并限制在途工作，避免渲染线程未读取数据时覆盖网格。网格按窗口比例居中显示，鼠标拾取使用相同布局并处理窗口／帧缓冲像素比例。

渲染器建议与 Render 模块和窗口保持相同生命周期。关闭时先等待在途帧完成，再关闭渲染器、Render 和窗口。现有 RHI 没有独立删除着色器／程序的接口，因此这些对象在窗口上下文销毁时回收；不建议在同一长期存活的 Render 上反复重建渲染器。

## 验证

2026-10-03 在项目现有 Windows / clang-cl Debug 构建中通过：

- `forest_fire_test` 的 12 组测试：同步传播、燃烧寿命、概率极值、边界与邻域、小网格邻居去重、种子复现、固定步长、参数校验、生命周期、组件／系统扩展、启动失败回滚、启动时初始化与统计一致性。
- `forest_fire_demo --capture build/forest_fire_smoke.ppm`：32 帧 RHI 渲染、32 步 ECS 更新，结果为 `GL_NO_ERROR`；已检查 1280 × 800 输出画面。

图形自检使用隐藏窗口；尚未进行可见窗口下的长时间鼠标键盘交互测试。
