# 裙摆布料与模块化人物资产

本模块将裙摆从人物网格中实际拆出，生成独立的骨骼网格包、布料模拟资产和人物装配资产。主体和裙摆分别蒙皮、上传与绘制，关闭裙摆会隐藏独立衣片；它不是在完整人物上再次叠画一件裙子。身体与衣片共享骨架参考姿态和表情索引布局，因此既有 VMD 烘焙动画可以同时驱动二者。

## 运行真寻示例

在项目根目录执行：

```powershell
.\Module\Model\RunMahiroCloth.cmd
```

也可以直接指定资产与动画。用于日常预览和性能测量的 Release 可执行文件位于 `output/Release/bin/`：

```powershell
.\output\Release\bin\skeletal_asset_editor.exe --character Asset/Generated/MahiroCloth/Character.asset.json --animation Asset/Generated/Animations/MahiroTaitui.anim.json
```

编辑器的 **Clothing** 面板用于切换主体与裙摆可见性、启停布料、重置模拟，以及调整风、伸展/弯曲/面积/动画驱动柔度、阻尼、子步、迭代数与碰撞开关。调试显示用于检查模拟网格、腰部固定点和骨骼驱动的碰撞体，粒子检查器可查看 Max Distance、驱动权重与当前/参考位置。

**Pause** 只暂停动画采样，布料会继续受重力作用并保留余摆，适合观察拖拽后的下垂；**Freeze physics** 单独冻结布料模拟。时间轴不连续跳转、修改暂停姿态或更换服装时会重置布料，单纯暂停动画不会重置。固定点继续跟随衣片蒙皮；自由粒子的参考形状由实际附着环拟合，并将腰部非刚性残差平滑延伸到自由区域。这样躯干与骨盆转动不一致时，重置也不会把两种参考姿态拼接成一条被拉开的腰缝。缺少可用附着环时才回退到参考骨骼。

自动生成的碰撞体还保存中性姿态。如果动画起始姿态已深度切入中性衣片，初始化会先将碰撞体从中性位置连续推进到当前姿态，让布料建立正确包裹关系。这段隐藏的准备过程不推进动画时间，也不计入实际播放步数；避免直接将三角形各顶点推向大腿两侧而形成尖刺。

可重复的动画与布料检查命令：

```powershell
.\bin\skeletal_asset_editor.exe --cloth-test --character Asset/Generated/MahiroCloth/Character.asset.json --animation Asset/Generated/Animations/MahiroTaitui.anim.json
```

该检查使用源 VMD 动作进行固定步长布料演算，默认连续 180 帧，每帧 1/60 秒；不在中途重新装备服装。换装检查使用独立的 `--cloth-equip-test`，会在第 30 帧重新装备，检查身体资源、当前动画与时间是否保留。检查命令成功仅说明对应断言成立，仍需检查渲染表面、运动时序和碰撞误差。

以下命令在动作播放约 1 秒后保持骨骼姿态，让布料继续演算到第 180 帧：

```powershell
.\bin\skeletal_asset_editor.exe --cloth-test --character Asset/Generated/MahiroCloth/Character.asset.json --cloth-hold-at 1 --frames 180
```

生成没有编辑器界面和调试线的视口 PNG 序列，用于逐帧对比拖拽、下垂与腿部接触：

```powershell
.\bin\skeletal_asset_editor.exe --cloth-test --character Asset/Generated/MahiroCloth/Character.asset.json --frames 180 --capture-dir build/cloth-motion --capture-every 6 --clean-cloth-view --yaw 0.8 --distance 3.4
```

`--capture-every 6` 按每 6 个渲染帧采集一次；在上述固定步长检查中间隔为 0.1 秒。`--yaw` 以弧度设置相机水平角，`--distance` 设置预览相机距离。`--screenshot` 保存完整编辑器截图，`--capture-dir` 保存纯视口图像。对比时应从相同起始状态连续演算至目标时刻；直接 `--time 2` 会在该姿态重新初始化，不能替代从 0 秒播放到 2 秒的惯性历史。

## 性能测量

视口标题显示完整帧耗时、实际 FPS，以及布料、CPU 蒙皮和几何上传耗时；将鼠标停在标题上可查看姿态采样、衣片回写、渲染、UI、swap、渲染同步和 GPU scene span。统计使用原有视口区域，不缩小渲染分辨率。默认保留 **16 次约束迭代上限、3 个子步**。

`Convergence (mm)` 控制收敛提前结束，默认 0.25 毫米，设为 0 则始终求解完整迭代次数。至少完成 4 轮，并连续两轮达到位移容差、且没有面积防翻面修复，才允许结束当前子步；复杂接触仍可使用全部预算。有效容差还限制在碰撞厚度的十分之一以内，避免薄布使用过宽容差；启用自碰撞时保留完整迭代。运行统计与 benchmark 报告记录实际执行的迭代数，不能将上限误报为每步始终执行的数量。

