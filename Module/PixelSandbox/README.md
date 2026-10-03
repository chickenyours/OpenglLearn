# PixelSandbox：材料、温度、气压与电流沙箱

基于项目现有 `IModule`、ECS Scene/System Pipeline、ApplicationWindow 和 Render 实现。主程序位于 `Test/main.cpp`。目前提供 36 种可绘制材料／装置（另有空地状态）、可编辑画布、保存／加载、确定性模拟及性能基准。分阶段进度和后续验收项见 [ROADMAP.md](ROADMAP.md)。

## 运行

分发到其他电脑请用根工程的 [Release 发布配置](../../RELEASE.md)：

```powershell
cmake --preset windows-release
cmake --build --preset windows-release --target package_pixel_sandbox_demo
```

便携 ZIP 位于 `output/Release/packages/pixel_sandbox_demo-windows-x64.zip`，包括程序、说明和第三方许可；运行库与 GLFW 静态链接，无需另外携带调试 DLL。

项目根目录：

```powershell
cmake -S . -B build -G Ninja '-DCMAKE_TOOLCHAIN_FILE=clang-msvc.cmake'
cmake --build build --target pixel_sandbox_demo pixel_sandbox_test pixel_sandbox_ui_test pixel_sandbox_benchmark texture_update_test -j 4
.\bin\pixel_sandbox_demo.exe
ctest --test-dir build -R '^(pixel_sandbox_test|pixel_sandbox_ui_test|texture_update_test)$' --output-on-failure
```

默认 320×200、60 个模拟步／秒，闭合边界。窗口可缩放，画布保持比例，鼠标按实际帧缓冲像素拾取。需要项目现有的 OpenGL 4.5、GLFW、GLAD。源代码和运行资源不依赖 Downloads 中的参考项目。

```powershell
.\bin\pixel_sandbox_demo.exe --width 640 --height 400 --seed 42 --preset volcano
.\bin\pixel_sandbox_demo.exe --preset circuit --paused
.\bin\pixel_sandbox_demo.exe --preset elements
.\bin\pixel_sandbox_demo.exe --save-file .\bin\experiment.pxsb
.\bin\pixel_sandbox_demo.exe --load .\bin\experiment.pxsb
.\bin\pixel_sandbox_demo.exe --preset volcano --capture .\build\sandbox.png
```

`--capture` 自动启用隐藏窗口自检；输出 PNG 或 PPM。`--smoke-test` 运行 64 帧、128 个固定步并检查 GL 错误。默认不把 GPU 自检注册到 CTest；可显式设置 `PIXEL_SANDBOX_GPU_TESTS=ON`。

## 操作

| 输入 | 动作 |
| --- | --- |
| 点击右侧材料 | 选择材料并切换为绘制工具 |
| 右侧分类标签 | 全部／固体／粉末／液体／气体／装置，切换时回到第一页 |
| `PageUp` / `PageDown`、`PREV` / `NEXT` | 材料分页，每页最多 20 种 |
| 左键拖动 | 当前工具，笔划插值防止快速移动留下间隙 |
| 右键拖动 | 擦除 |
| 中键 | 拾取材料 |
| 滚轮、`[` / `]` | 改变笔刷半径 |
| `Shift` | 覆盖已有材料；默认只在空地绘制 |
| `B` / `E` | 绘制／擦除 |
| `H` / `J` / `P` | 加热／冷却／注入气压 |
| `T` | 温度可视化 |
| `Space` / `N` | 暂停／暂停并单步 |
| `C` | 清空 |
| `1` / `2` / `3` / `4` / `5` | 落沙／火山／电路／生态／新元素实验预设 |
| `F5` / `F9` | 保存／加载 |
| `Esc` | 退出 |

失去焦点时不推进模拟。侧栏显示所选像素的材料、温度、气压、电荷；底部显示粒子数、活跃区块和单步 CPU 耗时。默认存档为可执行文件旁的 `pixel_sandbox.save`。

## 已实现的玩法

- 粉末堆积、液体流动和有限横向搜索、气体上浮／扩散；沙沉入水、水置换油，移动矩阵根据材料密度预计算。
- 水／冰／蒸汽相变，盐溶入水形成盐水，熔岩遇水形成石与蒸汽，沙受热形成玻璃。
- 木、煤、油、植物和种子燃烧；火与烟具有寿命；水可灭火。
- 酸具有有限腐蚀容量；墙和玻璃耐酸。种子在合适基底萌发，植物利用邻水生长。
- 电火花使金属、水和盐水产生有传播延迟的脉冲；导体进入冷却期，避免相邻导体无限互相激活。电流能够点燃邻近燃料。
- 燃气与火药爆燃产生热量和气压，玻璃可被击碎；足够强的粗网格流速可搬动气体、液体和粉末。连锁爆炸延后执行，不使用递归。