交互预览默认将布料交给独立工作线程。主线程提交姿态、衣片几何与经过的时间，消费已完成的不可变快照；身体蒙皮、裙摆、骨架线与碰撞形状使用同一份已完成固定步的姿态。工作线程保留不足一个固定步的时间余量，每完成一个完整固定步立即发布结果，避免两步批次将可见更新频率减半；不足一步时确认输入但复用上一份完整姿态和衣片，避免身体先到渲染输入终点、裙摆却留在上一物理时刻而出现腰缝。中间步从插值后的骨架重新蒙皮固定点，不能只线性插值旋转前后的顶点。队列合并待处理姿态并累计保留物理时间。重置、跳转和换装使旧代结果失效；首次碰撞准备完成前暂不推进动画时钟，避免启动期间累积延迟。传递几何不复制纹理，队列只保留一份待处理输入和最新结果。暂停动画仍持续提交物理时间；冻结物理提交零时间。没有工作时线程阻塞。失焦暂停会丢弃未完成的输入时间，当前最多一个完整固定步发布后阻塞；恢复从该完整快照继续，不重置裙摆，也不补算后台时间。

异步渲染会有一段物理结果延迟。界面和 benchmark 分别显示渲染 FPS、已完成物理步频、快照频率和物理延迟，不能仅凭渲染 FPS 宣称物理已跟上实时。`--sync-cloth` 可恢复同步预览进行对照；固定步长 smoke/捕获检查仍同步执行，以获得可重复的完整动作序列。

普通交互预览默认设置 **60 FPS 上限**，等待采用睡眠，不忙等；这限制不必要的绘制次数，并不保证慢帧能达到 60 FPS。Windows 优先使用进程内高精度 waitable timer，避免普通休眠的粗粒度造成明显过睡；不支持时回退普通 timer 或标准休眠，不修改全局计时器周期。benchmark 的 `frame_wait_backend` 记录实际后端。切到其他窗口后暂停动画和布料，仅以最多 15 FPS 刷新界面；最小化后阻塞等待窗口事件。恢复前清除未完成的后台时间，从最后完成的姿态继续，保留布料状态，避免补算积压导致持续占用或姿态突跳。

`--max-fps N` 可覆盖前台上限，`0` 表示不限帧。启用显式帧率上限时默认关闭垂直同步，避免交换等待与睡眠重复限速、错过下一次垂直同步而额外降帧；`--vsync` / `--no-vsync` 可显式覆盖。benchmark 和 smoke 默认仍不额外限帧；测量普通 60 FPS 配置时显式附加 `--max-fps 60`。benchmark 不因失焦切换到暂停模式，报告会记录焦点状态、限帧睡眠和 QoS，不能把压力测试的 CPU 占用当作后台空闲占用。不要同时运行多个预览器或在编译期间测量性能。

较大的衣片在初始化时将约束按共享粒子图着色，颜色内约束互不写入相同粒子。求解器默认使用 2 个总计算线程；轻量约束留在物理调用线程，面接触和铰链的整个颜色族一次发布，在内部按颜色同步。支持 AVX2 的 CPU/操作系统使用四路 double 伸展、面积和铰链核，指令仅限定在这些核内，没有全局 ISA 要求；其他平台自动回退标量。`MODEL_CLOTH_SIMD=0` 可关闭向量核进行对照。未使用 fast-math、近似倒数平方根或降低浮点精度。可用 `MODEL_CLOTH_THREADS=1` 关闭约束并行，或设为 2～8 单独比较；所有线程数保持相同颜色顺序，线程越多不保证越快。

线程间交接和颜色屏障默认最多主动自旋 **2048 次**，之后进入阻塞等待；空闲时仍阻塞。较短的轮询预算减少无效 CPU 活跃时间，不改变约束顺序、物理步长和迭代预算。本机 v3 四线程对照中，从 16384 降至 2048 后物理吞吐约为 34.05 / 34.01 Hz，进程 CPU 时间从墙钟的 4.05 倍降至 3.54 倍；这只是该次单变量测量，并非最终默认配置的实时性结论。`MODEL_CLOTH_SPIN_CHECKS` 可在启动前覆盖该预算，支持范围 512～262144；它是轮询次数，不是毫秒，较大值会增加 CPU 活跃时间，应按相同长测比较。

Windows 预览器默认沿用系统 QoS 策略，不主动请求高性能调度。仅在显式设置 `MODEL_PREVIEW_QOS=1` 时为当前进程请求 HighQoS，并在正常退出时恢复进入前的 `ProcessPowerThrottling` 状态；API 不可用或请求失败时继续使用系统策略。程序不设置亲和性、进程优先级或全机电源选项。Windows 的窗口可见性和焦点会影响默认 QoS，混合架构上的 QoS 又可影响核心选择及处理器功耗管理，因此测量时应记录此策略。[Microsoft：Quality of Service](https://learn.microsoft.com/en-us/windows/win32/procthread/quality-of-service)、[SetProcessInformation](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setprocessinformation) 说明了该进程级 HighQoS 用法。报告中的 `preview_qos` 记录请求结果，历史 HighQoS 数据不能直接当作当前默认配置的性能。

接触流形缓存目前是默认关闭的实验项，`MODEL_CLOTH_CONTACT_CACHE=1` 才启用；`MODEL_CLOTH_FULL_NARROWPHASE=1` 强制完整窄阶段查询，供质量和性能对照。

凸碰撞另有默认开启的分离平面缓存。每次使用前，必须以当前粒子或三角形的三个顶点重新证明它们仍完全位于该平面外，才能跳过重复端点查询；证明失效立即执行完整查询，连续运动碰撞检查仍然执行。该缓存不复用旧碰撞响应，`MODEL_CLOTH_SEPARATING_CACHE=0` 可关闭进行数值一致性对照。

从项目根目录运行以下 Release 实时基线，至少使用 1800 帧并排除前 180 帧预热，覆盖持续运行与动作循环：

```powershell
.\output\Release\bin\skeletal_asset_editor.exe --character Asset/Generated/MahiroCloth/Character.asset.json --animation Asset/Generated/Animations/MahiroTaitui.anim.json --benchmark-frames 1800 --benchmark-warmup 180 --no-vsync --benchmark-output build/cloth-release-real.json
```

`--benchmark-frames 1800` 表示运行总共 1800 帧，至少排除前 180 帧预热。异步模式还要求首份有效物理结果已经完成，建立累计计数基线后才开始测量，`warmup_frames` 记录实际排除帧数；若初始化延后，测量帧会少于 1620 帧。资产加载、编辑器初始化和字体构建也不计入测量。上述命令保留可见窗口并关闭垂直同步，报告平均/中位数/p95/最大帧耗时、分阶段均值、实际动画秒数、物理推进秒数与墙钟秒数，以及每秒固定步数。保持窗口可见、相同尺寸和相同相机/动作，比较相同构建配置；`bin/` 中的 Debug 可执行文件应单独标注，不能与 Release 混用。应关闭此前启动的重复预览窗口，再进行各组测量。

以下为上一轮 **v2 胶囊资产的历史性能基线**，不能代表 v3 皮肤碰撞代理的耗时。本机使用 Release、无环境变量覆盖、HighQoS、2 个计算线程和 16384 次有限自旋；完整衣片为 1034 个粒子、1976 个三角形，保留 16 次迭代上限和 3 个子步。OpenGL 报告 GPU 为 Intel(R) RaptorLake-S Mobile Graphics Controller，视口 885×749，关闭垂直同步且不限制帧率。总计运行 5400 帧，排除前 180 帧，测量 5220 帧 / 29.330 秒，原始结果见 [v2 默认配置长测 JSON](../../build/cloth_perf_default_final.json)。

| 指标 | 实测值 |
| --- | ---: |
| 渲染 FPS | 177.974 |
| 帧耗时 p95 / 最大值 | 6.602 / 24.009 ms |
| 已完成物理固定步频 | 58.438 Hz |
| 快照发布频率 | 63.416 Hz |
| 物理延迟平均 / 最终值 | 25.604 / 18.921 ms |
| 动画 / 物理推进时间 | 28.639 / 28.567 s |
| 进程 CPU 占全机容量 | 9.079% |

固定步长目标为 60 Hz；长测覆盖动作循环，循环边界的重置和碰撞准备仍计入墙钟，因此整段平均固定步频为 58.438 Hz。该结果同时记录物理推进与延迟，适用于上述本机配置和动作。

默认开启垂直同步的独立复测为 146.620 FPS、58.648 Hz 物理固定步频，平均延迟 26.662 ms，结束时 30.526 ms；报告见 `build/cloth_perf_default_vsync_final.json`。

上一轮 v2 的 26 项 Model/布料回归通过，包括资产读写、模型导入、动画烘焙、1/4/8 线程同步、SIMD 与标量回退、异步队列、实际 OpenGL 预览、暂停余摆和换装。完整 9.2 秒 VMD 的 553 个固定步检查中，最大三角面残余穿入约 0.267 mm、塌陷三角形为 0；实时异步暂停预览另行通过 8 mm 表面接触上限。历史日志见 `build/cloth_final_regression.log`。

性能模式使用与普通交互相同的真实帧间隔，保留单帧 `delta <= 0.1 s` 的安全上限。若布料计算已经慢于实时，下一帧会求解多个固定步；实际动画/物理秒数与墙钟秒数的比例可以暴露追赶和时间裁剪。`--cloth-test` 等 smoke 模式使用固定 delta，是可重复的质量检查，不能代替实时 FPS 测量，也不能与 benchmark 组合。动作跨循环边界时的重置和碰撞初始化属于实际运行成本，仍会计入对应测量帧。

测量帧率时不要附加 `--capture-dir`、`--screenshot`，并清除 `MODEL_CLOTH_PROFILE` 环境变量。图像回读、PNG 编码和逐帧终端输出会影响帧耗时；采集视频的运行应与纯预览性能基线分开。报告中的 `capture_enabled` 标记视口序列采集状态，启用采集不会自动从墙钟统计中扣除其成本。

分别运行以下两组单变量对照，输出到不同报告文件。相对上述基线，第一组只关闭布料，身体和衣片仍按骨骼动画蒙皮渲染；第二组只恢复垂直同步，布料和全部渲染质量保持不变：

```powershell
.\output\Release\bin\skeletal_asset_editor.exe --character Asset/Generated/MahiroCloth/Character.asset.json --animation Asset/Generated/Animations/MahiroTaitui.anim.json --benchmark-frames 1800 --benchmark-warmup 180 --no-vsync --no-cloth --benchmark-output build/cloth-release-no-cloth.json
.\output\Release\bin\skeletal_asset_editor.exe --character Asset/Generated/MahiroCloth/Character.asset.json --animation Asset/Generated/Animations/MahiroTaitui.anim.json --benchmark-frames 1800 --benchmark-warmup 180 --benchmark-output build/cloth-release-vsync.json
```

同步模式的 `cloth` 包含 CPU Reset/Step；异步模式该字段只计主线程提交输入的开销，物理批次耗时在界面单独显示。`cloth_async` 标记模式，`cloth_snapshots_per_second`、`fixed_steps_per_second`、`physics_lag_ms_mean/final` 说明结果更新与积压。`skin` 为主体和衣片蒙皮；`upload` 包含几何验证、打包与上传等待；`render` 包含预览帧录制及提交完成等待。渲染同步计时与 render/UI/swap 存在重叠，不能把这些数值全部相加。GPU 时间戳从前几帧异步轮询，结果未就绪时跳过采样，不强制 GPU 完成；`gpu_scene_span_ms_mean` 包含提交和 CPU 调度间隙，不能当作纯 GPU 着色执行耗时。

Windows 报告记录进程 PID、实际 exe 绝对路径和逻辑处理器数，可与任务管理器中的进程核对。每个测量帧首尾通过 `GetProcessTimes`/`GetThreadTimes` 采样进程和主线程的 user/kernel CPU 时间，并对全部有效测量帧求和，以减小单次 CPU 记账粒度的影响。CPU 秒表示实际被调度执行的时间，墙钟秒还包括等待和被抢占时间。主线程 CPU 秒接近墙钟秒说明它在持续工作；明显较低则需要进一步检查等待或调度，不能仅据此认定 GPU 瓶颈、虚拟化或限速。单线程负载也会被全机核数稀释：例如 24 个逻辑处理器上主线程忙碌约 91%，折算全机容量约为 `91% / 24 = 3.8%`，任务管理器总占用约 4% 仍可能对应单线程瓶颈。

需要定位求解器内部阶段时，可短暂开启逐帧 CPU 墙钟计时。以下输出包含准备、伸展、面积、防翻面、弯曲、粒子/表面碰撞与诊断的耗时；固定步长仅用于比较各阶段，并非实时 FPS 基线：

```powershell
$env:MODEL_CLOTH_PROFILE = '1'
try {
    .\output\Release\bin\skeletal_asset_editor.exe --cloth-test --character Asset/Generated/MahiroCloth/Character.asset.json --frames 60
} finally {
    Remove-Item Env:MODEL_CLOTH_PROFILE -ErrorAction SilentlyContinue
}
```

正常预览与正式 benchmark 不开启该逐帧日志。不要依据不同机器负载、不同动作时刻或不同构建的一次中间测量判断优化收益；最终对比应同时检查帧率、动画/物理推进比例和已有布料质量断言。

## v3 当前性能与剩余瓶颈

2026-10-15 最终复测使用 Release、默认两个计算线程、2048 次有限自旋、系统默认 QoS、60 FPS 上限与高精度阻塞计时器；软件限帧自动关闭垂直同步。视口 885×749，GPU 报告 Intel(R) RaptorLake-S Mobile Graphics Controller，保留 1034 个粒子、1976 个三角形、3 个子步与 16 次迭代上限。排除初始化和 180 帧预热后，独立测量 1800 帧，期间未并行运行本任务的编译或其他测试。

| 指标 | 测量值 |
| --- | ---: |
| 墙钟时间 | 30.766 s |
| 渲染帧率 | 58.505 FPS |
| 已完成物理步频 | 24.735 Hz |
| 快照发布频率 | 24.930 Hz |
| 物理推进时间 | 12.683 s |
| 平均 / 最终物理延迟 | 2511.883 / 2683.316 ms |
| 进程 CPU 占全机容量（24 逻辑处理器） | 7.965% |

报告见 `build/cloth_v3_final_clean_perf.json`，日志见 `build/cloth_v3_final_clean_perf.log`。此结果说明窗口绘制和资源节制已改善，**不代表人物物理达到实时 60 Hz**。当前 v3 的蒙皮凸代理表面接触仍是主要开销，持续播放仍会积压；不能用界面 FPS 宣称人物动作已经同样流畅，也不能按实时物理验收通过。后台暂停和最小化阻塞用于停止无效求解，不是通过丢弃前台物理时间伪造吞吐。

累计 34 项不同回归检查已通过，包括完整动作、18 姿态重置、蒙皮身体拟合、资产读写、并行/SIMD、时钟与实际 OpenGL 预览。末轮双平面优化另通过完整动作/附着检查，逐步发布另通过工作线程、时钟和异步 OpenGL 检查；对应日志为 `build/cloth_v3_final_cpu_regression.log`、`build/cloth_v3_final_editor_regression.log`、`build/cloth_v3_composite_quality_validation.log`、`build/cloth_v3_single_tick_validation.log`。Release 和 `bin` 下的 Debug 编辑器均已重新构建。

## 从骨骼中间资产生成服装

```powershell
.\bin\garment_asset_import.exe --source Asset/Generated/MahiroTaitui/Package.asset.json --output Asset/Generated/MahiroClothV2 --refinement 1 --density 0.25
.\bin\garment_asset_import.exe --inspect Asset/Generated/MahiroClothV2/Character.asset.json
```

输入也接受原始 PMX/FBX 等模型，由现有骨骼导入器先创建 CPU 中间资产。推荐使用已经检查过骨架与表情的 `Package.asset.json`，以便复用相同 skeleton ID 和已有动画。输出必须是新的或空目录，程序不会覆盖已有非空资产。

自动选择结合裙摆骨骼名称和蒙皮权重，先按原始索引连通性拆分三角形，再为主体和衣片分别紧凑化顶点、材质、纹理及表情顶点索引。裙摆与上衣即使接触于同一位置，也不会因为过早焊接而合并提取。随后只对独立衣片进行中点细分，插值 UV、蒙皮及表情 delta，主体网格保持原有分辨率。

`--refinement` 接受 0、1、2，默认 1；每级将一个三角形分成四个。`--density` 为面密度，单位 kg/m²，默认 0.25，允许 0.01～5。每个自由粒子的质量由相邻三角形面积和面密度确定，避免细分后因粒子变多而随意增加服装总质量。固定点逆质量为 0。

真寻源裙片有 494 个三角形；当前示例的一级细分结果为 **1976 个三角形、1034 个模拟粒子、44 个固定点**，已经保存于 `Asset/Generated/MahiroCloth`。上述命令使用新目录，便于生成其他配置而不覆盖现有示例。导入程序也打印约束和碰撞体数量，并报告无法自动处理的拓扑。自动识别并不适合所有服装；可以重复 `--material N` 显式限制源材质区域，例如：

```powershell
.\bin\garment_asset_import.exe --source Asset/Generated/MahiroMaid/Package.asset.json --output Asset/Generated/MahiroClothRevision2 --name Skirt --pin-fraction 0.14 --refinement 1 --density 0.25 --material 3 --material 4
```

这里的材质索引仅展示命令形式，实际索引应以目标模型的数据检查结果为准。固定区域优先识别焊接拓扑上明确的最高闭合边界，使用衣片真实附着位置；边界不明确时回退到髋部/裙摆根骨位置，再回退到各连通分量顶部高度比例。`--pin-fraction` 控制高度比例回退及动画驱动衰减范围，必须介于 0 与 1 之间。较低的独立附件使用各自顶部固定点，避免整块脱落。自由区域的驱动在附着过渡带下方衰减为零，较宽的 Max Distance 包络允许下摆拖后、摆动和下垂。

当前默认真寻资产已升级为 v3：两条大腿和两条小腿各两段、骨盆八块、双脚各一块，共 **18 个保留蒙皮信息的凸代理**，接触厚度为 **3 mm**。骨盆分界放在股骨/腰部混合权重区域，分区重叠为 1 mm；脚部也按实际皮肤拟合，避免大半径圆胶囊继续撑开小腿周围的衣片。生成失败的区域明确保留兼容胶囊并报告原因。旧默认资产保存在 `Asset/Generated/MahiroCloth_BeforeStabilityFix`，源模型和动作包继续独立保存。

烘焙动画保存在源包中时，会随身体包保留；若从只有模型的 `MahiroMaid` 包生成，可在预览器中通过 `--animation` 另行加载动作。旧生成资产不会因为重新编译自动细分或改变权重，应生成新目录并用 `--character` 指向新清单。

## 资产结构与换装

```text
MahiroCloth/
  Character.asset.json                  人物装配与服装插槽
  Body/Package.asset.json               已移除裙摆的主体骨骼网格包
  Garments/Skirt/Package.asset.json      独立裙摆骨骼网格包
  Cloth/Skirt.cloth.json                独立模拟网格、约束、权重与碰撞配置
```

两个骨骼包各自带有几何、蒙皮、形态、材质和已解码纹理，可脱离原始模型路径读取。骨架和形态布局保留一致，但身体与衣片的顶点/形态 delta 已分别重新索引。装配清单用相对引用连接它们，保存服装插槽名、服装 ID 以及启用/隐藏状态。当前运行时支持一个裙摆插槽；清单采用数组为后续衣服、饰品等插槽扩展保留空间，读取多个插槽会明确报错，避免静默丢失服装。

替换衣片时必须一起提供它的骨骼包和布料资产。兼容性校验要求相同 skeleton ID、骨骼顺序/名称/父子关系/参考姿态，以及相同表情名称/类型/顺序。尺寸相似但骨架不同的服装应先离线适配，不能直接用骨骼索引配对。布料另外持有绑定指纹，覆盖衣片顶点/拓扑、骨架、形态 delta、蒙皮权重和绑定变换；改变几何或蒙皮后需要重新生成布料。仅改变材质、颜色或纹理不会使该指纹失效。

编辑器可加载兼容衣片并单独显示它，导出装配到新目录。保存流程先在同级临时目录写出完整候选资产，重新加载并验证之后才发布。格式包含版本号，加载器拒绝不支持版本、绝对/越界路径、符号链接/Windows junction、错误索引、非有限数值、没有固定点的模拟连通分量，以及超出限制的模拟预算。

## 数据与每帧流程

布料 v3 资产保存：

- 模拟粒子的参考位置、按面积分配的逆质量、Max Distance 和 Animation Drive。
- 焊接渲染接缝后的三角形、伸展边、历史弯曲边数据及连接腰部固定点的测地距离 tether。
- 每个渲染顶点到模拟粒子的绑定，允许多个 UV/法线接缝顶点共享一个模拟粒子。
- `motionBone` 和 `motionInverseBind`，指定附着环拟合不可用时的自由粒子参考姿态；`motionBone = -1` 保留旧的完整蒙皮目标模式。
- 跟随骨骼的胶囊、按真实身体皮肤分区拟合的凸碰撞代理，以及固定步长、子步、迭代数、柔度、厚度、全局/局部/驱动阻尼、摩擦、重力和风参数。
- 凸代理的完整支持点云、参考表面三角形/半空间及蒙皮贡献。裁剪点保存按权重预乘的局部绑定位置，使其变形与源皮肤三角形上的重心插值一致；参考姿态凸包内部的支持点也会保留，避免这些点在混合蒙皮后突出时失去覆盖。
- 人工分块切面使用 `collision=false`：仍限定该块的内部范围，但不能作为向外排斥布料的法线，避免相邻分块把裙摆反复推到两侧。

运行时默认每块最多使用 96 个外皮源面方向，仍对全部蒙皮支持点求支撑距离，保持覆盖；人工切面和主骨骼局部包围盒继续限定 membership。布局按参考资产确定，准备过程、子步与 CCD 保持相同面索引。`MODEL_CLOTH_CONVEX_PLANES=0` 使用完整源方向作为质量对照。方向约简是碰撞代理的近似，应通过完整覆盖、外露皮肤间隙和实际动作接触误差验收，不能以减少布料粒子、步数或迭代代替。

碰撞计算另使用不改变离散查询结果的优化：完整支持点云与固定 CCD 样本一次打包为 double SIMD 数据；共享采样网格的包围盒并集先排除不可能命中的轨迹；三角面用单平面或两平面的非负组合证明分离。缓存每次对当前顶点重新验证，数值边界不确定时执行原查询；双平面证书每对每子步最多搜索一次，内存有上限。采用不同自适应采样网格时不能复用旧网格的包围盒。

`MODEL_CLOTH_CONVEX_SIMD=0`、`MODEL_CLOTH_CCD_SAMPLE_BOUNDS=0`、`MODEL_CLOTH_SEPARATING_CACHE=0`、`MODEL_CLOTH_COMPOSITE_CACHE=0` 可分别关闭对应优化进行对照。单元检查比较所有粒子 double 位模式和非计时统计；固定姿态的 PROFILE 对照还保留相同接触、面积修复和边误差。更少的查询次数不能单独证明整个物理步已达到实时预算，应继续测量正常播放的实际步频和延迟。

面积约束的参考面积，以及相邻三角形的有符号二面角和铰链连接，在模拟初始化时从参考网格派生，不必在 JSON 中重复保存。正常衣片拓扑使用二面角弯曲约束；历史 `bend` 距离边仅作为缺少铰链时的回退，旧版“对边顶点距离弹簧”不再是主要弯曲模型。参考角保留原有衣褶，面积约束帮助抑制三角形塌陷。

子步还使用上一有效面朝向建立有向面积下限，避免接触投影将局部三角形压成线或翻面。铰链角柔度与原距离弹簧的单位不同，编辑器为它提供独立调节范围。默认采用最多 16 次约束迭代，并按上述容差判断收敛。

默认 `MODEL_OPTIMIZE_PREVIEW=ON`：Clang 的 Debug 配置对 Model 导入、骨骼、渲染资源、预览器和其 ImGui 目标启用优化，并显式恢复函数内联。只追加 `-O2` 不会撤销 clang-cl 从 `/Ob0` 生成的 `-fno-inline`，会使 GLM 数学热路径仍然很慢。该选项保留 `/MDd`、STL 调试 ABI 和调试符号，不改变其他模块的构建选项，也不降低布料粒子数、子步或迭代。需要逐语句调试时可设置 `-DMODEL_OPTIMIZE_PREVIEW=OFF` 后重新构建。

执行顺序为：动画序列采样 → 共用骨架姿态和表情权重 → 主体/衣片分别 CPU 蒙皮 → 固定点读取衣片蒙皮位置，自由粒子读取实际附着环拟合后的参考形状 → 更新身体碰撞代理的蒙皮支持点及外表面包络 → 保留速度并按固定步长积分重力和风 → 求解 XPBD 伸展、面积、二面角、tether、Max Distance、弱动画驱动与碰撞 → 回写独立衣片 → 通过现有 Model/Render/Toon 管线绘制主体与衣片。这样自由下摆不会直接被大腿或裙骨动画目标拉走；腿部通过碰撞和摩擦与衣片交互。布料计算不会修改已经烘焙的 VMD 动画资产。

全局点阻尼降低整体速度；局部阻尼围绕服装的质量中心、整体线速度和角速度抑制相对运动，以减少抖动并保留整体惯性。动画阻尼只作用于驱动区域，抑制相对目标速度。人体碰撞同时检查粒子与三角面，并对快速运动进行时间采样；凸代理运动保留主骨骼旋转弧线和混合蒙皮形变。只检查顶点会漏掉腿穿过较大三角形中部的情况。

固定腰缝与人体之间可能存在源资产本身的穿入。真寻动作的独立检查中，3.5 秒的一个固定点已进入真实蒙皮身体约 5.65 mm；加上 3 mm 接触厚度，会形成约 8.65 mm 的不可同时满足约束。凸碰撞对固定缝邻接面使用局部运动学接触偏移：固定顶点保留源蒙皮位置，自由顶点保留完整厚度，三角面按重心坐标连续插值接触偏移。裁剪、接触点选择、连续碰撞和准备阶段使用同一规则，避免只在最终投影时缩小修正而漏检。偏移按当前碰撞体与固定目标计算，不写入动画，也不会更改身体或衣片的源蒙皮。

界面分别显示原始表面穿入、可求解接触残差与源腰缝/身体冲突。原始冲突仍报告；固定点严格保持附着，自由区域继续按原穿透阈值验收。判断稳定性时应同时看三者，不能把源绑定冲突报成已被物理解算消除。

### v3 稳定性验收

默认真寻资产的稳定性检查覆盖完整 9.2 秒动作的 553 个固定步、277 个动画关键姿态初始化，以及 18 个姿态各两次独立重置后的连续演算。重置集合包含 2.9、3.5、7.4 秒等极端姿态；每次重置保持原始 44 个固定点，并检查确定性、动态边误差、三角形塌陷和接触残差。实际 OpenGL 检查另覆盖播放、暂停后余摆、换装和异步快照。

完整动作检查的最大可求解表面接触残差为 **5.637 mm**，低于原有 8 mm 上限；最大自由粒子穿入约 **4.806 mm**，低于 5 mm 上限；塌陷三角形为 **0**。原始表面误差为 **8.646 mm**，对应单独报告的源腰缝冲突，不能解释为已经消除的穿透。日志见 `build/cloth_v3_resolvable_solver_validation.log`。这些是当前模型和动作的回归结果，其他服装与动作仍需同样检查。

碰撞拟合检查独立比较身体皮肤顶点与三角形中心，外露皮肤的代理间隙第 95 百分位阈值分别为：大腿 12 mm、小腿 13 mm、骨盆 16 mm，单样本上限 25 mm。被身体其他部分真实包住的皮肤通过双向射线和绕数独立确认并单独统计，不能直接丢弃超差样本。代理近似误差与配置的 3 mm 接触厚度是不同量，不能将全部实际衣物间隙宣称为 3 mm，也不能用接触厚度代替拟合验收。

显示法线按焊接后的模拟粒子累加，再映射回渲染顶点，使 UV 接缝两侧使用相同几何法线；切线仍按各自 UV 计算并重新正交化。这能避免几何位置连续、着色却产生断裂的假折痕。

模型源单位与 SI 模拟单位通过 `unitsPerMeter` 转换，重力以 m/s²、风以 m/s、碰撞厚度以米表示；几何、固定距离和胶囊尺寸按资产源单位保存。所有伸展连通分量必须有固定点，所有衣片顶点必须恰好有一个绑定，避免出现未约束岛或错误索引导致爆炸。

运行统计分别显示含动态粒子的边长绝对误差（最大值与第 95 百分位）、固定区域的源动画相对拉伸、顶点/三角表面的碰撞残余穿入、最大弯曲偏离和塌陷三角形数量。顶点误差为零不能证明三角表面没有穿透。固定点按源蒙皮运动，其边长变化不由布料求解器修正。`Target conflicts` 表示动画目标深入身体代理的程度超过 Max Distance；`pinned overlaps` 表示固定点进入身体代理。遇到这类不可同时满足的约束，应检查服装附着权重、源皮肤交叠和碰撞体配置。

读取器兼容 v1/v2 布料资产，并在保存时输出 v3。兼容读取不会自动重新细分、改变已保存的质量/驱动图或生成皮肤凸代理；获得新生成配置应从原始骨骼包重新生成服装。弯曲求解形式已经改变，旧柔度值也不能直接作为新铰链柔度的调参依据。

## 与 UE Chaos Cloth 的关系和当前范围

实现参考 Chaos Cloth 的**独立服装资产、模拟网格/渲染网格分离、固定点、Max Distance、Animation Drive、铰链弯曲、局部阻尼、tether 和骨骼碰撞体**工作方式，位置约束采用 XPBD 柔度形式。这是本项目的独立 CPU 实现，未集成 UE Chaos 源码或运行时。Epic 的文档将动画驱动定义为拉向动画/目标网格的约束；弯曲支持参考二面角，局部阻尼用于抑制相对运动而保留整体运动。

参考资料：

- [Epic：Panel Cloth Editor Overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/panel-cloth-editor-overview)
- [Epic：Panel Cloth Constraint Node Reference](https://dev.epicgames.com/community/learning/tutorials/eKwY/unreal-engine-panel-cloth-constraint-node-reference)
- [Epic：Clothing Tool Properties Reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/clothing-tool-in-unreal-engine---properties-reference)
- [Epic：Simulation Anim Drive Config](https://dev.epicgames.com/documentation/unreal-engine/node-reference/Dataflow/SimulationAnimDriveConfig?lang=en-US)
- [Epic：Simulation Bending Config](https://dev.epicgames.com/documentation/unreal-engine/node-reference/Dataflow/SimulationBendingConfig?lang=en-US)
- [Epic：Simulation Damping Config](https://dev.epicgames.com/documentation/unreal-engine/node-reference/Dataflow/SimulationDampingConfig?lang=en-US)
- [Epic：Simulation Max Distance Config](https://dev.epicgames.com/documentation/unreal-engine/node-reference/Dataflow/SimulationMaxDistanceConfig?lang=en-US)
- [Macklin、Müller、Chentanez：XPBD — Position-Based Simulation of Compliant Constrained Dynamics](https://matthias-research.github.io/pages/publications/XPBD.pdf)

当前实现以实时裙摆预览和换装资产结构为目标。它不提供完整 Chaos 的各向异性布料、缝合裁片编辑、多层衣物高质量连续碰撞、GPU 布料或通用服装自动适配。自碰撞目前仍为粒子间排斥，不包含完整的衣片点面/边边连续自碰撞；分块凸代理及兼容胶囊也不等同于精确人体三角网格碰撞。UV 缝焊接法线适合连续衣片，独立硬边、反向重叠面需要在资产处理时明确分开。线性细分增加求解分辨率，不会恢复源资产没有的几何细节。快速动作、极端穿插和复杂叠层需要调参及人工检查。

真寻示例的 VMD 首帧已抬腿，左腿全片保持明显抬起；完整播放检查应覆盖从初始化到连续动作的过程，暂停后继续演算用于检查余摆。是否自然及是否存在可见穿透，应结合视口序列、表面碰撞误差和固定区域检查判断，不能只凭“不同于蒙皮”的一个位移断言。现有 PMX 裙骨刚体/弹簧关节未直接等同于顶点布料约束；这里从实际衣片网格和蒙皮构建约束与碰撞资产。