相变和化学规则集中在 `Private/materials.cpp`。数值为本模块的独立游戏规则，不对应真实化学实验：温度用摄氏度、材料采用简化等热容量，传热为稳定局部交换并缓慢向环境温度冷却。水与蒸汽使用不同阈值避免反复闪变。

## 新增元素与组合玩法

| 材料／装置 | 作用机制 |
| --- | --- |
| Soil 土壤 | 粉末；吸收一个相邻水像素，变为泥浆 |
| Mud 泥浆 | 缓慢流动；供种子萌发后回到土壤；达到 100°C 且有排气空间时释放蒸汽并干燥 |
| Snow 雪 | 默认 -12°C 的粉末；达到 2°C 融化为水 |
| Wax 蜡 | 固体；达到 62°C 熔化，可燃烧 |
| Molten Wax 液态蜡 | 黏性液体；降到 52°C 凝固，转换保留燃烧剩余寿命 |
| Oxygen 氧气 | 邻火或燃烧燃料时消耗并产生更热火焰 |
| Hydrogen 氢气 | 受热点燃；邻氧气时产生蒸汽、热量和有限半径冲击，无邻氧气时产生普通火焰 |
| CO2 二氧化碳 | 熄灭邻近火焰和燃烧燃料 |
| Dry Ice 干冰 | 默认 -90°C 的冷粉末；达到 -78°C 升华为 CO2，可与传热组成冷却／灭火实验 |
| Fuse 导火索 | 固定线段；每段约 18 步燃尽再点燃邻段，水可中断燃烧 |
| Battery 电池 | 无限电源，反复给相邻导体脉冲；遵守导体冷却期 |
| Heater 加热器 | 只有处于通电脉冲活跃期才加热相邻物质，主动加热上限 450°C |
| Cooler 冷却器 | 通电后冷却相邻物质，主动冷却下限 -100°C |
| Clone 复制器 | 记忆第一个合适的邻接材料，每 8 步最多向一个空位置复制一次；不复制墙或装置、不覆盖已有粒子 |

普通水在电流传播时，若四邻域有空位，会转换成氢气和氧气。盐水保持原有导电行为。这里用像素配方表现电解与反应，不模拟分子计量；氧气不是所有燃烧规则的必需条件，气体仍使用当前上浮／扩散模型。

按 `5` 可打开四区实验：左上电池／加热器融蜡，右上冷却器制冰与雪／干冰，左下复制器浇灌土壤育苗，右下氢氧容器和导火索／火药。默认切到含新元素的第二页。长导火索需要等待逐段传播，也可用加热工具直接试验气体。

复制器的高 8 位 `Cell.flags` 保存目标材料；鼠标悬停可查看 `COPIES`，用 `Shift` 覆盖绘制复制器可重置。新元素 ID 追加在原有枚举之后，面板由材料目录自动生成分类，无需另维护按钮材料列表。装置规则、概率生长和延时复制都通过统一唤醒 API 保持休眠／完整扫描结果一致。

## 架构和性能

```text
ApplicationWindow + Test/main.cpp
           │ 输入、固定步、存读档
           ▼
SandboxModule : IModule
  ECS world entity → GridStorage（连续 Cell / coarse AirCell 数组）
  ECS chunk entities → ChunkRegion（16×16 区域及睡眠倒计时）
  Pipeline: FieldSystem → HeatSystem → MaterialSystem
           │ Revision + RGBA pixels
           ▼
SandboxRenderer → Render RHIDevice
  持久 RGBA8 纹理 + 固定四边形；SpriteBatch2D 绘制界面
```

每个 Cell 为 16 字节，包含材料、外观差异、寿命、温度、更新戳、电荷和标记。像素内循环不查询 ECS 实体，也不分配邻居列表。一个实体管理网格存储，区块实体管理活动区域；组件与系统沿用现有 ECS。

更新采用逐行扫描、每步交替左右方向；物质移动／反应产物盖上本步戳，避免粒子被重复主动更新。位置、步数、种子、规则盐值决定随机数，休眠不会改变其他像素的随机序列。密度交换携带整条粒子记录，保留热量和电荷。

无活动的区块停止访问像素；编辑、移动、传热、气流和反应会唤醒本区块及边界邻块。概率事件有合法机会时维持唤醒，不能把“本步随机失败”当成静止。热／空气阶段在本步唤醒的新区块会参与后续阶段，避免跨区块延迟。测试包含与关闭休眠的完整扫描对照。

4×4 像素对应一个空气节点，压力与二维速度双缓冲，带阻尼和夹取。当前空气是轻量游戏场，固体障碍按节点中心采样，单像素薄墙不保证阻隔气压。尚未加入不可压流体投影、涡度和严格气密边界。

渲染器仅在 `Revision` 或热图模式变化时转换和上传像素；每次上传为整张画布，主网格仅 4 个顶点。此次为 Render 增加了通用 `async_UpdateTexture`，支持子区域与独立上传字节所有权，但沙箱脏矩形合并尚未启用。上传完成后提交帧，限制一个在途帧，并在关闭前等待回调和帧退休。

## 扩展

```cpp
#include "PixelSandbox/module.h"

PixelSandbox::SandboxModule sandbox;
// 在 Startup 前替换某种材料规则；返回 true 表示源像素已经被替换或消费。
sandbox.SetRule(PixelSandbox::Material::Sand,
    [](PixelSandbox::SandboxModule& world, int x, int y) {
        if (world.At(x, y).temperature > 1300) {
            world.SetCell(x, y, PixelSandbox::Material::Glass, 1100);
            return true;
        }
        return false; // 继续默认运动。
    });
if (sandbox.Startup()) sandbox.Advance(1.0 / 60.0);
```

`SetRule` 替换该材料的默认作用规则；返回 `false` 后仍执行默认运动。也可在启动前 `AddSystem<T>()` 注册项目 ECS 系统，从 `Context::GetService<SandboxModule>()` 获取服务。`Public/sandbox_components.h` 定义真实存储组件。

扩展应通过 `At`、`SetCell`、`SetTemperature`、`TryMove`、`AddPressure` 操作状态。`Edit` 适合更新寿命、电荷和标记，已自动唤醒；不能用它直接修改 material，否则计数会失效。不要删除／搬迁模块拥有的世界或区块实体，不在系统回调重入启动、关闭、清空或存读档。当前模拟 API 为单线程；下一阶段并行化必须先解决跨区块写入冲突。

## 存档

本地版本化小端二进制格式保存材料、温度、寿命、电荷、更新戳、气场、种子、步数、睡眠计时、暂停状态及残余时间。当前写入 v2（包含新材料及复制器目标），仍能读取旧版 v1；旧程序不能读取 v2。先写临时文件，再替换目标文件；重复 `F5` 保存有测试覆盖。加载先验证格式、大小、维度、数值及复制器目标，再整体提交，失败时保留当前世界。

载入要求与当前画布宽高相同。参考游戏的存档格式尚未支持。`StateHash()` 用于物理结果核对，包含粒子、气场、种子与步数；它不覆盖暂停、残余时间等全部执行状态，也不是安全校验和。

## 独立 Release 基准

根工程已经提供共享 Release 预设。模块仍保留无需图形库的独立 Release 入口，适合单独测量核心模拟：

```powershell
cmake -S Module/PixelSandbox -B build/pixel-sandbox-release -G Ninja `
  '-DCMAKE_BUILD_TYPE=Release' `
  '-DCMAKE_TOOLCHAIN_FILE=C:/Users/16620/Desktop/projects/OpenglLearn/clang-msvc.cmake'
cmake --build build/pixel-sandbox-release -j 4
ctest --test-dir build/pixel-sandbox-release --output-on-failure
.\build\pixel-sandbox-release\pixel_sandbox_benchmark.exe --steps 120
```

工具链支持时默认开启 Release IPO，可用 `-DPIXEL_SANDBOX_IPO=OFF` 对照。基准固定种子、预热 30 步、计时 120 步，输出 JSONL／CSV 的 P50、P95、平均耗时、阶段耗时、访问像素数、活动比例、粒子数及物理校验值。落沙、密集惰性、九材料反应、静止、新元素实验五类场景分别测试 320×200 和 640×400；静止场景额外核对关闭休眠的结果，共输出 12 行。CPU 计时不包含渲染、颜色转换、上传或保存。实测结果与原始数据见 [PERFORMANCE.md](PERFORMANCE.md)。

## 源码参考

本机 `Downloads/sandspiel/sandspiel` 和 `Downloads/SandBox/sandspiel` 是同一干净提交 `9936e06b07a583aedfba9589c0cead2917f405fb`，并非两种不同游戏。sandspiel 根 LICENSE 为 MIT；Powder Toy 的 `The-Powder-Toy/LICENSE` 为 GPL v3。本模块独立实现玩法和数据结构，没有移植这些项目的源文件或资源。

| 参考位置 | 吸收的机制 |
| --- | --- |
| sandspiel `crate/src/lib.rs` | 紧凑像素记录、局部作用 API、更新标记、交替扫描 |
| sandspiel `crate/src/species.rs` | 密度置换、燃烧、酸蚀、植物与流体作用 |
| sandspiel `js/render.js`、`js/fluid.js` | 字段纹理绘制、离散物质与连续场分离 |
| Powder Toy `src/simulation/SimulationData.cpp` | 预计算移动判定表 |
| Powder Toy `src/simulation/Simulation.cpp` | 热／规则／移动的明确更新次序 |
| Powder Toy `src/simulation/Air.cpp` | 独立粗网格压力／速度场、双缓冲和数值限制 |
| Powder Toy `src/simulation/elements` | 材料属性表、局部特例、有限酸容量与电流冷却 |
