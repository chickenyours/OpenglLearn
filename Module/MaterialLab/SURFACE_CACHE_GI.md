# Surface Cache 与基础软件追踪 GI

2026-10-23 的架构预算优化、Release 实窗对照及实际射线审计见 [GI90_ARCHITECTURE.md](GI90_ARCHITECTURE.md)。持续运动尚未达到 90 FPS；原推荐启动器继续保留，新方案通过独立启动器显式选择。

本阶段在独立的 Screen Trace / Mesh SDF / Global SDF 实验之后，加入相机无关的表面光照缓存和单次漫反射反弹。当前默认采用每子格两个表面代表、按实际探针位置计算的 B2 连续核、像素到探针的屏幕深度可见性检查，以及受限的一阶方向重建。它是用于检查数据和光传输的基础版本，不是完整 UE Lumen。文中带日期的性能与质量表保留对应旧版本的记录，不能代替当前版本的验收。

## 历史：加入 Surface Cache 前的性能判断

当时判断现有路径可以继续承载基础 GI 的开发和验证，但不能视为所有视图均达到稳定 60 FPS。RTX 4060 Laptop、1280×800、优化后的 Debug 构建，在加入本阶段之前测得：

| 路径 | GPU ms | 含提交和等待的整帧 ms |
| --- | ---: | ---: |
| 模型与探针追踪 | 6.19 | 16.34 |
| Global SDF 总览 | 8.35 | 12.70 |
| Global SDF 与 HUD | 8.12 | 22.31 |
| Mesh SDF 可视化 | 31.53 | 35.91 |
| 移动 Clipmap | 5.02 | 51.73 |

移动时 Clipmap CPU 更新约 10.32 ms。原始记录在 `build/surface_cache_preflight_perf.log`；每个静态项目 6 帧，移动 4 帧，不含呈现、限帧或图像回读。整帧时间受提交、系统调度和 HUD 更新影响明显，不能把 GPU 时间的倒数作为交互帧率。此前的对照记录见 `SDF_CLIPMAP_PERFORMANCE.md`。这里的“可以继续”表示数据结构和正确性验证的基础可用，并非完整 GI 性能验收。

## 参考的本机 UE 源码

参考工程：`D:/UE5/engine/UE_5.7`，UE 5.7.1。

- `Engine/Source/Runtime/Renderer/Private/Lumen/LumenSceneData.h`：`FLumenSurfaceMipMap`、`FLumenCard`、`FLumenPageTableEntry`，分离卡片投影、页分配和实际图集。
- `Engine/Shaders/Private/Lumen/SurfaceCache/LumenSurfaceCacheSampling.ush`：`SampleLumenCard` 的投影、深度有效性和朝向校验；`EvaluateRayHitFromSurfaceCache` 读取命中表面的缓存光照。缺失覆盖的调试视图使用独立颜色。
- `Engine/Shaders/Private/Lumen/LumenSoftwareRayTracing.ush`：`ConeTraceMeshSDFsAndInterpolateFromCards` 将 Mesh SDF 命中转换为 MeshCards 查询；`EvaluateGlobalDistanceFieldHit` 通过 Global SDF 页中的候选对象寻找卡片。
- `Engine/Shaders/Private/Lumen/LumenScreenProbeGather.usf`：`CalculateUpsampleInterpolationWeights` 的几何兼容重建与自适应贡献，以及按接收面法线求值的方向积分。这里的实际位置 B2 核、每子格双表面代表和三点屏幕深度检查是本实验的适配，不是该函数的逐行移植。
- `Engine/Shaders/Private/Lumen/LumenScreenProbeTracingCommon.ush`：二维方向单元抖动。本版仍直接采样余弦半球，并在原 `E/PI` 之外拟合受限的一阶方向系数，没有照搬 UE 的完整球面方向表示、SH、重要性采样与过滤管线。
- `Engine/Shaders/Private/Lumen/LumenScreenProbeGather.usf::EvaluateDiffuse`、`LumenScreenSpaceBentNormal.ush`：区分探针方向光照与逐像素局部遮挡的职责。当前三点深度检查仅约束像素向探针借用光照的可见性，不等同于 UE 的 Short Range AO / GI、Bent Normal 或完整短程软件追踪。
- `Engine/Source/Runtime/Renderer/Private/Lumen/LumenScreenProbeGather.cpp:149–238,1759–1950`：独立于 TAA 的 Final Gather 历史、最大累积帧数、深度/法线拒绝和快速更新参数。UE 的 `MaxFramesAccumulated`、`DistanceThreshold`、`NormalThreshold`、`MaxRayDirections` 默认值分别为 10、0.01、45°、8。
- `Engine/Shaders/Private/Lumen/LumenScreenProbeGatherTemporal.usf:374–670`：`ScreenProbeTemporalReprojectionCS`，重投影历史的四个候选像素，先验证表面再合并历史，根据累计帧数控制新样本权重；快速更新区域可额外限制历史。
- `Engine/Source/Runtime/Renderer/Private/Lumen/LumenScreenProbeFiltering.cpp:18–45`：`SpatialFilterNumPasses=3`、`SpatialFilterHalfKernelSize=1`、`SpatialFilterMaxRadianceHitAngle=10` 度、`SpatialFilterPositionWeightScale=1000`。`Engine/Shaders/Private/Lumen/LumenScreenProbeFiltering.usf:84–105` 将抖动追踪散射到共同方向的 gather 单元；`292–454` 的 `GetFilterPositionWeight`、`GatherNeighborRadiance`、`ScreenProbeFilterGatherTracesCS` 在探针方向辐射率上过滤，通过相对深度、近处距离限制和共同锥方向的命中角度保护接触阴影。

此实现采用相同的职责拆分。UE 的自适应离线卡片生成、虚拟页分配与反馈、GPU 更新调度和多次反弹不在本版中。

## 2026-10-22：第十轮接收面追踪——分项归因、起点拆分与查缓存复用

本轮以第九轮mask12为控制：主探针单次原软件完整区间求交、受保护SC几何地址，加第八轮AS UPDATE和未使用上传剔除；保留静态、动态、接触阴影的原采样与历史策略。目标是分清此前约14.9ms接收面槽中的risk/mask、起点安全查询、GI方向求交及Surface Cache采样成本，再验证独立执行优化。实现、源生成检查、低层GPU契约及三项密集成本研究已完成：起点拆分、上下文查缓存复用及组合增加接收面槽成本，独立地址和既有软件接收面起点复测的区间跨零，均没有可靠净收益。继续保留第九轮query mask12控制，本轮四个receiver profile位全部关闭（receiver mask0），不推荐把本轮原型设为默认。最后mask8在修正测试guard的95F同程序正式批中取得模型事件循环89.8582→82.3217ms（均值下降8.387%）的正向点估计，224帧配对画质及150对PNG相同；但此前8区组密集mask8研究的接收面、GPU整帧及墙钟区间均跨零，尚未建立跨研究一致的净收益，不据单批结果默认开启。FE/5F05严格归因预检及最初正式性能的fixture错误均保留失败记录，未改写为通过。独立原始数据复核8,401项通过；最终程序、证据与选中源文件的封存清单见[本轮身份清单](../../build/gi_receiver_identity.json)。

### 当前调用结构、三个执行原型与既有软件起点复测

普通接收面GI方向的`SCNativeIntersect`在合法内部候选上只做一次硬件遍历和一个packed三角形严格谓词，随后返回；native miss、边界或无效候选才回退`SCIntersect`。它没有主探针旧路径那种必做的软件最近面认证，不能用第九轮bit8类比，声称每条接收面方向均删除了一次完整求交。source-anchor和outward-clearance仍是两条不同短查询，原融合版每选中像素只计算一次起点，并非每个方向重复。第九轮closest bounds复用的负结果及第八轮起点策略的跨零区间均保留；最后mask8只把已保留的receiver-only软件起点作为独立profile复测，不启用closest bounds或认证复用，不改造求交算法。

新增`contact.splitReceiverOrigins=false`（本轮mask bit1）把原`CTReceiverOrigin`放到独立全分辨率预处理，选择mask、新露出预算、几何有效性、source/clearance数学及失败unknown规则保持。RGBA32F的xyz保存未压缩FP32起点，w只在完整安全检查成功后为1；Gather读取该记录，失败不借用旧探针或历史认证为已知光。改变的是起点计算与方向循环的shader职责拆分，查询数量和采样方向未减少；可能改善编译器的寄存器/分支布局仍是待测假设，不能当成已证明原因。

split增加一个1280×800 RGBA32F目标，payload为16,384,000字节（15.625MiB），这是该图像的存储量，不是整机峰值VRAM或实际物理流量。第一项完成的成本研究按计划严格核对每帧增加1项draw、3项graphics descriptor分配，其余既定上传/AS/采样工作仍须匹配，未笼统取消资源计数检查。此前状态不一致的失败预检仍保留，不能被这项后续工作计数通过抹去。原融合Gather继续可选，split图像和program owner随实际路径pin，并保留尺寸变化与关闭路径的生命周期。

新增`contact.reuseLightingLookup=false`（本轮mask bit2）只作用于接收面Gather的`LookupGLSL(true)`。当前cache宽度为1024：对合法非负索引使用位运算地址，其他宽度和负诊断索引沿用旧除法/余数表达式；覆盖attribute、metadata和lighting访问。原最近anchor选择已经读到position/normal，新函数把选中的不可变texel值传给照明采样，消除随后对同一anchor的两个重复读取请求。card顺序、nearest选择、tap顺序/权重、深度层检查、black/unknown与最终求和不变。它不减少射线或query数量，也不把逻辑请求减少称为已测物理纹理事务减少。其他消费者继续用默认`LookupGLSL()`原raw GLSL。

由于上下文加地址原型包含两种变化，新增独立`contact.fastLightingAddressing=false`（本轮mask bit4）进一步隔离地址运算成本。`LookupGLSL(false,true)`只对原false payload的attribute/metadata/lighting地址使用相同1024/非负guarded shift-mask，原anchor函数、position/normal重复读取及调用参数保持；不携带额外anchor上下文值。`LookupGLSL(true,*)`仍优先采用原context加地址原型，其生成结果不变；两个参数均false继续原raw GLSL。旧两个负实验保留，不能把新地址单项的未来结果反写为旧原型收益。地址单项的低层合同及1,024帧密集成本研究已完成，但接收面/整帧/墙钟区间均跨零，未证明净收益。

本轮mask bit8是对既有`gi.receiverSoftwareOrigins=true`功能的新测试profile，不是新求交算法或新生产开关。只把接收面source-anchor与clearance改用原`SCIntersect`，两条不同线段仍保留；实际GI方向继续hardware contact，主探针origin策略不变。因此`surfaceCacheSoftwareReceiverOrigins`应为true，`surfaceCacheHardwareOrigins`仍可因主探针为true；不能解释成所有origin都关闭硬件，也不预计减少Gather的AS绑定。

三个新增执行开关、既有receiverSoftwareOrigins及`collectTraceTimings`、`collectTraceDiagnostics`和`traceDiagnosticPage`均保留关闭默认值；原启动器及第九轮完整控制路径继续保留。本轮mask与前几轮mask分别解释，不把相同bit编号误当作同一功能。

### 分项GPU时间与同帧查询分支诊断

可选分项时间在原`SurfaceCacheContactTrace`总槽内部标出risk、mask/扩展、origin预处理和Gather；融合Gather仍含起点工作，split的origin则单列。它们属于嵌套GPU范围，不可再与总槽相加，普通fence也包含GPU完成。诊断时间标记的控制与候选必须使用相同配置；正式墙钟另保留真实提交、等待和呈现范围。

查询分支诊断只改复制的GLSL，使用每fragment本地`uvec4[2]`，没有GPU原子或共享计数buffer。普通渲染及旧page0源不生成这些计数器，actual-N来自实际成功起点后的方向循环，不能拿配置预算替代。原七页独立重放接口保留，但三次跨帧状态对照未满足完整相等门槛，当前正式归因改用`contact.collectTraceDiagnostics=true`的同帧四MRT诊断；该设置必须同时启用sampling且page为0。

同一诊断fragment的attachment0保留原selected/actual/base/readable；其余三张RGBA32F分别编码下面的page1+2、page3+4、page5+6。每个scalar把相邻两个非负整数按`a+4096*b`编码，要求每项小于4096；最大为2²⁴−1，FP32精确表示。负值、非整数、非有限及溢出都输出无效sentinel并由解析器拒绝，未用clamp或丢低位掩盖错误。所有终态、起点与Gather计数在同一调用结束时写出，early return也保留已完成工作；不再把跨帧页签名当作同一次draw的原子快照。

四MRT在旧sampling之外增加三张1280×800 RGBA32F图像，payload合计49,152,000字节（46.875MiB），只在显式诊断路径分配。attachment0借用原sampling颜色，缓存按尺寸和颜色handle身份更新，并pin创建时的sampling owner，防止关闭诊断后resize往返复用旧附件。这个四MRT shader的分支、寄存器、输出与诊断工作均不同于普通Gather；它的运行时间或重放次数不计作成本研究样本，查询数量也不等同于GPU毫秒。正式成本继续使用无计数器/无四MRT的普通路径。

| page | RGBA含义 |
| --- | --- |
| 0 | 原sampling：selected、actual traced、base budget、readable |
| 1 | 方向终态：exhausted、completed miss、backface、finite cache-readable；cache-unknown由实际总量减这些终态 |
| 2 / 4 | 起点 / Gather查询低四项：实际native query、通过严格内部候选、native miss转SW、边界转SW |
| 3 / 5 | 起点 / Gather查询高四项：无效候选转SW、全部SCIntersect调用、origin认证调用、原范围重试 |
| 6 | 像素状态：mask选中、几何有效、起点成功、保留0 |

“通过内部候选”不是最终照明有效性；origin随后仍可能软件认证。认证数包括bounded认证及原区间重试，retry是其中子集；不能把这些计数简单相加称为独特射线。非法native primitive直接unknown，未归入“invalid转SW”；未分类差额必须单独解释。split诊断为了与融合版可比，额外重放原起点函数以收集origin页，再从实际预处理图像追方向；这些是排除计时的原安全函数重放计数，不能声称生产Gather又执行了一遍起点。

### 第一成本研究：两项原型及组合增加接收面成本

[第一四模式成本研究](../../build/gi_receiver_mrt_cost_cost-study_summary.md)使用主程序`557BAF2800A97B2FAF93F8D5E8A4EB61A67B503D1755F29245212A649AF10994`，退出0、运行前后hash一致；原始日志SHA为`1DA928B363564D830FFA58BC40AD77DF14C291632D5C2F20C8F51BD66FB36970`。Release实窗模型移动，8个配对区组，顺序`0,1,2,3,3,2,1,0`，每epoch32帧、每模式每区组64帧，共2,048次计时present（512/模式）。四组都保留第九轮mask12，仅起点拆分和上下文查缓存复用改变；新bit4尚未加入。分项时间标记两边启用，四MRT、计数器与回读关闭，不能混用另一次instrumented诊断的运行时间。

| 557B同批模型均值 | 接收面GPU ms | GPU整帧 ms | 事件循环 ms | 循环P95 ms |
| --- | ---: | ---: | ---: | ---: |
| 第九轮控制 | 13.512904 | 56.325454 | 96.140081 | 124.931000 |
| split，mask1 | 13.848998 | 56.455784 | 98.924600 | 125.547300 |
| context加地址，mask2 | 14.772970 | 58.065629 | 101.025125 | 131.672900 |
| 两项组合，mask3 | 14.019386 | 57.266335 | 99.876042 | 130.284300 |

| 候选减控制，8区组差值 | 接收面差 ms及95% t区间 | GPU整帧差 ms及95% t区间 | 墙钟差 ms及95% t区间 |
| --- | --- | --- | --- |
| split | +0.336094 [+0.065865, +0.606323] | +0.130330 [−0.895321, +1.155980] | +2.784519 [−1.550265, +7.119303] |
| context加地址 | +1.260066 [+0.784725, +1.735407] | +1.740175 [−0.092041, +3.572391] | +4.885044 [−0.643390, +10.413478] |
| 组合 | +0.506482 [+0.004449, +1.008515] | +0.940881 [−1.062338, +2.944101] | +3.735962 [−1.549047, +9.020970] |

控制的risk为0.193768ms、mask/扩展4.427290ms、融合Gather8.883094ms；split后的独立origin为3.904134ms、Gather5.469368ms。Gather局部减少3.413726ms不是净节省：拆出的origin还需执行，合计起点加方向工作不比原融合Gather更少，且增加目标与提交工作，完整接收面槽反而增加0.336094ms。上下文加地址原型的Gather增加约1.400412ms；尚无寄存器、占用率或编译器证据解释具体原因，不能把传参/缓存布局当成已证明根因。

三个接收面区间均在零以上，因此本批不支持将split、context复用或组合设为默认提速路径；组合下界仅0.004449ms，也不外推为所有设备与场景的稳定回退。所有GPU整帧及事件循环区间均跨零，不能声称整帧显著变慢或变快。51项区间为探索性边际t区间，未作多重比较修正；推断单位为8个连续配对区组，非512个独立帧。顺序平衡不等于随机化或锁频，热状态/时钟/非线性漂移仍可能违反独立性；嵌套GPU/host范围不相加，分项marker不是单条query timestamp。这是成本结果，尚非最终完整画质或默认推荐验收。

### 地址单项和既有软件起点复测：均未确认净收益

[地址单项成本](../../build/gi_receiver_address_cost_cost-study_summary.md)使用程序`C11AD9291C3715D1F88642E0FAE5E90D793FE55DECE039375791F2F82F95968D`，原日志SHA为`1A2EDF9B51FDCC168F229F4BBEF9071285E224873F76E4F81CBA55DBB785F32A`；[软件接收面起点成本](../../build/gi_receiver_software_origin_cost_cost-study_summary.md)使用`FE6650F5B04BEACA803F01D5EDF9C8ACE87D5D64E06D452AB4E3840E0AC97717`，原日志SHA为`45F2E394F8CAC827F14C84568169D235C98F2E5CFBE372D72F9A3DDBA63501A6`。两次身份分别记录退出0及运行前后hash一致，各1,024次计时present、512/模式、8个配对区组，顺序`0,1,1,0`。每次均在同一程序内部对照第九轮mask12，不将557B/C11/FE的绝对耗时跨批拼成前后对比。

| 独立同批模型均值 | 接收面GPU ms | GPU整帧 ms | 事件循环 ms |
| --- | ---: | ---: | ---: |
| C11控制→地址单项，mask4 | 13.379332→13.290724 | 56.081391→55.860737 | 80.019959→80.901525 |
| FE控制→软件接收面起点，mask8 | 15.670386→15.469136 | 65.737687→65.274448 | 99.832512→100.562695 |

| 候选减同批控制 | 接收面差 ms及95% t区间 | GPU整帧差 ms及95% t区间 | 墙钟差 ms及95% t区间 |
| --- | --- | --- | --- |
| mask4 | −0.088608 [−0.365476, +0.188260] | −0.220654 [−0.982230, +0.540922] | +0.881566 [−1.009913, +2.773045] |
| mask8 | −0.201250 [−0.801743, +0.399243] | −0.463239 [−1.720066, +0.793589] | +0.730183 [−5.493044, +6.953411] |

两次三项主要区间全部跨零。地址单项循环P95还由92.7230→100.4056ms，不能凭GPU均值小幅下降设为默认；软件起点不增加新的求交策略，也没有因少发native预查询就获得已确认帧率收益。每项研究17个探索性区间同样未做多重比较修正，连续8区组、非锁频、非随机化及嵌套范围等限制仍适用。源码调用数、逻辑fetch数和独立重放计数不等于实际GPU指令/事务或时间；本轮没有shader ISA、寄存器、占用率证据支持具体编译原因。当前继续第九轮query mask12和本轮receiver mask0，四种profile及mask3组合都保留为报告实验，不推荐额外默认开启。

### 既有软件起点合同的直接证据与适用边界

[已完成native-origin GPU合同](../../build/gi_receiver_address_backend_gpu.log)包含184项几何：以原`SCIntersect`为baseline，原`SCNativeOriginIntersect`的1,472个FP32输出通道逐位相同，覆盖128旋转掠射、32项远面掩盖近面、反向共面tie及2项部分耗尽。其实现位于`probe_native_contract.inl`的mode0/mode2逐bit比较；原certification-reuse helper也保留独立比较。另有30项receiver-origin、1,920通道合同，但它比较的是同helper内fused与prepass/load，不能冒称完整30项跨软件/native等价证明。地址-only新合同另以24场景×3宽度对照1,152个FP32通道、180个地址通道及24项独立期望通过，旧context合同继续保留。

184项已测一致仍不是全域定理：`SCIntersect`有16,384次visit和32项stack上限，旧certified native先缩短认证区间。大BVH完整软件遍历可能先访问大量远子树而耗尽，native近候选的bounded认证可能剪掉这些子树并完成；`CTReceiverOrigin`遇exhausted直接unknown，因此两种policy在此可能改变launch结果。现有两项部分耗尽用例不排除所有大树排列。无环中位树、节点总数不超过16,384且stack深度安全、有限运算及AS/源geometry一致是可用的完整遍历边界，现有receiver-only功能没有强制新增该资格限制。source/clearance实际均以anyHit=false调用；不能把本合同推广到anyHit任意首hit、malformed AS或所有输入。最后完整质量仍须严格gate，结论限于已测场景，未放宽unknown、接触或运动门槛。

### 完整同帧MRT分支归因：普通方向回退不是主要数量

[同帧分支归因](../../build/gi_receiver_mrt_attribution_diagnostics_summary.md)也使用557B，完成身份退出0、前后hash一致。控制与mask3在各已测轨迹的实际方向数、可读数、unknown/exhausted及下列调用计数相同；每个case从64帧canonical状态重置，再走16帧轨迹后的一次四MRT诊断。source签名与所有页来自该同一帧，不再要求另一时间重放的mask等于本帧。

| 同帧独立重放轨迹，控制及mask3均相同 | actual / readable方向 | origin软件调用 | Gather软件回退调用 |
| --- | ---: | ---: | ---: |
| 静止 | 760,128 / 760,128 | 190,084 | 28 |
| 相机移动 | 1,960,000 / 1,960,000 | 239,408 | 57 |
| 模型移动 | 3,840,248 / 3,840,248 | 393,069 | 131 |
| 新露出 | 5,942,848 / 5,942,848 | 484,722 | 181 |
| 灯光变化 | 756,800 / 756,800 | 189,252 | 31 |

本组所有轨迹的cache-unknown与exhausted方向均为0。模型普通GI方向仅131 / 3,840,248（约0.00341%）进入软件回退，不能把约14.9ms或本批13.512904ms整个接收面槽归因于大量方向回退。origin的393,069是source-anchor/clearance中的软件调用，含fallback、认证及重试，并非393,069条独特GI方向；不能与Gather回退相加后除actual当作一个回退率。即使生产启用split，origin页仍额外执行原安全函数进行可比较的独立重放，不表示生产Gather重复这些调用。这些是instrumented重放中的计数，不能直接视为普通shader指令数、采样性能或各阶段耗时占比，也不证明最终照明质量。

### 跨帧与严格预检失败保留，根因未定

三次失败分别保留在[最初跨页重放](../../build/gi_receiver_diagnostic_replay_failure.log)、[canonical预检](../../build/gi_receiver_canonical_focus_diagnostics.log)和[normalized重放](../../build/gi_receiver_normalized_focus_diagnostics.log)。它们都因geometry/mask/fresh状态的完整相等门槛未通过而拒绝，不作为已完成的跨页归因数据；部分较早轨迹的整数计数通过，不代表整批完成。normalized新露出失败记录中positions、normals、fresh及若干阶段/generation签名相同，但mask hash不同；canonical模型预检也出现mask不一致。原始hash和失败身份继续保留，不能删除不一致项再拼成通过。

目前尚未把这些差异归因于时域状态、GPU不确定性、编译布局、某项新优化或具体同步缺陷；相关因素仍是待查假设，不是结论。同帧四MRT是避免跨帧拼接的测量设计，不是已证明找到了差异根因或修复了GI瑕疵。失败预检中可见split的+1 draw/+3 descriptor观测，但完整状态门槛失败，仍不充当最终资源/画质验收通过。

### FE与5F05严格预检明确失败：首个已定位差异在探针world Y

[FE最后归因预检日志](../../build/gi_receiver_final_diagnostics.log)及其[身份记录](../../build/gi_receiver_final_diagnostics_identity.json)以`FE6650F5B04BEACA803F01D5EDF9C8ACE87D5D64E06D452AB4E3840E0AC97717`运行，本次退出1，前后程序hash相同；模型预热后的geometry/mask/fresh严格相等门槛失败。它不是最后mask8归因通过，也不能用其中相同的计数或阈值分类替代原状态hash门槛。

随后只增加只读CPU状态证据的5F05程序`5F05ED23B361E17F22748D428FAB7F7A27885E3BE077ABBD4CCA25F373C6B3E9`也[退出1](../../build/gi_receiver_state_probe_diagnostics_identity.json)。[独立核查](../../build/gi_receiver_state_probe_independent_evidence.json)明确标记`UNACCEPTED_STRICT_PREFLIGHT_FAILURE_EVIDENCE`，未声明画质或性能通过。五个profile 0/1/2/3/8的initial与final placement常量各400字节、100个word逐位完全相同，包含全部矩阵；phase均0，base seed0、ray seed16、实际GI sequence16、placement image1。前后CPU舍入模式记录均0、MXCSR记录值均8101，未修改浮点环境。它们验证CPU准备上传的内容，不是shader端UBO回读，不能据此直接断定GPU实际读取相同字节。

5F05中profile1/2与控制的探针记录精确相同，profile3/8与控制的首个已定位差异在uniform及spawned的world Y；X/Z、法线及其depth逐位相同，W有派生strength/state变化。下表是每个差异profile相对控制的通道统计，不是把两个profile累计为独立样本。

| 探针图像 | Y不同条目 | Y最大绝对差，世界单位 | W不同条目 | W最大绝对差 |
| --- | ---: | ---: | ---: | ---: |
| uniform，1,000条目 | 292 | 2.38418579e−7 | 43 | 5.36441803e−6 |
| spawned，8,000条目 | 593 | 4.76837158e−7 | 1,026 | 0.00226020813 |

[矩阵乘法数值复现](../../build/gi_receiver_state_probe_matvec_reproduction.json)以精确有理数计算乘积/和，再按IEEE32最近偶数舍入。对uniform源像素(32,0)和spawned源像素(23,7)，先合并已舍入x/y乘积、将z项与其用FMA计算、最后加translation能重现reference；z先单独乘再相加能重现actual。这个结果只说明两种求值/收缩方式可产生所见world-Y值，尚无shader端UBO、SPIR-V实际执行路径或GPU指令证据，不能确认具体编译器/驱动原因，也不能将其作为接受失败预检的理由。

[Khronos Vulkan Invariance Rule 4](https://docs.vulkan.org/spec/latest/appendices/invariance.html)对相同SPIR-V、相同pipeline状态和相同Vulkan状态/输入的重复执行要求相同结果，shader副作用另有规则；因此不能笼统把“同pipeline同输入不稳”说成允许浮点误差。当前CPU相等证据尚不足以证明GPU侧满足全部前提，也不足以宣称驱动违反该规则。[GLSL precise说明](https://docs.vulkan.org/glsl/latest/chapters/variables.html#the-precise-qualifier)解释无qualifier时表达式优化可改变求值方式，`precise`约束相关运算的顺序/一致性并阻止隐式乘加收缩；这种跨求值方式的说明不取消上述重复一致性要求。

精度提案只保存在build实验文件并做离线shader编译，未在GPU执行，也未修改Module内现有数学路径，不能列作已修复或已验收结果。后续5F05完整画质记录与正式性能断言失败的范围分列如下；receiver四位继续全部关闭，第九轮query mask12推荐不变。失败证据、严格相等门槛和此前所有负/不确定成本结果完整保留。

### 5F05完整画质通过，首次正式性能因测试断言错误拒绝

[5F05完整画质记录](../../build/gi_receiver_final_quality_summary.md)及[身份](../../build/gi_receiver_final_quality_identity.json)以同一5F05程序退出0，224个配对帧、60项实测ROI及20项未测canonical槽完成；已记录RGB、接触对比、历史残留和梯度差异均0，coverage逐项相同。独立[quality-only PNG核查](../../build/gi_receiver_final_quality_independent_images.json)完成108/108对精确相同，其中54对候选/控制、54对第九轮归档控制；本次没有读取或验收性能PNG。它证明这批已测画质与对照相同，不是路径追踪物理真值，也不会把此前严格状态预检失败改写为通过。

随后[首次正式性能日志](../../build/gi_receiver_final_present.log)及[身份](../../build/gi_receiver_final_present_identity.json)在完成640次计时present、打印10项轨迹/模式聚合后退出1，报“accepted resource/origin policies”断言失败。已查明本次是fixture预期错误：receiver协议仍要求`softwareReceiverOriginFrames`为0，而mask8候选按设计在64帧都使用软件接收面起点，实际64正确；先前bit8预期修改误放在另一QueryReuse协议guard。控制仍应0、mask8候选应64。原失败日志、GPU遥测及身份已另存于[guard遗漏失败清单](../../build/gi_receiver_guard_omission_failure_manifest.json)，不将其称为生产GI policy失效，也不从这批未通过完整协议的聚合值宣布最终提速或回退。

修正范围是两处对应协议的预期计数，未修改生产origin选择、unknown处理、采样或画质门槛，也不是删除资源检查。修正后的95F程序已独立重跑下述完整质量、正式性能及合并PNG；旧5F05质量通过仍只归属于原5F05身份，没有直接移作新程序验收。既有密集成本研究的负/不确定结论及第九轮默认推荐保留。

### 最终95F同程序正式对照：本批改善，跨批稳定收益仍未确认

修正guard后的程序SHA为`95F2C4E4B09034DD277E98140FEDF6486DC17F38195FC6071180DA6C7DBE56CA`。[正式性能](../../build/gi_receiver_guard_final_present_summary.md)及[身份](../../build/gi_receiver_guard_final_present_identity.json)退出0、前后hash相同，完成所有计时及后续42张HD预览输出；原日志SHA为`9EC35484371EEDDE764F62F3E65DEBDA2969D2F233B0A3D0B3CBAF10F6800FC2`。Release_NDEBUG、1280×800真实窗口present、不限应用帧率、不绘HUD；同程序内部按0,1,1,0顺序，每trial每轨迹32帧，两trial/模式，因此每轨迹每模式64帧、五轨迹合计640次计时present。预热、HD预览、图像回读和CPU几何oracle不进入FPS样本；细分receiver marker和四MRT插桩均关闭。

| 95F同批控制→软件接收面起点mask8 | 接收面GPU均值 ms | GPU整帧均值 ms | 事件循环均值 ms | 循环均值换算FPS |
| --- | ---: | ---: | ---: | ---: |
| 静止 | 11.1049→10.3769 | 50.7591→48.1169 | 55.8553→53.1124 | 17.903→18.828 |
| 相机移动 | 15.5876→14.9258 | 58.5688→53.1054 | 63.5993→57.8814 | 15.723→17.277 |
| 模型移动 | 15.2827→14.2013 | 61.3017→56.4179 | 89.8582→82.3217 | 11.129→12.147 |
| 新露出 | 17.9254→17.2635 | 59.2925→57.0424 | 63.7823→61.8487 | 15.678→16.168 |
| 灯光变化 | 11.4479→10.3376 | 54.7509→51.7678 | 59.3711→56.4212 | 16.843→17.724 |

模型均值事件循环下降8.387103%，中位89.0519→82.6567ms、P95 101.8400→87.4531ms，是本批观察到的正向点估计，不能因前面研究不显著而隐去。GPU整帧下降4.8838ms，接收面下降1.0814ms；其他未改阶段也同时变化，不能把整帧差额都归为少做native起点预查询。FPS是1000/事件循环均值，含提交、等待与present，不是GPU毫秒倒数或实际屏幕扫描频率。

这批每模式只有两次trial、每轨迹64帧，未计算置信区间、未锁频或随机化；此前独立8配对区组密集mask8研究的接收面差−0.201250ms、GPU差−0.463239ms及墙钟差+0.730183ms的区间都跨零。两项研究回答的采样范围不同，不能把95F点估计宣称为已确认的普遍8.4%加速，也不能反过来说所有正式批均失败或毫无改善。当前不额外默认启用mask8，更不推荐已增成本的split/context原型；query mask12、receiver mask0保持。

正式guard对十项轨迹/模式聚合检查通过：mask8的software receiver origin计数严格为每组64，控制0；主探针origin、hardware contact/cache、probe射线预算、历史/过滤、上传、AS、draw和descriptor等原计划计数继续匹配。两边不启用split、context、独立lighting地址或closest bounds复用；主探针保留direct软件refinement，不能将原SDF计时槽误称为纯SDF/GDF marching或纯硬件query时间。

[95F完整画质](../../build/gi_receiver_guard_final_quality_summary.md)及[身份](../../build/gi_receiver_guard_final_quality_identity.json)另行退出0，224个配对帧、60项实测ROI及20项未测canonical槽；RGB、接触对比、历史残留及梯度差均0，geometry metadata和receiver coverage的28,672,000/28,672,000项完全一致。black/unknown各256,000为四个view/variant记录的聚合；直接阴影224个签名相同，并对14个完整checkpoint的88,080,384个depth像素逐位核查。独立双精度三角形oracle检查130,182条已覆盖类别射线，无miss、最大端点误差2.11712e−5m；它在设定screen/mesh/GDF有效覆盖内抽样，不是所有射线或无偏物理照明真值。ROI重叠和配对类别不能累计为独立观测。

[主PNG检查](../../build/gi_receiver_guard_final_image_comparison.json)及[独立PNG解码核查](../../build/gi_receiver_guard_final_independent_images.json)均150/150对精确相同，共225张唯一图像：75对当前A/B，加75对第九轮归档控制；四份quality/performance身份、当前及归档hash关系核对通过。Root另人工查看frame88接触阴影tone4和frame112总图LDR，检查范围是已测画质保持。上述结果不外推为所有场景、所有设备或物理真值。[独立原始数据复核](../../build/gi_receiver_guard_final_independent_review.json)及[日志](../../build/gi_receiver_guard_final_independent_review.log)最终8,401项协议/算术检查通过，绑定相同95F程序与原性能日志SHA，核对640次ABBA计时present、九项host计数及工作相等关系，除预先声明的receiver origin policy差异外保留严格计数。时间、资源、质量与主解析器结果一致。独立分析初稿曾把shadow峰值和协议flag当作可加计数，产生分析断言失败；最终按峰值取max、计数求和、tag保持常量修正，未改原始日志或生产路径，不是放宽GPU工作检查。最终程序、证据及65份选中源文件见[身份/源封存清单](../../build/gi_receiver_identity.json)；源捕获只能证明捕获时选中文件的内容，不能代替编译器输入溯源或可复现构建证明。

### UE参照及保持的语义边界

本机UE5.7.1 `LumenScreenProbeTracing.usf:433–497`只把未完成且仍需追踪的射线压紧到后续列表，`LumenScreenProbeHardwareRayTracing.usf:74–80`按列表调度；`146–158`从未完成TMin续追并保留normal bias。`LumenHardwareRayTracingCommon.ush:486–506`消费inline查询结果，`608–639`按命中instance定位MeshCards并执行Surface Cache采样。这里借鉴按职责拆分、避免重复取数和先归因实际工作，不宣称已移植UE整套compute压紧、SER或材质命中管线。UE这几处没有本工程额外的原软件BVH安全认证层，不能据此删除source/clearance、扩大允许漏光、以少射线替代执行优化。

### 下一候选仅为方案：优先压紧真实接收面工作并减少mask重复访问

已测557B控制的risk加mask/扩展约4.621058ms，其中mask/扩展4.427290ms，是值得单独处理的成本；普通GI方向仅131/3,840,248进入软件回退，继续只消除这些罕见fallback难以解释或消除整个接收面槽。下一轮先把mask分类与扩展分别计时，检查邻域分类中重复的geometry/probe读取；再参考UE对eligible且unresolved工作的压紧调度，构建通过mask、几何与起点有效性检查的真实接收面列表，只为其中仍需的方向提交追踪。保留每像素原seed、方向索引、累加顺序、source/clearance安全语义及unknown规则，同时计入列表构建、存储、压紧与调度成本，用旧融合路径对照。该队列/调度方案本轮尚未实现或测量，不能据此承诺帧率；现有query mask12和receiver四位关闭的默认选择保持。

### 已通过的实现检查与保留的失败

[源生成检查](../../build/gi_contact_source_check.json)确认128项旧Gather payload与第九轮归档一致，7,168项新Gather参数/page组合、32项origin及512项四MRT组合生成成功，普通/旧page0无计数器；这是CPU源检查，不是7,168个GPU场景。[原Lookup/native源检查](../../build/gi_receiver_lookup_source_check.json)确认默认Lookup raw GLSL与旧native builder保留。

[低层GPU契约](../../build/gi_receiver_backend_gpu.log)以独立测试程序`91BEEF6D6A7473B64370BAE68F64BE4C0D7A069F9CA4E9BD4CC3168542C022DF`完成，validation开启、退出0、运行前后hash一致；身份保存在同名`_identity.json`，不能与后续主程序性能身份混用。查缓存24场景、width1024/17/257的1,152项FP32通道一致、180项精确拆分地址及48项独立期望通过，覆盖nearest/filtered、black/unknown、缺页、clamp、anchor标签、非有限光照和负索引不fetch。origin30场景×8种helper/fresh设置的1,920项FP32通道一致，88项有效、152项unknown，验证实际可选Gather从RGBA32F预处理读取。查询插桩在4种helper×184项几何上比较5,888项原输出及5,888项counter通道，通过简单命中/miss计数与认证关系检查。原第九轮求交、原点和AS UPDATE契约继续通过；这些有限几何输出检查尚不代替整段动态画质和实窗性能。

[四MRT真实GPU契约](../../build/gi_receiver_mrt_backend_gpu.log)另以`D8BAD6B13B0D0CD14E9692EB1DF80BF9A5D19CD3BE314C93FD79C3CCF830D6FD`程序退出0，validation开启且身份前后hash不变。132项packing场景检查1,584个FP32 packed通道、2,928个解码整数通道，120项无效计数对被拒绝；覆盖4095边界、最大24位整数、负数/小数/NaN/Inf/溢出，attachment0保持原page0。它验证真实四附件写入与编码，尚不等于完整renderer分支归因或普通Gather性能通过。[同阶段CPU 3/3](../../build/gi_receiver_mrt_cpu.log)及[frame-command检查](../../build/gi_receiver_mrt_frame_cpu.log)通过，独立程序身份与后续成本主程序分开保留。

[主协议21项](../../build/gi_receiver_parser_test.log)、[成本协议21项](../../build/gi_receiver_cost_parser_test.log)、[分支协议27项](../../build/gi_receiver_diagnostics_parser_test.log)，合计69项CPU合成测试通过；它们检查身份、样本、工作差异和诊断协议拒绝，不能充当渲染或性能证据。一次[构建文件锁失败](../../build/gi_receiver_build_test_lock_failure.log)保留：在CMake重链接`render_target_test.exe`时并行运行该CPU测试，使链接器写输出报`permission denied`。停止该并行测试、串行重跑后完成，没有为此修改生产源码或放宽门槛。

<!-- ROUND10_FINAL_VALIDATION: 95F corrected-guard formal performance, 224-pair quality and 150-pair primary/independent PNG PASS. Positive formal model-loop point estimate -8.387103 percent; denser 8-block mask8 intervals cross zero, so no consistent net-benefit/default-enable claim. Retain Round9 query mask12 and receiver mask0. Preserve all failed diagnostics and the 5F05 fixture-guard omission. Independent raw review PASS 8401 checks; frozen source/evidence identity manifest is gi_receiver_identity.json. -->
本轮完成执行原型、低层合同、69项协议合成测试、三项密集成本研究、95F同程序正式性能及完整数值/PNG质量对照。split/context增加接收面成本，地址单项和密集软件起点复测区间跨零；95F最后批的模型循环均值下降8.387%，但不足以建立跨研究稳定净收益，因此默认仍为第九轮query mask12、receiver四位关闭。所有新旧路径及失败记录保留，下一步优先减少mask重复访问并研究eligible/unresolved真实工作调度；该方案未实现、不承诺FPS。独立原始数据复核8,401项通过，最终身份/源/证据由本轮清单绑定；第九轮及更早记录完整保留。

## 2026-10-22：第九轮追踪执行优化——消除重复求交与复用几何查询数据

最终推荐mask12：主探针一次原软件完整区间求交，加受保护的SC几何地址。最终同程序Release实窗模型GPU62.0279→60.1896ms（减少2.96%），事件循环93.5267→89.8913ms（减少3.89%，10.69→11.12 FPS）；收益有限，模型事件循环median及GPU整帧P95还有回退，不能宣称每帧稳定加速。224对帧数值画质通过，已测RGB、接触对比及残影差值为0；150对独立PNG比较解码像素相同，包含75对本轮A/B与75对第八轮归档控制检查，结论限于已测视角、轨迹和检查点。

本轮保留原路径及独立实验开关：候选提示复用和closest bounds复用没有得到稳定整体收益，后者增加接收面成本，均保留为负结果。第八轮AS UPDATE、上传剔除、原起点策略与全部采样/历史/接触权重继续保留。历史约57.3ms只作为起点；四组ABBA及两项密集研究各有独立程序身份，不能跨批拼接或把局部节省相加。新增启动器显式启用mask12，旧启动器和API默认false设置保留，便于技术报告对照；仍未达到60/90/120 FPS。

### 实际重复工作与保留的安全要求

`ContactGatherShader`中的`CTReceiverOrigin`原本就位于射线batch循环之外：每个需要补追的接收像素计算一次source anchor和outward clearance，之后所有方向共享该起点。本轮没有新增所谓“把逐射线起点计算移到循环外”的优化，也不通过删除两项安全查询或借用不同表面的探针结果降低工作量。采样诊断的重放有意独立于正式计时，不能把去掉诊断重放算作普通帧加速。

主探针`PSRefine`在硬件候选上以原packed-float三角形重新计算严格交点；随后仍需原软件BVH认证最近面，认证抵达同一三角形时会再次读取三个顶点并重算交点/法线。新的`trace.sdf.reuseNativeCandidate`保留这一软件遍历，仅把已验证的候选ID、距离和法线作为不可变提示传入独立函数。只有原遍历实际到达该三角形的叶循环，且该距离仍满足当时的闭区间上限时才复用；不会预先设置命中、缩小原层级包围盒或改变访问顺序。近处掠射面被远候选遮蔽、候选未被原BVH bounds接受、native miss/边缘/端点、未知及遍历耗尽仍沿用原回退。非有限候选提示也使用原认证函数。

逻辑`fetches`计数仍在候选叶处增加原来的三次请求，使既有射线元数据和统计可比；这不是物理纹理读取次数。新统计`probeSdfNativeCandidateReused`表示本帧实际选中的程序允许此复用，不是每条射线确实消费提示的动态计数。原`NativeRefineGLSL`和两项软件遍历文本保留，新variant从它们派生，关闭开关时保留原生成来源。

接收面与探针安全起点共用的closest-hit软件查询另有父节点测试子bounds后、立即访问子节点时再次测试/读取同一数据的开销。`reuseClosestTraversal`实验只复用立即访问子节点的已知数据，较远兄弟在可能缩短的最大距离下仍须重新检查；原最近面/tie顺序、visit/stack耗尽、端点和三角形谓词不变。`fastGeometryAddressing`独立实验对width1024且合法非负索引的SC几何地址采用位运算，其余布局保留原有除法/取余。两项不改变Surface Cache页、追踪距离、方向数、接触权重或历史响应。

在上述数据复用之外，新增`trace.sdf.directSoftwareRefinement`（mask bit8）消除整项重复求交。原native-format/MRT路径为硬件候选、原packed三角形严格验证，再以原软件BVH认证最近面；direct路径在相同完整`[start,finish]`区间只调用一次原`PSSoftwareRefine`。它保留原三角形谓词、层级访问/tie顺序、边界、未知及耗尽处理；与原native profile一样绕过SDF/GDF步进，没有再以距离场命中后补追。新direct `PSRefine`不声明主探针AS/ray query，也不创建或使用native候选提示；起点、接收面和Surface Cache可见性等其它消费者继续使用原硬件路径。

native程序缓存加入独立bit8并扩展为16项，API末尾布尔参数默认false。direct优先于候选提示，实际统计为`probeSdfDirectSoftwareRefinement=true`、`probeSdfHardwareRayTracing=false`、`probeSdfNativeCandidateReused=false`，即使外部请求的hardware设置仍为true。内部triangle-profile资格和共享几何owner继续保留：现阶段仍要求原硬件能力、匹配Surface Cache场景、fragment/MRT及原非GS/非compact条件；不具资格时使用原回退。GPU距离场镜像上传剔除也只在实际不采样这些镜像的profile上启用，不能把requested开关当作实际执行证明。

新增开关默认均为false，第八轮三个AS模式、上传/起点策略及原shader入口继续保留。native主探针程序缓存把候选复用加入独立key，GS、compaction、无硬件能力或场景不匹配仍走原路径；接收面normal/fresh/diagnostic及probe-origin程序分别缓存并pin实际owner。请求设置、实际选中程序和动态query数量是不同概念，后续结果按各自范围披露。

### UE依据与实现差异

本机UE5.7.1的`Engine/Shaders/Private/Lumen/LumenScreenProbeHardwareRayTracing.usf:146–158`用既有trace hit距离设置`Ray.TMin`，只在未完成线段上回退少量pullback并应用normal bias；`LumenHardwareRayTracingCommon.ush:486–493`消费`TraceRayInlineWithCallback`的结果。本实验的保守扩张三角形及既有软件谓词有不同数值要求：不能直接删掉认证而宣称移植UE。候选提示实验只复用当前射线已取得、且仍经原层级访问验证的计算结果；direct实验则保留原软件认证并删除前置硬件候选查询。二者均不共享不同接收点、不同方向或过期模型的命中。direct是本工程针对双重认证成本的实现选择，并非宣称UE采用相同软件BVH路径。

### 已完成的低层检查与保留的测试失败

[低层GPU契约](../../build/gi_query_reuse_backend_gpu.log)通过：主探针候选复用在184个几何场景、两种safe-origin设置上，对原软件4,416个FP32通道及原native另4,416个通道精确一致，736个逻辑fetch计数通道一致；覆盖20项独立简单几何期望、128项旋转掠射、32项远面掩盖近面、两种反向共面tie及两项部分耗尽。共享closest复用另有115场景、两种any-hit模式、width1024/17和四个variant，11,040个FP32输出通道一致、116个受保护地址通道通过；568次fixture draw的逻辑请求减少说明缓存分支实际被覆盖，但不是物理GPU读计数或568个独立性能样本。[CPU 3/3](../../build/gi_query_reuse_cpu.log)及[frame-command检查](../../build/gi_query_reuse_frame_cpu.log)通过。

[最初地址oracle失败](../../build/gi_query_reuse_backend_gpu_initial_address_oracle_failure.log)保留：该fixture把负索引的GLSL余数直接当成C++ `%` 数学结果，触发“Guarded geometry addressing changed signed div/mod result”。后续只修正测试oracle；非负有效地址仍与CPU精确比较，负索引不发出越界texture fetch、记录旧GPU表达式结果，并继续约束商及guard。没有为此修改生产地址表达式、求交规则或放宽有效图像/几何门槛。旧未认证native-origin helper的9项历史诊断差异仍单列，未改为正确性参照。

[direct低层GPU契约](../../build/gi_query_reuse_direct_backend_gpu.log)另已通过：同一184项场景、两种safe-origin，对原软件及原native各4,416个FP32输出通道精确一致，736个逻辑fetch通道与一次完整区间软件求交一致。该逻辑计数不要求等于旧native的“候选加认证”总请求；fixture还验证direct源码无硬件query声明、无SDF/GDF march，每次refine只有一次原软件完整区间调用。以上是已测几何契约，不替代最终动态画质回归。

### 四组ABBA实窗计时：保留混合方向与负结果

以下四组均由同一主程序`1AE33556AE0F8B650EDB0CBF3EBB8E627EC08772B6D28014D396E409D9CA5850`完成，各自的完成身份记录退出0及运行前后hash不变。控制组为已验收的第八轮设置；AS UPDATE和上传精简两边都开启，仅候选分别启用本轮选项。Release、RTX4060 Laptop、1280×800、五轨迹ABBA，每模式每轨迹64个计时样本、每组640次计时present；cold、轨迹预热和HD截图不计入FPS。表中是整帧事件循环均值ms，完整median/P95、阶段GPU、CPU/上传工作量和source-log SHA保留在各行链接及同名JSON中。不同批次的控制耗时差别很大，不能跨行拼接“优化前后”、把收益相加或只选最快候选充当最后结果。

| 实验 | 静止 ms | 相机移动 ms | 模型移动 ms | 新露出 ms | 灯光变化 ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| [三项组合，mask7](../../build/gi_query_reuse_combined_present_summary.md) | 66.25→73.65 | 73.78→72.52 | 109.86→100.01 | 73.37→82.74 | 72.60→69.88 |
| [候选复用，mask1](../../build/gi_query_reuse_candidate_present_summary.md) | 79.04→56.86 | 92.60→62.69 | 126.53→94.41 | 91.89→69.26 | 85.98→54.19 |
| [closest bounds复用，mask2](../../build/gi_query_reuse_bounds_present_summary.md) | 49.40→49.87 | 52.67→55.02 | 76.74→89.00 | 54.24→56.64 | 50.14→52.03 |
| [几何地址，mask4，clean](../../build/gi_query_reuse_addressing_clean_present_summary.md) | 44.29→42.97 | 48.28→48.77 | 69.86→68.92 | 49.81→47.90 | 46.40→45.61 |

| 同批模型阶段GPU均值 | GPU整帧 ms | 主探针共享追踪槽 ms | 接收面追踪槽 ms |
| --- | ---: | ---: | ---: |
| 三项组合，mask7 | 71.14110→71.33840 | 13.70480→13.29100 | 16.13880→17.74940 |
| 候选复用，mask1 | 95.66930→63.09450 | 14.33520→12.35480 | 17.12720→15.11970 |
| closest bounds复用，mask2 | 48.68270→50.26480 | 9.59794→9.72146 | 12.26650→13.79340 |
| 几何地址，mask4，clean | 46.04120→45.60470 | 9.22659→9.27450 | 11.64290→11.02050 |

mask7的模型事件循环虽然109.855→100.012ms，GPU整帧却71.1411→71.3384ms基本不变，接收面16.1388→17.7494ms回退；静止与新露出均值也分别回退11.17%和12.77%，因此不能推荐“三项全开就是更快”。mask2五条轨迹的事件循环均值全部回退，模型增加15.97%，接收面12.2665→13.7934ms，不能因源码少做一次bounds检查就宣称GPU提速。

mask1模型GPU95.6693→63.0945ms的巨大差异不具备仅归因于候选缓存的依据：此选项不修改screen trace、重建、空间过滤、接收面shader或时域过滤，但这些阶段在该批同时大幅变快。例如模型screen trace4.40354→2.09288ms、接收面17.1272→15.1197ms、空间过滤9.94782→6.64030ms、时域3.94613→1.89637ms。事件循环五轨迹24.63%–36.97%的下降也不能直接记为候选提示复用的固定收益。没有锁频；设备运行状态、调度和批内非线性漂移等因素尚未被隔离，这些是需要进一步检查的可能因素，不是已证明原因。源码上的重复谓词确实省去，不等于该批全部阶段的差异由它造成。

mask4的clean批结果较小：模型事件循环69.8568→68.9185ms（减少1.34%），接收面11.6429→11.0205ms，GPU整帧46.0412→45.6047ms；相机事件循环48.2763→48.7707ms反而增加1.02%。该组方向混合且没有ABBA置信区间，仍需密集交错归因和最终画质，暂不作统一默认提速结论。此前非clean地址运行记录也保留，不替换该行的同批控制或择优混合。

四组计时的原射线预算、采样计划、实际绘制和物理上传工作量均匹配；每组64个模型帧仍为36,864,000逻辑探针射线容量、161,480,704物理上传字节，两边都保持AS UPDATE，新增程序选择统计也与mask一致。共享`sdf_trace`在native模式包含严格原几何认证与cache查询，不是纯SDF/GDF march；接收面槽还包含risk/mask/gather，不能当作单条query的时间。普通fence和GPU执行范围嵌套，不能相加；事件循环FPS是1000除以事件循环墙钟均值，不是GPU倒数或显示器scanout。

### 四模式密集配对：候选与bounds负结果、地址的局部收益

[四模式研究](../../build/gi_query_reuse_query_cost_summary.md)由程序`F81385D132C62677EEC56C756EAB2C8BEFF21FAC42BB683515EBDF159E0A6E8E`完成，运行前后hash一致、退出0。Release实窗模型移动，8个配对区组，每区组顺序`0,1,2,3,3,2,1,0`，每epoch32帧、每模式每区组64帧，共2,048次计时present（512/模式）。所有模式保留相同第八轮采样、AS UPDATE、上传剔除和原起点策略；此批不含bit8。原始日志SHA为`4EDA7307AF1995A09A4FC945005619E04E40E6E3BEFD647EA93EDFE4233358E5`。

| 四模式同批均值 | 主探针kernel ms | 主探针总槽 ms | 接收面 ms | GPU整帧 ms | 事件循环 ms | 循环P95 ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 第八轮控制 | 9.90355 | 11.35249 | 14.98654 | 62.09875 | 108.86492 | 125.10090 |
| 候选提示，mask1 | 10.21693 | 11.61397 | 15.02112 | 62.33908 | 108.72252 | 127.70830 |
| closest bounds，mask2 | 10.07192 | 11.58263 | 16.50569 | 64.49789 | 111.14668 | 128.96920 |
| 几何地址，mask4 | 9.99305 | 11.43796 | 14.02732 | 60.82572 | 107.66391 | 124.27610 |

| 候选减控制，配对区组差值 | 均值差 ms | 95% t区间 ms | 本批结论 |
| --- | ---: | --- | --- |
| mask1，主探针kernel | +0.31339 | [−0.02266, +0.64943] | 跨零，未证明GPU改善 |
| mask1，GPU整帧 | +0.24034 | [−1.69648, +2.17715] | 跨零 |
| mask2，接收面 | +1.51915 | [+0.79105, +2.24725] | 该槽增加开销 |
| mask2，GPU整帧 | +2.39914 | [−1.27816, +6.07644] | 跨零 |
| mask4，接收面 | −0.95922 | [−1.45130, −0.46714] | 该槽下降 |
| mask4，GPU整帧 | −1.27303 | [−3.90672, +1.36067] | 跨零，不能外推整帧收益 |
| mask4，事件循环 | −1.20101 | [−4.20261, +1.80060] | 跨零 |

因此没有把mask1早期ABBA中95.6693→63.0945ms的GPU整帧下降作为候选缓存收益；更密集研究也没有复现这种幅度。mask2虽然减少逻辑bounds请求，接收面实际更慢。mask4保留为受保护地址实验，其接收面槽下降有本批区间支持，GPU整帧及墙钟改善仍未确定。上述结果不证明寄存器压力、占用率或编译器是具体原因；仍未采集对应低层证据。

### 单次原软件求交的密集配对：主探针实际下降

[direct两模式研究](../../build/gi_query_reuse_direct_cost_summary.md)使用程序`B1D1BD0528BDB47631AF069987519EF18BD3F7B7E7E143CC858F0F27A5264A84`，运行前后hash一致、退出0。模型移动，8个配对区组，顺序`0,1,1,0`、每epoch32帧，共1,024次计时present（512/模式）；仅改变主探针“硬件候选加软件认证”为“一次原软件完整区间求交”。其它三个本轮开关全部关闭，接收面、起点、AS UPDATE、上传剔除和采样预算不变。两边都不做SDF/GDF步进，不能把该批差值标成SDF march加速。原始日志SHA为`91D6CAC3182796B3F04C7D870D57D657657BDF15B3A28F8300928291FE53DB54`。

| direct同批模型指标 | 第八轮控制 ms | direct ms | 配对均值差 ms | 95% t区间 ms |
| --- | ---: | ---: | ---: | --- |
| 主探针refinement kernel | 9.93208 | 7.97320 | −1.95888 | [−2.12064, −1.79713] |
| 主探针总槽 | 10.83620 | 8.92753 | −1.90867 | [−2.18913, −1.62821] |
| 接收面 | 14.19288 | 14.09075 | −0.10213 | [−0.47447, +0.27021] |
| GPU整帧 | 55.34965 | 53.42737 | −1.92229 | [−2.69702, −1.14755] |
| 事件循环 | 83.08076 | 80.81802 | −2.26274 | [−5.83119, +1.30572] |

该批主探针kernel减少19.72%、GPU整帧减少3.47%，区间均在零以下；接收面没有已确认改变。事件循环均值对应12.04→12.37 FPS，但差值区间跨零，P95还由92.1540→93.0689ms，因此不能承诺稳定墙钟帧率收益。fence下降约1.999ms与GPU完成范围重叠，不能再加到GPU节省上。

两项研究的t区间都以8个区组差值为推断单位，不把连续512帧当独立样本；配对顺序只能平衡线性时间漂移，没有锁频或随机化。相邻区组的热状态、时钟、背景负载或非线性漂移可能破坏独立性。四模式39项、direct13项区间均为探索性边际区间，未做多重比较修正。screen/spatial等未改阶段用于诊断，不是受控频率归一化。1Hz整机遥测包含预热，仅能近似对齐epoch，未单独证明采集开销为零。两研究测执行成本，不代替图像验收，也不能跨二进制/批次相加局部节省。

### 最终组合与保留的失败记录

依据上述结果，最终组合选择mask12，即bit8单次原软件主探针求交加bit4受保护几何地址；候选提示bit1和closest bounds bit2保留旧实验入口、默认false，不推荐纳入该组合。独立API开关仍默认false，原native候选认证、原软件/SDF路径及旧轮次启动器保留，供报告复现；最终组合性能已使用同一最终程序、同批控制重新报告，不能把两个密集研究的差值相加。

[B1D首次mask12记录](../../build/gi_query_reuse_direct_address_descriptor_guard_failure.log)保留为失败记录，末尾为“Query reuse changed graphics/AS resource work”。原因是direct主探针不再绑定AS，每计时帧合法减少一次graphics descriptor分配，旧fixture仍要求该计数两边完全相等。核对其余host工作、物理上传、射线容量、阴影和AS工作量保持原计划；修正只为测试明确加入`−计时帧数`这项严格descriptor预期，未取消其它相等检查、未放宽图像门槛或改变生产shader。该失败不能当作完整mask12通过。B1D之后GI生产shader已冻结；后续修改本轮harness并重建，但共享工程同时有无关的opaque-depth与strand-cache编辑，因此最终构建也重编译了部分Render C++，不能描述成“最终二进制只有harness变化”。各批程序身份分列，最终性能只用同一个最终程序内的A/B；画质另比较第八轮控制归档以检查fixture与控制行为。

[首次密集研究构建失败](../../build/gi_query_reuse_study_build_initial_failure.log)也保留：共享工程中OpaqueDepth/ResetHistory接口变化，使`MaterialRenderPipeline::ResetHistory()`定义与声明不匹配。此次优化未编辑该项无关接口，记录这次构建失败不代表GI shader契约失败；不得悄然删掉失败日志或把后续harness程序当作上述F813/B1D程序。

### 最终mask12五轨迹Release实窗性能

[最终完整性能](../../build/gi_query_reuse_direct_address_final_present_summary.md)使用封存程序`24A3F912D23397F7349BDD4A8A8DA6DD21F6060FFB8AE5748014092AE66E94F3`完成同程序A/B，运行前后hash一致、进程退出0、日志一致性为FORMAL PASS。原始日志SHA为`A26CFD50653C8F63C5FC07B7E297A82228D40509B9D37E0C9032276C66D528C6`，身份记录为同名`_identity.json`。Release、RTX4060 Laptop、Vulkan实窗1280×800、MAILBOX三图像、vsync关闭；ABBA五轨迹，每模式每轨迹64个计时样本、共640次计时present。HUD、应用限帧、validation、计时图像回读和GPU原子仪表关闭，cold/预热/HD预览排除在FPS之外。

| 最终mask12，控制→优化 | 事件循环均值 ms | 循环P95 ms | 循环FPS | 均值变化 |
| --- | ---: | ---: | ---: | ---: |
| 静止 | 60.57020→56.35390 | 67.66090→64.91800 | 16.51→17.75 | −6.96% |
| 相机移动 | 64.46370→63.17330 | 73.82250→69.91140 | 15.51→15.83 | −2.00% |
| 模型移动 | 93.52670→89.89130 | 131.95600→97.27080 | 10.69→11.12 | −3.89% |
| 新露出 | 67.65780→65.17840 | 77.90490→72.15120 | 14.78→15.34 | −3.66% |
| 灯光变化 | 63.84260→61.65440 | 71.63250→69.37120 | 15.66→16.22 | −3.43% |

| 最终同批GPU均值 | GPU整帧 ms | 主探针kernel ms | 主探针总槽 ms | 接收面 ms |
| --- | ---: | ---: | ---: | ---: |
| 静止 | 54.63240→50.74440 | 9.92582→7.88498 | 11.68560→9.05450 | 13.08540→11.73300 |
| 相机移动 | 59.18290→56.57430 | 10.21020→7.89901 | 11.66360→9.12010 | 16.51310→15.40780 |
| 模型移动 | 62.02790→60.18960 | 10.45730→8.05747 | 12.02820→9.64235 | 16.40510→14.90260 |
| 新露出 | 62.41640→58.35150 | 9.40402→8.04467 | 10.90460→9.64042 | 19.20130→18.34120 |
| 灯光变化 | 58.18310→54.59660 | 11.00010→7.87149 | 12.56740→9.19050 | 13.01880→11.83210 |

模型GPU整帧减少1.8383ms（2.96%），主探针kernel减少2.39983ms，接收面槽减少1.50250ms。分项变化不能直接相加：其它阶段、同帧调度与统计聚合也变化，最终ABBA没有置信区间。模型事件循环median反而88.9254→89.8397ms，GPU整帧P95也67.0239→67.7132ms；不能把均值或一次P95下降写成每帧稳定收益。GPU/host同步等待嵌套，模型fence62.7433→60.9554ms不再额外计入节省。该批AS准备3.10825→2.93407ms及geometry pack2.66813→2.49840ms仅作未改阶段诊断，不归因于本轮改变AS算法。

正式harness和解析器现严格要求direct每计时帧少一次graphics descriptor分配：每轨迹64帧减少64，模型406,120→406,056；其它draw、uniform snapshot、AS、上传、射线和阴影工作仍逐项匹配。模型两边仍403,240项draw、30,400项uniform snapshot、64次AS UPDATE、0次AS full BUILD/分配/handle创建，36,864,000逻辑探针射线容量、320项上传区域、24,576项area shadow pass。actual主探针native帧64→0、direct帧0→64，几何地址0→64，其它策略实际帧数保持一致。`paired_work_exact=0`反映这项明确计划的descriptor差异，`estimator_work_exact=1`；不冒称所有资源计数完全相同。[37项解析器合成测试](../../build/gi_query_reuse_parser_test.log)通过，包含缺失/错误descriptor计划与计数的拒绝案例；它是CPU协议检查，不是额外渲染或性能证据。

新增[RunSealedGiRoomQueryReuse.cmd](RunSealedGiRoomQueryReuse.cmd)显式选择direct加受保护几何地址，并调用原第八轮模型更新启动器继承其设置。旧启动器继续保留，独立API新增开关默认false，mask1/2负结果实验亦能单独复现。最终实窗结果仍约11–18 FPS，尚未达到60/90/120 FPS；后续要针对接收面、过滤与模型绘制等余下开销继续分析，不能把本轮当作目标帧率完成。

### 接收面剩余成本的边界与后续归因

bit8不能直接解释为接收面也少做一次完整求交。`surface_cache_contact_shaders.h:356,452`的普通GI方向调用`SCNativeIntersect`；`software_bvh_visibility.h:164–209`在普通内部命中上执行一次硬件遍历、读取三个原顶点计算严格谓词，然后直接返回（207–208行）。只有native miss、边界、谓词或区间无效时才回退完整`SCIntersect`。它与主探针`NativeRefineGLSL`在合法候选后仍必做一次软件最近面认证（`probe_sdf_shaders.h:308`）有本质调用结构差异。现有低层fixture已在旧`SCNativeIntersect`与原软件之间记录9个FP32通道差异，涉及共面owner、被远面掩盖的掠射近面、反向法线及部分耗尽；将所有接收面方向改为一次软件查询不是已证明逐项等价的删除重复查询。

接收面真正额外的最近面认证在`CTReceiverOrigin`的source-anchor与outward-clearance两个不同短线段（337、344行），整个起点函数在选中像素上调用一次（396行），并不在各方向循环中重复。二者约束不同，不能直接合成一条射线。其`SCNativeOriginIntersect:223–232`保留软件认证及必要原区间重试；已有第八轮起点复用/软件起点实验的接收面密集配对区间均跨零，本轮未重新选用。最终mask12对接收面的改变是bit4受保护几何地址，查询计划、方向数和起点认证不减；bit2虽复用逻辑bounds数据，却在本批增加约1.519ms接收面开销，继续保留为负结果。

UE的`LumenScreenProbeHardwareRayTracing.usf:146–158`续追未完成区间并应用normal bias，`LumenHardwareRayTracingCommon.ush:486–506`消费inline查询结果；这些位置没有本工程的原软件BVH认证层，不能据此直接删除本工程source/clearance要求。后续应先在排除正式计时的诊断中区分普通内部命中、native miss、边界回退、认证/原区间重试，以及起点和方向追踪的工作占比，再选择新实验。当前接收面GPU槽包含risk/mask/gather、起点及cache读取，尚无这些分支的独立成本或实际查询数量；14.9026ms不能全部称为重复求交开销。

### 最终完整数值画质：224对帧通过

[最终画质汇总](../../build/gi_query_reuse_direct_address_final_quality_summary.md)由同一封存24A3程序完成，标签`direct_address_final`；运行前后hash不变、退出0、validation无错误，日志SHA为`26F17E360D02C53E4E5C04C6B9A40D043622E4CCFBFDA6D251DC8C5FDA0AFCD1`。两种视角各112对320×200帧，共224对：每视角静止64帧、连续运动16帧、分离停留8帧、回位停留8帧、稳定16帧。比较第八轮控制和mask12的接收面过滤及时域输出，实际测量60项ROI记录；另20个canonical ROI槽在该视角/阶段没有测量，不能当成20项零误差通过。

| 最终质量检查 | 实测记录 |
| --- | --- |
| RGB MSE、relative RMSE、最大RGB差 | 所有已测ROI记录为0 |
| 接触对比差、分离/回位历史残余差、梯度差MSE | 所有已测ROI记录为0 |
| Surface Cache atlas RGB比较 | MSE、relative RMSE、最大差均为0 |
| receiver几何、placement、曝光、mask及逐像素actual-N | 224对帧满足原精确检查 |
| 接收面选择覆盖pixel-frame检查 | 28,672,000 / 28,672,000，包含两边的汇总 |
| 源metadata检查条目 | 18,063,360 |
| 已知黑/未知有效性 | 各256,000项，4个view×variant记录每项各64,000 |
| 直接阴影depth | 224帧signature一致；14处完整精确检查，共88,080,384个depth像素 |
| 独立double三角形几何oracle | 130,182条抽样射线，命中130,182、miss0，最大端点误差0.0000211712m |

几何oracle按原射线ID每64项轮换一类，在配置的mesh/GDF/screen认证区间覆盖内比较，端点误差小于原0.0003m门槛；不是逐条全射线oracle、没有把无覆盖区域算成已验证。ROI相互重叠，28,672,000项和known-black/unknown数量也包含视角、变体及帧的重复覆盖，不能当成同等数量独立场景/独特观察。阴影其它帧为signature比较，只有14处完整depth像素比较。数值辐射亮度对照的是已接受输出，没有path-traced物理真值；日志数值精度有限，不据此宣称全部内部trace记录逐位相同、任意动态场景无瑕疵或达到目标帧率。

### 程序与源码记录范围

最终主程序及quality/performance分别以各自完成身份记录绑定24A3文件；原1AE/F813/B1D研究身份继续保留，不能混用。最终记录使用[总身份清单](../../build/gi_query_reuse_identity.json)与[选定源码快照](../../build/gi_query_reuse_final_sources)。这些源码是构建后选择性快照，用于比对和复现线索，不是编译器输入追溯、完全可复现构建或无并发编辑证明。共享`pipeline_resource_builder.h`在最后构建后另有bootstrap等待期限10→30秒的编辑；这是冷资源创建期限，未编入本次封存24A3结果，不能用当前源码的该项新值解释已测性能或冒称当前所有源码均与被测二进制对应。

### 独立PNG、归档控制和最终检查完成

[主图片审计](../../build/gi_query_reuse_direct_address_final_image_comparison.json)及[独立图片审计](../../build/gi_query_reuse_independent_images.json)均通过150/150对：75对最终24A3的控制/优化图，加75对第八轮780339归档/本轮控制图。独立实现完成1,629项检查，解码225张独特PNG，RGBA不同像素/通道、最大8位差及RGB MSE均为0；75张第八轮归档文件还与其原[清单](../../build/gi_model_update_identity.json)固定的文件SHA核对。比较覆盖320×200质量检查点、1280×800实窗与预览的静态、移动、分离、回位及稳定阶段，不能把150对图当作150个独立场景。归档控制相同支持本轮已记录fixture与控制视角保持，不能证明无关C++重编译未改变所有应用功能。

[五张图目视抽查](../../build/gi_query_reuse_independent_visual_review.json)确认椭球渐变和局部柔和接触阴影保持，分离/回位样本未见新增轮廓；移动样本仍有控制组已有的方柱面斑驳，解码审计显示该检查点没有新增像素差。它是有限抽查，既有瑕疵没有因此被宣称全部消失。PNG/tone-mapped等价也不等于所有FP32/packed追踪记录逐位相同或符合物理真值。

[最终backend复核](../../build/gi_query_reuse_final_backend_recheck.log)退出0，独立测试程序SHA为`84B2EBACA7E769C64BA4D278428CB014042D11A12FEBC9AA5C6C258720C27704`，与主24A3文件分开记录，运行前后hash一致。184场景×2safe的direct主探针对原软件/原native输出及逻辑请求、115场景共享closest与地址契约再次通过，validation错误为0。[最终CPU 3/3](../../build/gi_query_reuse_final_cpu_recheck.log)与[frame-command退出0](../../build/gi_query_reuse_final_frame_cpu_recheck.log)亦通过。低层契约、完整帧数值回归与独立PNG各自提供不同范围的证据，不互相冒充额外性能样本。

图片报告第一次启动时，runner把stdout重定向到脚本自身需要写入的`.log`，导致JSON比较通过后写报告发生`PermissionError`；[失败日志](../../build/gi_query_reuse_direct_address_final_image_comparison_runner_collision_failure.log)保留。重跑改用独立`_runner.log`后退出0，未修改图像、生产代码或比较门槛；这是报告文件路径冲突，不是GI渲染失败。

本轮最终性能、224对帧质量、150对图片与最终backend/CPU检查均完成。正式性能在停止并行GPU测试、构建和图片分析后采集；不同二进制间旧研究只用于各自归因或归档控制，不替代最终同程序A/B。

## 2026-10-22：第八轮模型更新——AS UPDATE、未使用上传剔除与安全起点查询

本轮保留第七轮完整路径，新增可单独启用的执行选项，处理模型移动时的加速结构重建、未使用的GPU距离场镜像上传，以及接收面短程安全起点查询。最终推荐采用mask3，即AS UPDATE加未使用上传剔除，起点策略保持第七轮原设置；最终同批模型整帧82.4372→77.3344ms，降低6.19%，12.13→12.93 FPS。224对帧的数值画质回归及150对PNG像素审计完成，记录的RGB、接触对比及残影差值为0，解码PNG像素无差异。起点两项实验及最初mask15组合的负结果同样保留，供技术报告对照。API新增开关默认均为false，旧启动器和旧测试轮保留原设置；采样预算、历史响应、接触阴影权重和第七轮紧凑辐射亮度过滤不变。通过范围为本次已测场景与轨迹，不据此宣称任意场景无瑕疵、所有FP32内部记录逐位相同或达到60/90/120 FPS。

### 三种加速结构策略与生命周期

原`reuseAccelerationStorage=false, updateAccelerationStructures=false`继续采用每次脏场景重新分配六个buffer、创建两个AS handle，并对BLAS和TLAS各执行一次BUILD的路径。原`reuseAccelerationStorage=true, updateAccelerationStructures=false`仍保留分配与handle复用，但每次仍是BUILD；这一选项没有改名，也没有被UPDATE替代。新增`updateAccelerationStructures=true`隐含保留存储，首次以`ALLOW_UPDATE`构建，固定拓扑的后续脏帧对同一BLAS、TLAS分别执行原地UPDATE。输入仍为原来按世界坐标展开的三角形和恒等实例；本轮没有改造成逐模型局部静态BLAS加实例TLAS，也没有改变原保守顶点扩张、packed triangle ID或最近表面认证。

UPDATE仅在相同primitive count、相同允许更新策略、固定格式/索引和可更新初始构建的条件下使用。新增路径检查所有三角形xyz为有限值且绝对值不超过`FLT_MAX/8`，使原float边差及扩张不溢出；非有限/极端坐标、活动性可能变化、拓扑数或策略改变均使用BUILD。初次存储按BLAS/TLAS的build/update scratch四项最大值分配，并保留对齐和BLAS→TLAS→fragment读屏障。位置可变不代表允许改变索引、primitive活动性或任意构建描述；[Vulkan更新规则](https://docs.vulkan.org/spec/latest/chapters/accelstructures.html)约束仍适用。

`accelerationBuilds`保留既有语义，统计一次脏native场景刷新，其中包含一项BLAS及一项TLAS命令；新增`accelerationFullBuilds`和`accelerationUpdates`是这项刷新计数的两种互斥类别，并非单独`vkCmdBuildAccelerationStructuresKHR`调用数。`accelerationStorageReuses`、buffer分配和handle创建分开记录；同一纹理被cache/contact/probe多次消费时，只在首个消费者刷新AS。启用/禁用、拓扑切换和删除仍遵守已完成帧的资源生命周期。

保留原单帧完成fence和命令/描述符/上传arena复用约束。源码中`Destroy(Acceleration)`本身没有`vkDeviceWaitIdle`或额外fence调用，旧`as_retirement`范围主要包含驱动销毁及内存释放，不能将其直接称为已证明的设备级同步等待。整帧fence包含GPU执行，也不能与GPU时间相加；本轮没有通过删除必要等待来制造CPU计时收益。

### 剔除未使用的GPU镜像上传

`trace.sdf.nativeUploadPruning`只在匹配Surface Cache场景的native fragment/MRT主探针变体实际可用时，跳过mesh/object/global/range及独立geometry镜像的上传。主探针继续使用Surface Cache的同一geometry纹理与AS；safe-origin和实际接收面GI也读取该几何。compaction、geometry shader、无硬件能力、场景不匹配、slice/volume/hit-marker调试均保留原上传路径。录制阶段再次要求实际native路径成立，并给未使用的sampler绑定有效fallback资源。

跳过上传期间不推进原GPU镜像的scene/generation记录。回到软件或调试路径后，沿用原替换/完整追赶逻辑，覆盖跳过多代、脏区只保留最近一代以及scene指针替换但generation相同的情况。原CPU Mesh/GDF更新、clipmap ready和边界常量继续维护；此选项减少的是GPU镜像和对应上传准备，不等于停止CPU距离场更新，也不能将全部CPU场景准备成本算作本轮收益。已录制帧继续pin真正使用的scene、Surface Cache及program owner。

### 接收面新增成本的调查与两项独立控制

第七轮`SCNativeOriginIntersect`先用硬件候选缩短范围，再以原软件BVH认证最近面。若硬件miss、边界或无效候选本来已回退到一次完整`SCIntersect`且返回命中，外层wrapper还会再次做认证，形成重复查询。新增`reuseOriginSoftwareCertification`选择`SCNativeOriginIntersectReusingCertification`：直接保留已有软件回退的权威结果，普通硬件候选仍做原有缩短上界认证、空结果原范围重试；耗尽、未知/已知黑和最近面/tie规则仍保留。第七轮原helper继续可选。

另一独立选项`receiverSoftwareOrigins`使接收面的短source-anchor和clearance查询使用原`SCIntersect`，主探针及实际接收面GI射线仍可采用硬件候选。短查询增加硬件遍历后仍须软件认证，未必比直接软件查询便宜；这最初是由调用结构得到的成本假设，后续实测没有证明切换起点策略具有稳定收益。两项同时启用时，receiver-only软件选项覆盖接收面的起点策略，certification复用仍作用于硬件探针起点；不能把两项消融的收益直接相加。native-contact、fresh、diagnostic和两种origin helper分别缓存program，并保留owner pin和无能力回退。

[独立起点成本研究](../../build/gi_model_update_origin_cost_summary.md)、[完整数据](../../build/gi_model_update_origin_cost_summary.json)及[身份](../../build/gi_model_update_origin_cost_identity.json)使用最终780339程序，固定模型轨迹及第七轮主探针/过滤设置，完成1536次计时present，每种起点策略512帧。8个连续平衡block内部采用0,1,2,2,1,0顺序，每epoch32帧、每模式每block64帧；推断单位是8个配对block差，不把1536个串行帧当独立样本。日志SHA256为`A03D301BE476C83606B8B5009DFE02DE66979FFCAA5224DCABCACA2C37AD174E`。

| 起点模式 | contact GPU均值 ms | 相对原版差 ms | 配对block的95%区间 ms | 整帧均值/P95 ms |
| --- | ---: | ---: | --- | ---: |
| 第七轮原认证 | 13.778722 | 0 | 参照 | 84.060966 / 95.736900 |
| 复用已有软件认证 | 13.856322 | +0.077600 | [-0.225571, 0.380771] | 84.287947 / 99.888100 |
| 仅接收面软件起点 | 13.683392 | -0.095330 | [-0.569666, 0.379006] | 84.705148 / 95.776600 |

两项contact差值区间均跨0，GPU整帧及事件循环的相应区间也跨0。该研究没有足够证据把第七轮观察到的约1.1–1.3ms新增开销锁定为起点硬件查询，也不支持把mask4或mask8作为最终推荐优化；源码上减少部分重复调用不等于已证明整帧加速。`contact_gpu_ms`包含risk/mask/gather和起点工作，并非单个查询时间或实际硬件查询数。配对t区间仍假设各block差具有代表性和一定独立性；连续block的频率、热状态或非线性顺序效应可违背假设，顺序平衡不等于随机化或锁频。该研究只测成本，完整画质由另行回归验收。

[独立解析审计](../../build/gi_origin_cost_independent_review.json)的279项检查和[解析器15项合成用例](../../build/gi_origin_cost_parser_test.log)通过，核查身份、协议、block统计和错误拒绝；它们不增加实际GPU样本，也不把跨0区间变成等价性证明。

### 现有验证与最终记录位置

最终主程序SHA256为`78033900A80E02A1B911A0FEDA05DEF364B3B5E7F3E834568A4982822124AB7D`，另存为[最终程序归档](../../build/gi_model_update_final_validated.exe)，选定生产/测试源码另存[最终源码快照](../../build/gi_model_update_final_sources)，阶段程序、源码及证据哈希汇入[交付身份](../../build/gi_model_update_identity.json)。接受的第七轮主程序`342056A8…E88A`及选定源码保存在[基线归档目录](../../build/gi_round8_baseline)，第七轮历史报告和数字保留如下，不能代替第八轮同批控制组。各次增加测试入口后重新编译的主程序身份分开保留，不将其冒充同一二进制；Render生产实现未因这些测试入口调整而改变，身份记录也不等于可复现构建证明。

| 主程序SHA256 | 用途与归档 |
| --- | --- |
| B67CF08CC99F43872BA650BAF938FC5BEB7B4AE99518AC02F09070A7212B650D | 初始低层实现/构建阶段，非最终正式组合程序 |
| 7BDC4A68285F239E4A1BDB40B14841D0D078A3B1C86BD7A802A301A0FA33739E | focused上传契约及mask15/8/4，保存在[该阶段程序](../../build/gi_model_update_final_candidate.exe) |
| C87A1CB4E94900BFB0DEF8E5B370C69EB4B18274F9C82981F272164429E11F51 | mask1/2/16，保存在[该阶段程序](../../build/gi_model_update_frozen_c87a.exe) |
| 78033900A80E02A1B911A0FEDA05DEF364B3B5E7F3E834568A4982822124AB7D | 最终mask3、起点成本研究及最终质量检查，保存在[最终程序](../../build/gi_model_update_final_validated.exe) |

[低层GPU契约](../../build/gi_model_update_backend_gpu.log)已完成：AS测试37个有限场景、fresh BUILD/retained BUILD/UPDATE三种模式，共3,744个FP32通道精确一致；包含32个旋转掠射近/远遮蔽、位置变化、反向共面、最近ID变化、五项独立简单CPU期望、拓扑/模式切换、inactive NaN保护、重复消费者、缓存帧和脏AS待上传删除。记录122次刷新，其中85次BUILD、37次UPDATE；旧native helper的BUILD/UPDATE差异在这些测试中为0，仍仅作诊断。复用认证helper另有184项、1,472通道对软件及1,472通道对第七轮认证helper精确一致。旧未认证native helper的9项原诊断差异仍保留，未改成正确性oracle。[CPU 3/3](../../build/gi_model_update_cpu.log)与[frame-command](../../build/gi_model_update_frame_cpu.log)分开记录。

[focused上传契约](../../build/gi_model_update_focused_contracts.log)及[7BDC身份](../../build/gi_model_update_focused_contracts_identity.json)完成3对、1,207,612个输出FP32通道及16,625,664个atlas FP32通道精确一致；覆盖6个跳过的模型generation、9次CPU/GPU图集追赶、2次无reset历史切换、5种软件/调试回退、首请求帧即prune和2次resize。初始slice fixture仍启用GI而误期待独立slice路径执行，后续仅修正测试设置和诊断标签，未以改生产代码或放宽输出门槛掩盖失败；[带标签失败日志](../../build/gi_model_update_focused_contracts_labelled_failure.log)和[374A中间程序身份](../../build/gi_model_update_focused_contracts_labelled_failure_identity.json)保留。

[最终mask3质量汇总](../../build/gi_model_update_resources_quality_summary.md)、[逐项数据](../../build/gi_model_update_resources_quality_summary.json)、[原日志](../../build/gi_model_update_resources_quality.log)与[完成身份](../../build/gi_model_update_resources_quality_identity.json)记录最终780339程序退出0，224对帧验收通过，日志SHA256为`72EB4A5298A0993616975F0529BE38EBD3F7D48B63531BBFA44780FB5105ED7D`。两视角覆盖static64、连续运动16、分离保持8、返回保持8和settle16；60个实际ROI统计项的RGB MSE、相对RMSE、最大RGB差、梯度差、接触核心/外圈对比差和分离/返回残影残差差均为0。另20个规范槽位因该视角没有对应几何而未测量，不能把它们当作额外20项零误差样本。

接收面几何、placement、exposure及actual-N计划保持原约束，接触/时域输出coverage数值相同为28,672,000/28,672,000像素帧；已知黑与未知各256,000，运动时的黑色能量和未知保护也通过。卡片atlas记录的RGB差为0，直接阴影224份全图集签名及14个检查点逐FP32位比较88,080,384个深度像素一致。主探针两模式合计130,182次按原ID轮换抽样的区间查询均命中独立double三角形oracle，最大距离误差0.0210609mm、端点误差0.0211712mm，低于0.3mm门槛。配对模式包含相同来源射线的重复核对，该总数不是130,182根相互独立的唯一射线，也不覆盖所有射线或配置区间以外的几何。

RGB误差和coverage相等是此fixture的数值结果；ROI的`exact_pixel_frames`实际使用`glm::vec4 operator==`，coverage使用float `w==`，均不是对FP32位模式的`memcmp`/`bit_cast`检查。它们不能证明有符号零等位表示、全部packed内部记录或任意场景逐位一致。本轮没有把主packed内部的完整逐位比较作为gating；底层求交契约、独立几何核对、输出ROI和图片审计是职责不同的证据。质量参照为第七轮已接受输出，并非path-traced物理光照真值；ROI有重叠，像素帧和误差统计不能当作互相独立的样本相加。

[独立150对PNG审计](../../build/gi_model_update_resources_image_comparison.log)及[逐对数据](../../build/gi_model_update_resources_image_comparison.json)以strict-all完成且退出0：75对本轮控制→mask3候选与75对第七轮归档→本轮控制均为解码RGBA像素逐字节相同，变化像素/通道、最大RGB/RGBA差和RGB MSE均为0，无缺失或错误。包括两视角质量检查点及七个1280×800高清检查点；静止、运动第81帧、分离view1第88帧contact、回位第96帧temporal和settle高清第112帧均抽看，接触细节保留，未见新增轮廓。PNG是编码后的像素证据，不替代线性RGB和几何/历史约束，也不是全部内部FP32数据的位级证明。

### 六组预先完成的控制对照及负结果

下面五项单独策略及初次四项组合共6组，均是各自同批第七轮控制→候选，Release、RTX 4060 Laptop、1280×800实窗，五轨迹ABBA，每组每轨迹64样本、640次计时present。单元格为整帧事件循环均值ms，完整median/P95、GPU、host及工作量见各行链接。不同批次、不同主程序和GPU状态不能拼接为一次前后对比，也不能把独立收益相加；mask15的失败收益结论保留，后续推荐改为mask3。

| 对照 | 静止 ms | 相机 ms | 模型 ms | 新露出 ms | 灯光 ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| [AS UPDATE，mask1](../../build/gi_model_update_as_update_present_summary.md) | 56.61→51.90 | 62.14→57.56 | 89.19→81.44 | 62.67→63.00 | 57.11→57.97 |
| [上传剔除，mask2](../../build/gi_model_update_upload_pruning_present_summary.md) | 55.92→54.29 | 57.79→59.73 | 87.58→85.05 | 62.39→64.06 | 57.56→58.00 |
| [认证复用，mask4](../../build/gi_model_update_origin_reuse_present_summary.md) | 51.55→55.66 | 54.54→58.66 | 81.96→84.90 | 57.80→57.24 | 55.74→55.23 |
| [接收面软件起点，mask8](../../build/gi_model_update_receiver_software_present_summary.md) | 51.71→52.85 | 55.42→58.62 | 82.76→84.55 | 57.27→58.30 | 52.73→53.87 |
| [保留存储但BUILD，mask16](../../build/gi_model_update_retained_build_present_summary.md) | 50.75→51.19 | 56.63→55.86 | 84.43→78.39 | 57.12→58.30 | 54.03→53.20 |
| [初始四项组合，mask15](../../build/gi_model_update_combined_present_summary.md) | 51.46→55.02 | 55.12→60.21 | 79.26→80.65 | 54.88→56.69 | 52.19→65.64 |

AS UPDATE、上传剔除及原有retained BUILD各自的模型均值分别改善8.69%、2.90%、7.15%，模型P95分别为108.758→91.0429、96.2683→89.8936、96.3011→83.9271ms。它们不是同一批控制，不能据此断言UPDATE比retained BUILD快固定百分比。mask4/8模型均值回退3.59%/2.17%，尽管对应模型P95略降；mask15五条轨迹的均值全部回退，灯光mean65.6394高于median53.3889及P9559.758，少量长停顿会影响均值，未将其直接归因于某项origin代码。上述原始日志、各自程序/退出身份及source-log SHA均由同名前缀`*_present.log`、`*_present_identity.json`、`*_present_summary.json`保留，结论不使用第七轮更慢或其它批次更快的控制数。

### 最终推荐mask3的正式实窗性能

[最终同批汇总](../../build/gi_model_update_resources_present_summary.md)、[机器可读数据](../../build/gi_model_update_resources_present_summary.json)、[原日志](../../build/gi_model_update_resources_present.log)及[完成身份](../../build/gi_model_update_resources_present_identity.json)记录最终780339程序、完整HD预览和退出0，日志一致性为FORMAL PASS。最终日志SHA256为`63973C731F255E381059322FF2D210F8C417AF8692100F6630624758B383CF65`。控制组与候选均启用第七轮native probes/origins、辐射亮度过滤、MRT、defer和shadow四项，仅候选启用UPDATE和GPU镜像上传剔除，两个origin实验均关闭。

| 轨迹 | 事件循环均值 ms | 中位数 ms | P95 ms | 事件循环 FPS |
| --- | ---: | ---: | ---: | ---: |
| 静止 | 56.98→51.54 | 55.23→51.35 | 72.84→57.09 | 17.55→19.40 |
| 相机移动 | 56.60→55.69 | 56.04→54.86 | 61.5411→61.5709 | 17.67→17.96 |
| 模型移动 | 82.44→77.33 | 82.30→77.95 | 86.57→84.16 | 12.13→12.93 |
| 新露出区域 | 59.06→56.47 | 59.33→56.52 | 63.69→61.24 | 16.93→17.71 |
| 灯光变化 | 56.64→53.24 | 54.80→53.43 | 73.26→56.27 | 17.66→18.78 |

五条轨迹均值改善1.61%–9.54%；相机P95增加0.0298ms，不能称所有尾延迟均改善。FPS为1000除以事件循环墙钟均值，包含真实提交/帧完成等待，排除cold、预热和HD回读；不是GPU倒数或物理scanout。计时无HUD、应用限帧、验证层、图像回读、CPU几何oracle或GPU原子诊断，期间未并行构建、GPU测试或图片分析。未锁频，未为这组ABBA提供置信区间；mask16、最终mask3及起点研究另记录1Hz设备遥测，覆盖cold/预热/预览且开销未独立消融，不能据此给每帧分配精确频率或宣称遥测零开销。

静态路径双方计时段均没有AS刷新或纹理上传，不能把静态9.54%的整帧差全部归因于AS UPDATE；初次构建策略、运行状态和其它阶段波动仍可能影响观测。模型上传、BUILD/UPDATE及分配计数提供更直接的资源工作量证据。

模型GPU整帧57.3288→57.2966ms基本不变，GPU P95反而61.2373→62.5405ms；嵌套AS范围0.929857→0.455550ms减少约0.4743ms，但主探针10.8647→11.7396ms、接收面13.6953→13.9176ms有所回退，不能宣称GPU各阶段普遍提速或把GPU帧差全部归因于AS。此次接收面起点策略两边相同，新增约0.2223ms差值不能被算作新origin helper的成本，也不能据此替第七轮1.1–1.3ms波动确认原因。

### 模型资源与CPU/同步开销

以下工作量均为最终同批64个计时模型帧之和；每次AS刷新包含BLAS和TLAS各一项，初次创建/拓扑切换不在热帧零分配结论内。

| 工作量 | 第七轮控制 | UPDATE加上传剔除 |
| --- | ---: | ---: |
| 物理纹理上传字节 | 425,721,856（406MiB） | 161,480,704（154MiB） |
| 物理/延迟上传区域 | 2,752 | 320 |
| 专用即时上传提交/等待 | 0 / 0 | 0 / 0 |
| 热帧staging新分配 | 0 | 0 |
| AS场景刷新 | 64 | 64 |
| 其中完整BUILD / UPDATE | 64 / 0 | 0 / 64 |
| AS存储复用 | 0 | 64 |
| AS buffer新分配 / handle创建 | 384 / 128 | 0 / 0 |
| 逻辑探针射线容量 | 36,864,000 | 36,864,000 |
| graphics draw / descriptor / UBO快照 | 403,240 / 406,120 / 30,400 | 403,240 / 406,120 / 30,400 |
| 卡片重照texel / shadow面 | 9,830,400 / 24,576 | 9,830,400 / 24,576 |

上传字节降低62.07%，每帧6,651,904→2,523,136字节，区域43→5；保留的Surface Cache属性/几何更新不变。双方原来都已采用defer，所以专用上传等待原本就是0，本轮没有额外消除2752次fence之类的结论。实际AS命令数仍每帧BLAS+TLAS两项，区别在BUILD→UPDATE及资源保留。允许UPDATE可能改变AS/scratch大小，未独立测AS字节或峰值VRAM，热帧零分配不等于AS零内存。

聚合计数中staging容量峰值为控制16MiB、候选8MiB；trial0/1/2模型均为8MiB，trial3后置控制为16MiB。源码的arena只增不减，容量受前序追赶上传和ABBA顺序影响；不能把这项统计解读为新增路径主动释放8MiB、固有容量缩半或全部GI显存下降。

| 模型每帧host/wall范围 | 控制→候选 ms |
| --- | ---: |
| 场景准备 wall | 5.33936→5.54950 |
| 应用记录/上传 wall | 6.94335→3.74059 |
| 提交至完成/呈现 wall | 70.1356→68.0263 |
| render thread（上述范围内） | 70.0777→67.9657 |
| backend encode | 9.98843→8.79766 |
| pipeline资源准备 | 5.00324→1.74001 |
| 纹理更新准备 | 3.18042→0.820653 |
| AS准备 / 几何打包 | 3.28075 / 1.94953→2.09511 / 1.84193 |
| AS retirement | 1.31686→0 |
| 延迟copy录制 | 0.863352→0.376914 |
| 普通帧fence | 58.4799→57.9424 |

场景准备、应用记录/上传、提交至完成三项是串行wall区间；render-thread、backend、纹理及AS范围具有包含/嵌套关系，不能将整表相加。普通fence包含GPU57.2966ms执行；它保留而非被删除，提交等待减少也不等于纯CPU少做了相同时间。AS retirement归零对应不再逐热帧销毁资源，不是删除了源码中不存在的`vkDeviceWaitIdle`。CPU SDF场景准备5.28090→5.48986ms未改善；CPU仍维护距离场/clipmap、把全场景三角形变换到世界空间并refit packed软件BVH，AS上传仍全量展开顶点，未实现逐模型静态BLAS实例化、部分顶点更新或真正的多帧资源并行。模型候选仍为77.33ms，离60/90/120 FPS预算有明显距离。

四次cold依次为35,886.8、49.8711、53.1726、71.3378ms，包含编译、场景和AS准备并排除在FPS之外；后续模式共享热缓存，这不是35.9秒→50毫秒的成对冷启动加速。实际初次分配、长期大幅模型运动下的refit质量及任意场景的性能仍需各自验证。

### UE依据与可复现实验入口

本机UE 5.7.1的`VulkanRHI/Private/VulkanRayTracing.cpp:152,180,324–336`分别映射AllowUpdate及BUILD/UPDATE模式，`1215–1256`区分更新计数、scratch大小并将UPDATE的source/destination设为原handle；`450`附近通过延迟删除队列退役AS。`Renderer/Private/RayTracing/RayTracingScene.cpp:258–431`按实例构建buffer并安排上传/构建任务，`Lumen/LumenScreenProbeTracing.cpp:762–775`在硬件与软件主探针追踪之间选择。这里借鉴可更新构建、保留资源和实际消费者调度；没有照搬UE GPUScene、独立静态BLAS实例化、多帧并行或RDG调度器。

F1面板保留旧选项，新增“Update acceleration structures”“Skip unused native SDF uploads”“Reuse source-origin certification”“Software receiver source origins”。新[RunSealedGiRoomModelUpdate.cmd](RunSealedGiRoomModelUpdate.cmd)继承第七轮[入口](RunSealedGiRoomNativeProbes.cmd)，最终只开启UPDATE和上传剔除，原起点设置保留；末尾参数可覆盖，旧入口不变。AS三种模式为`--surface-cache-rebuild-acceleration`（fresh BUILD）、`--surface-cache-reuse-acceleration`（retained BUILD）、`--surface-cache-update-acceleration`（UPDATE）；`--surface-cache-build-acceleration`只关闭UPDATE，保留当前storage-reuse请求。上传正负开关为`--surface-cache-prune-native-uploads`/`--surface-cache-legacy-native-uploads`；起点策略为`--surface-cache-reuse-origin-certification`/`--surface-cache-legacy-origin-certification`及`--surface-cache-software-receiver-origins`/`--surface-cache-inherit-receiver-origins`，两项作为可选实验保留而不默认推荐。

测试入口`--surface-cache-model-update-{test,quality,performance}`与`--surface-cache-model-update-mask`保留独立消融：bit1=AS UPDATE、bit2=上传剔除、bit4=认证复用、bit8=接收面软件起点、bit16=retained BUILD，范围1–30且bit1/16互斥。测试选项默认mask仍为15，复现最终推荐性能/质量需显式传`--surface-cache-model-update-mask 3`；原mask15负结果和mask16旧策略继续可复现。两组均固定第七轮native probes/origins、辐射亮度过滤、MRT、defer及shadow四项，仅候选启用所选bit；原历史测试明确关闭新选项。`--surface-cache-origin-cost-study --benchmark-present`可重现1536样本的起点研究。报告应保留请求设置与实际完成帧统计，模式fallback不以命令行文字推断。

## 2026-10-22：第七轮架构优化——硬件候选求交与辐射亮度过滤拆分

本轮把主探针及安全起点的三角形查询接入已有硬件加速结构，并将空间过滤的辐射亮度与不可变命中元数据分开。每探针方向数、屏幕追踪预算、接收面基础/运动/新露出区域采样、历史响应和接触阴影权重保持原设置。新增路径均为可选开关；API默认关闭，原入口不变。90 FPS要求整帧不超过11.11ms，启动器中的120 FPS上限不代表达到120 FPS，局部阶段的加速也不代表整帧达到60/90/120 FPS。

### 实现与正确性边界

主探针通过`trace.sdf.hardwareRayTracing`选择新的Vulkan fragment/MRT路径，复用contact/cache已经使用的Surface Cache AS和原始三角形ID。它跳过原SDF/GDF距离步进，用硬件候选缩短线段，再由原packed-float BVH认证最近命中、朝向、闭区间端点、owner与共面tie顺序。无原生能力、场景不匹配、compaction或不支持的geometry-shader模式保留软件路径。因此`probeSdfHardwareRayTracing`表示实际选择的执行variant，不表示每条查询均由硬件独立完成；日志中的`sdf_trace`、`sdf_kernel`在该模式是共享主探针计时槽，不能将其称作纯SDF/GDF march或纯硬件追踪成本。

初版仅采用硬件候选的原浮点交点，不能保证保留最近表面：为覆盖重心坐标epsilon而扩张的三角形，逐顶点舍入可能使掠射面偏转，导致原生查询漏掉近处掠射面，或返回更远的面。最终版在原生命中后仍用缩短上界的原BVH认证；原生miss、边缘、端点或认证失败均保留原范围回退。主探针`PSRefine`和安全起点`SCNativeOriginIntersect`都采用这一策略；接收面锚定及向外clearance sweep保持原拒绝/耗尽规则。不能为了标榜纯硬件或减少查询而删掉这层验证。[初始掠射失败](../../build/gi_architecture_backend_gpu_initial_grazing_failure.log)与其它预检失败均保留，最终契约增加了“近面被远面掩盖”的回归。

`gi.hardwareOriginTracing`独立控制探针和接收面的安全起点查询；冷/热、新露出采样和diagnostic replay采用同一变体。原contact/cache的`SCNativeIntersect`保留旧行为；低层日志中其9项差异只作非gating诊断，新起点helper单独经过严格认证，没有把旧helper的结果当作正确性oracle。

`gi.spatial.radianceOnlyFiltering`把两张ping-pong图集改为每射线一个RGBA32F辐射亮度/status texel；方向、命中距离、owner等另外三个texel直接从原始追踪图集读取。第一遍读取原packed辐射亮度，后续遍读取紧凑图集；标量与方向积分分别使用对应地址，保持原运算次序、角度/可见性门槛和未知/已知黑的全样本分母。普通渲染没有重新打包；显式`ReadbackSurfaceCacheSpatialRays`才在CPU合并回原四texel记录。公共debug纹理带布局标志与metadata handle，resize、帧完成和资源pin仍遵守原生命周期。

1280×800预设下，仅两张过滤图集的有效纹理负载由73,728,000降至18,432,000字节，即70.3125→17.578125MiB，节省52.734375MiB；3遍过滤的attachment调用容量由每帧6,912,000降至1,728,000，减少75%。这不是全部GI/峰值显存，也不是75%的GPU时间或物理transaction下降。原始追踪图集、其它GI目标、shader variants、uniform ring、AS和shadow池仍保留；新旧pipeline的驱动代码缓存成本未量测。

### 验证与最终性能记录

最终主程序SHA256为`342056A8291C4F98974013DE8948B6B8251B165E770B48E02A50F33F13B6E88A`。从C433版本到该版本只修复native配置首次完成的冷编译等待期限，GLSL不变；C433旧组合数据和存在CPU并行工作的组合预检保留作诊断，不能替代最终组合正式数据。首次有效native配置允许120秒完成lazy pipeline编译，同配置完成后仍为10秒；普通非native首次启动仍按原30秒处理。这个期限调整不增加热帧的GPU/CPU工作，冷编译仍排除在性能样本之外。

[低层GPU契约](../../build/gi_architecture_backend_gpu.log)中，主探针184 case×2 safe变体，共4416个FP32通道零差；安全起点184 case共1472通道零差。覆盖128个旋转掠射、32个近/远遮蔽、反向共面tie、部分命中后visit耗尽、严格重心边界、闭端点、最近命中和背面。[独立过滤契约](../../build/gi_architecture_contracts.log)8对共3,843,284个FP32通道精确一致，另有1,195,008个不可变metadata通道、99,584条记录、2次resize及2次不reset的历史切换；包含1–4遍、4/8/16射线分辨率、非方形尺寸、曲率/方向重建、安全起点开关及部分strength。CPU [3/3](../../build/gi_architecture_cpu.log)与[frame-command](../../build/gi_architecture_frame_cpu.log)检查分开记录。[最终342程序的启动器复核](../../build/gi_architecture_launcher.log)也通过相同8对契约并退出0；不能将低层逐位相同直接外推成整幅GI逐位相同。

最终组合采用Release、RTX 4060 Laptop、Vulkan实窗1280×800、MAILBOX三图像、vsync关闭的五轨迹ABBA；每组每轨迹64个样本、640次主present，另有未计时的HD预览。FPS以事件循环墙钟均值倒数计算，包含真实提交/完成等待，不是GPU倒数或物理scanout。无HUD、应用限帧、validation、计时回读或GPU原子诊断。完整HD预览结束且进程退出0后，统计一致性检查为FORMAL PASS，最终日志SHA256为`33E80D0621E6D1658F8DFA4D79BDCB80D72B36665C17D071A14C88C87DFC52A0`。

以下为最终同批第六轮预设控制组→两项组合候选，详见[最终同批汇总](../../build/gi_architecture_combined_clean_present_summary.md)、[机器可读明细](../../build/gi_architecture_combined_clean_present_summary.json)、[实窗日志](../../build/gi_architecture_combined_clean_present.log)及[程序/退出身份](../../build/gi_architecture_combined_clean_present_identity.json)。该次正式运行期间没有并行构建、GPU测试或图片分析。

[空间路径独立审计](../../build/gi_architecture_combined_clean_independent_audit.json)的59项检查与[另行解析原始日志的探针审计](../../build/gi_architecture_combined_clean_probe_audit.json)的964项检查均通过；后者重新核算各试次加权均值、实际variant、采样/上传/AS工作量、程序身份和完整预览退出。被测程序及证据哈希保存在[交付身份](../../build/gi_architecture_identity.json)，程序另存为[验证归档](../../build/gi_architecture_final_validated.exe)。

| 轨迹 | 事件循环均值 ms | 中位数 ms | P95 ms | 事件循环 FPS |
| --- | ---: | ---: | ---: | ---: |
| 静止 | 89.21→62.37 | 88.35→62.16 | 104.80→70.43 | 11.21→16.03 |
| 相机移动 | 98.42→74.56 | 99.00→74.13 | 109.13→84.58 | 10.16→13.41 |
| 模型移动 | 167.42→140.57 | 167.61→139.13 | 182.86→154.10 | 5.97→7.11 |
| 新露出区域 | 98.94→79.16 | 97.98→79.41 | 112.91→84.99 | 10.11→12.63 |
| 灯光变化 | 90.48→66.54 | 91.13→67.38 | 103.44→75.62 | 11.05→15.03 |

最终模型移动GPU整帧100.819→73.6725ms（降低26.93%），主探针共享追踪槽33.0025→13.3435ms（降低59.57%），嵌套kernel 29.2886→10.7949ms、pack 3.70403→2.54248ms；空间过滤整体12.5997→7.6299ms。GPU子范围不能与其父范围相加。接收面追踪15.9295→17.0818ms反而增加，后端命令编码27.1634→28.4102ms也没有改善，不能宣称全阶段提速。最终整帧仍有140.57ms，尚未达到60/90/120 FPS。

相机移动的接收面追踪15.9225→17.0621ms，新露出区域18.332→19.6536ms，同样增加约1.1–1.3ms；这些局部回退与整帧收益一并保留。

CPU/等待瓶颈仍明显：候选模型场景准备11.7892ms、应用端记录/上传18.9611ms、提交至帧完成109.781ms；后者包含render-thread 109.643ms，细分为frame begin 4.07726ms、encode 28.4102ms和frame end 77.1496ms。Fence 76.3203ms包含GPU执行，不与GPU 73.6725ms相加。SDF场景准备11.5553→11.6041ms基本不变，pipeline资源准备15.3352→14.7815ms；纹理更新准备10.5819ms、AS准备6.96345ms、几何打包3.31282ms和AS retirement 5.40087ms是嵌套或跨准备阶段统计，不能合计成额外独立CPU时间。镜头交换、present及诊断服务很小；本轮没有处理这些串行场景/资源准备的结构性开销。

此次控制组和候选都比此前单项消融慢。相同最终程序不等于相同GPU频率、系统负载和运行环境；未锁频或记录足以归因的环境轨迹，不能把差异锁定为温度或某个后台进程。最终交付使用本表这一批控制数，保留较早测量作诊断，不挑选更快批次作为headline。四次cold墙钟为69,383.9、42,004.2、71.6656、87.6541ms，含编译/场景/AS且排除在FPS外；后两次共享缓存，不是成对冷启动加速。

[最终质量汇总](../../build/gi_architecture_combined_clean_quality_summary.md)、[逐项质量数据](../../build/gi_architecture_combined_clean_quality_summary.json)、[质量日志](../../build/gi_architecture_combined_clean_quality.log)及[质量身份](../../build/gi_architecture_combined_clean_quality_identity.json)均记录最终342程序完成并退出0：224对帧通过几何/placement/exposure/actual-N门槛，60个实际ROI统计项中最坏相对RGB RMSE为0.000655148（0.0655%，原门槛1%），最大绝对RGB差为0.00345698。接触核心/外圈对比与分离/返回残影残差通过原约束；已知黑和未知各256,000，卡片atlas RGB误差为0。直接阴影224个全图集签名一致、14个检查点逐位比较88,080,384个深度像素一致。

65,091条按原始射线ID轮换抽样的配置区间查询均与独立double三角形oracle命中一致，最大端点误差0.0211712mm，低于0.3mm门槛；这只覆盖实际配置的screen/mesh/GDF区间及抽样射线，不证明所有场景/所有射线。输出alpha/readability逐位相同为28,171,344/28,672,000像素帧，仍有500,656（1.746%）不同，因此整幅GI、追踪内部记录及覆盖率不能宣称全部逐位相同。质量比较的参考是已接受输出，不是path-traced物理光照真值；ROI相互重叠，统计项不能当独立样本相加。

[独立150对PNG审计](../../build/gi_architecture_combined_clean_image_comparison.log)与[逐对差异](../../build/gi_architecture_combined_clean_image_comparison.json)退出0、无缺失/错误：75对第六轮归档→本轮控制组全部逐像素精确；另75对native配对仅11对精确、64对有差异，其PASS表示输入完整且诊断已计算，不是native图像零差。最终renderer质量图最大差1级8位值，7个HD最终renderer检查点最大差2级；所有stage最大3级只出现在质量view1、第88帧的temporal tone4图。

HD最终renderer七帧共7,976个变化像素帧，按像素加权的RGB 8位值MSE为0.0003878813；它是PNG编码值误差，不是线性辐射亮度误差。HD第64帧变化1,051/1,024,000像素（0.102637%，最大1级），移动第81帧2,054像素（0.200586%，最大1级），settle第112帧1,494像素（0.145898%，最大2级）。静止、移动、settle及最坏第88帧已单独目视检查，接触细节仍在，未发现新增明显轮廓；移动方块表面原有mottle在控制组也存在。有限截图和目视不能证明任意轨迹无瑕疵，也不能替代上述几何/接触/残影数值门槛。

单项消融另有[仅native](../../build/gi_architecture_native_present_summary.md)与[仅过滤](../../build/gi_architecture_filter_present_summary.md)，两者都使用最终342程序、独立同批控制组和640次present，不能跨批相加收益或用不同批控制数拼接前后对比。[独立消融审计](../../build/gi_architecture_ablations_independent_audit.json)检查身份、最终日志SHA、每试次/轨迹样本、实际variant及工作量。仅过滤的模型`probe_spatial`为9.25821→5.02298ms（降低45.75%），这个范围包括邻居查找、过滤、积分与重建，不能称单一filter shader的用时。该消融的新露出区域整帧P95从92.8118升至109.577ms，尽管空间过滤自身均值与P95改善；相机控制组均值95.6003高于P95 92.8473，反映少数大停顿对均值的影响，不能把其整帧均值改善全部归因于过滤。未锁GPU频率、未提供置信区间或单帧原始最大值；这些限制不因解析器一致性PASS而消失。

本轮没有优化模型移动时的距离场准备与上传。硬件主探针虽跳过distance march，`PrepareProbeSdfTrace`仍维护原MDF/GDF和packed geometry；既有全场景三角形变换、BVH refit及Surface Cache更新继续运行。单项消融中模型64帧双方均为36,864,000探针容量射线、425,721,856字节物理纹理更新、24,576个shadow面、64次AS BUILD、0次storage reuse、384次buffer分配和128次handle创建。探针容量不代表实际march次数。没有为主探针再建一份AS，但原完整BUILD和串行准备/提交等待仍是后续瓶颈；fence包含GPU执行，不能和GPU整帧相加。

### UE依据与使用入口

本机UE 5.7.1的`Engine/Source/Runtime/Renderer/Private/Lumen/LumenScreenProbeTracing.cpp:762–775`将硬件screen-probe追踪与软件mesh追踪分支选择；`LumenScreenProbeFiltering.cpp`和`LumenScreenProbeFiltering.usf`在方向辐射亮度gather数据上过滤，几何/命中信息承担约束作用。本轮借鉴这两项职责拆分，保留本项目的余弦半球估计、精确BVH认证及接触细节，没有直接移植UE的完整gather atlas或调度器。UE的`LumenSceneLighting.cpp`页预算、`GlobalDistanceField.cpp`脏区域/错帧更新、`RayTracingScene.cpp`实例/TLAS管理是后续上传和并行化的参考，本轮尚未实现这些架构。

[Epic性能指南](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-performance-guide-for-unreal-engine?application_version=5.7)给出的常规High/Epic目标是60/30 FPS及1080p内部GI+反射约4/8ms；这不是所有场景90 FPS的承诺，也不能直接与本实验的完整GPU帧时间比较。

新增可选[RunSealedGiRoomNativeProbes.cmd](RunSealedGiRoomNativeProbes.cmd)继承第六轮优化预设并开启native probes与radiance filter，末尾参数可以覆盖。独立正负开关为`--surface-cache-native-probes`/`--surface-cache-software-probes`及`--surface-cache-radiance-filter`/`--surface-cache-packed-filter`；测试为`--surface-cache-architecture-{test,quality,performance}`，`--surface-cache-architecture-mask 1/2/3`分别选择主探针与起点、过滤、两项组合，控制组始终固定第六轮已接受预设。实际使用情况由`probeSdfHardwareRayTracing`、`surfaceCacheHardwareOrigins`、`surfaceCacheRadianceOnlyFiltering`等完成帧统计确认，不以请求参数推断硬件能力或执行次数。

## 2026-10-21：第六轮SDF执行效率——同次追踪样本复用

本轮模型SDF总范围27.66→25.39ms（降低8.20%），事件循环93.58→90.64ms（降低3.14%，10.69→11.03 FPS）；完整224帧严格质量与独立75/75图像零差对照均通过，静态画质、运动稳定性和接触阴影保持一致。收益仍未达到60/90/120 FPS，静止和相机轨迹的P95略有回退。交付仅增加可选入口，原默认不变。

控制组继承第五轮候选：fast atlas寻址、第四轮shadow四项、native contact/cache、MRT及defer均开启；双方compact和AS storage reuse关闭，候选仅开启`trace.sdf.reuseTraceSamples`。主程序全程冻结SHA256为`643DF8F4DA5DF3BD23A5A530342B07389808BCADD886D2CEF4A5381B58FB53B3`，见[构建日志](../../build/gi_sdf_reuse_build.log)、[交付身份](../../build/gi_sdf_reuse_identity.json)及[被测程序归档](../../build/gi_sdf_reuse_final_validated.exe)；六份核心shader中仅SDF shader改变。[C8AB开轮归档](../../build/gi_round6_baseline_c8ab/identity.json)保留旧程序和选定源码/证据，旧五轮fixture明确关闭reuse。两组名为`accepted_sdf_addressing`/`optimized_sdf_reuse`，日志前缀`SDF REUSE GI`。

### 正式实窗性能与成本

[正式日志](../../build/gi_sdf_reuse_present_performance.log)与[完整成本统计](../../build/gi_sdf_reuse_present_performance_summary.md)记录RTX 4060 Laptop、Vulkan、1280×800实窗五轨迹ABBA：每组每轨迹64样本，共640次present，另有两组各112帧HD，进程退出0，统计一致性检查通过。FPS取事件循环墙钟均值倒数，不是GPU倒数或物理scanout。以下均为同次运行的baseline→candidate，不能把第五轮模型SDF 29.34ms混作本轮27.66ms控制组的优化前数据。

| 轨迹 | 事件循环均值 ms | 中位数 ms | P95 ms | 事件循环 FPS |
| --- | ---: | ---: | ---: | ---: |
| 静止 | 62.69→62.24 | 62.66→61.92 | 64.21→65.56 | 15.95→16.07 |
| 相机移动 | 68.96→67.19 | 68.92→66.63 | 70.73→71.25 | 14.50→14.88 |
| 模型移动 | 93.58→90.64 | 92.72→90.80 | 99.24→94.38 | 10.69→11.03 |
| 新露出区域 | 70.37→68.57 | 70.14→68.57 | 72.56→70.68 | 14.21→14.58 |
| 灯光变化 | 66.46→62.57 | 66.47→63.41 | 67.68→65.27 | 15.05→15.98 |

模型SDF总范围27.6607→25.3915ms，嵌套kernel 27.0831→24.8332ms，pack 0.56256→0.52528ms；GPU整帧68.6596→65.9006ms。Kernel仍含screen认证、PSRefine、Mesh/GDF步进及Surface Cache查询，不能称纯march；kernel/pack与总范围不相加。CPU SDF场景准备5.86866→6.43424ms反而增加0.57ms（9.64%），backend encode 9.37173→8.91118ms、fence 69.5374→66.7437ms也有变化；本轮没有新增上传优化，未改阶段的差值不能全部归因于样本复用。pack在灯光轨迹0.46650→0.57704ms也回退，没有全阶段提速的结论。候选模型仍有GPU整帧65.90ms、SDF 25.39ms、接收面补追11.63ms、探针空间过滤7.82ms等成本。

工作量严格对照通过：每轨迹双方探针预算均36864000；模型64帧均为425721856字节物理纹理更新、24576个shadow面、403240次graphics draw、405864次descriptor分配和30400次UBO快照复制。AS均64次完整build、0次storage reuse、384次buffer分配、128次handle创建；预热后atlas零新分配、64次复用，双方fast variant各64帧，reuse variant为0/64帧。射线、步数、GI更新、接触阴影及原过滤预算没有降低。无新增GPU原子诊断、计时scope或中途读回；packed fetch仍是legacy逻辑请求数，不代表物理读取。

Cold不计FPS，四批依次为24694.7、1593.96、63.728、65.1778ms，后续共享热缓存，不能据此宣称成对冷启动加速。[320次present预检](../../build/gi_sdf_reuse_initial_present_performance_summary.md)中的模型SDF 29.6284→26.0928ms只作诊断，正式收益较小。对象metadata复用与单child缓存没有分别消融；未锁频、无置信区间或独立物理transaction测量。原64MiB uniform ring、48MiB shadow深度池及纹理布局未缩容，局部变量复用也不等于寄存器/显存减少；驱动代码cache、寄存器占用和峰值显存未量测。

### 严格质量与归档对照

[完整质量](../../build/gi_sdf_reuse_quality.log)退出0：224帧几何/owner/status/方向/起点/actual-N/readable签名精确一致，72482816个packed texel、108724224个离散metadata通道以及接触/时域GI、卡片atlas、梯度/接触对比误差均0。coverage 28672000、源metadata 18063360、接收面射线42461616、fresh 163140、运动174282、已知黑/未知各256000保持原值；224个完整depth签名及14份全atlas逐FP32位比较88080384个深度像素也均为0，未放宽严格门槛。

[75对图像审计](../../build/gi_sdf_reuse_image_comparison.log)及[逐对数据](../../build/gi_sdf_reuse_image_comparison.json)独立退出0：12对静态小图、21对本轮HD baseline/candidate、双方各21对第五轮候选HD归档均为零差，差异像素数/通道数、最大8位差和MSE均为0。PNG精确性与上述FP32/几何合同是分开的证据。本轮HD两组均额外64帧稳定原radiance/pose，再显式Reset后采集112帧及7检查点，额外预热未计性能、无回读。该对称初始化不反推上一轮微差的成因，旧67/75严格失败记录保留。

[低层后端合同](../../build/gi_sdf_reuse_backend_gpu.log)、[12对focused](../../build/gi_sdf_reuse_contracts.log)、[CPU 3/3](../../build/gi_sdf_reuse_cpu.log)和[frame-command](../../build/gi_sdf_reuse_frame_cpu.log)均退出0。低层18 case×8 variant、1152个FP32通道严格一致，覆盖metadata、BVH分支/最近命中收缩、栈/visit预算及耗尽；focused的1843200个完整packed texel、7372800个FP32通道、4718592个depth像素逐位相同，2次resize及2次无reset双向历史切换通过。

### 实现、UE参考与交付

实现仅复用同一次对象求值内原有`countsScale`、`worldLowBias`、`worldHigh`三个vec4，并在safe版PSRefine中携带一个马上访问的child bounds及已命中判断，保留未缓存回退、遍历/候选顺序和原浮点表达式。`probeSdfReuseTraceSamples`表示实际选中的执行variant，不是复用次数；safe关闭时仍复用对象数据，BVH保持legacy。纯执行开关不加入GI history key。

UE参考为本机`LumenSoftwareRayTracing.ush:140–169`的逐对象数据/asset加载及volume ray裁剪、`588–611`的grid候选对象迭代，和`LumenScreenProbeTracing.usf:598–600`的候选数/偏移读取。本轮未移植grid对象剔除或ray clip，以免改变当前union-min距离步进的safe/steps/fetch合同；单child缓存是本项目PSRefine适配，不是UE现成模块的直接移植。

[生产MRT离线SPIR-V证据](../../build/gi_sdf_reuse_ir_evidence/README.md)经相同Vulkan1.2优化/remap编译与验证：静态`psObjects`读取位点18→12、全部image读取119→113，但conditional branch 390→396、Phi 621→681、非debug指令6467→6563、模块173256→175664字节；triangle/mesh/GDF读取和box关联除法位点未减少。浮点opcode序列788→780且直方图不同，双方NoContraction均0。静态IR不证明动态fetch、寄存器、driver SASS或浮点精确性，后者由独立GPU合同验证。

交付新增可选[RunSealedGiRoomSdfReuse.cmd](RunSealedGiRoomSdfReuse.cmd)，调用上一轮SDF/shadow优化入口再启用reuse，末尾参数可覆盖；[启动器12对合同](../../build/gi_sdf_reuse_launcher.log)已退出0，主程序hash仍为同一643D。API默认false，原默认与既有入口均不改。测试入口为`--surface-cache-sdf-reuse-{test,quality,performance}`，正负开关为`--surface-cache-reuse-sdf-samples`/`--surface-cache-legacy-sdf-samples`。本轮保持每探针64条射线、屏幕/SDF追踪128/192步、receiver基础8/运动16及既有接触细节；本报告的提升只针对已测配置和轨迹。

## 2026-10-21：第五轮SDF执行效率验证——整数atlas寻址

本轮模型SDF总范围47.59→29.34ms（降低38.35%），事件循环151.48→113.28ms（降低25.21%，6.60→8.83 FPS）；尚未达到60/90/120 FPS。核心224帧严格质量通过，但独立75对图像的零差门槛**未全部通过：67/75精确，8对最大差1级8位值**。候选全部21张HD对第四轮归档精确一致，差异只见于本轮baseline的前两个检查点，成因尚未确认；不能声称全部HD配对精确通过。

主baseline固定第四轮shadow四项组合，候选仅打开`trace.sdf.fastAtlasAddressing`；两组native contact/cache、MRT、defer及shadow四项均开启，compact/AS storage reuse均关闭，没有重复计算第四轮收益。主程序冻结SHA256为`C8AB95B26F55A647E09CC3DD498DC8D89973A678A92BE63561534649D17737BA`，[身份记录](../../build/gi_sdf_efficiency_identity.json)明确保存HD严格配对false及候选21图归档精确结果；[被测主程序归档](../../build/gi_sdf_efficiency_final_validated.exe)保留本次二进制。与894相比，六份核心shader中仅`probe_sdf_shaders.h`改变。

### 正式实窗性能

[正式日志](../../build/gi_sdf_efficiency_present_performance.log)与[完整统计表](../../build/gi_sdf_efficiency_present_performance_summary.md)记录RTX 4060 Laptop、Vulkan、1280×800实窗ABBA：每组每轨迹64样本，共640次主present；另有两组各112帧HD，渲染/计时进程退出0。主表每格为shadow bundle baseline→仅增加fast寻址。FPS来自事件循环墙钟均值，非GPU倒数或物理scanout；无锁频、置信区间或正式GPU原子诊断。

| 轨迹 | 事件循环均值 ms | 中位数 ms | P95 ms | 事件循环 FPS |
| --- | ---: | ---: | ---: | ---: |
| 静止 | 86.23→66.39 | 85.47→66.57 | 98.44→70.18 | 11.60→15.06 |
| 相机移动 | 99.26→76.00 | 101.23→75.97 | 107.60→81.71 | 10.07→13.16 |
| 模型移动 | 151.48→113.28 | 149.65→111.97 | 185.16→123.21 | 6.60→8.83 |
| 新露出区域 | 103.90→86.00 | 108.27→85.95 | 114.98→97.01 | 9.62→11.63 |
| 灯光变化 | 73.81→74.23 | 73.12→72.25 | 80.60→89.35 | 13.55→13.47 |

| 轨迹 | GPU整帧均值 ms | SDF总范围 ms | 嵌套kernel ms | 嵌套pack ms |
| --- | ---: | ---: | ---: | ---: |
| 静止 | 81.01→61.68 | 40.55→28.58 | 38.33→26.63 | 2.19→1.94 |
| 相机移动 | 94.25→71.30 | 47.55→29.78 | 45.29→28.19 | 2.24→1.57 |
| 模型移动 | 101.02→77.42 | 47.59→29.34 | 45.09→27.84 | 2.48→1.48 |
| 新露出区域 | 99.04→81.01 | 46.78→30.59 | 44.29→28.85 | 2.47→1.73 |
| 灯光变化 | 68.50→69.19 | 30.30→29.42 | 28.46→27.71 | 1.83→1.69 |

灯光路径均值慢约0.57%，P95从80.60升至89.35ms，不能称全场景提速。Kernel包含screen-hit认证、PSRefine软件BVH/三角形查询、Mesh/GDF步进、精化和Surface Cache lookup，并非纯SDF march；pack是原布局打包，子范围与总范围不相加。pack算法及CPU准备代码未优化，但模型pack 2.478→1.482ms、backend encode 23.236→14.383ms、SDF场景准备8.808→6.836ms也有变化；不能把所有未改阶段的差值或全部loop改善都归因于整数运算。新增timestamp位于原自然pass边界，两组共有，无新增中途Submit；跨轮数值也不能直接作为本轮因果对照。

工作量对照通过：各轨迹每组探针预算36864000相同；模型64帧均为24576个面积光面、403240次graphics draw、405864次descriptor分配、30400次UBO快照复制、425721856字节物理纹理更新。双方预热后shadow atlas均零新分配、64次复用，AS仍为64次完整build、0次storage reuse、384次buffer分配、128次handle创建。没有降低采样、更新、接触阴影或过滤预算，也没有用输出状态计数冒称物理texture transaction。当前模型GPU仍约77.42ms，其中SDF总29.34ms、接收面补追12.51ms、探针空间过滤10.06ms，最终fence约78.65ms，仍是后续瓶颈。

Cold不计入FPS：首次baseline为31211.6ms，第一次新shader模式为2347.55ms，之后candidate为67.566ms、baseline为109.464ms；后续共享已热缓存，不能称31.2秒→2.35秒的成对冷启动加速。前置[320次present预检](../../build/gi_sdf_efficiency_initial_present_performance.log)只作诊断，不代替上述正式640次present。数据纹理、MRT布局、原64MiB uniform ring及48MiB shadow深度池未缩容；保留新旧shader/pipeline会带来编译和代码缓存开销，驱动代码cache及峰值显存未量测，不能声称零内存开销。

### 严格质量与HD限制

[完整质量](../../build/gi_sdf_efficiency_quality.log)已退出0：224帧原几何/owner/status/方向/起点/actual-N/readable签名精确一致，72482816个packed texel、108724224个离散metadata通道及接触/时域GI、卡片atlas、梯度/接触对比误差均0；coverage 28672000、源metadata 18063360、接收面射线42461616、fresh 163140、运动174282、已知黑/未知各256000保持原值。224个完整depth签名及14份全atlas逐FP32位比较88080384个深度像素也均为0，严格门槛未放宽。

[HD与归档全量审计](../../build/gi_sdf_efficiency_full_image_comparison.log)及[每对差异数据](../../build/gi_sdf_efficiency_full_image_comparison.json)仍为**严格FAIL、退出1**。12对小图全部精确；本轮HD配对17/21精确，baseline对第四轮归档17/21精确，candidate对第四轮归档21/21精确，总计67/75。四个非零HD配对的差异如下，baseline/archive出现相同计数；这里的pixel/channel均是单对图像计数，alpha差异0。

| HD帧 | renderer差异pixel/channel | temporal差异pixel/channel | contact | 最大8位差 |
| --- | ---: | ---: | ---: | ---: |
| 64 | 18/18 | 46/46 | 0 | 1 |
| 65 | 8/8 | 7/7 | 0 | 1 |

其它HD检查点和全部HD contact均精确。候选归档相同不能代替配对严格通过；关键帧contact相同也不证明前63帧全部current/alpha/count逐位相同。差异根因未定，没有额外重放结果，也不将其锁定为寻址误差、缓存初态或Reset残留。计时进程退出0与图像审计退出1是两个独立结果，保留原零阈值失败，未因最大差只有1而改为PASS。

[低层Vulkan合同](../../build/gi_sdf_efficiency_branchless_backend_gpu.log)、[12对focused](../../build/gi_sdf_efficiency_branchless_contracts.log)、[CPU 3/3](../../build/gi_sdf_efficiency_cpu.log)及[frame-command](../../build/gi_sdf_efficiency_frame_cpu.log)均退出0。Focused的1843200个packed texel/7372800个FP32通道、4718592个深度像素逐位相同，2次resize和2次无reset历史切换通过。低层生产helper覆盖1024/257/1031、行边界/末地址/INT_MAX、单输出/MRT，192个整数地址与432个实际采样helper记录通过；负值/INT_MIN仅对照legacy GPU观察，未把负余数当数学oracle或做负texelFetch。

### 实现与证据边界

CPU确认`scene.Width()==1024`且`PackedGeometry.Width()==1024`、visualMode为0才选branchless专用shader；对象、mesh voxel、triangle/BVH三个热helper用mask/shift，不逐tap判断。其它布局整shader退回legacy、actual `probeSdfFastAtlasAddressing=false`；生产合法索引原本要求非负，未承诺热helper的负index回退。纯等价开关不加入GI history key。UE参考为`DistanceFieldLightingShared.ush:54–56,424–426`的atlas mask/log2与线性brick index位解码；本轮保留原2D atlas、浮点采样和精化流程，并非移植UE完整brick系统。

[离线SPIR-V证据](../../build/gi_sdf_ir_evidence/README.md)直接编译生产MRT构造器并沿用backend目标/优化/remap：两份SPIR-V验证通过，静态SDiv 107→67、SMod 110→70，mask/shift各增加40，conditional branch均390；浮点算术/比较/extinst的有序opcode及类型签名均为相同788项。该检查未比较SSA操作数DAG，双方NoContraction均0；它不证明driver SASS、动态指令、寄存器、物理fetch或运行时bit-exact，不能代替上述GPU合同及HD失败记录。

首个逐tap guarded候选曾因[严格focused失败](../../build/gi_sdf_efficiency_debug_ray_contracts.log)拒收，未进入性能比较：test6 finalSDF ray551的status15、steps8/triangle-1、方向和混合fetch1090相同，但hit.x与distance各1 ULP、hit.y 2 ULP；distance已变化，不能仅归因最终坐标FMA。第二候选改为CPU布局校验后的branchless版本，保留该case及原逐位门槛，没有通过precision/容差放宽绕过失败。

交付仅增加可选入口[RunSealedGiRoomSdfOptimized.cmd](RunSealedGiRoomSdfOptimized.cmd)，调用第四轮shadow组合并启用fast寻址；原默认及既有入口均不改，末尾参数可覆盖。[新启动器12对合同](../../build/gi_sdf_efficiency_launcher.log)已退出0，主程序仍为同一C8AB。三个测试入口为`--surface-cache-sdf-efficiency-{test,quality,performance}`，正负开关为`--surface-cache-fast-sdf-addressing`/`--surface-cache-legacy-sdf-addressing`。本轮保持每探针64条射线、屏幕/SDF追踪128/192步、receiver基础8/运动16、原过滤与接触细节；正式预热为64帧静止和16帧真实轨迹，无单项消融。它是有明确HD严格验收限制的可选优化，不替换原默认。

## 2026-10-21：第四轮执行效率优化——面积光阴影提交与常量快照

本轮在保持GI和直接阴影结果精确一致的前提下，把模型持续移动时的事件循环均值从220.80ms降到153.34ms（降低30.55%，4.53→6.52 FPS），主要减少了面积光阴影的CPU准备和后端提交工作。该收益没有达到60/90/120 FPS，也不是所有场景都更快。控制组采用原启动器默认的原生接收面补追、原生缓存可见性、MRT单次求值和批量上传；两组都关闭第三轮的压紧与AS存储复用。候选只增加面积光caster准备复用、显式保守包围盒剔除、阴影图集存储池和帧内同内容UBO快照复用，四个API开关均默认false，原启动器保持不变。上一轮第三轮结果保留在下节，不能把它的SDF或AS收益移算到本轮。

交付主程序SHA256冻结为`894001B5D2EC4F874EA82E80258A2477445E7B36313FFC28ED5AB8E3FFB3C6A5`。[Release构建](../../build/gi_shadow_submission_build.log)、[Vulkan后端合同](../../build/gi_shadow_submission_backend_gpu.log)、[12对直接深度合同](../../build/gi_shadow_submission_contracts.log)、[生产pipeline生命周期合同](../../build/gi_shadow_submission_lifetime_gpu.log)、[CPU 3/3回归](../../build/gi_shadow_submission_cpu.log)、[frame-command回归](../../build/gi_shadow_submission_frame_cpu.log)和[完整GI质量](../../build/gi_shadow_submission_quality.log)均通过并退出0。生命周期程序独立构建并在真实Vulkan上运行；主程序后端、focused、quality与本轮正式实窗性能使用上述同一冻结程序。[身份记录](../../build/gi_shadow_submission_identity.json)同时保留F25控制版本和894交付版本，并确认本轮6份核心GI shader文件哈希未变；[冻结主程序归档](../../build/gi_shadow_submission_final_validated.exe)保留本次被测程序。

完整质量结果：224帧原13项几何/owner/status/方向/起点/actual-N/readable签名精确一致，coverage 28672000像素帧、源metadata 18063360条、接收面射线42461616条、fresh 163140、运动接收像素帧174282、已知黑256000、未知256000，均与上一轮相同。72482816个packed texel、108724224个离散metadata通道、接触与时域RGBA、卡片atlas、梯度和接触核心/外围对比误差全部为0。本轮在原相对RMS验收之外额外强制GI/packed精确相等，并非只因日志沿用的1%总结文字而接受微小差异。面积光深度224帧完整签名一致，14份完整atlas逐FP32位比较88080384个深度像素，包括clear像素，差异为0。

12对focused合同另外逐像素比较12976128个完整深度像素，12次静止cache hit、2次radiance-only编辑、1次关闭恢复、2次atlas尺寸变化、12次actual snapshot模式检查均通过，观察到10570次按面caster剔除。低层Vulkan合同验证真实hash碰撞A/B/A仍保留不同内容，逻辑buffer/range和旧不可变快照、shader/texture descriptor、不同帧退休均正确；indexed draw保留firstIndex/baseVertex、双实例、非零firstInstance与零count余量。

独立生命周期合同确认Record未提交/Discard不改变已完成depth、encoder租约仍有效时分配第三幅、尺寸往返保留旧租约、Cancel后可复用、pool关闭depth parity和Shutdown退休句柄；未知/NaN/Inf/反向包围盒与cutout layer均保留96次draw，可信包围盒可剔除且全depth不变。近面跨越专项保持80次draw、仅剔除16次，其中64个近面跨越的±X/±Z面不会被旁侧平面误拒。cutout案例使用固定自定义shadow shader，只证明layer保守回退，未覆盖完整alpha-discard纹理语义；精确贴面阈值和提交后Abort故障注入也没有专项实测，不扩展声称覆盖。

### 实窗性能与适用范围

最终配对主表为RTX 4060 Laptop、Vulkan、1280×800、MAILBOX三图、vsync关闭的真实窗口呈现。ABBA每组每轨迹64样本；每格为控制组→四项组合候选。FPS按事件循环墙钟均值倒数计算，不是GPU倒数或物理scanout计数。

| 轨迹 | GPU均值 ms | 事件循环均值 ms | 事件循环中位数 ms | 事件循环P95 ms | 事件循环 FPS |
| --- | ---: | ---: | ---: | ---: | ---: |
| 静止 | 79.07→71.39 | 82.68→76.77 | 80.99→74.19 | 104.55→103.78 | 12.09→13.03 |
| 相机移动 | 91.06→73.21 | 94.66→78.26 | 94.14→79.54 | 115.09→83.05 | 10.56→12.78 |
| 模型移动 | 100.83→98.88 | 220.79→153.34 | 218.32→164.66 | 260.71→178.70 | 4.53→6.52 |
| 新露出区域 | 100.76→98.73 | 104.78→104.17 | 108.41→111.61 | 116.43→120.66 | 9.54→9.60 |
| 灯光变化 | 74.96→74.22 | 79.02→79.80 | 77.89→80.30 | 99.94→84.09 | 12.65→12.53 |

模型轨迹均值下降30.55%，但灯光变化均值79.02→79.80ms略慢约0.98%；新露出均值只改善约0.58%，其中位数108.41→111.62ms、P95 116.43→120.66ms变差。静止帧没有阴影刷新，后端encode实际从1.576→3.080ms增加；该场景GPU/SDF下降抵消了新增主机开销，不能将其所有整帧收益归因于本轮优化。相机/新露出等未改算法的GPU范围也有变化，未锁定频率、温度或系统调度，未计算置信区间；ABBA是一次受控工作量对照，不是所有机器与场景稳定提速的保证。

交付采用可选组合入口[RunSealedGiRoomShadowOptimized.cmd](RunSealedGiRoomShadowOptimized.cmd)，适合频繁模型移动并触发阴影更新的场景；原[RunSealedGiRoom.cmd](RunSealedGiRoom.cmd)与[RunSealedGiRoomCompacted.cmd](RunSealedGiRoomCompacted.cmd)均不变。新入口只选择已测Release程序并打开本轮四项开关，不额外打开第三轮压紧/AS复用；末尾用户参数仍可覆盖。[启动器完整12对合同](../../build/gi_shadow_submission_launcher.log)已退出0，验证参数转发和退出码，之后主程序哈希仍为894。它是可选优化对照，不能称全场景默认提速版，单独snapshot选项更不应视为通用加速。

模型轨迹的阶段均值如下。前端面积光record包含在frame_record_and_upload中，后端encode/fence包含在render-thread总时间中；这些嵌套范围不能相加。

| 模型阶段 | 控制组 ms | 四项组合 ms | 解释 |
| --- | ---: | ---: | --- |
| 前端场景准备 | 10.449 | 10.216 | 包括SDF场景准备 |
| 前端命令record/上传 | 35.542 | 14.515 | 包含下列阴影record与资源准备 |
| 面积光阴影record | 22.422 | 2.901 | caster准备复用、保守剔除与存储复用 |
| pipeline资源准备 | 12.282 | 10.909 | 前端record内的子范围 |
| 后端命令encode | 64.510 | 23.775 | 在render-thread执行内 |
| render-thread总执行 | 174.525 | 128.488 | 包含GPU提交/等待，不是纯CPU |
| 最终frame fence等待 | 102.529 | 100.362 | 保留；包含GPU完成等待 |
| 面积光阴影GPU | 2.049 | 0.767 | 直接深度draw减少，384个面保留 |
| SDF/GDF追踪GPU | 46.084 | 46.733 | 未改追踪器，略慢 |
| 接收面补追GPU | 13.556 | 13.718 | 未改算法 |
| 探针空间过滤GPU | 11.033 | 11.010 | 未改过滤预算 |

模型GPU整帧仅100.825→98.880ms，SDF/GDF范围46.084→46.733ms并未提速；明确改善的是前端阴影record 22.422→2.901ms与后端encode 64.510→23.775ms。最终fence仍102.529→100.362ms，不能声称同步等待被消除。frame queue仅0.0374→0.0345ms，diagnostic gap 0.1412→0.0306ms、completion residual 0.0656→0.0253ms，未显示大的隐藏积压。两组测量期pipeline创建均为0；SDF场景CPU准备仍约10.30→10.07ms。GPU帧范围不含present拷贝，真正loop墙钟包括呈现与完成等待。

### 实际工作量、写入和存储

下表为主ABBA模型轨迹每组64帧累计值；标注“最大值”的两类字段是单帧/进程保留的最大值，未除以64。graphics计数不包含compute。

| 计数/字节 | 控制组 | 四项组合 |
| --- | ---: | ---: |
| 面积光面pass | 24,576 | 24,576 |
| 面积光caster实际draw | 614,400 | 390,312 |
| 按面剔除caster | 0 | 224,088 |
| 全部graphics draw | 627,328 | 403,240 |
| graphics descriptor分配 | 629,952 | 405,864 |
| descriptor写入条目 | 3,153,728 | 2,033,288 |
| graphics UBO快照复制次数 | 1,001,408 | 30,400 |
| graphics实际快照复制字节 | 355,873,792 | 3,739,648 |
| ring使用字节，单帧最大值 | 5,580,208 | 78,064 |
| 阴影caster材质参数逻辑Upload字节 | 550,502,400 | 1,433,600 |
| 阴影caster model逻辑Upload字节 | 39,321,600 | 102,400 |
| 阴影material pins | 614,400 | 1,600 |
| 阴影图集新分配 | 64 | 0 |
| 阴影图集复用 | 0 | 64 |
| 深度池payload字节，最大值 | 50,331,648 | 50,331,648 |
| 深度池项数，最大值 | 2 | 2 |

每帧384面×25 caster的9600次阴影draw降至平均6098.625次（减少36.47%）；累计减少224088次，恰好等于culled caster-face计数，全部graphics draw及descriptor分配的差额也相同；snapshot-only的descriptor仍为每帧9843个，因此不把组合descriptor减少单归因于内容去重。保留了64帧相同的24576个面积光面、相同caster集合与深度像素，没有把prepare复用误称为draw实例合批。每帧graphics快照复制15647→475次，实际复制约5.30MiB→57.06KiB（减少98.95%）；ring单帧最大使用5580208→78064字节。逻辑Upload量和后端按绑定range复制量是不同口径，不能混成同一字节预算。

候选64帧只有46144次content lookup、15744次reuse/comparison、无自然hash碰撞；actual enabled为64帧，控制组为0。碰撞正确性由独立强制碰撞合同证明，不因正式场景计数为0而略过。预热后候选atlas分配0、每帧复用1次，控制组每帧新分配1次；保留深度池最大两项48MiB。控制组也记录48MiB是因为ABBA后段仍保留候选建立的池，并不表示pool关闭路径主动复用了它。

本轮没有削减其余GI工作：两组模型轨迹同为2752个物理纹理更新区、425721856字节，专用上传submit/fence和staging分配都为0，原批量上传arena仍保留8MiB；探针射线/缓存更新/属性与几何上传工作量相同。两组仍各64次AS完整build、0次AS storage reuse、384次AS buffer分配与128次AS handle创建，未冒用第三轮AS复用收益。

### 单项消融、冷启动和剩余瓶颈

下表四项均为独立预热后的16帧模型轨迹，独立于主ABBA，样本较少且不是各自重新配对的baseline；不能把四项差值相加或将某项的GPU变化直接视作该开关的算法收益。

| 单独选项 | GPU均值 ms | 事件循环均值 ms | 中位数 / P95 ms | 后端encode ms | 阴影record ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| snapshot only | 109.240 | 328.123 | 332.547 / 347.548 | 167.489 | 20.839 |
| caster preparation only | 88.619 | 176.659 | 179.025 / 255.631 | 48.714 | 6.566 |
| atlas storage only | 104.306 | 218.088 | 215.713 / 269.091 | 62.332 | 18.971 |
| caster culling only | 84.431 | 179.254 | 170.232 / 216.921 | 47.100 | 15.601 |

snapshot-only明显回退到328.123ms，后端encode为167.489ms；虽然复制同样只有每帧475次，但仍有原9600次caster准备/draw，16帧产生250352次content lookup/242752次reuse-comparison，远多于组合模式平均每帧721次lookup。减少写入并不自动减少CPU总开销。prepare-only与cull-only观察到较低loop，atlas-only接近主baseline，但这些单项数据不能替代组合策略自己的640帧实测。本轮保留上述回退，未将不利样本删除。

四次模式切换的未计时cold阶段依次为baseline trial0 **36621.4ms**、candidate trial1 **106.051ms**、candidate trial2 **74.7709ms**、baseline trial3 **167.718ms**。trial0包含第一次shader编译、场景准备与AS；后续模式已共享热缓存，不能宣称冷启动由36.6秒加速至0.1秒。这些时间全部在正式静止64帧及轨迹16帧预热之前，未放入FPS主表。

下一轮的主要限制已是GPU：模型SDF/GDF约46.73ms、接收面补追13.72ms、探针空间过滤11.01ms，阴影GPU已约0.77ms；继续只缩减阴影CPU命令不足以实现60/90/120 FPS。应按现有计数继续剖析SDF/GDF遍历、补追与过滤的实际GPU负载，并单独评估静止/非更新帧1.5ms左右的额外快照查找开销。若重新组合第三轮压紧或AS复用，需另做同二进制配对，不从本轮数据推定组合收益。

本轮参考本机UE5.7的共享渲染基础，而非只修改Lumen追踪器：`ShadowDepthRendering.cpp:1117–1173`先处理静态阴影缓存，再以`ShadowDepthPass.BuildRenderingCommands`和InstanceCulling参数准备/提交阴影draw；`MeshDrawCommands.cpp:490–537`只在状态桶、PrimitiveId和实例条件满足时合并draw。本实现借鉴重复准备和可见性工作的拆分，没有引入UE GPUScene、RDG多线程提交或动态实例合批，也没有改变384个面积光采样面的投影、深度shader和采样权重。

实现边界：

- `areaShadows.reuseCasterPreparation`对每个eligible caster保留独立model/material逻辑UBO，每次图集刷新各上传一次；逐面仍使用原shader、texture、mesh和完整DrawIndexed范围/实例参数。材质参数的共享逻辑buffer不会被不同caster提前覆盖。
- `areaShadows.cullCasters`只接受显式有效世界包围盒的opaque caster；8个clip角点在有浮点余量的同一平面外才剔除。未知、cutout、近面跨越/贴边、非有限数据保留。sealed-room静态primitive的包围盒来自实际raster mesh的局部AABB，不以SDF近似边界代替；import/deform未擅自附加bounds。
- `areaShadows.reuseAtlasStorage`仅复用没有completed/pending/encoder lease的同尺寸图集，保留已完成深度直到可靠提交退休。不同尺寸的未租用池项可清理；关闭时仍走原fresh创建。pool字节只统计池中的深度纹理，不代表整个renderer显存；关闭开关不清理已存在池，因此ABBA后控制组也可能显示候选曾建立的保留池，不能把该值当作本模式新分配量。
- `areaShadows.reuseUniformSnapshotContents`在一帧内按range与完整字节去重；hash命中仍做完整memcmp，各逻辑buffer/range保持独立版本索引。descriptor保留完整34-word键及相等比较；关闭时仍用原版本快照与树索引。帧首清CPU索引，最终fence、GPU字节布局和浮点表达式保持。

验收方法：`--surface-cache-shadow-submission-test`运行12对直接深度合同，覆盖模型/相机/光源位置/光源轴/近远面变化、grid/tile/render尺寸变化、静止cache hit、radiance-only编辑、关闭恢复与四个独立模式；每组先可逆编辑emitter位置再精确赋值恢复，强制相同状态的真实刷新（snapshot-only不改变阴影几何cache key）；下一帧另验证正常静止cache hit。完整atlas包括clear像素按FP32位逐像素比较。更低层的DrawIndexed范围/实例、hash碰撞、Discard/Cancel与资源租约合同由后端测试单独覆盖；`area_light_gpu_test --shadow-submission-contracts --vulkan --validation`直接使用生产pipeline的Record/Discard，验证未提交encoder仍pin住图集时必须分配第三幅、64→128→64尺寸变化保留老租约、Cancel后可复用，以及关闭池的同姿态depth bit parity。该合同独立于普通Render接口，已在上述真实Vulkan日志中通过。

`--surface-cache-shadow-submission-quality`沿用两个视角各112配对帧、13项geometry/owner/status/方向/起点/actual-N/readable签名、接触/时域/卡片atlas、完整packed ray与已知黑/未知保护；本轮额外要求packed和GI RGBA逐值精确相等。224帧均检查完整面积光深度的尺寸、有限范围、非空覆盖和全atlas字节签名；每视角64/65/72/81/88/97/112帧保存完整深度并按位逐像素对照，共14次直接深度比较。其余帧只有完整签名比较，不冒称逐像素比较；避免保存112份24MiB深度导致额外约2.6GiB常驻。[独立归档图像对照](../../build/gi_shadow_submission_image_comparison.log)及[逐对数据](../../build/gi_shadow_submission_image_comparison.json)已通过并退出0：12对static64小图（本轮A/B及F25归档）、21对本轮HD配对、42对两组HD分别对F25归档，共75对；每对所有像素、通道、最大差和MSE均为0。此检查排除了已测图像的共享控制组退化。

`--surface-cache-shadow-submission-performance --benchmark-present --surface-cache-dynamic-frames 32`采用1280×800实窗五轨迹ABBA，每批64静止帧和16真实轨迹帧预热，每组每轨迹64样本、共640主present；snapshot/caster-preparation/atlas-storage/culling四个单项模型消融各16帧、共64present，单独报告，不能相加为主表收益。正式样本内不下载深度或图像；最后另跑两个模式各112帧HD，保存7个关键帧的renderer/contact/temporal，保持GI采样和过滤预算。

新增统计从已有完成帧回调取得，无额外中途Submit或队列回读：旧24项host span与7项graphics/AS计数不改顺序；`uniformSnapshots`记录actual enabled、content lookup/reuse/comparison/collision、graphics复制字节、ring占用及descriptor hit/write；`shadowSubmission`记录pass、atlas字节、refresh/cache/preparation、eligible caster、实际draw、按面culled、材质/model上传、material pins与池分配/复用/保留内存。draw/descriptor/UBO复制统计仅graphics；ring字节包含padding及可能的compute，不能混作graphics纯payload。主表要求GI工作量和面积光pass相同，实际caster draw允许经保守剔除下降；固定尺寸模型轨迹预热后应不再分配图集。GPU范围与嵌套host span不相加，交互FPS仍以事件循环墙钟计算。[正式实窗性能](../../build/gi_shadow_submission_present_performance.log)的640次主present、64次单项消融和224帧HD序列全部通过并退出0，测试后894哈希未变。

内存口径：sealed-room的25个caster各有model/material逻辑UBO，共50个逻辑句柄；Vulkan的CreateUniformBuffer仅建立CPU vector，不是50个原生GPU buffer。本轮仍使用原有固定64MiB原生uniform ring，降低复制字节和ring使用量不代表ring缩容或释放显存。相同尺寸稳态池的两幅图集各24MiB、合计48MiB只是深度payload；它不包含非池completed atlas、驱动对齐、descriptor池或其余renderer资源，不能称总显存或峰值显存。减少descriptor分配/写入也不证明底层descriptor池容量或VRAM同比下降。

## 2026-10-21：第三轮执行效率优化——待追踪射线压紧与加速结构存储复用

本轮的控制组是第二轮已通过的原生接收面补追、原生缓存可见性、MRT单次探针求值和批量动态上传；候选组只打开`trace.compactSdfRays`与`surfaceCacheGi.reuseAccelerationStorage`。64条/探针、screen/SDF上限128/192、接收面基础8条与运动16条、fresh追加、64帧静态收敛、过滤和接触阴影参数都保留。两组均已没有逐纹理区域的专用提交/等待，因此不能把上一轮1376次上传等待降至0再次计为本轮收益。正式结论以同一交付Release二进制中的配对测试为准，下面同时保留初测回退记录。AS稳态分配确实归零，但模型SDF范围46.46→48.18ms仍未稳定提速，因此两个新选项均保留为可选对照，原启动器默认不变。

交付主程序SHA256冻结为`F25EA8394B073E994968DD2E3A1785999D1870B63548B894A290CDF65DEB085E`。[Vulkan后端合同](../../build/gi_compaction_delivery_backend_gpu.log)、[9组压紧合同](../../build/gi_compaction_delivery_contracts.log)、[完整224配对帧质量](../../build/gi_compaction_delivery_quality.log)、[CPU 3/3回归](../../build/gi_compaction_delivery_cpu.log)与[frame-command回归](../../build/gi_compaction_delivery_frame_cpu.log)均通过且退出0；之后的最终交互性能也使用该同一冻结程序。

交付质量结果：原13项几何/owner/status/方向/起点/actual-N/readable签名224帧精确一致；coverage 28672000像素帧、源metadata 18063360条、实际接收面射线42461616条；fresh 163140、运动接收像素帧174282、已知黑256000、未知256000，全部与第二轮一致。72482816个packed texel、108724224个离散metadata通道、所有接触/时域RGB、缓存atlas、梯度与接触核心/外围对比误差均为0。224帧队列检查3803906个原ID，pending范围16160–19769，分配/分类/间接/消费/overflow及原发射条件全部通过。9组小规模合同另外覆盖两个空队列、两次缩小、三次resize，1327104个packed texel和1990656个离散通道误差0，独立检查79574个原ID。上述质量表示保留已验收估计器，不是独立物理真值精度提升。

交付baseline两个视角static64的renderer/contact/temporal分别与ADFF阶段baseline、第二轮optimized归档对照，共12对320×200图片完全逐像素一致，差异像素/通道与最大8位差均为0，排除了已测控制视图的共享退化。[最终实窗性能日志](../../build/gi_compaction_final_present_performance.log)的640次主present、32次单项消融及224帧HD预览全部通过并退出0，主程序仍为同一F25。收益不是全路径一致，因此两个新选项继续显式opt-in，原[RunSealedGiRoom.cmd](RunSealedGiRoom.cmd)保留已验收的MRT+defer默认。[RunSealedGiRoomCompacted.cmd](RunSealedGiRoomCompacted.cmd)是仅Release可用的可选对照启动器，打开compact+AS存储复用，不是保证提速版；末尾用户参数仍可覆盖这两个选项。[对照启动器完整9组合同](../../build/gi_compaction_delivery_launcher.log)已通过并退出0，验证了参数转发与退出码；测试后F25哈希未变。

最终配对主表如下。RTX 4060 Laptop、Vulkan、1280×800、实际窗口呈现，ABBA每组每轨迹64样本；FPS为事件循环墙钟均值的倒数，不是GPU时间倒数或物理scanout计数。

| 交付路径 | GPU均值 ms，基线→候选 | 事件循环均值 ms，基线→候选 | 事件循环FPS，基线→候选 |
| --- | ---: | ---: | ---: |
| 静态 | 65.9130 → 66.3216 | 70.4698 → 70.7364 | 14.1905 → 14.1370 |
| 相机移动 | 88.1114 → 68.1654 | 92.4028 → 72.2535 | 10.8222 → 13.8402 |
| 模型运动 | 103.918 → 96.5298 | 234.252 → 225.818 | 4.2689 → 4.4283 |
| 新露出区域 | 99.2750 → 85.3297 | 103.858 → 88.8820 | 9.6285 → 11.2509 |
| 光源变化 | 70.3644 → 63.7322 | 74.8755 → 67.7729 | 13.3555 → 14.7552 |

| 交付路径 | 事件循环中位数 ms，基线→候选 | 事件循环P95 ms，基线→候选 |
| --- | ---: | ---: |
| 静态 | 67.4625 → 69.8670 | 95.8493 → 101.877 |
| 相机移动 | 88.3879 → 71.1760 | 117.243 → 97.2591 |
| 模型运动 | 236.145 → 226.550 | 262.474 → 254.168 |
| 新露出区域 | 103.971 → 96.7049 | 119.020 → 102.913 |
| 光源变化 | 73.8291 → 66.7993 | 82.4505 → 74.4779 |

相机、新露出、光源变化的本次事件循环均值分别减少约21.8%、14.4%、9.5%；模型减少约3.6%。静态均值增加约0.38%，P95也增加，不能说全路径提高。更关键的是模型`sdf_trace`仍为46.4571→48.1764ms，约增加3.7%，后端命令编码71.2626→73.2501ms也没有改善。模型GPUframe下降同时伴随未改算法的probe spatial 12.9615→9.9975、contact trace 14.4036→12.9160、reconstruction 4.3397→2.7499ms等下降。未锁定GPU频率、热状态与系统调度，未给置信区间；这些范围变化不能全归因于compact，也没有证明模型SDF追踪稳定提速。初测和最终均保留，以免只挑有利轮次。

交付模型64样本的真实资源/工作对照：

| 计数 | 基线 | 候选 |
| --- | ---: | ---: |
| AS完整构建 | 64 | 64 |
| AS存储复用 | 0 | 64 |
| 新AS buffer分配 | 384 | 0 |
| 新BLAS/TLAS句柄 | 128 | 0 |
| 物理纹理上传区域 | 2752 | 2752 |
| 物理纹理上传字节 | 425721856（406 MiB） | 425721856（406 MiB） |
| 专用上传提交 / fence等待 / staging分配 | 0 / 0 / 0 | 0 / 0 / 0 |
| 最大staging arena容量 | 8 MiB | 8 MiB |
| 探针物理预算 | 36864000 | 36864000 |
| 面积光深度面 | 24576 | 24576 |
| Graphics draw | 627328 | 627200 |
| Graphics descriptor分配 | 629952 | 629824 |
| Graphics UBO快照拷贝 | 1001408 | 1001344 |

候选分类36864000个物理记录，实际pending/consumed/Mesh调用均为10875666，GDF调用7782370，间接dispatch共169964个64线程组、尾部空线程2030，overflow为0。跳过的24479488个非活动记录与1506542个已确认命中没有被当作新的减少采样；原预算及逐射线输出已用严格质量保护。基线未记录实际march调用，不能以容量差冒充实际march减少量。

模型主机平均耗时的进一步分解如下。它们存在嵌套与不同线程/阶段，不能相加：

| 主机范围，ms | 基线 | 候选 |
| --- | ---: | ---: |
| 前端场景准备 | 12.5399 | 12.2481 |
| 前端命令记录 / 上传 | 39.1988 | 36.0158 |
| 提交 / render-thread / GPU / present等待 | 182.493 | 177.533 |
| SDF场景准备（前端场景准备内） | 12.4103 | 12.1118 |
| Pipeline资源准备（前端记录内） | 11.9342 | 8.3281 |
| 面积光阴影命令录制（前端记录内） | 26.3890 | 26.8148 |
| Texture update准备（资源准备内） | 7.9161 | 4.2693 |
| AS退休（texture update内） | 4.2758 | 0 |
| 后端命令编码 | 71.2626 | 73.2501 |
| AS准备（后端编码内） | 5.9437 | 4.4861 |
| AS顶点展开（AS准备内） | 4.1921 | 4.3365 |
| 后端fence等待 | 105.114 | 97.7766 |
| Render-thread执行（含上述编码 / 等待） | 182.119 | 177.005 |
| Render-thread排队 | 0.0306 | 0.0270 |
| 诊断前间隔 / 诊断服务 / 完成回调残余 | 0.1288 / 0.0032 / 0.2116 | 0.1272 / 0.0053 / 0.3687 |

每帧仍约9800个graphics draw、9841个graphics descriptor和15646个UBO快照；主要增量来自384个面积光深度面×25个投影物体=9600个depth draw/帧。面积光的GPU范围仅1.8807→1.4901ms，前端却花约26.8ms录制相关命令，后端编码总量约73.3ms。排队/诊断间隔都很小，不能再把模型大部分剩余墙钟笼统称为隐藏等待。下一轮优先保留384面与当前采样数，批处理depth caster draw、复用不变descriptor/UBO快照与深度atlas资源；然后再测设备本地compact queue和Mesh/GDF分阶段排程，不能先降低采样/步数换帧率。AS复用已消除稳态分配，但其余约4.3ms顶点展开和约12.1ms SDF场景准备仍在。

最终16帧模型单项消融仅用于归因：`compact_only`的GPU/SDF/事件循环均值为101.146/50.201/240.138ms，`reuse_only`为99.7939/46.9031/235.075ms。历史、顺序和样本量不同，它们既不能与主ABBA相加，也不能拿主combined结果作为单项默认收益。两个新开关因此都保留为可选对照；本轮没有达到60/90/120 FPS，也不承诺下一轮固定加速。

最终HD在64/65/72/81/88/97/112帧的renderer/contact/temporal共21对F25 baseline/candidate图片全部逐像素一致；F25候选与第二轮warmed optimized归档21对、与第三轮9B initial候选21对也全部一致，共63对1280×800图片的差异像素/通道及最大8位差均为0。HD序列不启用诊断replay或CPU oracle、未计时，完整图像前缀为`build/gi_compaction_final_present_preview_...`。

本轮进一步参考本机UE 5.7.1：

- `Engine/Shaders/Private/Lumen/LumenScreenProbeTracing.usf:395–500`的`ScreenProbeCompactTracesCS`，先检查原探针与射线范围及已有命中，再将仍需追踪的原射线索引写入队列；使用组内归约降低全局分配器争用。
- 同文件`:697–710`的`ScreenProbeTraceMeshSDFsCS`只消费有效压紧索引，将工作恢复到原探针/方向单元。原索引的语义不能替换成压紧后的序号，否则方向采样和输出位置会变化。
- `Engine/Source/Runtime/Renderer/Private/Lumen/LumenScreenProbeTracing.cpp:566–650`的`CompactTraces`，分离分配器、列表和间接dispatch参数；按有效数量生成工作组，而不是由CPU读回数量后逐次调度。这里只实现现有RHI中的帧内Compute、存储纹理和间接dispatch，没有移植UE完整RDG、异步Compute队列或多帧并行调度。

本地的屏幕命中有更强的几何确认契约，不能直接照搬UE的`!bHit`条件。安全探针的Surface Cache模式仍先用软件BVH确认屏幕命中；只有确认未命中且尚有Mesh SDF/GDF工作时才进入待追踪队列。状态5既可能表示发射半球拒绝，也可能来自命中背面，不能只凭该状态丢弃。分类阶段沿用原追踪函数的前半部分，并保存原`rayGlobal`、确认未命中标记、原屏幕状态、起点/近端距离和方向/已消耗查询数；间接追踪从原函数后半部分继续，使用原索引散射回相同的四texel记录。每帧分类时写入完整物理图集，包含非活动记录与容量padding；队列从大变小或变空也不会暴露上一帧结果。重置、分类/确认、间接追踪与散射都计入原`sdf_trace` GPU范围，不能只计紧凑追踪内核而漏掉新增组织成本。屏幕追踪继续使用上一轮MRT路径；SDF候选改为Compute直接写原图集，因此省去该阶段的MRT输出与打包。

队列容量与物理射线容量相等，每条48字节，另有16个32位控制字。容量变化时创建独立的数据owner代次，已录制帧同时持有程序与对应数据owner；旧列表随最后一个帧引用退休，不能在未提交帧仍引用旧句柄时立即销毁。按1280×800、当前间距32/候选8/方向8×8的固定配置，容量为576000条，队列净载荷为27648000字节（约26.37 MiB），控制字64字节；这是需要在报告中列明的额外驻留资源，不能描述成零内存成本的提速。当前queue/control通过通用Compute buffer分配，要求HOST_VISIBLE|HOST_COHERENT并选择首个兼容memory type，没有强制DEVICE_LOCAL；实际heap未记录，26.37 MiB是buffer净载荷而非测得的纯VRAM增量。GPU队列访问/原子操作的实际heap与传输影响仍需后续测量，不能把当前性能变化直接归因于PCIe或显存带宽。超出单buffer或间接工作组上限、不支持帧内Compute、未启用单次输出或采用其它SDF可视化路径时保留原MRT追踪，测试必须断言实际路径标志，不能以请求开关代替生效证明。

动态模型的加速结构仍执行原有完整`BUILD`，仍在CPU生成相同的展开顶点，也保留原场景更新频率和最终帧fence。固定拓扑/三角数量时复用原六个buffer和两个BLAS/TLAS句柄，避免重复分配、重复尺寸/属性查询及不变索引/单位实例数据重写；容量或拓扑不兼容时走原重建分配路径。这不是`UPDATE`/refit、变形实例TLAS更新或删除同步等待。固定模型轨迹的性能夹具要求两组AS构建数相同且大于0，控制组复用数为0，候选预热后每次构建均复用且新增buffer/句柄分配为0；这些断言只适用于该固定规模轨迹。

第三轮独立入口为`--surface-cache-compaction-test`、`--surface-cache-compaction-quality`与`--surface-cache-compaction-performance`。交互开关为`--surface-cache-compact-sdf-rays`/`--surface-cache-full-sdf-rays`、`--surface-cache-reuse-acceleration`/`--surface-cache-rebuild-acceleration`，F1提供对应选项。API默认均为关闭；原启动器不启用这两个新选项，可显式选择新的Compacted对照启动器。

验收采用以下分工，旧native与第二轮效率夹具的默认行为和阈值继续保留：

- 小规模独立合同包含9组同状态全MRT/压紧输出配对，覆盖192×128、191×123、160×101，方向分辨率4/8/16，安全/非安全起点，Mesh-only/Global-only、较低步数，以及非空→空→恢复与容量resize。Mesh/GDF都关闭时列表必须为空且间接X/消费数/实际march调用数为0；整个物理图集仍逐texel比较。
- 正式画质保留两视角、每组112帧、合计224个配对帧；静止64、连续运动16、分离停留8、回位停留8、最终收敛16。继续核对原13项几何/owner/status/起点/方向/actual-N/readable状态签名、逐像素coverage、已知黑与未知覆盖、完整screen/SDF图集及Surface Cache atlas。离散字段必须精确，连续packed字段仅允许原16倍FP32 epsilon包络；接触与时域输出继续给出原几何ROI的RMS、梯度及接触核心/外围对比，不能为通过测试放宽签名。
- 质量阶段额外离线读真实GPU队列，校验原射线ID范围与唯一性、原屏幕status、原probe位置/法线、出射半球、安全起点、原采样方向与屏幕命中确认标记。分配器、间接参数、消费数、分类分区与overflow同时校验。该检查独立验证原始发射条件；完整物理图集配对负责检测漏写、误散射与陈旧记录。它没有独立重建每一次GPU三角确认的完整CPU集合，不将其宣称为完整独立几何确认oracle。
- 正式交互性能沿用1280×800、实窗present、无HUD/validation/应用限帧/图像回读，五轨迹ABBA，每次64静态预热及16实际轨迹预热，初测每组每轨迹合计32样本/320次主present；最终修订改为每次trial测32帧，即每组每轨迹合计64样本/640次主present，以增加时序覆盖。另测模型轨迹的`compact_only`与`reuse_only`各16帧，分离新增因素，不与主ABBA相加。再做独立未计时的1280×800普通渲染112帧/组，在7个相同检查点保存最终画面、接触与时域输出。

性能记录包括13个原GPU范围、5项原墙钟范围、24项新增主机区间、7项后端计数、8项上传计数与16个GPU压紧控制字。14个后端区间覆盖命令组织、延期拷贝、AS几何打包/准备/退休、提交、fence、交换链、present及timestamp等；另有SDF/Surface Cache/其它前端准备、render-thread队列/执行、资源准备、区域阴影录制，以及诊断前间隔、诊断服务与完成回调残余。这些层级存在嵌套，不能相加当作整帧。诊断前间隔可能包含额外资源操作或系统调度，不能直接命名为纯删除或纯GPU等待。draw/descriptor/UBO计数仅覆盖当前graphics路径，未统计Compute自身的descriptor/UBO分配，日志明确该范围。

GPU计数分别报告物理分类容量、真正pending/consumed、间接工作组/尾部空线程、确认命中/未命中/耗尽、拒绝、无需字段工作、实际Mesh/GDF marcher调用和overflow。确认未命中计数与其它分类可重叠；不可把它再次加进互斥分区。控制组未插入marcher计数，因此不能把控制组容量直接称为实际march调用数。正式计时通过已有完成诊断回调读取已经可见的16个控制字，不新增GPU提交或独立等待；列表身份读回与图像检查只用于未计时质量阶段。主机整帧墙钟是交互性能的主要结论，GPU倒数不能替代present事件循环帧率。

本轮首个冻结Release主程序SHA256为`9B0A2B15E2163A562AE8CDD9F17BF11B509D5389355CD24E66F97A835552C502`；正常构建日志[gi_compaction_initial_build.log](../../build/gi_compaction_initial_build.log)完成且退出0。该二进制的[初测Vulkan后端合同](../../build/gi_compaction_initial_backend_gpu.log)通过延期上传、帧内Compute采样/UBO快照/存储图像、直接/间接/零dispatch及已退休计数器测试，并验证借用Compute owner由帧引用在render thread退休不会死锁，既有索引/depth/blend/geometry/resize/MSAA/present/ray-query合同继续通过。[初测CPU回归](../../build/gi_compaction_initial_cpu.log)3/3通过，frame-command单独测试退出0。

[初测9组压紧合同](../../build/gi_compaction_initial_contracts.log)通过：两个空队列、两次同容量缩小、三次容量resize，共比较1327104个packed texel、1990656个离散metadata通道，独立检查79574个原射线ID，连续字段最大误差0。两个空队列的pending/indirect-X/consumed/Mesh/GDF调用数均为0；Global-only组实际消费34668条且Mesh调用为0。该合同覆盖容量padding、恢复后输出和真实工作计数，不替代下面的完整静态/运动画质和交互性能验收。

本轮控制组另与第二轮归档候选独立核对：`gi_efficiency_final_quality_view{0,1}_optimized_trace_upload_frame64`与`gi_compaction_quality_view{0,1}_accepted_mrt_deferred_frame64`的最终renderer、contact tone4、temporal tone4共六张320×200图逐像素完全一致，差异像素/通道、最大8位差和MSE均为0；两个新控制视角已目视检查正常。这用于防止两组共享退化而仅凭A/B相同通过验收。

[初测完整质量日志](../../build/gi_compaction_quality.log)已通过并退出0，仍为上述同一`9B0A…C502`二进制。224个配对帧的原13项签名精确一致，28672000个coverage像素帧、18063360条源metadata、42461616条实际接收面射线保持一致；几何fresh像素帧163140、运动接收像素帧174282、已知黑256000与未知256000，均与上一轮验收数值相同。72482816个完整packed texel和108724224个离散metadata通道的误差均为0，连续字段最大绝对误差也为0；所有几何ROI/阶段的接触与时域RGB、梯度以及缓存atlas差异均为0，接触核心与外围对比保持一致。

新增224帧候选队列共独立检查3803906个原射线ID，每帧pending范围16160–19769，范围/唯一性/原起点和方向/分类分区/间接参数/消费及overflow合同全部通过。此质量结论表示同样输入下与已验收估计器保持一致，未声称独立物理光照真值精度提高。

初测主程序已归档为[gi_compaction_initial_validated.exe](../../build/gi_compaction_initial_validated.exe)，仍为`9B0A…C502`。[初测实窗日志](../../build/gi_compaction_initial_present_performance.log)完成主ABBA 320次present、32次单项消融与224帧HD预览，并退出0。结果存在回退，不能据此宣称所有路径提速：

| 初测路径 | GPU均值 ms，基线→候选 | 事件循环均值 ms，基线→候选 | 事件循环FPS，基线→候选 |
| --- | ---: | ---: | ---: |
| 静态 | 76.2634 → 66.2799 | 80.2370 → 70.2237 | 12.4631 → 14.2402 |
| 相机移动 | 90.5957 → 81.8386 | 94.5715 → 86.1036 | 10.5740 → 11.6139 |
| 模型运动 | 88.6289 → 95.5910 | 233.427 → 225.907 | 4.2840 → 4.4266 |
| 新露出区域 | 93.2843 → 95.2322 | 97.7770 → 98.9159 | 10.2274 → 10.1096 |
| 光源变化 | 80.2168 → 67.4761 | 84.3259 → 71.7820 | 11.8588 → 13.9311 |

其中模型GPU均值增加约7.9%，新露出区域GPU均值增加约2.1%、事件循环增加约1.2%。初测模型P95为261.055→264.509ms，新露出P95为104.983→108.633ms，均不能隐藏为平均改善。AS仍各构建32次，候选32次全部复用，新增buffer分配192→0、句柄创建64→0；物理上传仍各1376区域/212860928字节（203 MiB），专用上传等待两组均为0。初测模型队列在18432000个物理容量记录中实际消费5438074条，Mesh调用同数、GDF调用3891922次，overflow为0；控制组没有同样的march调用仪表，不能宣称实际march次数降低了容量与pending之差。

新主机仪表把模型开销进一步拆开：前端SDF准备11.9630→12.1967ms，面积光阴影命令录制28.2017→27.7351ms，后端命令编码81.8899→72.3673ms，后端fence等待89.7940→96.6435ms；AS几何展开仍4.4370→4.3633ms，AS退休4.9619→0ms。每32帧graphics draw为313664→313600，约9802→9800次/帧；其中面积光8×8个采样单元×6面=384个深度面，每面25个投影物体，产生9600次额外draw/帧。来源为`area_light_shadows_pipeline.inl:46–54`对每面的`Depth(...,true)`，以及`material_render_pipeline.cpp::Depth`逐投影物体录制。其GPU范围仅约1.7730→1.5124ms，CPU命令录制/编码却明显更重。后续应优先调查这些draw、descriptor/UBO快照与静态资源的批处理/复用，不降低光源采样数或接触射线预算。此处前端录制与后端编码分属不同阶段，AS等区间又嵌套在其中，不能把表中数值相加。

初测HD在64/65/72/81/88/97/112帧共21对renderer/contact/temporal图中，两组逐像素全部一致；候选对应第二轮warmed归档的21对也全部一致，共42对比较的差异像素/通道和最大8位差均为0。图片保留`gi_compaction_present_preview_...`前缀；这项跨版本检查不能代替新内核修订后的重新验收。

由于初测呈现混合性能，随后已精简分类器中理论上不可达、但仍带完整march调用的收尾分支；保留legacy追踪与压紧队列消费者的数学行为。这是控制流/寄存器压力的待验证优化假设，没有GPU寄存器计数器证据，不能先称为回退原因已确定。修订同时将逐dispatch的Compute→Host可见性依赖合并到一次Submit末尾，Compute→Compute/graphics/indirect依赖仍保留。CPU只在整帧完成后读取控制字，正式渲染没有中途CPU读队列或独立Submit等待。统一Host可见性footer位于FrameEnd GPU timestamp之后，其实际成本包含在事件循环/提交fence墙钟内，不计入`sdf_trace`或GPUframe；原GPUframe同样不包含present拷贝。因此SDF范围差值不能直接代表全部新增GPU组织成本。内核trim阶段的合同与质量使用`gi_compaction_final_*`日志单独记录；最终仍呈混合收益，压紧保持显式opt-in。原启动器继续第二轮已验收配置，不能拿上述combined FPS宣称AS复用单独的收益。

内核trim阶段归档SHA256为`ADFF78C05A5AAF584BEEC98C592FB70AE3F0782293708D1FE01C6A4D35AE09A5`，程序[gi_compaction_trim_validated.exe](../../build/gi_compaction_trim_validated.exe)、构建[gi_compaction_trim_build.log](../../build/gi_compaction_trim_build.log)，对应`gi_compaction_final_backend_gpu/contracts/quality`日志。该阶段完整合同与224帧质量已退出0，计数与9B及交付F25一致，全部误差0；它保留为中间证据，交付结论以F25的`delivery_*`日志为主。

严格质量完成后又增加一处主机诊断兼容保护：仅Vulkan执行`host.frameIndex==frameIndex`断言，避免对其它后端默认空统计套用Vulkan帧编号。现有OpenGL测量本就走独立query路径且没有传入这些主机统计，因此该修改是防御性保护，不表示发现原有GL benchmark已损坏。此后重编还纳入并行任务在共享pipeline/header增加的skinAlbedo调试输出；不能把新二进制描述成只改该guard、直接继承ADFF全部验收。

因此对F25重新运行了本节开头列出的完整交付合同和质量，再用同一程序测最终实窗ABBA；没有仅凭“guard不改变Vulkan语义”跳过共享代码重编后的重新验收。ADFF阶段日志保留，不与交付数据混合。

## 2026-10-21：第二轮执行效率优化——一次求值的探针输出与动态上传合批

本轮控制组继续使用第一轮已验收的原生接收面补追和Surface Cache光源可见性；候选组只改变探针结果写出的执行组织，以及模型变化时的纹理上传调度。两组均保留近端Mesh SDF、远端GDF、软件BVH精确确认的混合估计器、64条/探针、screen/SDF步数上限128/192、接收面基础8条与运动16条、已有几何fresh追加、静态64帧收敛和原接触过滤。第一轮归档的SHA256 `09308F961C312F6C71A350E3C1AFC6740BA3BD4C3B10CCA97ADDC9140E836FF7`作为历史版本保留；本轮收益以同一最终Release二进制中的新配对测试为准，不能直接用旧表的均值拼接新候选得到加速倍数。

本轮参考本机UE 5.7.1的以下实现职责：

- `Engine/Shaders/Private/Lumen/LumenScreenProbeTracing.usf:55–363`：`ScreenProbeTraceScreenTexturesCS`在一次射线工作中写出`RWTraceRadiance`和`WriteTraceHit`，将辐射率与命中信息留给后续阶段。
- 同文件`:395–449,551–710`：`ScreenProbeCompactTracesCS`读取已有TraceHit并保存待追踪索引，`ScreenProbeTraceMeshSDFsCS`只消费压紧后的索引并调用`TraceMeshSDFs`。本轮借鉴“一次求值、保存结果供后续使用”，没有移植UE的Compute/UAV压紧队列、间接dispatch或完整RDG调度。
- `Engine/Source/Runtime/Renderer/Private/Lumen/LumenSceneRendering.cpp:2585–2593`与`LumenMeshCards.cpp:504–554`：同一`FRDGScatterUploadBuilder`收集页表与卡片缓冲更新后统一`Execute`，卡片结构缓冲按2的幂保留容量并只上传dirty索引。本地只适配保留上传容量和合批执行这两项原则，没有减少本轮传输区域。
- `Engine/Source/Runtime/Renderer/Private/Lumen/LumenScene.cpp:519–610`：`FLumenSceneData::UploadPageTable`通过`FRDGScatterUploadBuilder::AddPass`组织页表和卡片页数据更新。这里采用显式排队的纹理区域拷贝与帧命令合批，没有将本地Vulkan实现写作UE RDG或GPU scatter uploader的直接移植。

旧探针图集每条射线占四个RGBA32F texel；旧fragment shader为每个记录分量再次求值共同的屏幕追踪或SDF/GDF过程。新`ScreenProbeTraceSettings.singlePassOutputs`使用四个MRT输出，在原图集四分之一宽度上，每个fragment只求值一次，再用只含`texelFetch`的打包pass恢复原来的四texel布局。屏幕追踪与SDF fallback继续消费相同的packed图集，probe slot、射线顺序、容量填充、状态、owner、步数、辐射率与方向的公开表示均不变。这消除的是四个记录分量的重复计算，没有把64条物理采样改成16条。

两个追踪阶段顺序复用同一组四张四分之一宽度RGBA32F scratch纹理，额外纹理容量合计等于一张原packed图集；全帧增加两个打包pass，分别包含在原screen/SDF GPU scope内。采样的共同函数体仍在fragment阶段编译，避免此前Geometry Shader单次调用试验的执行代价。旧`singleInvocation`实验保持关闭；本轮使用独立的`singlePassOutputs`，有效统计为`screenProbeSinglePassOutputs`、`probeSdfSinglePassOutputs`。它尚未压紧无效slot或把尚未命中的射线放入GPU工作队列，也没有改变SDF/GDF步进、Mesh SDF驻留、GDF重建算法或BVH认证。

上传优化由`SurfaceCacheGiSettings.deferResourceUploads`控制，最终传递为`UpdateRHITextureDesc.deferUntilFrame`。它同时覆盖Surface Cache的脏属性/元数据/几何区域，以及SDF场景的对象、几何和脏Global区域更新。Vulkan在接收更新时取得字节副本的所有权；回调成功表示数据已被接收，并不表示GPU已完成。下一次开始命令记录时，将待更新区域按16字节对齐复制到可复用的host-coherent staging arena，并把拷贝和资源依赖记录到同一帧命令中。RGBA32F直接保留原字节，避免逐标量格式转换；实际区域和传输字节仍不减少。

同一纹理的重叠更新按入队顺序拷贝，并保留transfer写后写依赖；读回开始的命令也会先排空待上传内容，未开始GPU使用的资源被删除时取消其待拷贝记录。CPU镜像和两类native AS失效继续按原规则更新。上传arena按需要扩容并保留高水位容量，因此稳态可复用分配；这会增加常驻staging容量和短暂的排队CPU副本，不能只报告减少的等待而不记录容量。

该改动消除每个更新区域独立提交与独立fence等待，仍使用现有帧完成fence退休arena和其它帧资源。没有移除最终帧等待，没有实现多帧在途、独立异步传输队列或CPU/GPU流水化。初次纹理创建和不可变资源上传继续使用原阻塞路径；多mip更新保留即时上传的安全回退。帧命令正在记录时更新资源仍被拒绝，不能用排队绕过已有生命周期限制。

公开API和Options的两个新增开关均默认false。CLI为`--surface-cache-single-pass-outputs`/`--surface-cache-legacy-trace-outputs`及`--surface-cache-deferred-uploads`/`--surface-cache-immediate-uploads`；`--surface-cache-efficiency-quality`和`--surface-cache-efficiency-performance`显式组织配对，不依赖全局默认。F1新增`Trace each probe ray once`与`Batch dynamic texture uploads`。性能夹具还检查两组原生接收面与缓存通路均有效、新组MRT通路确实启用且旧GS通路关闭，防止误把其它后端或静默回退当成优化收益。

`--surface-cache-efficiency-performance --benchmark-present`在1280×800实际窗口按baseline/combined/combined/baseline运行五条固定轨迹：静止、相机运动、模型运动、新显露与灯光变化。每批先预热64个静态帧，路径切换后实际执行对应轨迹16帧再计时，正式两组每路径各32帧，共320次计时present调用；关闭validation、图像回读、CPU oracle、HUD和应用限帧。窗口显隐逻辑显式以performance模式的`benchmarkPresent`为准，避免测试模式默认隐藏窗口覆盖这次实际呈现。比较探针预算、卡片重照、面积光绘制和物理上传区域/字节的相等性；模型轨迹要求候选确实发生延后上传，并减少独立上传fence等待。均值、中位数与P95一并报告，没有置信区间或恒定加速保证。

主ABBA后另有两个模型移动归因用例：仅MRT追踪、仅上传合批，每项单独预热64个静态帧和16个实际模型运动帧，再计时16帧，合计32次诊断present。它们的样本较少，不属于主ABBA的320次，也不声称与主测试拥有完全相同的历史与工作量；用于识别阶段成本，不能替代主配对结论。最后独立运行高清静止/连续模型运动/停稳预览，图片读回不计入性能样本。

本轮以`event_render_loop`的墙钟时间作为真实呈现回路的主要指标，包含事件轮询、宿主准备、提交、GPU完成等待与present调用；其倒数不是物理显示器scanout计数。GPU `frame`仍结束于present拷贝前；延后拷贝虽记录在帧时间戳之前，TOP_OF_PIPE起点也不证明传输已全部完成，后续消费者的等待可能进入GPU区间。因此不能将该scope差值解释为纯射线算术收益，也不能把宿主墙钟、GPU scope及嵌套AS时间相加得出总帧时间。`frame_record_and_upload`是可能包含同步的墙钟区间，不能改名为纯CPU执行成本。

`FrameTiming.textureUploadStatistics`在原有计时回调中抓取同帧八项计数，不增加另一轮诊断等待。它们依次为实际拷贝区域、实际字节、延后区域、延后字节、独立即时提交、独立即时fence等待、staging分配及staging容量。前七项按样本相加，容量取最大值。被删除而取消的待上传不进入实际拷贝计数；初次创建可能进入统计，预热将它排除在正式稳态轨迹之外。独立上传等待变为0也不意味着最终帧等待消失。

`--surface-cache-efficiency-quality`在320×200、validation开启的独立进程中，沿用两种接触/运动接收面近景，每组112帧：64帧静止、16帧连续变化、8帧分离停留、8帧返回和16帧停稳，共224个配对帧。原13项几何、owner/status、风险/mask、几何fresh、probe位置、起点、接收面数据、actual-N与readable签名仍要求精确一致；两阶段覆盖率和当前alpha也精确比较。新增逐帧读取物理screen/SDF packed图集，含全部容量填充，status、owner/步数等离散字段要求相同；连续字段仅允许`16*float_epsilon*max(1,abs(before),abs(after))`舍入包络，该包络不代替或放宽原13项签名。再按静止、连续变化、分离、返回与停稳阶段比较contact/temporal的RGB、梯度和接触core/shell对比，检查有效黑色、unknown及运动下的零能量保护。参考是已验收输出，不是独立路径追踪真值；1%相对RMS只是既有验收上限，最终实际误差需另列。同二进制配对只能证明两个模式相等，不能证明控制组自身没有被共同代码或其它并行修改改变；还需要核对上轮归档图像及一致的启动配置，控制组异常不能靠候选与它相等来验收。

早期曾用保留原生成规则的`gi_round2_snapshot.ninja`进行中间检查，当时并行的其它CMake修改引用尚未到位的eye-shell测试源。相关文件到位后，最终已经使用普通CMake完整构建GI目标并链接其依赖，[完整Release构建](../../build/gi_efficiency_complete_build.log)及[补充最终断言后的构建](../../build/gi_efficiency_final_build.log)均通过；snapshot不作为最终交付构建方式，也没有回退其它工作。[advanced material CPU](../../build/gi_efficiency_final_material_cpu.log)检查通过，其余CPU与GPU回归以本节最终记录为准。


首轮[配对检查](../../build/gi_efficiency_quality.log)虽报告两个新模式完全相同，却在独立核对上轮图像时发现控制组本身出现大块黑色三角斑，不能作为验收。原因是同时进行的PBR顶点修改在普通`PbrVertexShader`添加了`invariant gl_Position`，而GI深度预通过的`PbrShadowVertexShader`没有相同声明；深度相等测试与后续位置/法线捕获出现不一致。为shadow顶点输出补上匹配的声明后重新正常构建，未改动其它PBR材质工作。夹具还增加密闭房间关灯时`readableBlack==width*height`的绝对覆盖保护，防止两个共同退化的模式仅凭彼此相等通过。该保护的当前四组64,000像素覆盖由日志独立确认，后续正式夹具也执行该断言。

修复后[最终严格画质检查](../../build/gi_efficiency_final_quality.log)以exit0通过：224个配对帧的原13项签名精确相同，28,672,000个阶段覆盖像素帧与18,063,360条source metadata精确一致；两组累计42,461,616条receiver射线、163,140个几何fresh像素帧和174,282个运动选择像素帧，恢复第一轮记录的相同总量。有效黑色256,000、unknown256,000，包含模型运动的零能量保护。新增72,482,816个物理packed图集texel与108,724,224个离散元数据通道全部比较，连续值最大绝对差也是0，实际没有使用舍入容差。contact/temporal各阶段与ROI的RGB MSE、相对RMS、梯度差及卡片atlas RGB误差均为0；接触core/shell对比完全保持。此结果证明本轮所覆盖轨迹中候选保持了控制组输出，不能外推为所有未来场景的物理真值。

同时对照上轮归档的native静态frame64，以确认控制组自身恢复。下表为每视图64,000像素的8bit图片比较，差异均不超过1个灰阶；图像视觉检查已经恢复干净椭球表面与接触阴影。两个版本不是逐位相同，不能把本轮同二进制两组的RGB零误差改写为跨版本所有像素相同。

| 归档native → 修复后控制组，frame64 | 不同像素 / 不同通道 | 最大8bit通道差 |
| --- | ---: | ---: |
| 接触视图，实际renderer | 17 / 17 | 1 |
| 接触视图，contact tone4 | 18 / 18 | 1 |
| 接触视图，temporal tone4 | 25 / 25 | 1 |
| 运动接收面视图，实际renderer | 10 / 11 | 1 |
| 运动接收面视图，contact tone4 | 0 / 0 | 0 |
| 运动接收面视图，temporal tone4 | 20 / 20 | 1 |

可查看[上轮接触视图](../../build/gi_native_edge_quality_view0_native_contact_cache_frame64_total_renderer_ldr.png)与[修复后控制组](../../build/gi_efficiency_final_quality_view0_accepted_native_frame64_total_renderer_ldr.png)，以及[本轮MRT/上传候选](../../build/gi_efficiency_final_quality_view0_optimized_trace_upload_frame64_total_renderer_ldr.png)。上述最终严格画质运行二进制SHA256为`F02A4CA4E49E1A66C819284B3CE522349BA0EDAD940C1A981A9CB16FE4EF98B9`；之后仅补充夹具的绝对覆盖断言、相关检查入口与实际轨迹预热，渲染kernel保持不变，最终性能二进制身份另列。

最终[Surface Cache CPU](../../build/gi_efficiency_final_cache_cpu.log)、[帧命令CPU](../../build/gi_efficiency_final_frame_cpu.log)、[render-target CPU](../../build/gi_efficiency_final_target_cpu.log)和[advanced material CPU](../../build/gi_efficiency_final_material_cpu.log)检查均通过；render-target日志中的无效target、retired attachment和不完整framebuffer拒绝信息属于主动负例。最终[GPU上传与Vulkan RHI合同](../../build/gi_efficiency_upload_gpu.log)通过，覆盖拥有上传字节的回调、重叠更新、非法更新、BeginFrame之前读回排空、即时/延后混合、arena扩容复用与pending删除，并继续通过索引绘制、UBO快照、深度/混合、geometry shader、resize、4xMSAA resolve、呈现及原生ray query。

Vulkan的[完整MRT传输检查](../../build/gi_efficiency_mrt_vk.log)以exit0完成13对：606,208个packed texel、1,446,735个精确元数据通道，连续值及RGB最大误差均为0；覆盖138,240个几何上传texel，静态64帧有效像素24,547、有效黑色24,547、unknown24,576及两次自动历史重置。此专项补充主224帧接触/运动对照中的probe输出协议、debug view和历史切换检查。

OpenGL的Intel 4.5、驱动32.0.101.6790环境中，[首轮](../../build/gi_efficiency_mrt_gl.log)与[重试](../../build/gi_efficiency_mrt_gl_retry.log)均完成6对不同seed/分辨率/安全起点的MRT传输对照，以及4个debug view的相等检查；之后启用空间/时域资源时，冷资源创建触发既有10秒期限，未完成后续完整13对与静态64帧检查。这里仅记录已经通过的传输覆盖，不能写作完整OpenGL验收通过。本轮没有增大超时或关闭GI阶段来掩盖该限制；Vulkan交付路径的完整检查另列。

首轮[呈现与高清预览记录](../../build/gi_efficiency_final_present_performance.log)完成了主ABBA及预览，但审核发现路径切换的16个预热帧仍在静止姿态，候选模型组的计时区间内出现一次arena扩容分配。因此该轮保留为“首次动态路径成本仍可能进入计时”的诊断，不作为最终充分预热的性能表。随后仅修正效率夹具的预热轨迹：每个预热帧先设置对应相机/模型/曝光/灯光姿态；模型归因用例也以64帧静止加16帧实际运动预热。旧native夹具行为和GI kernel均未改变。最终采用独立的`gi_efficiency_warmed_present`记录，不把两轮的均值或不同模式拼接比较。
最终充分预热的[Release实窗呈现ABBA、归因与高清预览](../../build/gi_efficiency_warmed_present_performance.log)以exit0完成。RTX4060 Laptop、1280×800、MAILBOX三缓冲、vsync关闭，主配对320次present、每模式每轨迹32帧，另32次模型归因present；五条主轨迹均`paired_work_exact=1`，两个硬件求交通路每组逐帧有效，候选32帧全部启用MRT。下表为**均值 / 中位数 / P95 ms**，FPS仅为`1000/event_render_loop均值`。这些数值均来自本轮同二进制、实际轨迹预热后的配对，不沿用前一轮或初次静态预热诊断的数据。

| 轨迹 | 整帧GPU：控制组→候选 | 事件渲染回路：控制组→候选 | 回路平均FPS：控制组→候选 |
| --- | --- | --- | ---: |

| 静止 | 92.1635 / 91.4304 / 117.069 → 71.2841 / 75.8369 / 78.8075 | 95.7261 / 95.6493 / 121.134 → 76.0052 / 80.2121 / 88.5046 | 10.4465 → 13.157 |

| 相机移动 | 100.392 / 115.658 / 123.445 → 78.2761 / 81.2024 / 82.9902 | 105.08 / 119.547 / 127.317 → 82.0513 / 85.0442 / 87.9337 | 9.51655 → 12.1875 |

| 模型移动 | 109.089 / 122.742 / 128.637 → 84.4288 / 83.7801 / 92.2344 | 271.928 / 265.867 / 312.543 → 215.408 / 212.042 / 248.637 | 3.67745 → 4.64236 |

| 新显露 | 117.389 / 119.925 / 124.018 → 80.8556 / 82.288 / 83.9078 | 122.009 / 124.006 / 129.087 → 84.5837 / 86.1342 / 88.0446 | 8.19612 → 11.8226 |

| 灯光变化 | 75.4346 / 73.8686 / 77.354 → 56.4108 / 55.2167 / 78.5355 | 79.7648 / 78.1559 / 81.4546 → 59.9014 / 58.4252 / 82.2301 | 12.5369 → 16.6941 |

本组平均呈现回路吞吐提高约26%–44%，整帧GPU均值下降约22%–31%；各批次时长仍有明显变化，CPU/GPU频率、桌面调度等没有同步遥测，不能由当前样本断言变化的具体原因，也不能承诺恒定收益。主表不是显示器扫描时间测量，所有轨迹仍远未达到60FPS的16.67ms预算，更没有达到90/120FPS。

主要GPU阶段如下，仍包含候选的两个打包pass；AS为父阶段内嵌子项，不再加到frame：

| 阶段 / 轨迹 | 控制组：均值 / 中位数 / P95 ms | 候选：均值 / 中位数 / P95 ms |
| --- | ---: | ---: |

| 屏幕追踪及候选打包 / 模型 | 1.98275 / 1.99014 / 2.21696 | 1.34262 / 1.32966 / 1.42029 |

| SDF/GDF与BVH及候选打包 / 静止 | 63.2449 / 62.4876 / 84.7135 | 40.6869 / 44.5353 / 46.2387 |

| SDF/GDF与BVH及候选打包 / 相机 | 66.1996 / 80.1075 / 86.1553 | 42.8042 / 44.2911 / 45.6223 |

| SDF/GDF与BVH及候选打包 / 模型 | 69.4827 / 81.3819 / 85.4077 | 44.1173 / 43.7949 / 48.4997 |

| SDF/GDF与BVH及候选打包 / 新显露 | 77.6576 / 79.6826 / 83.5011 | 42.1593 / 43.2809 / 44.4211 |

| SDF/GDF与BVH及候选打包 / 灯光 | 46.3396 / 45.1308 / 47.1286 | 26.9384 / 25.8125 / 43.8548 |

| 探针重建 / 模型 | 2.47821 / 2.44992 / 3.60141 | 2.39926 / 2.40077 / 2.54669 |

| 探针空间过滤 / 模型 | 7.11152 / 7.66157 / 8.26778 | 7.68435 / 7.65542 / 8.27904 |

| 接收面补追与分类 / 模型 | 10.755 / 11.1017 / 12.6648 | 11.3657 / 11.2061 / 13.0202 |

| 接触过滤 / 模型 | 2.20733 / 2.12582 / 2.44531 | 2.13565 / 2.12838 / 2.36646 |

| 时域 / 模型 | 1.51978 / 1.48582 / 1.78483 | 1.49613 / 1.48275 / 1.63123 |

| 缓存重照 / 模型 | 1.35462 / 1.32659 / 1.5657 | 1.3519 / 1.34144 / 1.47251 |

| AS构建，嵌套 / 模型 | 0.618898 / 0.592016 / 0.815712 | 0.603712 / 0.598528 / 0.66048 |

| 面积光深度更新 / 模型 | 1.62634 / 1.65325 / 2.06643 | 1.39747 / 1.35373 / 1.5616 |

模型轨迹的宿主区间仍存在显著成本，以下为墙钟而不是纯CPU算术时间；GPU、帧完成等待及这些区间不能相加：

| 模型移动宿主区间 | 控制组：均值 / 中位数 / P95 ms | 候选：均值 / 中位数 / P95 ms |
| --- | ---: | ---: |

| 场景准备 | 11.9641 / 11.5491 / 16.1161 | 10.5583 / 10.7707 / 14.9831 |

| 命令记录与资源上传 | 72.9508 / 69.7362 / 97.5256 | 41.4503 / 38.0539 / 64.5842 |

| 提交至帧完成/计时返回等待 | 186.989 / 191.271 / 207.275 | 163.376 / 160.067 / 183.01 |

上述场景准备算法没有因MRT而改变，不能把其区间差值全归为新算法收益；提交等待还包含render thread、驱动/队列及呈现工作，不能把163ms写作纯GPU耗时。即使独立上传fence已去除，现有最终帧等待仍保留。

上传计数给出了与时长波动分开的结构性证据。以下为两组模型轨迹各32计时帧的合计，容量为最大值：

| 物理上传指标 | 控制组 | 候选 |
| --- | ---: | ---: |

| 实际拷贝区域 | 1376 | 1376 |

| 实际拷贝字节 | 212860928 | 212860928 |

| 记录进帧命令的区域 | 0 | 1376 |

| 记录进帧命令的字节 | 0 | 212860928 |

| 独立上传提交 | 1376 | 0 |

| 独立上传fence等待 | 1376 | 0 |

| staging分配次数 | 1376 | 0 |

| 保留arena最大容量，bytes | 8388608 | 8388608 |

双方实际上传同样的203MiB，平均每帧43个区域、6.34375MiB；本轮没有通过少更新模型数据提速。候选把1,376次独立提交和等待归入既有帧提交，充分预热后的计时分配为0，保留arena容量为8MiB。ABBA后半段的控制组也可能保留之前候选建立的arena，所以“容量两组相同”不表示候选没有新增内存。候选额外四张MRT纹理在本配置为256×2250的RGBA32F，texel payload合计36,864,000 bytes（35.15625MiB）；实际驱动分配可包含额外对齐。两类资源保留期间，关闭开关不意味着容量立即回收。

模型组逻辑Surface Cache更新也一致：每组589,824个attribute texel、4,423,680个geometry texel和160个区域，重照4,915,200 texel、执行12,288次面积光深度pass；全部轨迹每组probe预算18,432,000条。实际上传计数还包含SDF场景更新，不能把160个Surface Cache区域误写成全部物理上传区域。

两个单项归因用例单独预热，样本各16帧，以下同样报告均值 / 中位数 / P95；其场景历史、采样阶段与主ABBA不完全一致，不用它们计算可相加的贡献百分比：

| 模型归因模式 | 整帧GPU ms | SDF/GDF/BVH与打包 ms | 命令记录/上传墙钟 ms | 事件回路墙钟 ms | 独立上传等待 |
| --- | --- | --- | --- | --- | ---: |

| 仅MRT追踪 | 85.6692 / 85.1866 / 91.0064 | 44.673 / 44.3346 / 47.7573 | 108.065 / 90.3115 / 240.033 | 286.316 / 264.537 / 412.808 | 688 |

| 仅上传合批 | 99.2659 / 99.1074 / 123.976 | 62.1487 / 61.7498 / 81.4285 | 42.9061 / 41.9218 / 68.2375 | 241.512 / 239.233 / 289.728 | 0 |

归因轨迹中，仅MRT减少了追踪scope，仍保留688次独立上传等待；仅上传合批消除了这些等待，但没有消除旧追踪执行。单项的事件回路均值受到其它区间影响，不能只引用较好的GPU项就声称完整交互帧率按同样比例上升。

充分预热记录末尾的高清预览独立执行，14张实际renderer及28张阶段图都在计时之外。64/65/72/81/88/97/112七个时刻、每张1,024,000像素的控制组/候选实际renderer比较全部逐像素相同，覆盖静止、连续模型运动与停稳，包含原接触阴影。[控制组移动第81帧](../../build/gi_efficiency_warmed_present_preview_accepted_native_frame81_total_renderer_ldr.png)与[候选移动第81帧](../../build/gi_efficiency_warmed_present_preview_optimized_trace_upload_frame81_total_renderer_ldr.png)可直接查看；[候选contact层](../../build/gi_efficiency_warmed_present_preview_optimized_trace_upload_frame81_contact_tone4.png)保留接触细节。该结论限于这些8bit实际renderer检查点，不能扩展为所有HDR值及所有未来帧逐位相同。

最终[实际轨迹预热后的普通Release构建](../../build/gi_efficiency_warmed_build.log)同时重新编译了并行变化的eye-helper头文件依赖；GI/SDF/上传kernel没有改动。为防止共同依赖改变画质，另将本轮14张renderer与上一版本41AC的相同时刻比较：12张完全相同，只有控制组frame64的18个通道、frame65的8个通道相差1灰阶；候选七张全部相同，累计26个像素帧/通道项变化，最大1灰阶，没有重现早期的黑色三角斑。不能宣称这14张跨二进制图片全部逐位一致。

最终可执行文件为`output/gi-dynamic/Release/bin/material_lab_demo.exe`，SHA256 `F4B7ED576C42C5A570BBB7BCE19BADB15E47C36F52FD820272D3F0FAE7CC8EF8`。[最终CPU CTest](../../build/gi_efficiency_final_cpu_ctest.log)3/3通过（render target、advanced material、Surface Cache），帧命令测试另以exit0通过。[RunSealedGiRoom.cmd](RunSealedGiRoom.cmd)已仅为当前Release追加`--surface-cache-single-pass-outputs --surface-cache-deferred-uploads`，保留两项原生求交与60帧上限；60上限不是已经测得60FPS。旧Debug回退仍只传软件射线，普通API/Options默认值不变，尾部参数可分别关闭新模式。

在冻结上述Vulkan性能二进制后，源代码另补充两项OpenGL API保护：采样反馈检查同时覆盖额外颜色attachment，并明确拒绝当前不支持的多颜色MSAA resolve，避免错误地广播color0。随后只[重新构建render_target_test](../../build/gi_efficiency_gl_guard_build.log)并通过[CPU合同检查](../../build/gi_efficiency_gl_guard_cpu.log)，其中两条拒绝信息为主动负例。没有重新链接material_lab_demo，F4B7…可执行文件SHA256保持不变，因此不能声称它已经包含这两项后置OpenGL保护；用户本轮的Vulkan与single-sample MRT GI通路不依赖它们，也没有用该CPU测试代替前述未完成的OpenGL完整GI验收。

当前测量的剩余重点是：模型候选SDF/GDF/BVH加打包约44.1ms，约占其84.4ms GPU帧的52%；接收面补追与分类约11.4ms，探针空间过滤约7.7ms。宿主记录/上传仍约41.5ms、场景准备约10.6ms，提交至完成区间约163.4ms与GPU区间的差额还未细分。下一轮应先分清render thread、驱动/队列、资源处理与最后完成等待的组成，再研究有效射线工作队列、共享几何数据/结果、动态几何局部上传和实例化；不能把尚未定位的等待全归给SDF，也不能假设再删一个同步就达到60FPS。本轮没有降低采样、静态累积或接触过滤质量来换取结果。



## 2026-10-21：第一轮执行效率优化——Release呈现基线与原生接收面/缓存求交

本轮将已经验收的2倍完整当前采样作为画质基线，只替换接收面补追与Surface Cache光源可见性的求交执行方式。移动接收面仍使用16条当前射线，几何新显露仍使用已有追加预算；静态64帧收敛、移动历史响应、接触过滤半径与pass数均保持原设置。本节新的软件/原生配对结果才用于评价本轮收益，下节保留的数百毫秒旧压力结果不直接作为新方案的性能分母。

当前探针追踪已经实际使用Mesh SDF与GDF：默认近端0–2m进行Mesh SDF步进，更远处使用GDF，候选表面局部再用软件BVH精确确认；安全屏幕命中也可能先由BVH确认。本轮不替换这条混合探针估计器，也不将日志的`sdf_trace`写作纯BVH或纯距离场耗时。普通密闭房间使用现有固定GDF配置，并非因为工程具备clipmap功能就默认启用四层clipmap。新增硬件通路仅作用于contact的主射线循环和卡片direct lighting的可见性测试；接收点起点认证、探针安全起点和SDF/GDF步进仍走原代码。

原生接入的实现约束如下：

- `SurfaceCacheGiSettings.hardwareContactTracing`与`hardwareCacheVisibility`为独立开关，公开API初始均为false。CLI分别使用`--surface-cache-hardware-contact`/`--surface-cache-software-contact`和`--surface-cache-hardware-visibility`/`--surface-cache-software-visibility`；F1显示`Hardware receiver tracing`与`Hardware cache visibility`。请求值不等于实际采用值，帧统计的`surfaceCacheHardwareContact`、`surfaceCacheHardwareVisibility`必须确认有效路径。设备或后端不支持时保留软件实现，专项原生测试则要求两项有效标志均成立，不能把静默回退当成硬件性能。
- 原生shader使用独立的`SC_NATIVE_GEOMETRY_SLOT`标记，Vulkan仅为该变体建立ray-query描述符；软件变体和探针shader不因此构建AS。后端反射`SurfaceCacheData.scGeometry`的实际UBO绑定与偏移（当前12/80），避免误读旧world-probe通路在binding15的参数。AS按同一packed triangle序号构建，primitive index继续对应原owner与材质查找。
- 硬件候选命中保留开放远端区间，防止光源端点成为自身遮挡。候选AS按软件1e-6重心容差扩展三角形边界，非零扩展按浮点ULP向外舍入，平面上的恒定坐标轴保持不变；AS缓存key区分Surface Cache扩展版与旧RT非扩展版，几何更新与删除同时使两者失效。候选距离与法线仍以原packed-float三角形公式重算；接收面closest-hit的原生miss、不满足原软件谓词以及距边8e-6内的候选可回退原BVH，以保留其边界与并列命中规则。此兼容处理不是逐位等价的预先证明，仍须通过下述严格状态检查与图像误差对比。
- 当前加速结构仍是一份展平world BLAS与identity实例TLAS；静止时复用，几何纹理更新使其失效，模型运动需要重新构建。没有将这一阶段写作“模型移动仅更新TLAS”。新增`acceleration_build_nested`计时位于首次消费它的缓存重照或contact阶段内，已包含在父阶段和整帧中，不能再次加到总成本。录制帧期间的上传/删除先拒绝，防止先销毁仍被命令引用的AS。
- 切换求交开关会改变GI历史key，缓存可见性有效模式也进入重照key。两组均保持相同射线数、种子、起点约束、缺失cache语义及有效黑色分母；新增诊断不在普通渲染中执行图像回读。

新增`--surface-cache-native-performance`在同一支持硬件ray query的Vulkan设备上按软件/原生/原生/软件（ABBA）运行，五条固定轨迹为静止、相机运动、模型运动、新显露和灯光变化。`--benchmark-present`启用实际窗口呈现，不带该选项则可隔离测量离屏路径；本轮正式结果运行present模式，没有把下节旧离屏压力数据混入配对表。每批先单列冷启动时间，再预热64帧；路径切换预热16帧，每条轨迹至少记录16帧且为完整16帧周期，两组各2批。计时关闭validation、图像回读、CPU oracle、HUD和应用限帧，原生有效标志逐帧检查。呈现性能测的是事件处理、实际渲染、提交及present调用组成的重复回路，不等于带全部编辑UI的人工操作，也不声称测到了物理显示器的扫描完成时间。

本轮使用普通密闭房间的screen/SDF步数上限128/192，保留64条/探针及接收面基础8条、运动16条与既有fresh追加。下节旧压力fixture采用512/256，不能把那一组371.764ms直接与本节新值相除得出加速倍数；新软件对照与新原生组的配置、轨迹和工作量必须相同。旧启动器选择Debug、强制软件射线以及60帧上限的历史状态也不能代替本轮Release呈现测试。

计时字段的含义为：

| 字段 | 测量范围与限制 |
| --- | --- |
| `frame`及各GPU scope | GPU时间戳；整帧终点位于present拷贝前，不包含CPU场景准备或提交等待；AS子scope是嵌套成本 |
| `render_submit_present` | `MeasureFrames`的一次渲染调用墙钟时间，包含准备、提交、GPU完成等待及present调用 |
| `event_render_loop` | 事件轮询加上述测量的回路墙钟时间；`1000/mean_ms`仅称为呈现渲染回路的吞吐，不是物理scanout FPS |
| `scene_prepare` | 帧构建开始至记录阶段开始的宿主时间区间，含场景、几何与缓存准备；不能直接等同某个算法的纯CPU执行时间 |
| `frame_record_and_upload` | 命令记录、资源准备与上传所在的墙钟区间，可能包含上传回调及GPU传输等待 |
| `submit_gpu_present_wait` | 提交开始至帧完成与计时读取结束的墙钟区间，包含GPU/队列/present及回调等待，不能再与GPU时间相加 |

原始计时日志保留`includes_gpu_wait`字段；最终源码将其更正为`includes_frame_completion_wait`，并单列`may_wait_upload_callbacks`，只澄清区间含义，不改变计时数据或渲染行为。该日志修改已通过对象编译；本轮配对测试与日常启动继续使用下述已验收的Release二进制。

新增`area_shadow`单独观察面积光深度图重建；原有contact scope仍包含风险/遮罩分类及主补追，不能把全部时长归为求交指令。各scope报告均值、中位数、P95、样本数及是否实际执行，未执行项不是测到了0ms。probe预算、缓存重照texel、上传量、面积光绘制次数、history复用数、实际呈现调用数和有效硬件帧数同时报告，以免以减少工作或漏运行换取表面提速。

`--surface-cache-native-quality`另在320×200关闭呈现并开启validation，以当前已验收软件输出作配对参考，覆盖接触近景与移动接收面近景。每组112帧包含64帧冷静止、16帧连续运动、8帧分离后停留、8帧返回及16帧停稳；保存实际renderer、contact与temporal图。它检查原始几何/owner/status、接触风险与mask、曝光分类、实际receiver N与readable计数等严格合同，并比较各阶段、ROI的RGB误差、梯度与接触core/shell对比；另造有效黑色及缺失cache unknown用例。软件对照不是独立路径追踪真值，1%相对RMS容差也不能解释为“画质提高1%”或肉眼完全无差别。静态PNG相同不足以证明运动路径通过，任何严格状态不符必须先定位，不能静默放宽成图像容差。

初始专项在静止截图后、第一组相机运动的严格采样状态检查失败，记录保留于`build/gi_native_initial_quality.log`；该轮不作为原生方案验收。第一轮交点公式兼容修正后，`build/gi_native_precision_quality.log`仍在相机第71帧发现一个像素的readable计数由23变24，其actual N=24及全帧104,152条射线预算保持相同；严格合同仍失败，未通过放宽门槛接受该轮。随后加入上述候选边界外扩与原BVH边界回退，以最终质量结果验收。过程中`advanced_material_test`曾在已有PBR skin布局断言`reSkinEnabled`偏移448处失败，探索日志`build/gi_native_synced_material.log`保留；随后按当前一致源码重新构建，最终[Surface Cache CPU](../../build/gi_native_final_cpu.log)、[advanced material](../../build/gi_native_final_material.log)与[帧命令](../../build/gi_native_final_commands.log)检查均以exit0通过。PBR布局属于本轮之外的已有修改，本轮未改动或宣称修复该布局，最终没有遗留的CPU测试失败。

最终[严格质量对照](../../build/gi_native_edge_quality.log)以exit0通过，没有放宽13项几何/采样状态合同：224帧几何、owner、status、风险/mask、曝光及实际N/readable计数精确相同；contact/temporal两阶段累计28,672,000个覆盖像素帧精确相同，检查18,063,360条source metadata。两组实际接收面射线合计42,461,616条；几何fresh与moving选择分别163,140和174,282像素帧，确认测试确实覆盖了额外当前测量。另测256,000个有效黑色和256,000个unknown接收点，包含模型运动时的零能量保护。卡片atlas的RGB MSE、最大绝对差均为0。

图像验收的门槛为各阶段/ROI相对软件输出的RMS不超过1%；最终最坏值为`1.64142e-6`（约0.000164142%），出现在移动接收面视角的静止contact ROI，RGB MSE为`4.25298e-14`、最大单通道绝对差`1.22488e-4`。不能把整段平均误差写成每个像素的相对上界。许多连续运动、分离与返回阶段RGB逐像素相同；移动接收面视角连续运动的temporal contact ROI相对RMS为`2.96435e-8`，分离停留为`1.63954e-9`，最终停稳为0。接触core/shell对比在日志精度下保持一致，例如该视角静止`0.0255461→0.0255461`、运动`0.00778749→0.00778749`、最终停稳`0.0248493→0.0248493`；相机接触近景的各阶段temporal contact ROI MSE均为0。结果说明在本组已覆盖轨迹中保留了软件方案的画质，不能声称比软件参考更物理准确，也不代表所有未来场景逐位一致。

正式[Release呈现ABBA与高清预览日志](../../build/gi_native_final_present_performance.log)以exit0通过：RTX4060 Laptop、1280×800、Vulkan ray-query可用、MAILBOX三缓冲、vsync关闭。计时共320次实际present调用，两组每路径各32帧，五条轨迹的`paired_work_exact=1`；均保持相同探针预算、上传量、卡片重照texel与面积光绘制次数。没有计时图像读回或CPU oracle，没有置信区间。下表“整帧GPU”与“事件渲染回路”均为**均值 / 中位数 / P95 ms**，呈现回路FPS为`1000/回路均值`，不是GPU时间倒数或显示器scanout计数。

| 固定轨迹 | 整帧GPU：软件→原生 | 事件渲染回路：软件→原生 | 呈现回路平均FPS：软件→原生 |
| --- | --- | --- | ---: |
| 静止 | 101.904 / 101.983 / 103.421 → 70.2319 / 70.2035 / 71.2994 | 104.861 / 104.818 / 108.145 → 73.4107 / 73.3629 / 75.3858 | 9.536 → 13.622 |
| 相机移动 | 198.241 / 182.533 / 246.832 → 75.0469 / 75.0333 / 77.1112 | 229.414 / 186.355 / 640.736 → 97.8291 / 77.6806 / 81.7702 | 4.359 → 10.222 |
| 模型移动 | 224.276 / 195.843 / 340.928 → 77.5565 / 77.6392 / 80.0877 | 300.617 / 256.062 / 392.964 → 137.792 / 136.782 / 144.570 | 3.326 → 7.257 |
| 新显露 | 217.886 / 206.909 / 270.743 → 76.2860 / 76.1766 / 78.8886 | 220.513 / 209.421 / 272.374 → 78.3818 / 78.4771 / 81.2596 | 4.535 → 12.758 |
| 灯光变化 | 155.548 / 155.618 / 158.257 → 73.0838 / 72.9490 / 74.4633 | 158.568 / 158.589 / 162.214 → 75.2976 / 75.3234 / 77.1761 | 6.306 → 13.281 |

相机原生组的均值97.8291ms大于P95的81.7702ms，说明少量长尾仍可明显拉高均值；不能只引用中位数或将P95称为最坏帧。旧软件组相机和模型轨迹也有较大离群值，因此保留均值、中位数、P95及有限样本限制，不将一次实验的全部时长差写作恒定收益。所有轨迹虽有明显提升，仍未达到60FPS的16.67ms整帧预算，更未达到90/120FPS。

主要GPU阶段的配对结果如下，仍为**均值 / 中位数 / P95 ms**；AS为父阶段内嵌子项：

| 阶段 / 轨迹 | 软件 | 原生 |
| --- | ---: | ---: |
| contact补追与分类 / 静止 | 38.1748 / 38.1891 / 39.1352 | 6.89322 / 6.87667 / 7.25811 |
| contact补追与分类 / 相机 | 112.918 / 101.915 / 140.993 | 9.56861 / 9.18579 / 11.6634 |
| contact补追与分类 / 模型 | 88.8277 / 77.0903 / 131.423 | 9.42979 / 9.31533 / 11.5139 |
| cache重照 / 模型 | 62.6374 / 51.4990 / 99.9793 | 1.25446 / 1.23648 / 1.35782 |
| cache重照 / 灯光 | 51.3549 / 51.4043 / 52.7534 | 0.708864 / 0.697856 / 0.769024 |
| AS构建，嵌套 / 模型 | 未执行 | 0.552682 / 0.551424 / 0.582656 |
| 面积光深度重建 / 模型 | 1.58554 / 1.62509 / 1.71622 | 1.66608 / 1.64915 / 1.90874 |
| SDF/GDF及BVH探针补追 / 静止 | 44.7492 / 44.6935 / 45.8168 | 44.4096 / 44.3336 / 45.5199 |
| SDF/GDF及BVH探针补追 / 相机 | 62.1668 / 61.8593 / 81.2411 | 44.4361 / 44.3141 / 45.6520 |
| SDF/GDF及BVH探针补追 / 模型 | 48.6041 / 44.5266 / 79.5464 | 43.6924 / 43.6193 / 44.7201 |

模型组每个计时帧均重新构建AS，其0.553ms已包含在1.254ms缓存重照及77.557ms整帧内；静止、相机、新显露及仅灯光变化的计时段没有AS构建。各模式的模型轨迹均上传589,824个attribute texel、4,423,680个geometry texel、160个区域，并重照4,915,200 texel、执行12,288次面积光深度pass；这些为每组32计时帧合计。静止、相机与新显露计时段不执行cache重照，灯光组重照4,915,200 texel但不重建阴影几何。探针算法没有修改；其跨组时长变化不能据此归为新的SDF算法加速，GPU频率、缓存和其它工作竞争仍可能影响分项时间。

宿主墙钟区间也没有随求交全面消失。模型组`scene_prepare`均值`6.58978→6.04745ms`、中位数`5.84395→5.33310ms`、P95`10.3503→10.2717ms`；`frame_record_and_upload`为`26.9828→27.3253ms`、`25.8025→26.6899ms`、`36.6881→31.2755ms`。后者包含资源上传和同步，不因日志的区间分类字段就变成纯CPU算术时间。`submit_gpu_present_wait`均值`267.016→104.390ms`包含GPU完成、驱动/队列和呈现等待，不能全部归给某一CPU算法。原生静止场景准备约0.085ms、记录/上传约0.814ms，模型变化仍带来明显额外成本。

当前原生GPU瓶颈已转移到探针混合补追：静止约44.4/70.2ms、模型约43.7/77.6ms。模型的场景准备与记录/上传区间还合计约33.4ms，单靠继续减少GPU求交时间也不足以在现有串行执行下达到60FPS。下一轮应继续研究相同探针射线只执行一次、有效工作队列，以及动态几何上传与等待；本轮未通过减少当前采样、关闭历史响应或放大接触过滤来获取性能。

同一日志末尾的1280×800高清预览在计时完成后独立运行，每组64帧静止、32帧连续模型运动及16帧停止，共保存14张实际renderer和28张contact/temporal图，读回不计入上述ms。frame64比较1,024,000像素，只有10像素、10个通道不同，最大差1个8bit灰阶，因此静态画质近乎相同，不能称为逐像素完全相同。移动第81帧可直接查看[软件参考](../../build/gi_native_final_present_preview_accepted_software_frame81_total_renderer_ldr.png)与[原生接收面/缓存](../../build/gi_native_final_present_preview_native_contact_cache_frame81_total_renderer_ldr.png)；[软件contact层](../../build/gi_native_final_present_preview_accepted_software_frame81_contact_tone4.png)与[原生contact层](../../build/gi_native_final_present_preview_native_contact_cache_frame81_contact_tone4.png)可检查接触细节。预览PASS只认证捕获和有限性等合同，不把保存速度写作帧率。

最终验收的Release可执行文件为`output/gi-dynamic/Release/bin/material_lab_demo.exe`，SHA256为`09308F961C312F6C71A350E3C1AFC6740BA3BD4C3B10CCA97ADDC9140E836FF7`。日常[RunSealedGiRoom.cmd](RunSealedGiRoom.cmd)现优先选择该Release，并显式传入两项硬件开关；公开API和Options的默认值仍为false，不通过改变全局默认影响其它实验。缺少该Release时回退旧Debug并保留`--software-rays`；当前设备不支持时，普通渲染仍可使用软件回退。用户可用F1的两项硬件开关分别排查，或给启动器追加两项software开关恢复已验收软件求交；尾部用户参数保持覆盖能力。

最后串行运行的[原生缓存时域回归](../../build/gi_native_final_temporal.log)、[禁用硬件能力的回退时域回归](../../build/gi_native_fallback_temporal.log)及[旧world-probe原生RT回归](../../build/gi_native_legacy_rt.log)均以exit0通过。前两项保留认证模型运动的几何拒绝、动态cap4、3个成功settling帧、拒绝帧不推进、光照/材质/种子/camera-cut严格重置，以及静态64帧收敛、微小运动恢复原cap、有效黑色和unknown保护；相机重投影复用48,449像素，新显露698像素。能力关闭用同一设备的`--software-rays`验证正常软件回退。该既有时域用例验证cache原生模式与历史管理，接收面原生主循环的专项验收由前述224帧严格对照承担。

旧world-probe测试仍报告`native ray query=1`，封闭天空遮挡、动态/变色光、发光面、关闭光源反馈、相机无关缓存和MSAA/TAA组合均通过，`backend errors=0`；薄墙暗/亮DC比为0.00356718，MSAA/TAA差值为0.0714961。这项回归验证了新SC扩展AS与旧RT非扩展AS的分离没有破坏既有路径，不将旧通路的速度用来替代本节GI性能。最终CPU、严格接收面质量、Release实际呈现ABBA、独立高清预览与这三项GPU回归均已完成，文中保留的长尾、非逐位图像差异及距60FPS尚远的限制不因PASS而消失。

## 2026-10-20–21：移动方块接收面上的椭球轮廓拖影（当前2倍预设）

用户补充的近景截图暴露了上一轮漏测的路径：移动方块的侧面携带着低矮椭球的旧阴影轮廓，留下多层弧线。上一轮旧阴影响应主要验证静止地板；其`rigidReceiver`分支为保护模型自身的稳定历史，绕过了新的photometric响应。物体重投影只能证明“仍是同一材质点”，不能证明该点相对静止椭球的遮挡与照明仍然相同。因此，地板专项的改善不能证明这个移动接收面问题已经解决。下节数据保留为上一轮版本的历史验收。

本轮参考本机UE的`Engine/Shaders/Private/Lumen/LumenScreenProbeTracingCommon.ush:28–46`与`LumenScreenProbeTracing.usf:356,648`。`IsTraceMoving`比较probe与ray-hit的运动；当前启用的近似路径使用probe速度与hit速度之差，按probe深度归一，以判断照明是否在变化。其关闭的准确速度分支还比较前后probe-to-hit方向。这体现了接收位置和命中位置的相对运动会改变光输运：接收物体自身移动时，也需要检查照明，而不能因为材质点重投影成功而始终保留旧阴影。本地仍使用严格当前邻域的photometric证据，没有移植UE的完整trace-moving速度信号链。

新增`temporal.reactiveMovingReceivers=true`，只有`reactiveShadows`启用时生效；设为false严格恢复上一轮刚性接收面绕过响应的分支。`reactiveShadows`公开API仍默认false，密闭场景仍默认true，现有F1的`Respond to moving shadow changes`控制这两类响应的总开关；新增`Update lighting on moving surfaces`单独切换`reactiveMovingReceivers`，关闭时精确恢复旧`rigidReceiver`绕过响应的路径。时域运动标志复用temporal screen bits的524288；仅历史响应本身不增加射线、纹理、UBO或pass。下述额外当前采样会真实增加运动接收面的contact射线，两者成本必须分别报告。

候选分支仅处理已通过原位置、owner、法线及完整可读验证的`rigidReceiver`，复用严格3×3同面、同材质且至少5个完整可读支持点的响应检测。多数邻点在同一变亮或变暗方向上支持历史过时后，才按连续权重将旧history移向既有局部边界，并缩短该点的temporal count；没有这种证据时保留原刚性历史。强响应目标cap1.5、其它位置仍用原动态cap4的仅历史响应候选已完成质量合同检查，但其运动波动仍有回退，不能仅凭拖影指标将其验收。已有默认fresh分类提供年龄0、成熟bilinear支持且非几何边缘时，current恢复计数维持原动态策略，避免降低history count后额外扩大young-current空间恢复。新曝光、部分支持和边缘继续使用原策略。静止接收面的上一轮局部bounds响应不变。

[仅历史响应cap1.5的探索质量](../../build/gi_moving_receiver_balanced_quality.log)已PASS合同检查。连续相邻姿态变化中，参考确认左侧面光照改变的78,496像素帧，RGB MSE下降72.9455%，额外时域有向滞后下降93.243%，旧轮廓投影由1.10811降至0.418430；这些不是等比例主观改善。全方块运动RGB MSE下降66.8517%，但对齐残差MSE由0.0000989875升至0.000131236（增加约32.58%），左侧面由0.000100896升至0.000133102（增加约31.92%）。这证明仅缩短历史虽能减少拖带，却不足以同时维持稳定；这些探索结果不替代下述采样候选的最终验收。

追加当前采样候选为`contact.movingReceiverRayMultiplier=1/2/4`，公开API默认1即关闭，CLI为`--surface-cache-moving-receiver-rays 1|2|4`，F1为`Moving surface ray multiplier`，密闭场景现选择2倍及full-current开启作为质量优先预设。1倍仅提供新历史响应，虽是低射线成本选择，但上述约32%的运动对齐波动回退尚在，不作为推荐质量预设。只有fresh分类以及`reactiveShadows && reactiveMovingReceivers`同时开启、接收点属于认证的实际运动实例、严格几何/材质/owner支持至少0.75且原风险mask至少0.25时，才将已有8条当前receiver射线增加为16或32条。新预算与几何fresh预算取最大值，避免双重追加；原风险mask、有效混合mask和过滤跨度均不变，不把局部额外射线伪装成额外时间帧。

该候选不增加image、pass或UBO，在既有16字节采样UBO的W高位传递moving multiplier；分类器Z单独加入reason4/8并保留bit0的几何fresh含义，X/Y/W仍仅表达原几何曝光语义。几何multiplier=1时，几何基础输出仍为`(0,base,0,1)`，之后仅叠加Z中的运动采样原因，不以运动重新定义几何显露年龄。各完整8射线批使用独立scramble、首8条保持common prefix，unknown/有效黑色仍按实际N计算完整分母。诊断预算先归因几何fresh，再统计超过该预算的moving增量，不能重复计数。

[原risk范围内2倍采样](../../build/gi_moving_receiver_2x_quality.log)与[4倍采样](../../build/gi_moving_receiver_4x_quality.log)均通过质量合同，但左侧面运动对齐残差仍分别增加约29.95%和28.27%，没有解决仅历史响应的主要波动回退。2倍用例中155,420个运动tag像素帧，仅30,209个进入原`mask>=0.25`范围，实际增量232,304条射线；其它被认证运动的接收点仍依赖较稀疏的probe估计。这两次是范围受限的探索，不能写作最终方案，也不能仅从其较少的增量射线推断下一方案成本。

最终方案使用独立的`contact.movingReceiverFullCurrent`（公开API仍默认false）；CLI为`--surface-cache-moving-receiver-full-current`/`--surface-cache-no-moving-receiver-full-current`，F1为`Current GI on moving surfaces`。配合moving multiplier2/4，只在本帧真正移动且严格几何/owner支持至少0.75的接收点上，分类Z额外加入bit16（组合tag可为20/24）。该位使`CTEffectiveMask`独立取得moving confidence1，当前测量不再受原risk门控，并按几何fresh与moving预算的最大值真实执行16/32条射线，以当前表面测量替代运动面上稀疏probe估计。原risk mask的内容仍不改，静态与纯相机运动不启用此范围。原“mask0的unfiltered必须等于probe”保护必须排除这些被full选中的点，因为它们现在具有真实的新增当前测量；有效黑色/unknown与实际N完整分母合同不变。

full候选沿用已有图像、pass及UBO，在原采样UBO的bit256传递开关；它扩大的是当前receiver追踪范围，不是空间模糊跨度。API的moving multiplier1/off与full=false默认不变；密闭场景的质量优先预设选用2倍及full=true，仍受fresh、reactiveShadows及reactiveMovingReceivers门控。下列连续运动质量、干净预览与正式ABBA成本均已完成；2倍明显减少多层旧轮廓，但仍有当前采样颗粒，也有显著性能开销，不能称为低成本或整体问题已完全消失。

[full2候选质量](../../build/gi_moving_receiver_full2_quality.log)通过320×200、32帧连续模型运动及16帧停止的合同检查。对照继续使用上一轮的刚性接收面路径，full2组同时开启cap1.5移动历史响应和16条完整当前测量；本表不能隔离为“单独将8条改为16条”的贡献。运动阶段temporal结果如下，均为旧刚性基线→full2：

| 独立几何ROI | RGB MSE | RGB MSE变化 | 对齐残差MSE | 对齐残差变化 |
| --- | ---: | ---: | ---: | ---: |
| 全部几何 | 8.47767e-5 → 6.22294e-5 | −26.5961% | 3.30009e-5 → 3.13023e-5 | −5.1471% |
| 移动方块 | 3.33930e-4 → 5.82640e-5 | −82.5520% | 9.89875e-5 → 7.61815e-5 | −23.0393% |
| 方块左侧面 | 3.76899e-4 → 6.08062e-5 | −83.8667% | 1.00896e-4 → 7.68937e-5 | −23.7891% |

参考确认左侧面发生光照改变的78,496像素帧，RGB MSE下降85.7041%，额外时域有向滞后下降92.3704%，旧轮廓投影由1.10811降至0.409914。相较仅历史响应及risk-only追加，full2开始同时降低总运动误差与对齐波动，但投影尚未归零，也不能把85.7%的MSE变化写作同等比例的主观轮廓消除。

同一full2候选的32帧运动预算：共2,048,000像素帧，155,420个moving tag全部被选择并实际追踪；总receiver射线由3,626,456增至5,615,080，增加1,988,624条，约54.84%。两组共同的几何fresh extra均为750,016；在原base基础上的moving增量为1,223,264，另有原先未追踪的接收点所需base射线765,360，两者合计才是本轮总N差，不能只报moving extra而漏掉新接收点的首8条。原风险mask之外的moving选择有125,211像素帧、实际射线2,009,432，其中moving incremental extra为990,960；这些是子集统计，不能再与前述全局增量相加。此预算来自GPU同射线循环的独立诊断回放，不是主pass硬件counter，也不是1280×800性能计时结果。

full2记录64帧冷静止GI及current contact精确一致，112帧上游probe/risk/mask及几何曝光hash精确一致，4,515,840条原探针metadata保留；移动阶段current contact有意改变，不作逐帧相同承诺。每组另测64,000个有效黑色与64,000个unknown接收点，temporal能量均为0；主场景`unknown_current_preserved=0`本身不是unknown保护证明，依据是另造的负向用例。Debug full构建及`build/gi_moving_receiver_full_{cpu,material,commands}.log`检查通过，material日志为advanced material测试；上文旧PBR schema断言失败仍单独保留。

full2的1280×800实际renderer第81帧检查中，多层弧状残影明显弱化，但当前采样的grain仍可见，侧面尚不够平稳，故继续比较full4。该观察只对应保存帧，不能替代连续画面的完整主观验收。干净的direct-only隔离关闭GI并禁止probe placement显示，方块目标侧面全黑且没有这些弧线，支持残影来自GI路径，本轮没有修改direct sampling。旧的marker污染图仍排除在证据外；[干净基线direct-only](../../build/gi_moving_receiver_full2_preview_Vulkan_cube_fixed_camera_rigid_receiver_legacy_direct_only_pose1_total_renderer_ldr.png)及[干净候选direct-only](../../build/gi_moving_receiver_full2_preview_Vulkan_cube_fixed_camera_reactive_moving_receiver_direct_only_pose1_total_renderer_ldr.png)仅用于分层归因，这些是干净预览序列中的分层归因图，性能测量见后表。

[full4实际renderer预览](../../build/gi_moving_receiver_full4_preview.log)已PASS：Release、1280×800，两组各112帧、111帧复用history、frame64精确一致，合计26张total renderer、18张stage及6张direct-only图。direct-only明确`gi_enabled=0`且`probe_placement_enabled=0`，没有旧探索中的marker污染。第81、84帧检查显示分离的多层弧线明显弱化，但细颗粒仍可见，不能宣称移动画面已无瑕疵。full2及full4两套干净direct-only目标侧面均全黑，作为GI归因证据；本轮没有修改面积光采样。

| 候选实际保存帧 | 旧刚性接收面基线 | 新响应及完整当前采样 |
| --- | --- | --- |
| full2，第81帧 | [基线](../../build/gi_moving_receiver_full2_preview_Vulkan_cube_fixed_camera_rigid_receiver_legacy_frame81_total_renderer_ldr.png) | [full2](../../build/gi_moving_receiver_full2_preview_Vulkan_cube_fixed_camera_reactive_moving_receiver_frame81_total_renderer_ldr.png) |
| full4，第81帧 | [基线](../../build/gi_moving_receiver_full4_preview_Vulkan_cube_fixed_camera_rigid_receiver_legacy_frame81_total_renderer_ldr.png) | [full4](../../build/gi_moving_receiver_full4_preview_Vulkan_cube_fixed_camera_reactive_moving_receiver_frame81_total_renderer_ldr.png) |
| full4，第84帧 | [基线](../../build/gi_moving_receiver_full4_preview_Vulkan_cube_fixed_camera_rigid_receiver_legacy_frame84_total_renderer_ldr.png) | [full4](../../build/gi_moving_receiver_full4_preview_Vulkan_cube_fixed_camera_reactive_moving_receiver_frame84_total_renderer_ldr.png) |

预览不执行atlas回读、CPU参考或性能计时，不能从保存速度推算帧率。full4定量质量通过下述合同并保留指标权衡；正式ABBA证实其开销明显大于2倍，故选择2倍作为默认。jump-hold及legacy回归另列最后验收结果。

[full4候选质量](../../build/gi_moving_receiver_full4_quality.log)也通过同一320×200连续运动合同，基线、参考、ROI和历史响应参数与full2相同。运动temporal结果如下，均为旧刚性基线→full4；这些是质量测量，与后表实际成本共同用于选择默认档：

| 独立几何ROI | RGB MSE | RGB MSE变化 | 对齐残差MSE | 对齐残差变化 |
| --- | ---: | ---: | ---: | ---: |
| 全部几何 | 8.47767e-5 → 6.20517e-5 | −26.8058% | 3.30009e-5 → 3.08005e-5 | −6.6677% |
| 移动方块 | 3.33930e-4 → 5.60911e-5 | −83.2027% | 9.89875e-5 → 6.94443e-5 | −29.8454% |
| 方块左侧面 | 3.76899e-4 → 5.86936e-5 | −84.4273% | 1.00896e-4 → 7.01293e-5 | −30.4930% |

同一78,496像素帧的参考确认左侧面光照改变子集，full4的RGB MSE下降86.1773%，额外时域有向滞后下降92.1373%，旧轮廓投影1.10811→0.415872。与full2直接比较，整个左侧面的RGB MSE仅再下降3.47%、对齐残差再下降8.80%；旧投影却由full2的0.409914略增至0.415872，额外有向滞后的降幅也略少。因此4倍不是所有指标都更好，收益已明显递减，不能只引用相对旧基线的约84%误差降幅来证明应使用4倍。

full4的同一32帧运动仍有155,420个moving tag/选择/实际追踪，原mask之外仍为125,211像素帧，几何fresh extra仍是750,016。总receiver射线3,626,456→8,091,688，新增4,465,232条（约123.13%）；其中moving incremental extra为3,699,872，新接收点的base为765,360，不能漏报后者。原mask之外的实际射线4,006,752、其中moving incremental extra2,988,280，仍是全局统计的子集。full4相对full2共多2,476,608条射线（约44.11%），正式ABBA的2倍及4倍均值开销分别约27.3ms与56.2ms，4倍增益不足以支持升为默认；射线比例不是GPU时间比例。

full4同样通过64帧冷静止GI/contact精确一致、112帧上游probe/risk/mask及几何曝光hash精确一致、4,515,840条原probe metadata保留，以及每组64,000个有效黑色和unknown零能量保护。其运动contact具有真实新增测量；两个版本合同PASS均不代表主观残影已经不可见。两档正式performance均已通过，hold、legacy和最终preset构建结果另列下方。

专项机位从低矮椭球旁近看方块左侧面，新增独立几何ROI `cube_left_face`与`low_oval`；测试中两组都启用上一轮`reactiveShadows`及几何fresh采样；基线`rigid_receiver_legacy`为`reactiveMovingReceivers=false`及moving multiplier1，候选`reactive_moving_receiver`为新响应true及显式指定的moving multiplier。只有multiplier1试验仅比较历史权重；multiplier2/4试验同时增加当前射线，必须连同新增射线成本一起解读。参考仍是各姿态8个独立seed×32条receiver射线、共256spp，带split噪声底限，共享本地单反弹Surface Cache，不是路径追踪真值。沿已知方块刚性变换跟踪同一材质点，在旧参考中要求4个bilinear tap均为同一方块、同一平面法线、可读且插值世界点误差小于2mm。参考光照差显著高于split不确定性后，才归入移动接收面的光照变化ROI；同时记录变亮与变暗，不能只统计旧暗影释放。

质量记录同时覆盖六个GI阶段、全部几何和左侧面、低矮椭球、接触core/shell、原射线metadata及黑色/unknown保护。multiplier1的仅历史响应试验要求112帧current contact精确一致；启用2/4倍后运动current contact会改变，不能再作此保证。追加采样候选要求64帧冷静止全部GI及contact精确一致，112帧上游探针/risk/mask/几何曝光精确一致，并单独记录实际射线增量。连续运动模式的`age=1`是32个相邻姿态变化事件的聚合，不是新曝光年龄，也不是“停止后首帧”；停止阶段与姿态跳变保持测试另行报告。额外历史滞后指标按参考改变方向计量最终history相对同帧current contact的延迟，不能代替总RGB误差或主观可见程度。旧形状投影接近0通常表示旧轮廓减少，负值也可能是过冲，必须与亮暗误差和对齐波动一起判断。

```text
--vulkan --validation --software-rays --procedural --no-debug-ui --surface-cache-shadow-history-test --surface-cache-moving-receiver --surface-cache-dynamic-view 2
--vulkan --validation --software-rays --procedural --no-debug-ui --surface-cache-shadow-history-test --surface-cache-moving-receiver --surface-cache-shadow-history-continuous --surface-cache-dynamic-view 2
--vulkan --software-rays --procedural --no-debug-ui --surface-cache-shadow-history-performance --surface-cache-moving-receiver --surface-cache-dynamic-view 2 --surface-cache-dynamic-frames 16
--vulkan --validation --software-rays --procedural --no-debug-ui --surface-cache-shadow-history-preview --surface-cache-moving-receiver --surface-cache-dynamic-view 2 --screenshot build/gi_moving_receiver_preview.png
```

以上命令现在默认moving multiplier2及full=true；评估原risk范围内追加采样时显式附加`--surface-cache-no-moving-receiver-full-current`，比较完整运动接收面4倍时附加`--surface-cache-moving-receiver-rays 4`；仅响应探索需显式指定`--surface-cache-moving-receiver-rays 1`。各模式保存独立日志，不能混用仅历史响应、risk-only追加及full当前测量的结果。`--surface-cache-moving-receiver`是专项fixture修饰符，不是交互场景的功能总开关。

实际renderer预览按相同轨迹对比，同时隔离direct-only与GI层以确认残影来源。最早探索中的direct-only图含探针标记，不能用于归因或最终画质验收；fixture已清理该显示混入，上面仅采用禁GI且禁placement后的干净重跑作为隔离证据，旧图不进入正式对照。本轮没有改动面积光采样。性能沿用ABBA配对计时与独立射线诊断回放，分别列temporal、整帧GPU及wall；参考、预览、读回不纳入计时。

本轮`surface_cache_test`、`advanced_material_test`及`rhi_frame_command_buffer_test`已通过。额外运行的旧`material_lab_pbr_test`在`pbr_material_test.cpp:36`的schema 208断言失败；`MakePbrTemplate`已有skin/flow扩展，本轮未修改PBR schema。此失败单独保留，不将其写作GI回归通过，也不为本任务修改无关PBR合同。

正式[full2性能](../../build/gi_moving_receiver_full2_performance.log)与[full4性能](../../build/gi_moving_receiver_full4_performance.log)及各自末尾独立预算回放均PASS。RTX4060 Laptop、1280×800、Release/NDEBUG、Vulkan软件BVH路径；两次实验依次执行，各自严格按ABBA顺序，每批预热64帧、计时16帧运动及16帧停止，每组/阶段2次trial、32个timestamp样本，无validation、呈现、图像读回或CPU oracle，没有置信区间。下表单位ms，均为**均值 / 中位数 / P95**，两档各用自己的配对基线，不能把跨批次差直接作为倍率成本：

| 档位 / 阶段 | scope | 旧刚性接收面基线 | 新响应及完整当前采样 |
| --- | --- | ---: | ---: |
| 2倍 / 运动 | 整帧GPU | 344.445 / 343.906 / 368.413 | 371.764 / 368.673 / 411.729 |
| 2倍 / 运动 | contact trace | 102.404 / 99.1826 / 124.964 | 128.283 / 123.948 / 159.439 |
| 2倍 / 运动 | contact filter | 3.49392 / 2.23232 / 10.2042 | 3.14864 / 2.41050 / 9.36858 |
| 2倍 / 运动 | temporal | 1.64605 / 1.44947 / 3.01773 | 1.77270 / 1.65274 / 4.08064 |
| 2倍 / 运动 | wall | 465.798 / 414.964 / 664.108 | 451.001 / 447.583 / 503.957 |
| 2倍 / 停止16帧 | 整帧GPU | 199.032 / 196.840 / 222.236 | 198.718 / 197.846 / 224.893 |
| 2倍 / 停止16帧 | contact trace | 68.7641 / 66.9466 / 87.7107 | 66.8148 / 65.5483 / 84.4257 |
| 2倍 / 停止16帧 | contact filter | 4.14118 / 2.52058 / 10.4448 | 4.65037 / 3.05050 / 10.0250 |
| 2倍 / 停止16帧 | temporal | 1.37997 / 1.23187 / 1.95994 | 1.27882 / 1.21190 / 1.52269 |
| 2倍 / 停止16帧 | wall | 201.046 / 199.637 / 225.084 | 200.558 / 199.364 / 228.947 |
| 4倍 / 运动 | 整帧GPU | 350.038 / 352.287 / 378.556 | 406.220 / 407.486 / 437.807 |
| 4倍 / 运动 | contact trace | 106.222 / 108.502 / 129.534 | 165.304 / 165.206 / 188.579 |
| 4倍 / 运动 | contact filter | 3.85446 / 2.46784 / 9.91437 | 2.71366 / 2.47398 / 4.89677 |
| 4倍 / 运动 | temporal | 2.04784 / 1.64352 / 3.49798 | 1.69270 / 1.68909 / 2.05824 |
| 4倍 / 运动 | wall | 438.863 / 428.737 / 482.296 | 479.114 / 472.853 / 520.615 |
| 4倍 / 停止16帧 | 整帧GPU | 198.440 / 197.381 / 216.890 | 197.416 / 196.045 / 219.681 |
| 4倍 / 停止16帧 | contact trace | 68.2270 / 66.8928 / 82.9860 | 68.6356 / 67.2394 / 85.3146 |
| 4倍 / 停止16帧 | contact filter | 2.89830 / 2.33267 / 5.85216 | 3.67869 / 2.31322 / 10.4561 |
| 4倍 / 停止16帧 | temporal | 1.38320 / 1.25798 / 2.06438 | 1.53376 / 1.23699 / 3.59731 |
| 4倍 / 停止16帧 | wall | 201.199 / 199.935 / 222.611 | 199.396 / 197.694 / 225.113 |

2倍运动整帧GPU均值增加27.319ms、中位数增加24.767ms；contact trace分别增加25.879ms、24.7654ms，temporal增加0.12665ms、0.20327ms。4倍整帧GPU分别增加56.182ms、55.199ms，contact trace增加59.082ms、56.704ms。额外当前射线是本轮主要成本，不能将仅响应的算术成本当成整项改进的开销。filter、temporal和wall的局部均值下降不构成算法提速证明，特别是2倍基线wall有较大调度离群值。停止两组实际moving射线都为0，微小正负差属于本批测量波动。以上是高工作量软件验证路径，不能作为60/90/120 FPS性能结论，也不能用GPU或wall倒数宣称实际交互帧率。

运动的fresh分类两组每档均32样本；2倍基线/新方案均值0.374752/0.400448ms，中位数0.357376/0.364032ms；4倍为0.512960/0.394400ms与0.364032/0.359936ms。receiver-motion同为32样本，2倍均值0.177632/0.144672ms、中位数0.089088/0.087552ms；4倍均值0.446496/0.284736ms、中位数0.140800/0.111104ms。停止分类在每次trial只执行6帧，因此每组12个样本；这不表示其它帧没有有效场景几何。分类及运动捕获复用原pass，这些带波动的绝对时长未证明新增逻辑为零成本。

计时结束后的GPU同循环诊断回放不纳入上述ms；每组单次16帧运动共16,384,000像素帧。两档均有1,321,406个被认证moving tag、选择并实际追踪的接收点像素帧，占8.06522%；原几何fresh选择303,520个（1.85254%）保持一致。完整预算如下：

| 单次回放 | 实际receiver总N | 原几何fresh extra | moving incremental extra | 相对旧基线的新接收点base | 相对旧基线总N变化 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 旧刚性基线 / 运动16帧 | 18,066,864 | 4,985,384 | 0 | 0 | 0 |
| 2倍full / 运动16帧 | 37,192,480 | 4,985,384 | 10,443,648 | 8,681,968 | +19,125,616（+105.86%） |
| 4倍full / 运动16帧 | 58,278,008 | 4,985,384 | 31,529,176 | 8,681,968 | +40,211,144（+222.57%） |
| 三组 / 停止16帧 | 12,292,320 | 135,216 | 0 | 0 | 0 |

原mask之外有1,152,833个moving选择；2倍/4倍实际N为18,473,680/36,890,656，其中moving incremental extra9,154,680/27,571,656，均是上表子集，不再相加。新接收点base等于实际追踪接收点由1,635,185增至2,720,431的差乘8，必须与moving extra共同报告；这也说明320×200质量预算的+54.84%不能代替1280×800性能回放的+105.86%。每个实验末尾确认128计时帧、256预热帧、`cache_work_exact=1`及32帧`geometric_exposure_exact`；两组探针预算18,432,000相同，缓存上传/重照相同，计时读回和CPU oracle均为0。本轮不新增图像、UBO或pass，但新增真实射线的成本显著。

综合收益与成本，密闭场景预设选择full2：移动接收面的完整当前16射线与cap1.5响应共同启用。full4左侧面RGB误差仅再降约3.47%，在独立配对实验中的移动GPU开销却约56.2ms，约为full2的27.3ms两倍；此选择保留质量优先取向，并不宣称满足生产帧率目标。F1可关闭`Current GI on moving surfaces`或将moving multiplier设为1作成本排查，但已测risk-only或仅响应模式有运动残差回退，不作为同等画质替代方案。

冻结内核及最终2倍/full=true预设已完成Release与Debug构建，见`build/gi_moving_receiver_final_{release_build,debug_build}.log`。随后以最终Release默认参数运行[最终分离后停留质量](../../build/gi_moving_receiver_final_hold_quality.log)，日志明确`moving_receiver_candidate_multiplier=2`、`moving_receiver_full_mode=1`且合同PASS。固定近景相机先冷静止64帧，跳至pose8停留16帧，再返回pose0停留32帧；两个姿态各有256spp安全接收点参考和128spp split，不是连续运动用例，也不是路径追踪真值。下面仅列参考确认光照改变的方块左侧面temporal阶段，age按两次姿态跳变后的成功帧计数，首帧像素帧合计8,716，不能与连续运动的78,496像素帧混用：

| 跳变后的age | 像素帧 | RGB MSE变化 | 额外时域有向滞后MSE变化 | 旧轮廓投影：基线→2倍full |
| --- | ---: | ---: | ---: | ---: |
| 1 | 8,716 | −99.1044% | −99.9035% | 0.707603 → 0.0224644 |
| 2 | 8,716 | −19.7018% | −55.0172% | 0.0207034 → 0.00643595 |
| 3–4 | 17,432 | −4.73662% | −38.0599% | 0.0316301 → 0.0277192 |
| 5–8 | 34,864 | −0.976075% | −4.61412% | 0.0224144 → 0.0207429 |
| 9–16 | 69,728 | −0.286747% | −1.36671% | 0.0184290 → 0.0175677 |
| 17–32 | 88,976 | −0.516504% | −0.393855% | −0.00666344 → −0.00710726 |

首帧的大幅下降说明移动接收面不再携带主要旧阴影；后续差异逐步缩小。第17–32帧只有返回pose0事件，投影的负值表示相对参考变化方向的过冲，候选绝对值略大，不能将该行写成所有指标均改善。MSE百分比不等于肉眼改善比例，旧投影的正负和整体平均也不能认证每个像素都已无残影。该用例从第二帧起模型已停下；本轮实际运动full采样只在跳变首帧触发，未通过给静态面持续加射线掩盖停止后的误差。

最终hold的停止16帧`contact_0to70cm` RGB MSE为5.66238e-5→5.65572e-5（−0.117619%），残差梯度MSE为4.29494e-5→4.29471e-5。参考接触shell-minus-core对比0.0224679，实测0.0209235→0.0209118，绝对对比误差0.00154434→0.00155611，存在很小的对比回退；不能称为接触细节完全逐像素相同。过滤跨度和pass数没有增加，信号梯度MSE为6.16396e-6→6.16342e-6，两者远低于参考4.86376e-5，反映既有平滑误差仍在，本轮没有用加宽空间过滤换掉轮廓。

该最终hold再次通过112帧上游probe/risk/mask及几何曝光精确一致、64帧冷静止GI/history与current contact精确一致，每组保留4,515,840条原探针metadata，frame64有64,000个成熟历史像素。运动current contact有意不同，仅64个冷静止帧要求相等。每组另造64,000个有效黑色与64,000个缺失cache unknown接收点，temporal能量均为0；主场景`unknown_current_preserved=0`仍不单独用作证明。`staticConvergence=false`及`maxFrames=1`静止策略的额外射线均为0。合同只认证这些保护，未把误差改善作为PASS条件。

[最终Debug legacy时域回归](../../build/gi_moving_receiver_final_temporal.log)以exit0通过：固定相机的认证模型运动继续复用同一材质点历史，几何拒绝、动态cap4、3个成功提交的settling帧、拒绝帧不推进状态，以及光照/材质/种子/camera-cut严格重置均通过。静态收敛记录`phase_count=1`且第8帧继续产生新射线，49,150像素增长并达到64帧，微小运动恢复原cap、重新停稳49,148像素恢复累积；另有48,978个有效黑色与49,152个unknown-current像素保护。相机重投影复用48,449像素、新显露698像素，变化探针phase、静态方差降低及有界计数检查通过；主累积测试关闭final-image TAA，独立GI/TAA共存检查也通过。此legacy测试保护原有路径，不代替上文开启新响应与追加射线的专项质量合同。

最终默认hold两组的frame64，以及先前full2连续质量的frame64实际renderer PNG，SHA256均为`B71B5A23CDFA1C2A47F8CE2A9BB0C26527A24C078FE895F6F45D4C5D44515626`；与日志64帧静止精确一致共同提供跨最后预设构建的静态一致性证据。最终版本沿用已通过的Release full2/full4干净预览；最后预设/UI变更未改内核，Debug已完成最终时域回归，未重复生产相同的预览序列。构建、连续质量、最终默认hold、干净renderer隔离、ABBA计时和独立预算回放、legacy回归均已完成；上述限制与轻微指标回退仍保留，不以合同PASS宣称移动瑕疵已不可见。

## 2026-10-20：分离后旧阴影的快速响应

遮挡物离开后，原阴影处的地板或静态物体仍可能具有完全有效的位置、法线、材质及owner历史；变化的是光输运。这类旧阴影轮廓不属于下节的几何新显露，不能靠放宽disocclusion分类解决。原动态cap4只给当前帧约四分之一权重，原3帧settle结束后又恢复长历史，旧暗影可能继续留在积累中。

新增`temporal.reactiveShadows`，公开API默认false，密闭陶瓷场景默认开启。可用`--surface-cache-reactive-shadows`/`--surface-cache-no-reactive-shadows`切换，F1对应`Respond to moving shadow changes`。它仅在已有`dynamicLighting`及原3帧settle窗口、认证移动物体附近启用；冷静止及纯相机运动使用原temporal shader permutation。既有`rigidReceiver`保护仍保留，被刚性映射标记为自身运动的接收点也不进入此光照响应。

局部范围来自已认证mover的旧/新world AABB并集：将网格local bounds的8个角分别按前后transform转换，并对移动实例合并；外扩半径取前后单个world bbox对角长度较大者的一半，至少0.05m。CPU使用17项环形历史，保留当前事件及配置的最多16帧settle范围，默认保留当前与之前3帧事件。新移动事件B不会丢弃事件A尚未到期的范围，连续运动的旧位置到期后也会清除，避免影响范围无限累加。仅成功提交推进并保存此历史。GPU仍只接收32字节的合并bounds与半径；`STShadowInfluence`按接收点到该AABB的外部距离计算权重，半个外扩半径以内保持1，至完整半径连续降到0；范围外直接使用原base clamp。此范围是接触附近的保守局部响应策略，并非证明所有间接照明变化都被AABB覆盖。

`STClampMovingHistoryResponse`复用当前3×3邻域，在至少5个严格支持的点上检查旧history与当前亮度的分离：完整可读覆盖、反照率每通道差不超过0.02、法线夹角小于10°、双向平面误差不超过原容差与2mm中的较小值。局部版本的亮度尺度取邻域标准差与当前均值4%的较大值；残差在1.5–3σ之间连续增强，并要求多数邻点朝同一变亮或变暗方向投票，60%–90%的同向比例连续加权，再乘上述距离权重。支持不足或距离权重为0时不增加响应，不把单个噪声样本当成阴影已改变的证明。

响应只将该像素的temporal count从原动态上限连续降向2，默认范围为2–4，保留至少两帧的抑噪能力；不替换当前RGB，不增加过滤跨度，也不改上游current contact测量、接触过滤、几何历史拒绝及有效黑色/unknown合同。新增32字节UBO（binding14）传递bounds与外扩半径，不新增射线、pass或渲染目标，只在原时域pass中增加局部范围判断、严格统计及历史权重计算。

历史权重与young-current恢复分开处理。通过旧tap的几何验证后，`reuseExposureAge`取可复用tap的最大显露年龄。对上述局部动态响应接收点，只有当fresh分类有效、复用旧年龄为0、bilinear有效支持至少0.9、且未被`edgeFast`标记时，整个动态窗口内传给`STRecoverCurrent`的计数保持原动态cap，避免前一帧主动降低temporal count后，下一帧反而额外开启young空间过滤。其它点仍沿用原young策略；新显露、部分支持、边缘及没有独立曝光年龄信息的情形不作此保证。`responseRecoveryCount`还保留当帧缩短权重之前的恢复计数，但这不是对所有后续像素或所有分支承诺当前恢复信号逐帧相等；上游current contact精确一致与temporal内部恢复策略应分别验收。

本机UE依据是`Engine/Shaders/Private/Lumen/LumenScreenProbeGatherTemporal.usf:585–596`：依据moving-lighting信号缩短历史，并结合上一帧响应加快遮挡物刚离开处的照明变化传播。UE的信号来自trace-moving/probe moving fraction；本地使用动态场景证据加严格邻域的photometric响应，是职责相同的适配，并非该信号链的逐行移植。

正式验收使用固定相机：64帧冷静止，跳至pose8并保持16帧，再回pose0保持32帧。仅pose0/8各生成256spp独立seed参考及split底限；旧阴影ROI由两姿态始终可见的地板、显著高于split噪声的参考变亮量选取，排除几何disocclusion，不使用生产mask或候选结果挑选ROI。另按至前/后mover几何盒体的最小距离划出`near_mover_0to70cm`独立子区，同时报告同一参考变化定义下的`all_visible_floor`汇总，避免只展示局部受益像素。记录分离后1、2、3–4、5–8、9–16、17–32帧的RGB/暗区误差、最终history相对当前contact的额外暗差及旧阴影投影；投影按参考变亮方向归一，旧轮廓残留应趋向0，负值也可能表示过亮，不能仅凭数值下降判断质量。另验证冷静止64帧精确一致、上游current contact不变、接触core/shell及黑色/unknown保护。预览保存相同轨迹的实际renderer序列；独立配对计时分别报告temporal及整帧GPU/wall，不含参考、读回或预览开销。

```text
--vulkan --validation --software-rays --procedural --no-debug-ui --surface-cache-shadow-history-test --surface-cache-dynamic-view 2
--vulkan --validation --software-rays --procedural --no-debug-ui --surface-cache-shadow-history-preview --surface-cache-dynamic-view 2 --screenshot build/gi_shadow_history_preview.png
--vulkan --software-rays --procedural --no-debug-ui --surface-cache-shadow-history-performance --surface-cache-dynamic-view 2 --surface-cache-dynamic-frames 16
```

固定相机正式hold已完成，记录为[最终hold质量](../../build/gi_shadow_history_final_hold_quality.log)。对照组为已开启fresh射线、物体重投影与相机稳定的当前路径，仅切换`reactiveShadows`。这是320×200、Vulkan软件射线、64帧预热后两次姿态跳变的合同与误差测量，不代表所有运动情形或人眼不可见；本节数据只取冻结后的ring及current-recovery修正版本，早先initial/conservative/local探索不作为交付改善依据。以下均为**基线 → 局部响应**，误差采用线性HDR平方；时域额外暗差只度量history比同帧current contact更暗的部分，不能代替相对参考的总RGB或暗区误差。

**附近0–70cm的参考确认旧阴影区域（`near_mover_0to70cm`）：**

| 帧龄 | 像素帧 | RGB MSE | 暗区亮度MSE | 时域额外暗差MSE | 旧阴影投影 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 2466 | 1.8736e-05 → 1.86154e-05 | 1.28483e-05 → 1.19848e-05 | 1.31928e-06 → 6.48686e-07 | 0.14631 → 0.126372 |
| 2 | 2466 | 3.04872e-05 → 3.13475e-05 | 1.84745e-05 → 1.87655e-05 | 2.15933e-07 → 1.14948e-07 | 0.19962 → 0.199621 |
| 3–4 | 4932 | 2.76654e-05 → 2.85708e-05 | 1.41934e-05 → 1.42982e-05 | 4.82606e-07 → 2.97428e-07 | 0.0797869 → 0.0756382 |
| 5–8 | 9864 | 2.36089e-05 → 2.15116e-05 | 1.05806e-05 → 1.01323e-05 | 1.14152e-05 → 1.04912e-05 | 0.0473193 → 0.0622942 |
| 9–16 | 19728 | 1.62455e-05 → 1.57535e-05 | 6.47193e-06 → 6.59856e-06 | 1.24715e-05 → 1.19719e-05 | 0.029064 → 0.0384748 |
| 17–32 | 22096 | 8.34676e-06 → 8.27226e-06 | 3.97955e-06 → 3.75436e-06 | 1.05752e-05 → 1.01932e-05 | 0.0174648 → 0.0140754 |

**全部参考确认的始终可见地板（`all_visible_floor`）：**

| 帧龄 | 像素帧 | RGB MSE | 暗区亮度MSE | 时域额外暗差MSE | 旧阴影投影 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 5458 | 1.80997e-05 → 1.81781e-05 | 9.77868e-06 → 9.37454e-06 | 8.53532e-07 → 4.8549e-07 | 0.163216 → 0.14519 |
| 2 | 5458 | 2.6132e-05 → 2.65653e-05 | 1.03269e-05 → 1.04598e-05 | 2.15914e-07 → 1.5957e-07 | 0.13444 → 0.133967 |
| 3–4 | 10916 | 2.83151e-05 → 2.88124e-05 | 1.07836e-05 → 1.08392e-05 | 3.42686e-07 → 2.33619e-07 | 0.0930183 → 0.0889822 |
| 5–8 | 21832 | 2.31945e-05 → 2.21052e-05 | 7.96504e-06 → 7.79344e-06 | 6.57334e-06 → 6.18077e-06 | 0.0563814 → 0.0712181 |
| 9–16 | 43664 | 1.7476e-05 → 1.7178e-05 | 5.60292e-06 → 5.70457e-06 | 8.86649e-06 → 8.64119e-06 | 0.0444056 → 0.0543062 |
| 17–32 | 55584 | 1.36909e-05 → 1.36728e-05 | 4.56628e-06 → 4.49233e-06 | 7.67494e-06 → 7.51823e-06 | 0.0440308 → 0.0414553 |

近场第1、2、3–4帧的时域额外暗差分别下降50.83%、46.77%、38.37%，首帧RGB MSE下降0.64%、暗区MSE下降6.72%；但第2及3–4帧RGB MSE分别增加2.82%和3.27%，不能只引用额外暗差作为整体质量改善。第5–8/9–16帧近场RGB MSE虽下降8.88%/3.03%，旧阴影投影却由0.0473193/0.029064升至0.0622942/0.0384748，分别多约1.50/0.94个百分点；同期全部地板也分别多约1.48/0.99个百分点。第9–16帧暗区MSE亦增加近场1.96%、全部地板1.81%。这说明该版本加快了早期部分旧暗影的释放，但仍有后续残留与噪声权衡，并未证明旧轮廓已消失或所有指标同步改善。第17–32帧仅来自第二次返回姿态后的保持，不能将不同帧龄子集当成同一批像素的完整时间曲线。

近场参考RGB split底限在第1–16帧为5.50792e-6，第17–32帧为5.31863e-6；全部地板分别为3.25398e-6与3.02653e-6。首帧两事件合计近场2466、全部地板5458像素帧。参考仍共享单反弹Surface Cache，并非路径追踪真值，未建立置信区间。

正式hold通过112帧上游及current contact精确一致、64帧冷静止GI/history精确一致，每组4,515,840条探针射线metadata逐条保留；frame64的64,000个像素具有成熟历史。主场景`unknown_current_preserved=0`，不能用零个未知样本证明unknown恢复正确；另造的64,000个有效黑色与64,000个缺失cache未知接收点，两组temporal能量均为0。移动黑色用例的选中点及额外射线也在两组一致，静态策略`staticConvergence=false`和`maxFrames=1`额外射线均为0。合同PASS只表示这些保护成立，日志明确不把质量百分比作为通过条件。

冻结版本也完成[正式连续运动质量](../../build/gi_shadow_history_final_continuous_quality.log)，使用同一固定相机，共32帧连续往返模型运动和随后16帧停止，基线仍开启fresh采样，仅切换局部阴影响应。下面均为相对基线的百分比变化，负数表示该误差下降，正数表示回退：

| 独立几何ROI | 运动RGB MSE | 停止RGB MSE | 运动对齐残差MSE |
| --- | ---: | ---: | ---: |
| 全几何 | −0.162458% | −0.204907% | +2.88501% |
| 直立椭球 | +0.0314716% | +0.0008214% | +0.255261% |
| 接触0–70cm | +0.153577% | −0.322710% | +1.27872% |
| 远平面 | +0.250147% | −0.169965% | +0.782514% |
| 移动模型自身 | 0% | −0.100897% | 0% |
| 模型扫过地面 | +1.02297% | −7.63517% | +3.47819% |

连续运动的扫过地面停止误差下降7.64%，但运动误差增加1.02%、对齐残差增加3.48%；全几何对齐残差也增加2.89%。移动模型自身的运动RGB及对齐残差与基线一致，符合刚性接收面保护；其他ROI仍有少量运动波动。这些结果与hold第5–16帧旧影投影回退应同时保留，不能把“首帧额外暗差减少”扩展成全程更稳或全部旧轮廓消失。连续用例同样通过112帧上游/current contact、64帧冷静止精确一致、每组4,515,840条探针metadata保留、64,000个黑色及unknown零能量保护和两项stationary-policy检查；主场景同样没有未知样本，保护依据仍是单独构造的用例。

正式[性能计时日志](../../build/gi_shadow_history_final_performance.log)及末尾独立预算回放已PASS，逐帧缓存工作一致（`cache_work_exact=1`）。RTX4060 Laptop、1280×800、Release/NDEBUG、Vulkan软件BVH路径，按ABBA顺序各组预热64帧，每批测16帧运动和16帧停止；每组每阶段2次trial、32个timestamp样本，无validation、呈现、图像读回或CPU oracle。下表单位ms，均为**均值 / 中位数 / P95**：

| 阶段 | scope | fresh基线 | 局部阴影响应 |
| --- | --- | ---: | ---: |
| 运动16帧 | temporal | 1.45619 / 1.36090 / 2.38592 | 1.47251 / 1.40032 / 2.07667 |
| 运动16帧 | 整帧GPU | 361.908 / 361.444 / 375.707 | 361.319 / 358.776 / 382.169 |
| 运动16帧 | wall | 433.758 / 419.610 / 459.131 | 437.211 / 416.459 / 447.752 |
| 停止16帧 | temporal | 1.29731 / 1.20627 / 2.26509 | 1.26998 / 1.21190 / 1.48890 |
| 停止16帧 | 整帧GPU | 191.945 / 189.205 / 226.124 | 192.729 / 187.203 / 230.264 |
| 停止16帧 | wall | 193.455 / 190.505 / 228.696 | 194.411 / 188.329 / 233.697 |

新增局部响应的运动temporal阶段均值增加0.01632ms、中位数增加0.03942ms，观察到约0.02–0.04ms的阶段净开销；它包含该阶段的完整绘制，未单独测出每条新增算术指令的成本。停止temporal均值下降0.02733ms而中位数增加0.00563ms，不能把前者当作算法收益。整帧GPU及wall波动更大，运动整帧GPU均值略降也不足以宣称提速。两次trial未提供置信区间；本批是高工作量软件验证路径，不构成60FPS生产性能结论，wall还包含提交及等待，不能由temporal或整帧GPU倒数推算交互帧率。

计时完成后，按同一帧序列分别进行不计时的GPU同射线循环诊断回放；两组实际receiver预算完全相同，如下表所示。这里的extra来自两组共同开启的fresh 4倍采样，**本次局部阴影响应新增射线为0**，不能把既有fresh射线算作它的新增开销；这些数值也不是CPU按分类数乘预算推测或硬件counter。

| 单次回放阶段 | 选中像素帧 / 全阶段像素帧 | 比例 | 实际receiver总射线 | 其中extra批射线 |
| --- | ---: | ---: | ---: | ---: |
| 运动16帧 | 668238 / 16384000 | 4.0786% | 35579864 | 11036800 |
| 停止16帧 | 45009 / 16384000 | 0.274713% | 20553096 | 480032 |

停止16帧中fresh分类实际只执行6帧，所以诊断`valid_geometry_pixel_frames=6144000`仅覆盖有分类数据的帧，并非其余10帧没有几何。相应两次trial的fresh分类scope只有12个timestamp样本；temporal及receiver-motion仍各有32个。性能末尾确认128个计时帧、256个预热帧，诊断回放及其读回不计入上表ms，也不反馈改变生产GI。

正式[实际renderer预览](../../build/gi_shadow_history_final_preview.log)已PASS：两组各112帧、111帧使用历史，各输出13张PNG，共26张；不执行atlas读回、CPU oracle或性能计时。检查第65、72、81帧的两组画面，局部接触/阴影轮廓仍可见，差别细微，不能声称肉眼显著改善或已经清尽。以下仅提供实际输出的对照链接：

| 姿态切换帧 | fresh基线 | 局部阴影响应 |
| --- | --- | --- |
| 第65帧，分离到pose8 | [基线PNG](../../build/gi_shadow_history_final_preview_Vulkan_cube_fixed_camera_fresh_current_rays_frame65_total_renderer_ldr.png) | [响应PNG](../../build/gi_shadow_history_final_preview_Vulkan_cube_fixed_camera_reactive_shadow_history_frame65_total_renderer_ldr.png) |
| 第81帧，返回pose0 | [基线PNG](../../build/gi_shadow_history_final_preview_Vulkan_cube_fixed_camera_fresh_current_rays_frame81_total_renderer_ldr.png) | [响应PNG](../../build/gi_shadow_history_final_preview_Vulkan_cube_fixed_camera_reactive_shadow_history_frame81_total_renderer_ldr.png) |

本次基线frame64、本次响应frame64及[上轮fresh版本frame64](../../build/gi_fresh_sampling_preview_cube_Vulkan_cube_fixed_camera_fresh_current_rays_frame64_total_renderer_ldr.png)三个PNG文件的SHA256均为`18DE1775594E363B1BE0E0581F11CC9F4B7DBE44118C69C116E5BF89CD7CE3E3`，与日志`static_frame64_exact=1`共同提供跨本次开关及上轮构建的静态一致性证据。

冻结版本的Release/Debug构建及Debug CPU、材质、frame-command-buffer检查已通过，记录为`build/gi_shadow_history_final_{release_build,debug_build,cpu,material,commands}.log`；正式hold、连续运动、配对性能、独立预算回放、实际renderer预览和legacy时域回归均已完成。

[最终legacy时域回归](../../build/gi_shadow_history_final_temporal.log)以exit0通过：认证物体运动覆盖固定相机历史复用、几何拒绝、动态cap4、3个成功提交后的settling帧、拒绝帧不推进状态，以及严格的光照/材质/种子/camera-cut重置。静态收敛记录`phase_count=1`且第8帧仍产生新射线，49,150像素增长并达到64帧，微小运动恢复原上限、重新停稳有49,148像素恢复积累；有效黑色覆盖48,978像素，未知当前覆盖49,152像素。最终累积测试的相机重投影复用48,449像素、几何新显露698像素，并通过变化探针phase、静态方差降低及有界计数检查；该测试关闭final-image TAA，另有GI/TAA共存检查通过。最终日志未发现`ERROR`、`Validation Error`、`VUID-`或`MaterialLab failed`记录。上述回归与合同保护不改变本节已列出的运动及后期旧影指标回退结论。

## 2026-10-20：新显露接收点的额外当前射线

本轮针对没有自身成熟历史的新显露接收面，实施局部增加当前帧真实射线。`contact.disocclusionSampling` 是独立开关，公开API仍默认false；`contact.disocclusionRayMultiplier` 为1–4、默认4。密闭陶瓷场景默认开启4倍，属于质量优先预设；启动器不改，用`RunSealedGiRoom.cmd --surface-cache-no-fresh-sampling`可关闭。F1的`Extra rays on newly exposed surfaces`及`Fresh surface ray multiplier`可调开关/1–4倍。本文完整默认GI保持64条探针射线、8条接收点补追射线，只为新显露接收点增加完整8射线批，最多32条。

分类在contact tracing之前进行，先从当前接收点寻找上一帧可见位置，检查历史深度、世界距离、同面法线、反照率签名及可用owner。移动模型使用既有刚性实例的上一材质点/法线映射，避免把模型整体移动误判为全表面新显露。有效bilinear严格支持不足0.5时才建立新曝光寿命；正常支持只继承已验证旧tap的寿命并减一。分类不读取历史RGB、当前GI亮度或生产接触风险来挑更亮的估计。

曝光年龄独立于temporal history count，剩余寿命为3→2→1→0。边缘原本可能一直使用cap2，不能因为时间计数小而永久追加射线。两个年龄bits复用历史位置alpha的整数签名高位：原material/owner key小于`2^22`，新增`age * 2^22`，最大16777215，仍可由RGBA32F精确表示；owner/材质比较先取回原key。不增加一张年龄历史图，也不把空间支持或额外射线当成更多时间帧。真正新几何置3；通过严格旧几何验证的tap才能继承并消耗寿命。

分类目标RGBA为confidence、计划总N、是否几何新显露、`valid + 2 * remainingAge`。4倍配置在年龄3/2/1时分别使用4/3/2个8射线批，confidence为1、2/3、1/3。2倍配置在这三个年龄均为两个批。有效补追权重为原接触mask与confidence的较大值，原几何风险图和公开mask保持不变。因此总工作增加既包含已有补追接收点的extra batches，也包含原mask外新接收点必须先追的base8，两项都应报告。

每个批覆盖相同余弦半球strata，新增批使用独立pixel scramble；第一批保留原8条射线的common prefix，不通过改变总N把原方向重新拉伸。实际N同时用于RGB完整分母、可读样本覆盖率和harmonic命中距离元数据。有效黑色、背面命中和缺失cache样本沿用原合同：黑色仍是测量，unknown仍占完整射线预算；不按可读数重新归一、不借历史填出当前可读性。无效安全起点不追射线，也不因计划N非零而捏造有效样本。原接触过滤跨度、历史caps、停止响应和缓存重照预算保持。

默认`staticConvergence=true`的冷静止64帧不执行分类pass，接触/保存位置也使用原shader permutation，保持已验收静态序列。运动时及停止后的受限fresh settle窗口允许分类，独立年龄消耗完即不再追加；已有物体动态settling仍按原响应处理。物体previous-world捕获每帧只执行一次，移至contact之前供分类和时域共用；材质捕获后重新绑定Surface Cache，避免材质纹理槽覆盖后续追踪输入。

新增资源是一张RGBA32F分类目标，1280×800为16,384,000字节，即15.625 MiB，加16字节分类常量；开启后目标可在冷静止时保留分配，但没有分类绘制。普通渲染不增加图像读回或射线诊断pass。`contact.collectSamplingDiagnostics`默认false，只供QA额外分配另一张同尺寸RGBA32F目标并执行独立gather replay；它逐条进入相同安全起点和真实射线循环，输出selected、actualTracedN、baseN、readableCount。这是GPU相同循环的独立诊断回放，不是先前主pass的硬件射线counter，也不是用CPU分类数乘预算推测射线数。

参考的是本机UE5.7职责，而非声称UE采用本地3帧/8射线批方案：`Engine/Shaders/Private/StochasticLighting/StochasticLightingTileClassification.usf:775–900`在时域前检查四个历史tap的旧深度/平面距离、可选normal与foliage材质标志，899行保存`SetHistorySampleValidity`；`LumenScreenProbeGatherTemporal.usf:444,478`消费该逐tap validity，597行在没有有效历史时将累积计数置0。这是本轮前置几何分类更直接的参考。`LumenScreenProbeGather.usf:1760–1915`的adaptive probe spawn依据几何插值覆盖补探针，不等同于本地按像素增加receiver射线。`LumenScreenProbeTracingCommon.ush:8–20`提供方向单元内随方向帧推进的blue-noise抖动；`LumenScreenProbeGather.cpp:445–450`按Final Gather质量选择追踪分辨率。这里的receiver曝光寿命、刚性owner检验、整批common-prefix追加是本地适配，没有移植UE完整重要性采样/探针调度。

新增入口（均使用当前object reprojection与motion stability作为基线，只切换fresh sampling）：

```text
--vulkan --validation --software-rays --procedural --no-debug-ui --surface-cache-fresh-sampling-test --surface-cache-dynamic-view 1 --surface-cache-fresh-sampling-multiplier 4
--vulkan --validation --software-rays --procedural --no-debug-ui --surface-cache-fresh-sampling-test --surface-cache-dynamic-view 1 --surface-cache-fresh-sampling-multiplier 2
--vulkan --software-rays --procedural --no-debug-ui --surface-cache-fresh-sampling-performance --surface-cache-dynamic-view 1 --surface-cache-dynamic-frames 16 --surface-cache-fresh-sampling-multiplier 4
--vulkan --validation --software-rays --procedural --no-debug-ui --surface-cache-fresh-sampling-preview --surface-cache-dynamic-view 2 --screenshot build/gi_fresh_sampling_preview.png
```

质量view0/1/2分别为椭球相机、接触相机、固定相机移动方块，view3顺序执行三者；默认320×200，显式尺寸可覆盖。9个姿态各用8独立seed×32射线的256spp安全接收点参考，提供split误差底限，共享单反弹Surface Cache，不是路径追踪真值。两组各64帧冷静止、32帧连续往返运动及16帧停止，要求112帧raw/placement/风险/mask/探针metadata与预算精确一致，冷静止64帧的GI和实际history精确一致。另测真实移动中的已知黑色/unknown、接触core/shell、亮暗偏差、材质点对齐残差及模型扫过地面足迹。曝光第1、2–4、5–8帧由独立primary-ray oracle的上一owner改变与世界点跟踪定义，不使用生产mask或history count；不认证同一网格自反遮挡或移出上一画面的全部情形。

性能两组按ABBA各预热64帧，默认每批16帧运动及16帧停止，每组/阶段合计至少32个fresh timestamps；无validation、呈现、图像读回或CPU oracle。报告fresh classification、contact trace/filter、temporal、receiver motion、整帧GPU及wall的均值/中位数/P95，并逐帧核对缓存上传/重照和探针预算。新增`fresh_sampling`只量分类；追加射线的成本落在contact trace，变N的接触resolve/filter也须单独检查。receiver-motion捕获现在在contact之前，`receiver_motion`与`temporal`是并列阶段，不再是下节旧计时版本的包含关系。停止16帧是紧接运动的恢复阶段，不代表独立预热的成熟静态性能。

计时完毕另按同一seed/姿态轨迹进行无计时GPU诊断回放，统计选中比例、实际总N及额外N；诊断不反馈图像或历史，回读耗时不归入生产性能。1280×800预览另存两组共26张实际renderer PNG，验证frame64相同并保留运动连续序列，不用这些读图帧推算FPS。

最初`build/gi_fresh_sampling_contact.log`仅为接触机位探索，现以下面的三机位正式日志为准，不把探索构建当成最终验收。4倍Release、Vulkan软件射线的三机位正式质量均通过，记录为 [最终三机位质量](../../build/gi_fresh_sampling_final_quality.log)。基线已开启物体重投影与相机稳定，只增加当前射线，temporal阶段结果如下：

| 机位 / ROI | 运动MSE | 对齐残差 | 曝光第1帧 | 第2–4帧 | 第5–8帧 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 椭球相机 / 全几何 | −6.81% | −0.74% | −43.68% | −38.77% | −18.34% |
| 椭球相机 / 椭球 | −2.80% | −0.12% | −35.46% | −56.91% | −45.65% |
| 接触相机 / 全几何 | −11.86% | −1.19% | −60.09% | −37.29% | −19.31% |
| 接触相机 / 椭球 | −12.87% | −0.27% | −66.68% | +11.91% | +11.80% |
| 接触相机 / 接触区 | −10.64% | −0.14% | −67.23% | −32.26% | −21.05% |
| 移动模型 / 全几何 | −1.13% | −2.13% | −64.16% | −32.27% | −0.61% |
| 移动模型 / 模型扫过地面 | −21.99% | −30.71% | −66.13% | −28.84% | −0.07% |
| 移动模型 / 接触区 | −1.58% | −4.72% | −66.08% | −36.60% | −0.24% |
| 移动模型 / 模型自身 | −1.16% | −0.08% | 无此子集 | 无此子集 | 无此子集 |

百分比是MSE变化，不是等比例主观噪声降幅。接触机位的椭球第2–4/5–8帧只有384/309像素帧，仍确有约12%退化，不能删除或称为所有年龄均改善。正式日志中两种相机的远平面运动MSE分别下降15.81%/3.16%，未列入主表的其它非空ROI亦保留日志。停止16帧的全几何MSE在椭球/接触/移动模型机位分别下降0.96%/3.94%/0.12%，模型扫过地面下降3.66%；改善主要发生在刚显露、缺少自身历史的阶段。

模型扫过地面第1帧包含15476像素帧，MSE为1.18773e-5，参考split底限为8.84004e-6；接触机位接触首帧为1.88768e-5，参考底为3.92761e-6，仍有剩余误差。该接触首帧误亮MSE由4.66039e-5降至1.17372e-5。参考接触对比0.047075，实测0.0457656→0.0459303，恢复约真实对比的0.350%；移动模型机位参考接触对比−0.0380868，实测−0.0429221→−0.0426047，绝对对比误差0.00483533→0.00451789。接触细节未因这次加射线而进一步抹平，但这些数据不表示残差已经不可见。

每个机位均通过112帧上游精确一致、64帧冷静止GI/history精确一致，以及4,515,840条原探针射线metadata逐条保留检查；冷静止没有追加射线。64,000个灯灭可读黑色和不匹配cache的unknown保护通过，另有真实移动保护。三机位正式质量构建早于随后仅针对`staticConvergence=false`/`maxFrames=1`修正实际运动触发条件及新增两个stationary-policy断言；默认64/10配置走同一已测逻辑，但不能把这两个新增断言写成已在4倍三机位正式日志中执行。这两个新增断言随后已在2倍接触质量构建单独通过，记录`stationary_policy`的两项extra rays均为0。

三机位运动32帧的GPU相同循环诊断回放工作量如下。总N同时包括原mask外新接收点的base8和额外批，两个来源不能遗漏；这是320×200质量用例的预算，不是1280×800性能结果：

| 机位 | 选中像素帧比例 | 基线 / 新总射线N | 原mask外新增base射线 | extra-batch射线 | 总N变化 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 椭球相机 | 2.727% | 437768 / 1749672 | 393768 | 918136 | +299.68% |
| 接触相机 | 2.091% | 1853152 / 2853176 | 290752 | 709272 | +53.96% |
| 移动模型 | 4.198% | 4363552 / 6307632 | 525880 | 1418200 | +44.55% |

选中比例小不能称为追踪成本低。独立oracle新显露首帧的选中覆盖，三机位分别为12416/12416、9966/10022、30154/30154；它只描述本文保守定义的异owner新显露子集。椭球机位低分辨率下的相对追加预算最大；下文分别记录三机位4倍及接触机位2倍的实际计时和预算，并附真实renderer预览。

独立 [2倍接触质量](../../build/gi_fresh_sampling_2x_contact.log) 也通过，保持相同320×200参考、轨迹与2.091%选中比例。下表均相对同一当前基线，4倍数据来自上面的正式三机位日志：

| 接触机位ROI | 2倍首显露MSE变化 | 4倍首显露MSE变化 | 2倍整段运动MSE | 4倍整段运动MSE |
| --- | ---: | ---: | ---: | ---: |
| 全几何 | −48.19% | −60.09% | −10.91% | −11.86% |
| 椭球 | −47.64% | −66.68% | −10.99% | −12.87% |
| 接触区 | −54.34% | −67.23% | −9.11% | −10.64% |

2倍接触机位的椭球第2–4/5–8帧MSE仍增加19.81%/17.07%，比4倍的11.91%/11.80%更差，因此减倍率没有解决这个小区域退化。并非每个子集都由4倍占优：接触区第5–8帧2倍/4倍分别下降21.68%/21.05%。重点首显露子集及整段运动总体支持保留4倍质量预设；2倍只验证了接触机位，不能据此替换所有机位的默认方案。

2倍GPU诊断总N为2,486,488，相对基线1,853,152增加34.18%，其中extra batches342584条、原mask外base仍为290752条；4倍对应2,853,176与extra709272条。额外批减少的收益仍须看实际毫秒，不能把射线数比例直接当作整帧提速。该后续Release/Debug构建及CPU/material/frame-command-buffer检查也通过，记录为`build/gi_fresh_sampling_delivery_{release_build,debug_build,cpu,material,commands}.log`；默认配置数学与4倍三机位验收一致，新增非默认stationary-policy由本次2倍用例实际覆盖。

**1280×800、4倍接触相机的正式性能。** Release/NDEBUG、RTX4060 Laptop、Vulkan软件射线，见 [4倍接触相机计时](../../build/gi_fresh_sampling_perf_4x_camera.log)。该组ABBA已PASS，每组/运动或停止阶段均32个逐帧timestamp样本；冷静止预热帧不计时。以下为 **均值 / 中位数 / P95**，单位ms，分类一行另附实际执行样本数：

| 阶段 | scope | 当前基线 ms | 4倍 ms |
| --- | --- | ---: | ---: |
| 运动16帧 | 分类 | 未执行（n=0） | 0.38112 / 0.368128 / 0.439296（n=32） |
| 运动16帧 | contact trace | 136.922 / 135.277 / 154.079 | 195.185 / 194.698 / 207.293 |
| 运动16帧 | contact filter | 4.69741 / 4.47386 / 7.07482 | 6.00707 / 5.90694 / 9.59386 |
| 运动16帧 | temporal | 1.5921 / 1.27181 / 3.10784 | 1.67392 / 1.29997 / 3.81645 |
| 运动16帧 | receiver motion | 0.28944 / 0.100352 / 1.6128 | 0.26288 / 0.112128 / 1.58003 |
| 运动16帧 | 整帧GPU | 256.622 / 249.69 / 285.354 | 314.426 / 313.06 / 343.66 |
| 运动16帧 | wall | 258.55 / 251.291 / 288.504 | 317.058 / 316.174 / 347.139 |
| 停止16帧 | 分类 | 未执行（n=0） | 0.372565 / 0.374784 / 0.388096（n=6） |
| 停止16帧 | contact trace | 133.942 / 132.174 / 144.446 | 137.796 / 134.282 / 166.61 |
| 停止16帧 | contact filter | 4.89837 / 4.55885 / 8.71322 | 4.90176 / 4.7145 / 9.0839 |
| 停止16帧 | temporal | 1.39517 / 1.20218 / 2.93888 | 1.38963 / 1.22061 / 2.53133 |
| 停止16帧 | receiver motion | 0.338272 / 0.090112 / 2.3552 | 0.270592 / 0.10752 / 1.21958 |
| 停止16帧 | 整帧GPU | 246.666 / 244.136 / 268.741 | 252.251 / 251.804 / 286.539 |
| 停止16帧 | wall | 248.385 / 245.606 / 271.353 | 254.108 / 253.462 / 288.046 |

这不是低成本修复：运动整帧GPU均值256.622→314.426ms，增加57.804ms（22.52%）；contact trace为136.922→195.185ms，增加58.263ms，filter另增加约1.310ms。分类本身约0.381ms，只是新增工作的一小部分，不能只报它当成全部成本。两个候选并没有减少缓存/探针工作；该相机用例cache上传/重照始终为零，lighting scope未执行，每组32帧探针预算均18,432,000条。其它GPU阶段和调度也有波动，只有两批/组、无置信区间，不把整帧差值逐毫秒强行归因于某条shader语句。

独立无计时诊断回放仅一轮16帧运动，选中329,938/16,384,000像素帧（2.0138%）；总N为65,847,656→72,810,120（+10.57%），其中extra batches 5,456,832条、原mask外base另增1,505,632条。与320×200质量预算相比，分辨率改变了原接触mask覆盖，不能用低分辨率射线增幅代替这次实际预算。总射线约增加10.57%，contact trace时间却约增加42.55%，因此也不能假定GPU时间与N线性缩放；本组没有单独隔离分支、SIMD分歧或编译器造成的比例。

停止回放选中19,970像素帧（0.1219%），总N为64,019,640→64,354,496、extra batches 214,304。停止分类只在32个计时帧中的6帧执行，其表中均值/P95仅以6个执行样本统计，不是每个停止帧都增加0.373ms。停止整帧均值246.666→252.251ms，仍为停止恢复阶段，不代表成熟静态的独立预热性能。

4倍采用质量优先预设；这一选择不表示具有实时帧率收益，本次每帧数百毫秒远未达到60/90/120FPS预算。其它机位/倍率须分别按各自配对基线比较，不能据4倍接触相机单组推断其它场景成本。

**1280×800、4倍移动模型的正式性能。** [4倍移动模型计时](../../build/gi_fresh_sampling_perf_4x_cube.log) 同样通过ABBA，以下为均值 / 中位数 / P95、单位ms；分类单列实际执行样本数，其它行各n=32：

| 阶段 | scope | 当前基线 ms | 4倍 ms |
| --- | --- | ---: | ---: |
| 运动16帧 | 分类 | 未执行（n=0） | 0.371136 / 0.361984 / 0.439296（n=32） |
| 运动16帧 | contact trace | 83.2143 / 82.5979 / 93.1021 | 155.256 / 155.686 / 172.765 |
| 运动16帧 | contact filter | 4.08659 / 3.97005 / 9.02554 | 3.8809 / 2.57024 / 9.44435 |
| 运动16帧 | temporal | 1.67091 / 1.35066 / 4.14413 | 1.41882 / 1.35322 / 1.71622 |
| 运动16帧 | receiver motion | 0.263936 / 0.128512 / 0.81408 | 0.56976 / 0.1408 / 2.42278 |
| 运动16帧 | 整帧GPU | 321.608 / 319.892 / 337.412 | 392.868 / 396.544 / 412.275 |
| 运动16帧 | wall | 433.013 / 426.594 / 492.33 | 509.378 / 509.556 / 589.544 |
| 停止16帧 | 分类 | 未执行（n=0） | 0.359509 / 0.361472 / 0.371712（n=12） |
| 停止16帧 | contact trace | 79.9508 / 78.9473 / 87.2796 | 83.9721 / 80.8371 / 120.971 |
| 停止16帧 | contact filter | 4.72838 / 4.53786 / 9.56928 | 4.22195 / 3.79136 / 10.3578 |
| 停止16帧 | temporal | 1.35712 / 1.19603 / 3.34848 | 1.35056 / 1.22112 / 2.83034 |
| 停止16帧 | receiver motion | 0.372 / 0.141824 / 1.78278 | 0.284704 / 0.144896 / 1.66605 |
| 停止16帧 | 整帧GPU | 209.712 / 208.065 / 219.642 | 211.963 / 209.852 / 249.762 |
| 停止16帧 | wall | 212.226 / 210.77 / 222.448 | 214.37 / 211.568 / 255.579 |

运动整帧GPU均值增加71.260ms（22.16%），contact trace增加72.0417ms（86.57%）；不能把分类约0.371ms当成全部成本。这里每帧真实保留153600 texels的全缓存重照，lighting均值/中位数/P95为100.210 / 99.6669 / 106.687→100.351 / 100.499 / 106.64ms。每组32个运动帧的属性589824、metadata32768、geometry4423680 texels与160个上传region、4915200重照texels、18432000探针射线均逐帧对齐。wall还包含模型更新、提交和等待，不以433.013→509.378ms的全部差值定义GPU加射线成本。

无计时16帧运动回放选中668,238/16,384,000像素帧（4.0786%），总N 20,277,216→35,579,864（+75.47%），extra batches 11,036,800条、新base 4,265,848条；同样是GPU相同循环回放，而非主pass硬件counter。

模型停止窗口共32个计时帧，其中分类仅执行12次（每批6次），表中0.359509 / 0.361472 / 0.371712ms仅描述这12个执行样本。既有物体动态settling与fresh settle共同允许短期分类，不能把它解释为每个停止帧都执行。停止回放选中45,009像素帧（0.2747%），总N 19,786,896→20,553,096，extra batches 480,032；停止期间无cache重照/上传。

**1280×800、2倍接触相机的正式性能。** [2倍接触相机计时](../../build/gi_fresh_sampling_perf_2x_camera.log) 使用独立ABBA及其自身基线，已PASS。以下仍为均值 / 中位数 / P95、单位ms，其它行各n=32，分类附实际执行数：

| 阶段 | scope | 本组当前基线 ms | 2倍 ms |
| --- | --- | ---: | ---: |
| 运动16帧 | 分类 | 未执行（n=0） | 0.378272 / 0.373248 / 0.388096（n=32） |
| 运动16帧 | contact trace | 133.1 / 132.216 / 143.169 | 159.079 / 160.046 / 167.431 |
| 运动16帧 | contact filter | 6.28576 / 6.86541 / 8.53914 | 7.9392 / 8.43469 / 10.7233 |
| 运动16帧 | temporal | 1.36547 / 1.26157 / 2.51699 | 1.28765 / 1.25901 / 1.4377 |
| 运动16帧 | receiver motion | 0.096576 / 0.088064 / 0.121856 | 0.23376 / 0.0896 / 1.13971 |
| 运动16帧 | 整帧GPU | 245.737 / 244.05 / 257.676 | 273.295 / 274.107 / 284.87 |
| 运动16帧 | wall | 247.2 / 245.558 / 259.049 | 274.813 / 275.284 / 287.397 |
| 停止16帧 | 分类 | 未执行（n=0） | 0.369323 / 0.365056 / 0.379904（n=6） |
| 停止16帧 | contact trace | 131.047 / 129.894 / 139.306 | 137.143 / 134.657 / 154.959 |
| 停止16帧 | contact filter | 6.18186 / 6.8951 / 8.43674 | 5.904 / 6.79168 / 8.52992 |
| 停止16帧 | temporal | 1.2296 / 1.19194 / 1.62714 | 1.39027 / 1.19962 / 2.87334 |
| 停止16帧 | receiver motion | 0.094432 / 0.086016 / 0.111616 | 0.207456 / 0.089088 / 1.11411 |
| 停止16帧 | 整帧GPU | 239.166 / 236.805 / 258.347 | 246.448 / 243.193 / 284.913 |
| 停止16帧 | wall | 240.611 / 238.307 / 259.638 | 248.073 / 245.063 / 286.21 |

2倍也不是免费：本组运动整帧GPU均值245.737→273.295ms，增加27.558ms（11.21%）；contact trace133.100→159.079ms，增加25.979ms，filter另增加约1.653ms。分类约0.378ms，接近4倍分类本身，新增耗时主要在接收点追踪。该组无cache上传/重照，固定探针预算仍相同。停止分类只在6/32个计时帧执行；停止GPU均值239.166→246.448ms，不能当作成熟静止成本。

独立16帧运动诊断选中329,938/16,384,000像素帧（2.0138%），与4倍同一分辨率回放相同；实际N 65,847,656→69,992,712（+6.29%），extra batches 2,639,424条、新base仍1,505,632条。停止N 64,019,640→64,299,952，extra batches 159,760条。

质量优先的sealed-room默认保留4倍；2倍作为已验证接触机位的成本选项，牺牲部分首显露质量，且没有消除椭球后续小子集退化。4倍/2倍本组额外GPU均值分别约57.8/27.6ms，但两次实验的各自基线不同、没有跨倍率同批差分或置信区间，不能直接用314.426−273.295ms承诺精确节省；也没有2倍椭球/移动模型的同等质量与性能认证。需要更低成本可用F1调为2或完全关闭追加，但这不会把当前完整软件GI变为60/90/120FPS方案。

Debug、Vulkan的1280×800实际renderer预览已全部通过，见 [接触预览日志](../../build/gi_fresh_sampling_preview_contact.log) 与 [移动模型预览日志](../../build/gi_fresh_sampling_preview_cube.log)。每机位两组各13张，共26张，两个机位合计52张PNG；两组frame64冷静止renderer RGBA逐字节相同，112帧完整序列保持111帧有效历史。它们不用于GPU性能或FPS估算。

移动模型第81帧的 [当前基线](../../build/gi_fresh_sampling_preview_cube_Vulkan_cube_fixed_camera_current_object_stability_frame81_total_renderer_ldr.png) / [4倍当前射线](../../build/gi_fresh_sampling_preview_cube_Vulkan_cube_fixed_camera_fresh_current_rays_frame81_total_renderer_ldr.png) 可直接对照新露出地面；另检查81/88及停止97帧，地面与模型侧面的亮痕减轻，抽看的硬轮廓没有新增明显模糊或双边缘。接触机位第88帧的 [当前基线](../../build/gi_fresh_sampling_preview_contact_Vulkan_contact_near_current_object_stability_frame88_total_renderer_ldr.png) / [4倍当前射线](../../build/gi_fresh_sampling_preview_contact_Vulkan_contact_near_fresh_current_rays_frame88_total_renderer_ldr.png) 在total LDR中差别较细微；检查72/88及停止97帧没有新增明显模糊/双影，但不能据此称主观瑕疵下降67%。上面的约67%是特定oracle首显露子集相对带噪参考的MSE变化，不是整幅图像的主观改善率。

最后的Debug [legacy temporal回归](../../build/gi_fresh_sampling_delivery_temporal.log) 也通过，涵盖原相机重投影、光照/材质/种子/cut重置、失败提交快照、认证物体运动cap4及3个成功settling帧、独立于TAA的64帧静止收敛、已知黑色与unknown保护。它验证当前构建没有破坏既有历史合同，不代替本轮带参考的新显露质量测试；相关Release/Debug构建、CPU/material/frame-command-buffer及两套预览均已完成，新增采样交付范围为Vulkan软件射线。

**1280×800、4倍椭球相机的正式性能。** 最后的 [4倍椭球相机计时](../../build/gi_fresh_sampling_perf_4x_oval.log) 已PASS，沿用相同最终Release实现，没有新增构建。以下为均值 / 中位数 / P95、单位ms，其它行各n=32，分类附实际执行数：

| 阶段 | scope | 当前基线 ms | 4倍 ms |
| --- | --- | ---: | ---: |
| 运动16帧 | 分类 | 未执行（n=0） | 0.409184 / 0.37632 / 0.434176（n=32） |
| 运动16帧 | contact trace | 213.396 / 211.043 / 231.146 | 269.971 / 270.036 / 283.746 |
| 运动16帧 | contact filter | 5.80739 / 5.43898 / 8.35277 | 6.29613 / 5.7984 / 8.03533 |
| 运动16帧 | temporal | 1.49402 / 1.28102 / 3.07712 | 1.3712 / 1.26464 / 2.65728 |
| 运动16帧 | receiver motion | 0.202976 / 0.087552 / 1.07827 | 0.083616 / 0.078848 / 0.11776 |
| 运动16帧 | 整帧GPU | 328.329 / 322.358 / 369.814 | 378.979 / 379.818 / 393.797 |
| 运动16帧 | wall | 330.115 / 323.853 / 371.377 | 399.321 / 382.861 / 405.443 |
| 停止16帧 | 分类 | 未执行（n=0） | 0.372736 / 0.374272 / 0.376832（n=6） |
| 停止16帧 | contact trace | 216.576 / 213.332 / 236.524 | 216.361 / 213.438 / 241.427 |
| 停止16帧 | contact filter | 6.85014 / 6.41075 / 12.757 | 5.67283 / 5.42157 / 7.79366 |
| 停止16帧 | temporal | 1.89507 / 1.26208 / 4.23629 | 1.24086 / 1.20218 / 1.52474 |
| 停止16帧 | receiver motion | 0.638944 / 0.098816 / 6.30272 | 0.2168 / 0.080896 / 1.11923 |
| 停止16帧 | 整帧GPU | 339.775 / 333.37 / 392.545 | 327.04 / 328.177 / 348.88 |
| 停止16帧 | wall | 342.064 / 335.181 / 393.963 | 330.975 / 330.596 / 379.173 |

该机位运动整帧GPU均值增加50.650ms（15.43%），contact trace增加56.575ms（26.51%），分类本身为0.409184 / 0.37632 / 0.434176ms。相机用例无cache上传/重照，探针预算不变。其它阶段存在批次漂移，不能把整帧差值逐项强归因到追加射线。

独立16帧运动GPU回放选中461,885/16,384,000像素帧（2.8191%），实际N 118,402,816→128,450,992（+8.49%），其中extra batches 7,600,792条、原mask外新base 2,447,384条。该分辨率的原接触mask已很大，因此总N相对增幅与320×200的+299.68%不同；两者是各自实际测量，不能混用。停止回放选中31,490像素帧（0.1922%），总N 119,168,040→119,728,128，extra batches 336,936、新base 223,152条。

停止分类仅执行6/32次。整帧均值339.775→327.040ms的负差，同时伴随SDF、reconstruction、receiver motion等阶段及wall长尾变化；contact trace均值基本接近（216.576→216.361ms）。这不是新增算法负成本或静态提速的证据。所有性能组均为有限两批ABBA、没有置信区间；停止恢复窗口也不是成熟静止测量，完整保留均值/中位数/P95以展示批次波动，不能挑单一统计量作强因果结论。

本轮正式验收已覆盖4倍三机位质量与性能、2倍接触质量/性能、两套Debug实际预览及legacy temporal合同。交付保留4倍质量预设与独立关闭/倍率选择；真实收益集中于刚显露接收面，追加当前测量改善了模型移开后的地面，仍有接触机位椭球后续小子集退化。当前软件GI的额外运动GPU成本约50.7–71.3ms（本次4倍三机位各自配对结果），需要继续优化追踪执行效率，不能称为低开销、无残差或60/90/120FPS方案。



## 2026-10-20：移动模型的接收面重投影与新显露区域实验

本轮在下节已验收的相机运动稳定路径上，补齐移动模型自身的历史坐标。密闭陶瓷场景默认开启 `temporal.objectMotionReprojection`，F1 的 `Reproject GI on moving objects` 可直接对照；`RunSealedGiRoom.cmd --surface-cache-no-object-motion-reprojection` 关闭它。公开 API 的该字段仍默认 false，其它场景可用 `--surface-cache-object-motion-reprojection` 或 API 显式开启。`temporal.disocclusionFilter` 是另一项默认关闭的研究开关，不能把物体重投影收益算作新显露区域过滤收益。

旧历史只用当前世界位置寻找上一画面，移动物体上的同一材质点通常已经不在那里。新捕获 pass 按上一成功提交的实例变换与当前变换，计算 `previousModel * inverse(currentModel) * currentWorld`，同时提供逆转置法线映射。CPU 使用实际实例矩阵是否改变的标志区分移动接收面，静态接收面继续采用原世界位置与法线，避免矩阵往返误差改变已验收图像。实例快照仍要求来源、顺序、身份、材质和绘制结构兼容；不支持的编辑按原路径拒绝历史。

材质必须显式提供 `MaterialPassResources.previousWorldCapture=true`，表示其 normal variant 实现 capture mode 4；内置 opaque PBR 提供这项能力，自定义 normal shader 不因资源大小相同而自动获得资格。owner 按全部非 overlay 实例的原顺序编号，最多64个，不只统计可移动或可捕获对象。超过容量、不可逆/非有限变换、无此能力或不兼容实例均不会猜测 owner。捕获支持实例平移、旋转和可逆仿射法线映射，较大的尺度变化仍可能被几何验证保守拒绝；本轮不认证骨骼或顶点变形的通用运动矢量。

历史 tap 还须通过相同 owner、反照率/覆盖签名、上一位置、深度和法线验证。新 owner 签名在原历史位置 alpha 中编码6bit实例、4bit RGB和3bit coverage，整数小于 `2^22`，RGBA32F可精确表示；未开启物体功能时保留原签名。只有 CPU 确认实际移动、当前覆盖完整且严格支持至少0.75、占普通有效支持90%以上的接收面，才跳过把历史拉回相关探针邻域的 RGB clamp；参与这一步的 RGB 仅来自严格支持的 `rigidSum`。没有放宽全局动态上限：默认仍为4帧，运动边缘默认上限2帧，使用用户配置的原 caps；静态表面上变化的间接阴影继续原动态 clamp 和停止响应。空间支持不增加中心可读性或时间计数，已知黑色正常参与，不可读当前样本仍为零。

本节以下性能数据属于捕获仍包含于temporal的历史版本；上节fresh sampling改动把同一次捕获提前至contact之前，当前receiver_motion与temporal已并列，不能用旧scope关系重算新成本。

该功能新增一张RGBA32F上一世界位置/owner捕获目标，1280×800为16,384,000字节（15.625 MiB），复用当前深度；法线变换 palette 为64个mat4、4 KiB UBO。开启且有支持材质时，即使相机/模型静止也执行一个额外场景 raster pass 保存 owner，原四个全屏时域 pass 保留，所以不能称为零静态成本。新增捕获及其后时域都包含在本轮 `temporal` GPU scope 内；整帧 GPU 和含提交/等待的 wall 另报，不用质量读图时间推算成本。关闭物体功能不执行该捕获；关闭/不激活新显露过滤使用不含其实现的 shader permutation。

参考的是 UE 对职责的拆分：本机 `LumenPosition.ush:35–56` 先用无TAA jitter的 `ClipToPrevClip` 定位上一位置，存在有效velocity且深度匹配时再解码物体运动；`StochasticLightingTileClassification.usf:754–761` 编码该重投影。`LumenScreenProbeGatherTemporal.usf:438–449` 解码向量、由屏幕位置减去screen velocity并建立bilinear历史采样，`585–600` 再根据 `LightingIsMoving` 及上一快速更新状态限制累积。`LumenScreenProbeGather.cpp:195–200,1886` 的 `FastUpdateModeUseNeighborhoodClamp` 默认0，对应shader `600–607` 的可选fast-update邻域clamp；这不表示UE所有GI/AO路径都不裁剪，也不是本地免clamp的质量保证。此处是本地刚性实例捕获、64项 palette 和保守 cap4 的适配，没有移植 UE 的完整 velocity/GBuffer、逐探针运动光照分类或局部缓存更新调度。

新验收以当前 `motionStability=true` 为基线，四组分别为 current stability、仅 object reprojection、仅 disocclusion filter、两者组合；旧 camera/local-history 实验仍有独立入口。每个机位用相同9个姿态、64帧冷静止、两轮32帧移动及16帧停止，保持64探针射线、8接收点补追射线和32像素间距。每姿态参考是8个独立seed各32射线，共256spp，共享Surface Cache且有split误差底限，不是路径追踪真值。移动方块用独立实例变换将同一材质点映射到上一姿态，扣除参考真实变化后才报告对齐残差；不把不同画面的 raw delta 当成噪声。

几何新显露子集由上一可见 oracle owner 改变判定，并通过世界点重投影跟踪第1、2–4、5–8帧，独立于生产历史计数。模型移动足迹使用9个姿态地面投影的并集，覆盖往返两端；另报已建立几何支持的接收面误差、误亮/误暗、亮度偏差、最差帧与接触核心/外圈对比。最近像素跟踪可能丢失或合并后续样本，因此各分段必须附像素数和参考底限，且不认证同一网格的自反遮挡。64帧静态输出/实际历史及112帧原始射线、位置、风险、覆盖、metadata仍要求精确相同；保存历史位置的owner标签允许不同。

**V2研究记录，尚非最终源码的正式验收：** `build/gi_dynamic_recovery_v2.log` 中，移动方块的object-only运动RGB MSE下降18.55%、材质点对齐残差下降53.53%、最差帧MSE下降30.81%；误亮/误暗分别下降18.47%/18.63%，停止16帧MSE下降1.20%。该机位接触参考对比为−0.0380868，旧/新为−0.0429221/−0.0429157，绝对对比误差为0.0048353→0.0048289，基本保持。移动足迹参考对比0.0113972，旧/新0.00812808/0.00813811。两种纯相机机位的object-only输出指标不变。最终源码随后收紧为严格RGB子集、保留静态接收面的原世界重建并拆分shader permutation，必须以下面的最终复测为准，不能直接沿用V2性能或静止成本。

新显露过滤仍默认关闭。V1仅扩大当前帧邻域时，移动足迹的显露首帧MSE增加1.95%，完整足迹MSE增加0.36%，见 `build/gi_dynamic_recovery_cube.log`。V2使用两侧对称同权样本，保护局部线性梯度，并研究在纯相机运动中转移成熟邻居的时间残差，而不是直接搬运邻居亮度。V2椭球机位的椭球显露首帧MSE下降3.35%，接触机位的接触区/远平面分别下降1.18%/2.55%，但全几何新显露首帧反而增加1.61%；模型移动足迹首帧仍增加2.17%。这些混合且较小的变化不能称为新显露或模型移开后的旧阴影已经解决，也不作为默认开启依据。

当前源码在V2二进制运行后，进一步加入光照连续稳定至少32个已完成帧才允许相机残差借值的保护；真实物体动态及其settling会清零这一等待，不借用该残差。接触跨度仍由实际短命中距离限制，不增加alpha/count。V2相机用例已预热64帧且之前没有物体事件，因此新增等待不会改变这些用例的资格，但其保护收益并未由V2单独量化；shader permutation和严格RGB子集也属于随后收紧的源码，不能归入V2已经测量的结果。

本节验收时提出的后续方向，是在contact tracing之前按几何新显露与置信度分类，只为这些当前接收点自适应增加当前射线。完整8射线strata批、原8条common prefix、真实N贯穿coverage/harmonic元数据及静止64帧不变的要求，现由上节另行实施和测量；其收益不能归入本节已有的物体重投影数字。

```text
--vulkan --validation --software-rays --procedural --no-debug-ui --surface-cache-dynamic-recovery-test --surface-cache-dynamic-view 2
--vulkan --validation --software-rays --procedural --no-debug-ui --surface-cache-dynamic-recovery-object-test --surface-cache-dynamic-view 2
--vulkan --software-rays --procedural --no-debug-ui --surface-cache-dynamic-recovery-performance --surface-cache-dynamic-view 1 --surface-cache-dynamic-frames 16
--vulkan --software-rays --procedural --no-debug-ui --surface-cache-dynamic-recovery-performance --surface-cache-dynamic-view 2 --surface-cache-dynamic-frames 16
--vulkan --validation --software-rays --procedural --no-debug-ui --surface-cache-dynamic-recovery-preview --surface-cache-dynamic-view 2 --screenshot build/gi_dynamic_recovery_preview.png
```

性能按 `0,1,2,3,3,2,1,0` 对称顺序比较四组，每批64帧预热，运动帧数由CLI指定且必须为16的倍数，随后另测16帧停止。相机路径验证cache上传/重照为零；移动方块路径保留实际完整缓存重照及属性、metadata、geometry上传，并逐帧确认四组工作量和探针预算相同。fresh GPU timestamps分开记录各阶段、整帧和wall，报告均值、中位数、P95；独立 `receiver_motion` scope直接测实例捕获，关闭时应为0 samples/未执行，不能写成测得0ms。它是temporal scope内的子区间，不能将两者相加。计时无图像回读、CPU oracle或呈现；两批/组的结果不带置信区间，不能声称微小差值具有统计显著性。独立1280×800预览保存四组共52张真实渲染PNG，不用于帧率测量。

停止16帧紧接运动，包含停止响应和历史恢复，不是独立预热后的成熟静止性能测试。实例捕获在这些停止帧仍执行，其直接scope可量出这部分额外GPU工作，但不能据此推出长期稳定静止的整帧差值。

最终Release、Vulkan软件射线的320×200 object-only质量复测已完成，见 [正式移动模型日志](../../build/gi_dynamic_recovery_final_cube.log)。以下只比较当前相机稳定基线与默认交付的object reprojection，不包含研究过滤：

| 可见移动方块区域 | 当前基线 | 新重投影 | 变化 |
| --- | ---: | ---: | ---: |
| 32帧运动RGB MSE | 8.95620e-5 | 7.29518e-5 | −18.55% |
| 材质点对齐残差变化MSE | 1.08078e-4 | 5.02189e-5 | −53.53% |
| 最差运动帧RGB MSE | 1.27801e-4 | 8.84286e-5 | −30.81% |
| 运动误亮RGB MSE | 4.75765e-5 | 3.87881e-5 | −18.47% |
| 运动误暗RGB MSE | 4.19855e-5 | 3.41637e-5 | −18.63% |
| 停止16帧RGB MSE | 9.46647e-5 | 9.35261e-5 | −1.20% |

运动亮度偏差为−8.17680e-4→−7.35555e-4，最大单帧亮度绝对偏差为0.00366544→0.00240078。该区参考RGB误差底为2.07955e-5，对齐残差参考底为4.15473e-5；不能把MSE降幅说成同等比例的主观噪声下降，或将接近参考底的对齐结果称为没有残差。完整画面运动MSE下降2.26%，收益主要来自模型自身；最终接触区、远平面、移动地面足迹的质量指标在日志中变化全部为0，显露第1/2–4/5–8帧也不变。这与上游和冷静止的逐字节一致检查分开报告。该修复没有解决模型移开后静态表面的新显露噪声。

原始上游112帧和冷静止64帧检查通过，4,515,840条射线metadata逐条保留，运动及停止后3帧的动态cap4保护通过。灯灭时64,000个可读黑色像素保持零能量；不匹配缓存的64,000个未知当前像素也保持零，未用旧历史补出光照。这里的模型运动轨迹只有固定旋转/缩放下的平移，不能扩展为所有旋转、缩放或导入模型的量化收益。

Debug/Model=ON、Vulkan的1280×800四组预览已完成，共52张实际渲染PNG，各组冷静止第64帧的renderer RGBA逐字节相同，见 [最终预览日志](../../build/gi_dynamic_recovery_preview.log)。移动第88帧的 [当前相机稳定基线](../../build/gi_dynamic_recovery_preview_Vulkan_cube_fixed_camera_current_stability_frame88_total_renderer_ldr.png) 与 [新增物体重投影](../../build/gi_dynamic_recovery_preview_Vulkan_cube_fixed_camera_object_reprojection_frame88_total_renderer_ldr.png) 可直接对照；每组另有81–88帧连续序列及停止检查点。这些图像不是CPU时间平均或生成图片，高分辨率预览也不代替320×200的带参考定量验收。

最终Release/Debug构建、Surface Cache CPU检查、advanced material CPU检查及frame-command-buffer回归均通过，日志为 `build/gi_dynamic_recovery_final_{release_build,debug_build,cpu,material,commands}.log`。[最终legacy temporal日志](../../build/gi_dynamic_recovery_final_temporal.log) 验证原运动/静止上限、3帧动态停止响应、失败帧快照、灯光/材质/种子/cut重置、已知黑色与未知保护，以及独立于TAA的64帧静止收敛。它是既有算法合同回归，不能代替上面的模型运动参考质量验收；本轮量化交付范围为Vulkan软件射线，不据此增加OpenGL动态捕获验收声明。

最终计时采用最后一次仅新增timestamp scope/中位数统计的Release重建，光照数学未改变；正式质量、Debug预览和legacy temporal来自此前的同一光照实现，不能把它们误记成计时版额外重跑。计时版Release/Debug构建及CPU/material/frame-command-buffer检查也重新通过。RTX4060 Laptop、1280×800、Vulkan软件射线的两条路径分别记录在 [相机独立捕获计时](../../build/gi_dynamic_recovery_scoped_camera.log) 与 [移动模型独立捕获计时](../../build/gi_dynamic_recovery_scoped_cube.log)，两者均PASS。每批运动16帧加停止16帧，每配置/阶段合计32个fresh样本；每个机位共256个计时帧，另有512个预热帧。

以下每格按 **均值 / 中位数 / P95** 原样保留日志数值，单位ms。四种配置均开启已交付的相机稳定，研究过滤默认关闭；显示全部配置是为了保留长尾和对照，不能只选最快的一组。

**接触相机机位：**

| 阶段 | 配置 | receiver_motion ms | temporal ms | 整帧GPU ms | wall ms |
| --- | --- | --- | --- | --- | --- |
| 运动16帧 | 当前稳定基线 | 未执行（0 samples） | 2.13309 / 1.15558 / 6.09587 | 268.719 / 266.456 / 289.537 | 270.379 / 267.825 / 291.92 |
| 运动16帧 | 物体重投影 | 0.695712 / 0.108032 / 3.74477 | 2.57222 / 1.57184 / 6.6775 | 269.796 / 268.523 / 309.752 | 272.391 / 270.82 / 321.26 |
| 运动16帧 | 研究过滤 | 未执行（0 samples） | 4.68282 / 3.78112 / 9.8519 | 285.503 / 282.989 / 314.523 | 356.116 / 284.785 / 321.877 |
| 运动16帧 | 物体＋研究过滤 | 0.940672 / 0.103424 / 5.28998 | 4.19501 / 3.12166 / 9.43821 | 268.985 / 266.646 / 273.487 | 405.478 / 268.041 / 274.772 |
| 停止16帧 | 当前稳定基线 | 未执行（0 samples） | 2.33306 / 1.25491 / 8.6784 | 269.32 / 266.537 / 296.541 | 272.529 / 268.33 / 302.161 |
| 停止16帧 | 物体重投影 | 0.235456 / 0.098304 / 1.6425 | 2.30358 / 1.50835 / 6.64986 | 260.464 / 256.744 / 286.978 | 263.07 / 258.418 / 299.522 |
| 停止16帧 | 研究过滤 | 未执行（0 samples） | 1.63357 / 1.08493 / 5.94227 | 255.279 / 254.549 / 262.649 | 257.204 / 256.736 / 266.863 |
| 停止16帧 | 物体＋研究过滤 | 1.00992 / 0.101888 / 7.45984 | 3.02605 / 1.56774 / 11.5456 | 263.388 / 258.25 / 289.106 | 267.418 / 259.742 / 301.325 |

**固定相机、移动模型机位：**

| 阶段 | 配置 | receiver_motion ms | temporal ms | 整帧GPU ms | wall ms |
| --- | --- | --- | --- | --- | --- |
| 运动16帧 | 当前稳定基线 | 未执行（0 samples） | 2.00845 / 1.36858 / 4.88141 | 329.244 / 330.471 / 336.01 | 394.885 / 392.088 / 413.064 |
| 运动16帧 | 物体重投影 | 0.3608 / 0.10752 / 1.77357 | 2.50992 / 1.6343 / 6.38464 | 330.966 / 329.181 / 344.299 | 406.361 / 396.979 / 479.952 |
| 运动16帧 | 研究过滤 | 未执行（0 samples） | 3.14125 / 3.20205 / 6.86387 | 327.546 / 326.807 / 336.034 | 394.452 / 392.747 / 423.214 |
| 运动16帧 | 物体＋研究过滤 | 0.1192 / 0.100352 / 0.256 | 2.45056 / 1.87904 / 4.83123 | 327.69 / 328.207 / 334.159 | 390.949 / 389.314 / 415.233 |
| 停止16帧 | 当前稳定基线 | 未执行（0 samples） | 1.35299 / 1.05318 / 2.7392 | 216.412 / 212.15 / 245.371 | 219.417 / 213.922 / 249.549 |
| 停止16帧 | 物体重投影 | 0.378752 / 0.10496 / 2.4617 | 2.49286 / 1.56262 / 6.30886 | 215.418 / 212.671 / 232.84 | 217.736 / 214.866 / 236.012 |
| 停止16帧 | 研究过滤 | 未执行（0 samples） | 1.64854 / 1.19603 / 3.78573 | 211.81 / 211.434 / 219.864 | 213.68 / 213.361 / 221.083 |
| 停止16帧 | 物体＋研究过滤 | 0.359936 / 0.09216 / 1.7111 | 1.92384 / 1.33734 / 4.27213 | 211.731 / 211.667 / 217.431 | 214.364 / 213.198 / 221.505 |

默认交付的object-only在模型运动中，实例捕获直接scope为0.3608 / 0.10752 / 1.77357ms；相机运动中为0.695712 / 0.108032 / 3.74477ms。停止16帧分别为模型机位0.378752 / 0.10496 / 2.4617ms、相机机位0.235456 / 0.098304 / 1.6425ms。中位数约0.1ms不能代替均值和长尾，也不是每帧固定新增成本。模型运动的temporal均值由2.00845增至2.50992ms，相机运动由2.13309增至2.57222ms；这些包含原时域工作的净span变化，与嵌套捕获scope不应相加或互减来构造独占成本。

模型运动时四组都真实重照153600 texels/帧；每配置32帧共上传属性589824、metadata 32768、geometry 4423680 texels，上传160个region，重照4915200 texels。相机路径及停止段的这些更新均为零，probe配置预算也逐帧匹配，工作量检查通过。模型wall包含CPU场景更新、提交和等待，不能当作纯GPU shader耗时。

完整保留的结果有明显长尾：相机组合组运动wall均值405.478ms、中位数268.041ms、P95 274.772ms；均值高于P95表示极端尾帧未被剔除。相同捕获工作在不同配置中的均值/P95也不同，不能用组合组更低的某个数字替代object-only成本，或宣称增加过滤会提速。只有两批/组且没有置信区间，不据整帧的正负差证明稳定加速、免费过滤或零静态成本。当前整帧GPU仍为数百毫秒的软件GI压力负载，未达到60/90/120FPS目标；没有由离屏GPU时间的倒数推算交互帧率。

最初未分离捕获scope的两份 `build/gi_dynamic_recovery_perf_{camera,cube}.log` 仅作探索记录；其净temporal均值曾在新增pass后反而下降，不能用于声称新捕获没有成本。本节成本结论使用上表最后的独立scope复测。

## 2026-10-20：相机运动的接收面历史稳定

密闭陶瓷房间默认启用 `temporal.motionStability`。常用启动器仍是 `RunSealedGiRoom.cmd`，F1 的 `Stable GI during camera movement` 可以直接对照；`--surface-cache-no-motion-stability` 恢复原运动历史。其它场景不自动启用，公开设置的默认值仍为false；显式 `--surface-cache-motion-stability` 或 API 设置可单独启用。

该方案保留已经验证的射线采样、安全起点、探针空间过滤和接触补追。在相机运动但场景/光照未变、projection不变且已有有效历史时，历史候选还须通过10°法线、严格双向平面距离、像素尺度的世界距离和当前颜色/覆盖一致性检查。复用至少半个bilinear支持且占普通有效支持的90%以上时，上限为 `min(64,max(32,maxFrames))` 再乘有效支持；默认是最多32帧。几何边缘也必须通过这些条件，不能简单沿用旧edge cap2描述新分支。不可读当前样本仍直接为零，空间支持不提高中心alpha或时间复用权重。

当前颜色一致性是5bit RGB加5bit coverage的签名，保存在原RGBA32F历史位置alpha中，最大整数1048579可精确表示；它是albedo代理，不是物体/材质ID。世界位置RGB、法线和深度不变，无新增历史图、射线或渲染pass。旧local-camera实验的full-readable标签现在严格检查`w==2`，不会把新签名误认成该标签。真正的物体运动仍使用原cap4和快速clamp，相机的长历史不应用于动态场景。冷启动静止64帧的输出/历史及整个运动序列的原始射线、位置、风险和覆盖必须精确一致。

默认运动角度过滤设置下，相机刚停下时，曲面过滤切回matching angular strata，其估计均值可能与运动分支不同。首个停止帧把包含当前帧的累计权重上限降至 `min(maxFrames,4)`，随后正常向64帧增长，避免32帧运动估计在停稳后迟迟不退。上限为4时旧估计保留3份权重、当前估计占1份。该措施有更快退掉旧偏差与首帧新噪声的取舍；不会把物体动态历史放宽到32帧。

质量方法是在320×200完整默认GI下，按相同9个姿态、64帧静止、连续两轮共32帧运动及16帧停稳，与每姿态256spp安全接收点参考比较；参考共享单反弹cache，不是路径追踪真值，另报split-reference误差底限。三倍速度测试同时放大平移与转角。几何确认的新显露静态接收面使用previous oracle owner判断，独立于生产history count或补追mask；同一物体的自反遮挡不在该子集认证范围。

| 机位 / 区域 | 运动RGB MSE变化 | 对齐残差变化MSE变化 |
| --- | ---: | ---: |
| 椭球近景 / 椭球 | −56.41% | −54.24% |
| 接触近景 / 椭球 | −47.09% | −48.63% |
| 接触近景 / 接触区 | −35.93% | −35.90% |
| 接触近景 / 远处平面 | −38.64% | −97.72% |
| 三倍速度接触近景 / 椭球 | −44.88% | −48.14% |
| 三倍速度接触近景 / 接触区 | −37.57% | −41.72% |

表格是误差的减少，不是标准差、人眼不可见率或帧率的同等变化。正常接触机位的参考接触对比为0.047075，旧值0.0459913，新值0.0457656；新增损失约参考真实对比的0.479%。新显露像素因缺少自身历史，收益远小于已有支持区域，不能据整片ROI改善宣称新边缘已无噪。固定相机物体运动的112帧中，history-only候选与旧输出精确一致，表示保留响应，不表示物体运动噪声已经改善。

两个进一步实验保持独立开关且默认关闭：`motionSpatialFilter`只过滤运动时的显示残差且不反馈历史；`motionDisocclusionRecovery`只在纯相机运动、中心刚失去重投影历史时，从附近成熟历史恢复当前估计。后者逐像素验证当前几何/颜色路径、逐候选验证重投影、至少两个方向的成熟支持，并用实际接触命中距离限制跨度；不填不可读样本，不制造时间计数。额外显示过滤在接触区只再改善约0.057% MSE却损失更多接触对比；恢复实验在三倍速度的接触区仅额外改善约0.219% MSE、几何新显露子集约0.258%，正常椭球新显露子集约2.51%，不足以宣称反遮挡已经解决。

新增入口：

```text
--vulkan --validation --software-rays --procedural --no-debug-ui --surface-cache-motion-stability-test --surface-cache-dynamic-view 1
--vulkan --validation --software-rays --procedural --no-debug-ui --surface-cache-motion-recovery-test --surface-cache-dynamic-view 1 --surface-cache-dynamic-fast
--vulkan --software-rays --procedural --no-debug-ui --surface-cache-motion-performance --surface-cache-dynamic-frames 32
--vulkan --validation --software-rays --procedural --no-debug-ui --surface-cache-motion-preview --screenshot build/gi_motion_preview.png
```

性能入口在1280×800完整默认GI中，按 `0,1,2,3,3,2,1,0` 比较旧路径、history-only、history＋display-filter、history＋recovery；每批预热64帧、测量32帧，相机运动时不更新/重照cache，不进行图像回读或CPU oracle。temporal scope包含现有4个pass及显示过滤，GPU整帧和含提交/等待的wall分别报告。预览入口另存真实渲染PNG并逐字节验证四种模式的第64帧静止输出，不用这些读图帧推算性能。

RTX4060 Laptop、Release、显式软件追踪的该组实际均值为：temporal scope旧路径3.92971ms，history-only 2.02224ms，display-filter 2.34362ms，recovery 2.91482ms（每模式64个样本，P95分别9.99834/5.24493/5.36166/7.21613ms）。后两者相对history-only额外约0.32138/0.89258ms。新分支通过严格接收面检查后不再运行24点边缘搜索；本批时域阶段均值降低约1.90747ms，但未隔离各分支贡献，不能将全部差值归因于这项搜索。整帧GPU均值分别274.144/267.329/265.785/262.031ms，wall为276.451/269.750/268.119/263.869ms；整帧差值不只来自temporal，有运行负载差异、仅两批且无置信区间，不能把较低的整体数值解读成额外过滤更快。全缓存和射线预算均相同；仍远未达到60FPS。记录为 `build/gi_motion_stability_perf.log`。这些测量早于最后一次停止权重4的调整，连续相机运动分支相同，未测最终停止帧的单独耗时。

最终停止权重4的椭球近景记录为 `build/gi_motion_final_oval.log`。16帧停稳期间，全部几何、椭球、远处平面的MSE分别下降18.93%、22.01%、31.85%；可见小方块的MSE为旧0.000150395、新0.000150455，增加约0.040%。该小方块只占图像1.45%，仍需单独报告；先前保留32帧运动历史、折至10帧的探索版本在该区停稳误差分别增加约49.6%、19.3%，均未交付为默认方案。

过程记录为 `build/gi_motion_stability_{contact,oval,cube,fast_contact}.log`；停止权重10的恢复实验为 `build/gi_motion_recovery_{oval,fast_contact}.log`。首次正常接触机位的运动数字早于停止权重修正，连续运动分支相同，不能把这些旧日志的停稳结果当成最终停止表现。

最终常用Debug/Model=ON版本通过 `surface_cache_test`、`rhi_frame_command_buffer_test` 和Vulkan `--surface-cache-temporal-test`；Release最终椭球质量测试通过有限值、未知/有效黑色、metadata、公平性和静止精确一致约束。验证日志为 `build/gi_motion_final_{cpu,commands,temporal,oval}.log`。最终Debug的1280×800预览共保存52张实际渲染PNG，四种模式的静止第64帧逐字节相同，日志为 `build/gi_motion_preview_final.log`。移动第88帧的 [旧图](../../build/gi_motion_preview_Vulkan_contact_near_legacy_frame88_total_renderer_ldr.png) 与 [新图](../../build/gi_motion_preview_Vulkan_contact_near_motion_history_frame88_total_renderer_ldr.png) 可直接对照；这些截图不是图像生成或CPU平均图，也不用于帧率推算。

## 2026-10-20：动态路径与更新预算的第一轮基线

本轮先保留已验收的静态路径，补齐完整默认 GI 的运动回归和独立 GPU 计时。UE 参考点是本机 `LumenScreenProbeGatherTemporal.usf:489,585–600` 的局部 `LightingIsMoving` / fast-update 权重；本实现原有的整屏 object-motion cap4、相机运动 cap10 仍不是这种局部变化分类。物体的 previous-transform/velocity 重投影、局部变化信号和受预算约束的缓存更新仍是后续工作。

Surface Cache 上传现在使用相邻 CPU generation 的 `DirtyCards`，把属性的三层页范围及 metadata 记录合并成连续纹理行后局部上传。漏代、dirty 被 no-op 清空、cache/owner/topology/layout 不兼容时完整上传；GPU 上传完成后才推进 owner/generation 快照。变化的 BVH 几何仍完整上传，所有卡片仍重新打光，避免移动遮挡物让其它卡片保留旧阴影。256×192 sealed-room fixture 中，单物体移动的属性为 `18432/460800` texels、metadata 为 `1024/8192`，分别减少96%和87.5%；geometry仍为138240 texels，不能把96%称作总上传或整帧提速。

`SurfaceCacheGiSettings.reuseAnyHitBounds` 通过独立 visibility helper 复用 any-hit 子节点入栈前已经通过的 AABB 检查，仅跳过非根节点出栈后的重复检查。any-hit 的最大距离在首个命中立即返回之前保持不变，节点/三角形顺序、访问预算、exhaustion、根节点检查保留；原 `SCIntersect` 和 closest-hit 代码保持原样。开关进入缓存重打光和历史兼容 key，关闭即可比较原算法；没有减少阴影射线或复用旧光照。`--surface-cache-upload-test` 对初始、移动、强度变化和彩色面积灯＋点灯比较完整 RGBA32F 字节，并保留漏代/dirty 清空/尺寸变化和未知/有效黑色检查。主 PBR 阴影图及 GI 空间/接触/时域阶段继续执行。

限制：额外尝试关闭主 PBR 阴影图、放宽校验使用双面积灯时，Vulkan 四对完整图像比较精确通过，但 OpenGL 的 raw GI 出现非有限值，根因尚未定位，不能归因于某个 any-hit 开关模式。已恢复原有单面积灯校验；本轮不扩展多面积灯支持。相关失败记录为 `build/gi_dynamic_upload_visibility_gl.log`，对应 Vulkan 记录为 `build/gi_dynamic_upload_visibility_vk.log`。正式 fixture 使用原有受支持的面积灯＋点灯组合；通过该用例不表示双面积灯问题已修复。

常用 `build/sdf-banquet` Debug/Model=ON 构建的最终验证：Vulkan 上传 fixture 全部通过，四组 any-hit 对照合计12976128字节精确相同。OpenGL 在 Intel 4.5 / 32.0.101.6790 驱动上也通过四组精确对照及四组 incremental/full 比较，但末尾从32扩到64的缓存页重建出现 covered-card validity 失败，整套 OpenGL fixture 因此失败；没有跳过或放宽该断言。这与双面积灯实验分别记录，均未定位根因。本轮验收范围为日常启动器所用 Vulkan 路径，不表示 OpenGL 尺寸变化已经可靠。日志为 `build/gi_dynamic_debug_upload_{vk,gl}.log`。

三个研究开关保持默认关闭：`motionConsistentSpatialReuse` 延续匹配角度单元的过滤；`localCameraHistory` 在连续纯相机运动、完整几何支持和局部光照兼容时渐进扩展至最多 `min(64,2*maxFrames)`（默认20）；`ScreenProbeTraceSettings.singleInvocation` 用一次 Geometry Shader 射线调用写出原来的四个 atlas texel。局部历史首动帧仍用旧上限，固定相机的物体动态路径、静止64帧序列和未知样本不采用扩展；光照兼容只是启发式，不能称作探针估计器稳定性的证明。单次调用是等价性/性能实验，不应因为调用次数减少而默认开启。

新增命令（使用 `material_lab_demo.exe`，下列完整 GI 默认64探针射线、8接触射线、间距32；性能运行不加 validation）：

```text
--vulkan --validation --software-rays --procedural --no-debug-ui --surface-cache-upload-test
--vulkan --validation --software-rays --procedural --no-debug-ui --surface-cache-single-invocation-test
--vulkan --validation --software-rays --procedural --no-debug-ui --surface-cache-dynamic-test --surface-cache-dynamic-view 0
--vulkan --software-rays --procedural --no-debug-ui --surface-cache-dynamic-performance --surface-cache-dynamic-view 1 --surface-cache-dynamic-frames 16
--vulkan --software-rays --procedural --no-debug-ui --surface-cache-lighting-performance --surface-cache-dynamic-frames 32
```

动态 view0/1/2 分别为椭球近景、椭球/方柱接触近景、固定相机的移动方块；默认view3顺序执行三者。质量入口按每个姿态的256spp安全接收点参考，比较实际 GPU raw、spatial、contact 和 temporal，另报参考噪声底、对齐残差变化、接触对比、明暗区域及新历史误差；参考共享单反弹卡片缓存，不是路径追踪真值。候选逐帧核对相同原始射线、位置、覆盖、风险与 metadata；64帧静态序列必须精确一致。质量没有“候选必优”的断言，改善必须从日志判断。

完整性能入口每条路径预热64帧，再分别记录静止、固定轨迹相机、固定轨迹物体和灯强度变化。新 scope 为 screen trace、SDF trace、probe reconstruction、probe spatial、contact trace/filter、temporal、cache lighting，原有 scope ID 保持不变。未执行的打光 scope 报0 samples，不能当成测到0ms；GPU span 不含 CPU 提交和等待，wall 单独计入。lighting-only 入口按 ABBA 比较完整缓存重照，排除 final gather。普通渲染没有增加图像回读或额外提交。

第一轮 Release/NDEBUG、RTX4060 Laptop、1280×800、Vulkan、显式软件追踪、无validation/呈现/图像回读的接触近景压力结果如下。每个配置两批、每批每路径16帧，顺序为 legacy / single / single / legacy，表中是两个批均值的平均；未给置信区间，模型负载为程序几何 sealed room（本诊断构建关闭 imported Model 模块）。

| 路径 | 原追踪 GPU / wall ms | 单次 GS 追踪 GPU / wall ms |
| --- | ---: | ---: |
| 静止 | 240.38 / 241.84 | 541.05 / 542.62 |
| 相机移动 | 246.77 / 248.08 | 546.94 / 549.52 |
| 物体移动 | 351.14 / 408.76 | 642.94 / 700.45 |
| 灯强度变化 | 348.76 / 350.42 | 643.67 / 645.88 |

单次 GS 在本机明显退化，保持关闭。Vulkan 的6组种子/视角/分辨率/安全起点组合加64帧累积对照中，完整 atlas（含padding）、状态和计数均匹配，连续通道最大误差0，但等价不意味着更快。原路径首批静止的 SDF约79.58ms、接触补追126.52ms、探针空间过滤7.51ms、接触过滤5.60ms；变化物体的完整缓存重照约105.38ms，wall还包含额外CPU场景更新。它们是近景压力条件，不能替代正常房间或导入模型的交互帧率。60/90/120帧分别只有16.67/11.11/8.33ms整帧预算，本轮没有达到这些目标。

最终独立 visibility helper 的完整缓存重照另按 ABBA 测量，每批32帧、每种配置/路径合计64帧，均重照153600 texels/帧，射线预算不变。灯强度变化的 lighting scope 由108.06降至98.91ms（约8.47%，节省9.15ms）；物体变化由107.65降至98.97ms（约8.07%，节省8.68ms）。同批 wall 分别由112.28降至102.87ms、152.55降至144.16ms；后者还包含场景更新，不能把 wall 差值全部归因于 shader。记录为 `build/gi_dynamic_lighting_perf_final.log`。此隔离入口不运行 final gather，不能将其 wall 作为完整 GI 帧率。

默认开关的正常房间视角另测一批16帧：静止 GPU/wall 为191.70/193.20ms，相机运动196.70/198.12ms，物体变化295.25/354.20ms，灯变化287.60/289.05ms；静止的 SDF约85.51ms、接触补追70.57ms。记录为 `build/gi_dynamic_perf_room_final.log`，采用上述相同软件追踪、分辨率和程序几何条件；该批早于 visibility helper 的最终分离，不能代替最终优化的整帧配对性能比较。当前软件路径的主要问题是几何追踪、接触补追与全缓存重照的工作量，远未接近60FPS。

三个320×200 Vulkan完整 GI 质量测试使用64帧静止、16帧往返运动及16帧停稳。两个候选均通过 finite、unknown、有效黑色、原始射线 metadata 和上游公平性检查。静止64帧逐帧精确相同，local-camera 候选在固定相机物体运动的96帧也精确相同。Angular 的接触 ROI 运动 MSE 下降6.53%、对齐残差下降3.47%，但接触对比误差由−0.00111346扩大至−0.00157654、暗区误差增加8.39%；另一椭球近景的停稳 MSE 增加5.46%、新生历史运动 MSE 增加18.05%。因此不启用。Local-camera 实验超过旧 cap 的像素帧占两个相机轨迹的0.2585%和4.0462%，平均历史仅7.5595→7.58374及8.92577→8.94955，全画面运动 MSE 改变量不足0.002%，没有有意义收益，也不启用。不能把质量测试 PASS 当成运动光斑已经解决；下一步需要局部光照/物体运动信息，不能仅调高历史或加重过滤。日志和真实 GPU 视图为 `build/gi_dynamic_final_view{0,1,2}.*`。

最终常用 Debug/Model=ON 版本通过 `surface_cache_test`、`rhi_frame_command_buffer_test` 及 Vulkan `--surface-cache-temporal-test`。时域回归包含静止64帧增长、微小相机运动回到原上限、重新停稳恢复、物体动态cap4、有效黑色与未知样本、相机重投影及光照/材质/种子/cut重置；Vulkan validation 日志无错误。记录为 `build/gi_dynamic_debug_{cpu,frame_commands,temporal}.log`。常用启动器仍为 `RunSealedGiRoom.cmd`。

基线日志为 `build/gi_dynamic_perf_{legacy_a,single_b,single_c,legacy_d}.log`；Release构建为 `build/gi-dynamic-release`、输出为 `output/gi-dynamic/Release/bin`。后续预算应优先约束接触补追、软件几何追踪和缓存重照，再处理最终帧流水，不能将减少上传量或延长历史直接写成60FPS收益。

## 2026-10-19：近距静止椭球的探针采样收敛

正确交互入口是 [RunSealedGiRoom.cmd](RunSealedGiRoom.cmd)，以 `--surface-cache-sealed-room` 启动方形顶灯 / 陶瓷椭球场景，默认渲染 **1280×800**。按F1，在GI时域面板切换 `Static probe convergence (64 frames)` 对比。`RunGiRoom.cmd` 的 `--gi-room` 是旧点灯房间 / lumenGi路径，不是本节场景；文中2560×1440数据是显式指定尺寸的高分辨率专项，不代表本场景默认启动尺寸。

上一轮的曲面权重与补追交接约束解决的是照明复用和颜色连续性；近距离静止观察仍可能看见每个探针的采样残差。旧路径默认每探针64条射线，probe seed按 `temporalSequence % directionPhases` 推进，默认8帧后重复同一组方向；时域默认最多累积10帧。反复平均固定八组测量，不能继续消除它们共同留下的探针相关 Monte Carlo 误差。单独延长历史也不能消除不同法线半球间近邻角度重采样产生的固定偏差，因此本轮结合长方向序列、64帧静止历史与匹配角度单元的空间复用，保留后文曲面 / 色彩修正。

`SurfaceCacheTemporalSettings.staticConvergence` 默认开启。时域路径有效且 `maxFrames>1` 时，probe seed使用基础种子加**已完成帧的 temporal sequence**，再取29bit掩码 `0x1fffffff`，不再先对默认8相位取模。该分支也用于移动期间，使停走时不会在两种种子序列间切换；它改变的是探针方向实现，不改变探针位置或每帧射线数。29bit种子空间是有限的，不能称为无限永不重复，也不保证每条实际浮点方向永不碰撞。关闭开关仍使用用户的 `directionPhases` 周期；`maxFrames=1` 同样不启用这项长序列。

64帧历史只在CPU确认以下条件时开放：历史已有效、新开关开启、用户 `maxFrames>1`、本帧没有 dynamic-lighting 标志，且当前 view / projection 与上一已完成帧**精确相同**。历史上限此时为 `max(userMaxFrames,64)`；现有设置允许的最大值为64。GI接收的是实际渲染相机，TAA投影抖动也会使这一精确比较失败，不能把抖动中的静止相机直接当作64帧静止视图。

相机移动、动态照明或几何运动的响应路径继续使用原上限与运动 / dynamic / edge caps。GPU仍逐像素验证历史位置、深度、法线、readability及有限值；不可读当前样本不会被旧历史填成有效光照。64是可用的累计权重上限，不保证所有像素都累计64帧，达到上限后仍为有界EMA更新，不是无限增加样本的算术平均。真实光照或材质变化造成历史key变化时按原规则重置，新开关本身也进入历史兼容key；丢弃或未完成提交的帧不推进已完成序列。

在上述静止判定成立且 `reconstructionDebug=0` 时，管线在 `RecordScreenProbePlacement` 完成之后给运行时 `placementFrameConstants.refinement` 添加数值16的标志（bit4），约束 `SFGeometryWeight` 的角度复用。只有法线点积 **大于 `1-1e-6`（约0.081°容差）**，且 `abs(normal.z)<0.99` 的切线轴分支相同的邻探针才能参与空间过滤，按对应的角度stratum索引复用。各探针仍有独立jitter，实际射线世界方向并不逐条相同；这个容差和索引对应也不等于消除所有过滤偏差。物理分配、实际探针位置和追踪起点已经确定，不被运行时标志改变。

不同法线的曲面邻居不再在静止模式下通过 nearest angular warp 借用射线。该近邻重映射没有严格保持角度单元的测量分布，跨三轮重复可能留下与探针相关的偏暗，即使随机噪声继续收敛也不会自动消失。新限制按法线与切线基判定，不根据RGB或亮度挑选邻居；同向平面仍保留三轮空间过滤，实际接收点的接触过滤也保留。首帧、移动或动态settling期间继续沿用原复用路径和历史保护；刚进入静止模式的历史还混有旧路径估计，需要后续帧逐步更新，不承诺立即无偏。UE使用共同世界方向gather grid，本实验仍是局部余弦半球表示，只在匹配角度单元时保守复用，不等于移植了完整UE方向过滤管线。

这项组合没有增加GPU绑定接口、持久纹理 / UBO、pass、每帧射线数或空间模糊，也不加入receiver-query偏移、额外查询路径纹理读取或Contact反照率绑定。静止运行时常量与分配时常量不同，会多一份placement UBO版本snapshot；它使用现有接口和资源管理机制，不能据此称为严格零上传成本，配对GPU测量包含这部分工作。原上采样查询位置及真实接收点 `p/n/rho`、安全起点和接触射线保持原定义；变化的是方向序列、历史上限和静止时允许参与空间过滤的角度兼容邻居。静止等待按帧计，实际秒数取决于运行帧率；该方法不能消除固定几何、缓存覆盖或方向拟合的全部系统偏差。新的方向与复用限制仍可能改变短命中分类和接触mask，因此无新增持久资源不等于无成本，整帧与接触组仍需配对测量。

F1的 `Static probe convergence (64 frames)` 控制该开关。专项CLI使用 `--surface-cache-probe-imprint-convergence` 修饰原质量或性能入口；修饰参数本身不启动专项。新旧两组都保持 `curvatureAwareWeights=true`、`coherentProbeRisk=true`，仅对比 `staticConvergence=false/true`，避免把上一轮颜色过渡修改的收益或代价混入本轮。

质量模式默认在重置后渲染128帧，检查第32/64/128帧；`--surface-cache-probe-imprint-convergence-frames 64` 可选择32/64帧检查点，此参数接受64或128。`current` 是用户实际看到的当前GPU阶段，`cumulative` 是CPU对重置后全部帧的平均，`last32` 是最近32帧的CPU块平均；三者分开保存和报告。它还比较相隔8帧的当前图、相邻32帧块平均，以及实际历史权重、活动探针数、mask覆盖和未知 / alpha约束。相隔8帧不再相同只能说明短周期被打破，不能单独证明降噪；仍需结合current图的参考误差、偏色、接触剖面和参考噪声底。参考继续使用同安全起点、共享Surface Cache的独立seed采样，沿用split-half噪声底及几何ROI，不能当作完整路径追踪真值。

默认 convergence 质量视角为正对椭球的 oval_frontal_static；显式 `--surface-cache-probe-imprint-view 1` 可测 contact_near，`2`可测 wall_corner。性能仍固定 contact_near、AB/BA/AB/BA四对，每批预热64帧、测量16帧，每组64个fresh frame / trace / filter样本，使用四个配对批均值的Student t(3)区间。初次SDF / atlas / 卡片准备不计时；禁止validation、呈现、图像回读与CPU质量oracle。原非convergence专项保留16帧预热与上一轮两项开关的对照语义，并显式关闭新的静止收敛选项。

```text
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --procedural --surface-cache-probe-imprint-test --surface-cache-probe-imprint-convergence --surface-cache-probe-imprint-view 0 --surface-cache-curved-contact-reference-seeds 32 --render-width 640 --render-height 400 --screenshot build\probe_stationary_front.png
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --procedural --surface-cache-probe-imprint-test --surface-cache-probe-imprint-convergence --surface-cache-probe-imprint-convergence-frames 64 --surface-cache-probe-imprint-view 1 --surface-cache-curved-contact-reference-seeds 32 --render-width 640 --render-height 400 --screenshot build\probe_stationary_near.png
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --software-rays --procedural --surface-cache-probe-imprint-performance --surface-cache-probe-imprint-convergence --render-width 1280 --render-height 800
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --software-rays --procedural --surface-cache-probe-imprint-performance --surface-cache-probe-imprint-convergence --render-width 2560 --render-height 1440
```

### 正面静止视图的正式结果

[正式正面日志](../../build/probe_stationary_front.log) 已完成640×400、1024-spp共享Surface Cache参考、217606个椭球外壳几何接收点的128帧对照；完整结果见 [指标JSON](../../build/probe_stationary_front_metrics.json)。以下是**当前GPU temporal图**，没有用CPU累计平均替代显示结果：

| 重置后帧数 | 旧RGB MSE | 新RGB MSE | MSE变化 | phase32 x/y残差能量和变化 | 新历史平均权重 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 32 | 4.14489e-6 | 2.63043e-6 | −36.5% | +16.6% | 32 |
| 64 | 4.17808e-6 | 2.42033e-6 | −42.1% | +10.6% | 64 |
| 128 | 4.17574e-6 | 2.30427e-6 | −44.8% | −5.6% | 64 |

旧历史平均权重始终为10。第128帧新图平均亮度偏差从−2.273e-4减为−9.7e-6，但phase32只小幅减少，前两个检查点甚至上升，因此不能将MSE降幅称为同等比例的探针瘢痕或总噪声改善。参考RGB噪声底为2.08007e-6，已接近新图MSE；高通及梯度也受参考底限制，不据扣底比例追加更大的降噪声明。CPU累计128帧平均的MSE下降38.3%，phase32反而增加约0.8%，进一步说明当前图、长期平均和格相关残差应分开解读。

第32/64/128帧新旧活动探针数均为1157；probe raw / spatial的未知几何alpha计数均为0，选中几何未知计数为0，alpha、mask零、有限值及三项参数历史失效检查通过。旧probe raw相隔8帧及相邻32帧块平均精确重复，新方向序列打破该短循环；这仅说明得到新方向测量，不能单独作为画质改善证据。

第128帧实际总图：[旧配置](../../build/probe_stationary_front_oval_frontal_static_previous_frame128_total_current_renderer_ldr.png) · [新配置](../../build/probe_stationary_front_oval_frontal_static_improved_frame128_total_current_renderer_ldr.png)；独立当前GI：[旧temporal](../../build/probe_stationary_front_oval_frontal_static_previous_frame128_current_temporal_tone4.png) · [新temporal](../../build/probe_stationary_front_oval_frontal_static_improved_frame128_current_temporal_tone4.png)。这里没有把仅长历史或查询抖动的探索版本作为交付结果。

### 接触近景的正式结果

[正式近景日志](../../build/probe_stationary_near.log) 完成640×400、1024-spp共享缓存参考的64帧对照，完整的分阶段与三种估计结果见 [指标JSON](../../build/probe_stationary_near_metrics.json) 和 [汇总](../../build/probe_stationary_near_summary.log)。第64帧**当前GPU temporal图**如下；不能用CPU累计平均替代这张实际显示结果：

| 几何区域 | 像素数 | 旧RGB MSE | 新RGB MSE | MSE变化 | phase32 x/y残差能量和变化 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 椭球外壳（>35 cm） | 137757 | 1.64935e-6 | 9.50110e-7 | −42.39% | **+17.74%** |
| 椭球过渡（3–35 cm） | 4825 | 2.43257e-5 | 2.10286e-5 | −13.55% | −10.9% |
| 方柱接触核心（0–3 cm） | 1131 | 6.28672e-7 | 4.55754e-7 | −27.51% | −11.0% |
| 方柱过渡（3–35 cm） | 33992 | 1.17257e-6 | 8.88243e-7 | −24.25% | −14.0% |

椭球外壳MSE下降，但该近景的32像素相位残差上升17.74%，因此不能宣称各视角的探针印痕都下降，或把MSE变化当作同等比例的总噪声下降。外壳参考RGB底为6.90394e-7，接近新图MSE；多数区域高通和梯度仍受参考底限制。CPU累计64帧平均的方柱核心MSE反而上升约4.8%、phase32上升1.2%，也说明组合没有在所有估计与区域上取得一致收益。

方柱核心1131个像素的平均亮度绝对误差为6.67384e-5→3.21739e-5，核心相对外壳的对比误差为1.08029e-4→6.80233e-5，原接触细节保护通过。该视角椭球0–3 cm核心ROI没有有效像素，不能把方柱核心结论扩展为椭球核心验证。新旧活动探针数均为964，mask平均覆盖7.82891%→7.82148%；raw与spatial每帧各4个未知几何像素在新旧配置中一致，选中几何未知计数为0。alpha、mask零、有限值及三项参数历史失效检查通过。

第64帧实际总图：[旧配置](../../build/probe_stationary_near_contact_near_previous_frame64_total_current_renderer_ldr.png) · [新配置](../../build/probe_stationary_near_contact_near_improved_frame64_total_current_renderer_ldr.png)；独立当前GI：[旧temporal](../../build/probe_stationary_near_contact_near_previous_frame64_current_temporal_tone4.png) · [新temporal](../../build/probe_stationary_near_contact_near_improved_frame64_current_temporal_tone4.png)。

### 静止收敛的GPU成本与回归范围

1280×800的 [正式性能日志](../../build/probe_stationary_perf_1280.log) 已完成。硬件为RTX 4060 Laptop，Debug、Vulkan软件射线、contact_near视角；每配置64个计时帧，另有每批64帧预热。两组仅改变 `temporal.staticConvergence`，不包含validation、呈现、图像回读或CPU质量oracle。各scope的帧样本分布如下，单位ms：

| scope | 配置 | 均值 | 中位数 | P10 | P90 |
| --- | --- | ---: | ---: | ---: | ---: |
| frame | 旧 | 358.082 | 352.248 | 324.245 | 403.965 |
| frame | 新 | 333.923 | 334.263 | 307.472 | 364.422 |
| trace | 旧 | 162.574 | 161.764 | 153.681 | 172.341 |
| trace | 新 | 158.606 | 158.588 | 148.507 | 167.302 |
| filter | 旧 | 14.9091 | 12.9336 | 7.45114 | 23.2818 |
| filter | 新 | 13.4312 | 12.1651 | 6.54152 | 21.0190 |

| scope | 新减旧 ms | 四对批均值的95%区间 ms |
| --- | ---: | --- |
| frame | −24.1582（−6.74656%） | [−48.2311, −0.08528] |
| trace | −3.96765 | [−9.39643, 1.46113] |
| filter | −1.47784 | [−4.24234, 1.28666] |

整帧配对区间上界仅略低于零，当前批次有变快迹象，但证据有限，不能据此普遍承诺提速。trace与filter区间均跨零，不能分别宣称其成本下降；`filter` 是接触resolve / filter组，**不是探针辐射率空间过滤的独占成本**。这些scope也不是可相加的独占分解，不能用组差之和解释整帧净变化。正式近景质量的探针数相同，但性能模式不回读图像，仍明确允许活动计数 / mask覆盖随方向变化，不能称为共同总射线预算的严格对照。

2560×1440的 [第一轮正式性能日志](../../build/probe_stationary_perf_2560.log) 使用相同协议。各scope分布与配对区间保留如下，不剔除波动较大的首对批次，单位ms：

| scope | 配置 | 均值 | 中位数 | P10 | P90 |
| --- | --- | ---: | ---: | ---: | ---: |
| frame | 旧 | 919.460 | 834.081 | 788.920 | 1238.100 |
| frame | 新 | 894.377 | 829.905 | 788.514 | 1106.700 |
| trace | 旧 | 516.977 | 502.140 | 477.805 | 574.196 |
| trace | 新 | 523.203 | 507.926 | 482.520 | 563.220 |
| filter | 旧 | 25.3955 | 13.0760 | 12.3119 | 54.0200 |
| filter | 新 | 35.0719 | 13.1917 | 12.3615 | 132.0670 |

| scope | 新减旧 ms | 四对批均值的95%区间 ms |
| --- | ---: | --- |
| frame | −25.0833（−2.72805%） | [−115.868, 65.701] |
| trace | +6.22571 | [−12.3256, 24.7770] |
| filter | +9.67642 | [−20.6726, 40.0255] |

首对旧 / 新整帧批均值为1225.20 / 1115.07 ms，后三对各配置约793–844 ms；首对filter为62.1814 / 100.464 ms，随后约12.6–14.0 ms。三scope的配对区间均跨零且较宽，不能由该轮确定净成本，更不能称为零成本或提速。该结果保留为正式第一轮。

相同协议的 [2560×1440独立复测](../../build/probe_stationary_perf_2560_repeat.log) 也已完成，每配置仍为四批、共64个计时帧。下表与第一轮独立报告，单位ms：

| scope | 配置 | 均值 | 中位数 | P10 | P90 |
| --- | --- | ---: | ---: | ---: | ---: |
| frame | 旧 | 824.312 | 824.621 | 784.068 | 856.171 |
| frame | 新 | 783.679 | 781.517 | 734.670 | 822.616 |
| trace | 旧 | 489.554 | 488.144 | 465.284 | 506.035 |
| trace | 新 | 475.543 | 474.793 | 443.991 | 503.091 |
| filter | 旧 | 14.8128 | 14.3370 | 12.0300 | 17.1922 |
| filter | 新 | 13.2237 | 12.8753 | 11.9457 | 15.0065 |

| scope | 新减旧 ms | 四对批均值的95%区间 ms |
| --- | ---: | --- |
| frame | −40.6336（−4.92939%） | [−101.346, 20.0791] |
| trace | −14.0103 | [−59.6722, 31.6515] |
| filter | −1.58914 | [−3.32623, 0.14796] |

复测三scope区间仍全部跨零；两轮都不能可靠确定小幅净增减，不能宣称零成本或普遍提速。两轮不合并、不挑轮，也不剔除首对较慢批次。`filter` 仍只是接触组，不能据其均值变化解释全部探针空间复用成本；1280结果的有限变快迹象也不能外推到2560或其他设备。

[最终构建](../../build/probe_stationary_final_build.log)、[最终CPU设置回归](../../build/probe_stationary_final_cpu.log) 与 [Vulkan时域回归](../../build/probe_stationary_temporal_vk.log) 已完成并通过：新模式生成第8帧之外的新方向，49150个像素可达到64帧权重，微小相机运动回原cap、恢复静止可再累积；认证物体运动仍受4帧cap约束，可读黑色与不可读当前样本的契约保留。这些数学 / 历史检查不能代替画质专项。

[OpenGL空间回归](../../build/probe_stationary_spatial_gl.log) 和 [OpenGL方向契约](../../build/probe_stationary_directional_gl.log) 已通过，使用的是 **Intel OpenGL 4.5（驱动32.0.101.6790）**，不是上述Vulkan性能所用的RTX 4060 Laptop。空间回归检查噪声收敛、探针密度、自适应覆盖、屏幕可见性和unknown保留，metadata / light-scale误差均为0；方向回归检查原RGB / alpha数学契约、最近几何命中元数据、可读黑色 / 不可读样本、方向重建开关和历史失效。该320×200方向专项的raw未知计数4883保持，on/off/on误差为0。这些结果支持结构与数学兼容，不证明两设备或两后端的视觉 / 性能等价。

原Final Gather时域fixture显式关闭静止收敛与接触补追，分别隔离原相位 / history cap及probe覆盖契约；probe / spatial无覆盖时，默认接触路径仍可在真实接收点合法恢复光照，因此不应把该最终结果误判为历史填洞。旧“几何变化全重置”的初始与shipped配置还显式设置 `retainHistoryOnObjectMotion=false`，隔离原reset / raw-equality契约；默认开启的运动历史保留仍由独立认证物体运动fixture以true测试。两项职责隔离不改变候选像素、断言或门限；完整edge / recovery GPU回归不在这里列为已通过。

[最终隔离连续运动回归](../../build/probe_stationary_legacy_motion.log) 已通过yaw、translation与combined三条路径，实际渲染320×200、间距8、每探针16条射线，参考为4×256条；每条路径预热24帧、运动16帧、settle 8帧。72个对齐帧的 `raw_phase_fairness_max=0`，调参候选的噪声与几何保护检查通过。该专项关闭新静止模式，只验证隔离后的既有Final Gather运动对照与输入相位公平性；它不代表已完成全部edge / recovery GPU专项，也不把运动候选之间的收益归到新静止组合。

## 2026-10-19：表面探针印痕与补追色彩过渡

**历史范围：** 本节记录曲面 / 色彩过渡修正，静止收敛选项未启用；质量和成本数字仍对应本节原配置，不能代替上方静止收敛专项。

本轮处理椭球表面随探针格排列的照明印痕，以及探针估计与真实接收点补追交接时的色彩变化。曲面上的两点本来就会偏离彼此的切平面；若将这种二阶弦误差全部当成表面不兼容，再乘较强的法线幂次权重，邻点支持会围绕每个探针集中。不过，直接放宽所有曲面支持也会在近处遮挡附近借入不同颜色的照明，所以新增支持还必须受真实几何命中视差限制。这些是本实验重建与复用策略的取舍，不表示 Lumen 无法进一步改善。

### 照明复用与物理聚类分别判断

`ScreenProbePlacementSettings.curvatureAwareWeights` 默认开启，只影响 GI 的照明兼容权重。重建和探针射线空间过滤共享中点切平面残差 `abs(dot(delta,normalize(n+pn)))`：平滑曲面的弦位移不再因端点切平面误差而过度集中在某个探针周围。重建的法线兼容在原有 0.6 硬门限至 0.9 之间平滑上升；空间过滤使用自己的原有法线硬门限。平面支持在门限前的最后25%区间平滑降到零，硬拒绝边界仍保留。平行分层的法线相同，中点平面残差仍保留实际层间距离；屏幕段 / BVH 可见性也继续独立约束复用。

表面代表的**物理聚类、实际选点和 GI 分配覆盖**使用独立的严格权重，不使用放宽后的照明模型。物理聚类保持原双端切平面判定 `SPGiClusterPlaneWeight`；分配阶段的 `SPUniformWeights` 和 `SPGiCoarseCurvatureError` 也使用原 Cluster 权重，未启用 refinement 时仍用原 `SPGiPlaneWeight`。这样不会仅因为两折面位移在平均法线方向抵消，就把它们合入同一候选槽，也不会因为新照明支持更宽而改变活动探针的分配数量或位置。曲率代表分配、实际位置 B2 核和固定完整支持窗口继续保留；没有加入按 RGB 亮度选择邻居的规则。

接收点重建进一步在严格旧权重与曲面新权重之间混合。对每个探针，使用接收点切向位移乘该探针**最近真实几何命中的逆距离**，得到命中视差近似；`SPGiCurvedHitSupport` 在视差小于 `tan(5°)` 时允许新权重，在 `tan(5°)` 到 `tan(10°)` 之间连续退回严格旧支持，之后只用旧支持。采用最近命中，是为了让少数近处障碍也能保守限制新增借光支持，避免平均命中距离掩盖它们。这个约束只决定是否允许新增曲面支持，不删除合法的旧支持；缺少可用元数据时同样退回严格权重。几何覆盖和解析重建诊断不读取该照明元数据。

重建与探针射线空间过滤还会依据两端法线旋转角限制新权重：10°以内允许完整新支持，10–20°之间平滑退回严格旧支持，20°及以上使用旧支持。这是另一项角度约束，不要与上述 **5–10°命中视差**混用。它针对本实现的有限法向半球与局部方向拟合；几何表面连续不代表大角度法线变化下的方向估计同样可靠。几何 coverage 以及解析 constant / impulse 重建诊断使用同一法线旋转支持，只有真实照明额外读取最近命中 metadata，避免诊断宣称几何可用、常量重建却因另一套支持变黑。新旧几何权重都为零时提前返回，避免继续归一化无效法线。接收点真实照明同时使用几何命中视差与法线旋转约束；它不是 UE 共同世界方向表示的完整移植。

### 内部几何元数据与补追交接

现有 directional atlas 每探针四个 RGBA32F texel，第一项仍为原 `E/PI` 与公开 readability，后三项为 RGB 方向系数。这里只复用 **x 系数 texel（part1）的 `.w`**，存储 `max(1/d)`，即最近真实几何命中的逆距离。只统计有效正距离的真实几何命中状态 `1/17/9..12`；黑色、背面或缓存未知几何命中都参与最近距离判断，miss 不参与，无真实几何命中时为0。该几何 metadata 不是调和平均，也没有按射线数求平均；原 RGB 与 readability 仍使用完整射线预算作为分母，未知射线不会被移除。RGB、part0 的公开 coverage 和 part2/3 的零 alpha 均保持原契约；关闭曲面权重时 part1.w 为零，并跳过额外的命中距离读取。

内部 metadata 可用位与 RGB 方向拟合开关分开。默认方向重建开启时复用现有积分 pass；关闭 `directionalReconstruction` 但保持曲面权重开启时，仍执行内部 directional 积分以生成 metadata，公开 `surfaceCacheDirectionalCollected` 和 directional readback 仍保持关闭。这使 RGB 方向拟合的开关不会顺带改变几何权重或 readability；两项都关闭时无需该内部 pass。

Contact mask 与探针 RGB 重建共用 `SPGiReconstructionStencil`、`SPGI_CURVED_SUPPORT` hook 和同一 directional metadata，确保几何风险按实际照明所用的支持计算。`SurfaceCacheContactSettings.coherentProbeRisk` 默认开启：除原有切向视差风险之外，以完整射线预算中的近几何命中比例提供风险下限，避免恰好位于探针源点时位移为零、接触风险反而消失。该下限随切向世界距离在 `0.15D..D` 内平滑衰减；RGB 不参与风险选择。近黑命中和缓存未知几何仍属于风险，新增选中接收点仍独立认证安全起点并发射真实射线。

同一开关将探针风险到 mask 的映射由旧 `smoothstep(0.5t,1.5t,risk)` 改为 `smoothstep(0,4t,risk)`，默认 `t=0.05`。低风险外带从0开始以更宽范围平滑过渡，避免探针估计与补追结果在较窄选区内交接；接收点屏幕几何风险仍以 `max` 合入，明确接触核心仍可保持 mask=1。这个变化会改变实际补追覆盖，低风险外带可能扩张，成本需纳入测量；它不对 RGB 施加额外模糊。

同一开关也让年轻历史恢复将连续 readability 当作置信度，而非用 `abs(alpha差)>.001` 把同表面的探针 / 补追混合区域分开。未知 tap 仍拒绝，逐像素路径、法线、平面、材质和世界距离门限继续生效；置信度仅衰减过滤权重，不提高中心 alpha，也不增加真实时域样本数。

本轮未增加每探针或每个选中接收点的射线数，默认仍分别为 **64 / 8**；未新增纹理或 UBO，默认方向重建开启时也没有额外 pass。严格分配模型独立于新照明权重，固定场景 / 相位下预期活动探针数量、位置及对应探针射线数保持一致，专项仍记录 actual counts 核对。接触风险会改变补追覆盖，metadata 读取与权重计算也有成本。因此不能称为整套方案总射线预算相同或零成本，需分别实测完整帧、接触检测 / 追踪组与过滤组。

本机 UE 5.7.1 的 [LumenScreenProbeFiltering.usf:358](D:/UE5/engine/UE_5.7/Engine/Shaders/Private/Lumen/LumenScreenProbeFiltering.usf:358) 用中心和邻点较短命中距离约束近邻视差，保护近处接触；[LumenScreenProbeGather.cpp:294](D:/UE5/engine/UE_5.7/Engine/Source/Runtime/Renderer/Private/Lumen/LumenScreenProbeGather.cpp:294) 说明全分辨率短程 AO 对低分辨率探针缺失高频接触的补充职责。这里参考的是“方向光照复用与接收点短程修正分开、近遮挡限制借光”的原则。本实现仍使用独立法向余弦半球与受限一阶拟合，并非 UE 的共同世界方向 gather grid、SH3 或完整 Short Range AO / GI 移植。

### 专项范围与复现

`--surface-cache-probe-imprint-test` 比较 previous（两项新开关均关闭）、improved（均开启）及 contact_off，分开记录 probe raw、probe spatial、contact unfiltered、contact filtered、temporal 五级 HDR。`--surface-cache-probe-imprint-view 0|1|2|3` 对应 oval_close / contact_near / wall_corner / 全部，默认全部。接收区域依据独立 CPU 主射线对象归属及几何距离确定，不由生产 mask 选择；接触核心为0–3 cm、过渡3–35 cm，外壳超过35 cm。另记录真实活动探针数量、补追覆盖、未知 / mask零 / alpha约束、接触剖面和参数历史失效。

参考复用 `--surface-cache-curved-contact-reference-seeds 32|64|128`：每 seed 32条安全接收点射线，总计1024 / 2048 / 4096 spp，参考重照明预热不计入样本。参考和生产共享 Surface Cache，因此它检查接收点估计与重建偏差，**不是完整路径追踪真值**。无可读参考的像素从质量 ROI 排除，同时保留独立未知覆盖约束。完整参考噪声底按两半参考均值差平方的四分之一估计，高通与残差梯度也使用对应算子估底。

质量指标针对预热16帧之后8帧的**平均 HDR 图**计算 RGB MSE、区域偏色、残差高通及梯度；另行记录帧间方差，不能把平均图 MSE 当作单帧颗粒方差。周期4/8/16/32的 x/y 相位指标是去掉区域平均偏差后的条件均值能量，适合观察探针格相关印痕，不能当作频谱证明，也不能仅凭相位或高通下降断言改善。应同时核对参考噪声底、RGB偏差、核心 / 外壳对比和实际图像。

```text
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --procedural --surface-cache-probe-imprint-test --surface-cache-probe-imprint-view 3 --surface-cache-curved-contact-reference-seeds 32 --render-width 640 --render-height 400 --screenshot build\probe_imprint_views.png
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --procedural --surface-cache-probe-imprint-test --surface-cache-probe-imprint-view 3 --surface-cache-curved-contact-reference-seeds 32 --render-width 1280 --render-height 800 --screenshot build\probe_imprint_verified.png
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --software-rays --procedural --surface-cache-probe-imprint-performance --render-width 1280 --render-height 800
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --software-rays --procedural --surface-cache-probe-imprint-performance --render-width 2560 --render-height 1440
```

性能专项固定 contact_near 视角，仅交错上述两个开关，保持相同每探针 / 每选中接收点射线数。四对顺序为 AB / BA / AB / BA，每批预热16帧、测量16帧，每配置各64个新鲜 frame / trace / filter 时间戳；报告均值、中位数、P10/P90和四个配对批均值的 Student t(3) 95%区间。SDF准备、初次卡照明和阴影atlas创建不计入稳态样本；禁止 validation、呈现、图像回读和 CPU oracle。`trace` 包含接触风险 / mask / 实际补追，`filter` 包含既有 resolve 与过滤；两组跨度不能简单当作整帧净增量。

F1 中 `Curved surface lighting weights` 控制曲面权重，`Coherent near-contact probe risk` 控制风险下限与年轻恢复置信度；它们分别进入历史兼容键。CLI质量与性能专项自行设置新旧模式，当前没有将这两个 F1 开关各自暴露为单独的命令行禁用参数。

### 最终1280×800质量与结构回归

[正式质量日志](../../build/probe_imprint_verified.log) 已完成 oval_close / contact_near / wall_corner 三视图及 previous / improved / contact_off 对照，使用1024-spp安全接收点参考；完整逐区域、逐阶段结果与参考底限制见 [指标JSON](../../build/probe_imprint_verified_metrics.json)。下表均为 **temporal 阶段8帧平均 HDR 图**的新减旧百分比；phase32 为去区域偏差后 x/y 条件相位残差能量之和，未将它解释为总噪声降幅。

| 视图 / 几何区域 | RGB MSE变化 | phase32残差能量变化 |
| --- | ---: | ---: |
| oval_close / 椭球外壳 >35 cm | −0.83% | −44.78% |
| contact_near / 椭球外壳 >35 cm | +0.328% | −41.97% |
| contact_near / 椭球过渡3–35 cm | +0.126% | +2.54% |
| contact_near / 方柱过渡3–35 cm | −2.88% | +15.73% |

椭球外壳 phase32 的减少高于对应参考噪声底，说明这两个视图中的32像素格相关残差减弱；RGB精度变化远小于相位指标变化。contact_near 的椭球过渡 RGB MSE 为 `1.03568e-5 → 1.03698e-5`，基本持平；过渡区的相位指标并非全面下降。方柱接触核心 RGB MSE 减少1.58%，接触核心绝对偏差、亮侧过冲及核心 / 外壳对比约束均通过。多数高通与梯度变化受参考底限制，墙角过渡变化也较弱，不能据其扣底比率宣称可靠的全面降噪幅度。JSON中“可识别”使用大于1.25倍估计参考底的启发式规则，并非统计置信区间。

严格分配的实际计数也已核对：新旧8帧的活动探针均值、最小值和最大值完全相同。oval_close 为 **4020=1000均匀+3020自适应**，contact_near 为 **3161=1000+2161**，wall_corner 为 **1341=1000+341**。探针射线数量因此在该固定场景 / 相位下保持一致；mask 选中覆盖仍有变化，三个视图的新旧全图选中比例分别为61.777%→61.838%、43.700%→45.470%、11.430%→12.401%，不能把整套方案称为总射线预算相同。

椭球视图未知几何计数为0；墙角新旧每帧平均4个未知几何样本保持原样。mask零、alpha与有限值约束、两项开关的历史失效检查均通过，没有把未知值变成有效光照。

同视角实际总图：[contact_near旧配置](../../build/probe_imprint_verified_contact_near_previous_total_renderer_ldr.png) · [contact_near新配置](../../build/probe_imprint_verified_contact_near_improved_total_renderer_ldr.png)；椭球近景：[oval_close旧配置](../../build/probe_imprint_verified_oval_close_previous_total_renderer_ldr.png) · [oval_close新配置](../../build/probe_imprint_verified_oval_close_improved_total_renderer_ldr.png)。总图采用 renderer 的显示变换，HDR质量数字仍来自上面的独立阶段和几何 ROI。

[独立几何回归](../../build/probe_imprint_ship_geometry.log) 的8种布局已通过，覆盖常量、有效黑、内部几何、跨面覆盖和微小运动。曲率激活 ramp 的独立 CPU 权重 oracle 使用新几何权重与实际像素插值，`impulse_contract_max_error=1.309e-6`，保留原 `3e-4` 门限；将部分激活错误当作满权的反事实响应差为0.161403，说明该检查能够区分激活权重错误。它验证重建契约，不能替代上述实际照明质量对照。

### 最终GPU成本与后端回归

RTX 4060 Laptop、Debug、Vulkan软件射线、固定 contact_near 视角，按上述四对批次、每配置64帧完成 [1280×800性能专项](../../build/probe_imprint_perf_1280.log) 和 [2560×1440性能专项](../../build/probe_imprint_perf_2560.log)。validation、呈现、图像回读和 CPU oracle 均关闭，初始准备与每批16帧预热不计时。下表为真实GPU时间戳，区间按四个配对批均值计算 Student t(3) 95%区间。

| 分辨率 / scope | 旧均值 ms | 新均值 ms | 新减旧 ms | 配对95%区间 ms |
| --- | ---: | ---: | ---: | --- |
| 1280×800 / frame | 253.307 | 257.823 | +4.516 | [−10.825, 19.857] |
| 1280×800 / trace | 129.856 | 135.175 | +5.319 | [−1.847, 12.485] |
| 1280×800 / filter | 6.145 | 5.705 | −0.440 | [−2.210, 1.329] |
| 2560×1440 / frame | 844.059 | 925.243 | **+81.185** | **[27.739, 134.631]** |
| 2560×1440 / trace | 464.270 | 508.567 | +44.297 | [12.028, 76.566] |
| 2560×1440 / filter | 20.883 | 24.506 | +3.623 | [1.428, 5.818] |

2560×1440高分辨率专项的整帧增加 **81.1847 ms（9.618%）**，区间没有跨零，支持本轮存在明确额外成本；默认没有新增 pass 并不等于没有成本。1280×800整帧点估计为+4.51595 ms（1.783%），三个scope的区间均跨零，本批未充分区分变化与批间波动，不能称为免费，也不能将 filter 的负点估计当成确定加速。

各scope的中位数及P10/P90如下，保留批内波动范围；这些分位数不是上述配对差值的置信区间。

| 分辨率 / scope | 旧中位数 [P10, P90] ms | 新中位数 [P10, P90] ms |
| --- | --- | --- |
| 1280×800 / frame | 254.547 [240.929, 263.987] | 257.814 [253.275, 262.902] |
| 1280×800 / trace | 129.761 [122.357, 136.418] | 135.206 [129.411, 139.080] |
| 1280×800 / filter | 6.167 [2.950, 7.866] | 6.025 [2.825, 7.428] |
| 2560×1440 / frame | 850.474 [770.665, 924.312] | 915.382 [871.356, 991.737] |
| 2560×1440 / trace | 458.799 [423.185, 530.553] | 511.107 [481.998, 524.590] |
| 2560×1440 / filter | 19.292 [15.478, 28.752] | 22.544 [17.712, 33.049] |

frame包含整帧，trace / filter是其中的组跨度，三者不是可相加的独占成本；两组差值也不足以精确解释整帧净增。补追覆盖扩大、最近命中 metadata 和受限曲面重建都增加工作，过滤组还包括原有 resolve。质量专项已核对固定视角的活动探针数一致，但性能日志的覆盖可变声明仍然适用。此结果限于当前 Debug 软件射线路径、硬件和视角，不能直接外推 Release、硬件光追或其他视图。

[Vulkan方向契约回归](../../build/probe_imprint_verified_directional.log) 和 [OpenGL方向契约回归](../../build/probe_imprint_directional_gl.log) 均完成320×200、64 rays/probe的检查：原scalar/RGB/alpha数学契约、自己的法线求值恒等式、最近几何 metadata、关闭曲面权重时 metadata 为零、方向重建开关往返、历史失效与关灯零能量均通过。Vulkan raw unknown为4，OpenGL为4883；强制未知路径各保留19584条不可读射线，有限值约束通过。该回归证明各自数据契约，不能把OpenGL结果称为与Vulkan画质等价。

[Vulkan灯框回归](../../build/probe_imprint_lamp_vk.log) 已通过480×300、3视图、13配置、256-spp共享Surface Cache参考，`improve_assertions=1`；六项参数历史失效、光强线性及关灯检查通过，后两项最大RGB误差为0。[OpenGL灯框结构回归](../../build/probe_imprint_lamp_gl.log) 已通过320×200、3视图、13配置，`reference_spp=0`、`improve_assertions=0`；四帧微小运动复用254391个pixel-frame，六项参数历史失效、光强线性误差0及关灯最大RGB为0。OpenGL直接光RGB8读取仍存在旧问题，该批仅作为结构兼容范围，不用于证明直接阴影画质或两后端视觉等价。

## 2026-10-19：墙顶静止宽条带与确定性面光源采样

**历史范围：** 本节记录上一轮仅改变面积灯源点分布的版本，尚未包含上方曲面权重与补追一致性组合；下方图像观察、改善范围和性能数值不能代替本轮最终验收。

墙顶交界仍能做得更干净、柔和。这次用户图中的一部分宽平行明暗台阶来自本地直接面光源的离散采样，并非 Lumen 无法改进的固有限制。把总光拆成直接光与间接光后，旧配置在关闭 GI 时仍保留宽台阶；再关闭直接面积阴影，台阶明显消退。直接光图在连续静止帧间几乎不变，而接触 GI 的各级图仍有独立的采样方差，因此不能把两者统称为接触 GI 白噪声。

旧版把矩形灯分为 8×8 个单元，固定采样每格中点。64 个点沿任一轴只有 8 种坐标，灯框边缘可能同时遮住整排或整列光源，使多个样本一起切换可见性。继续增加 GI 历史或平滑 GI RGB，不能移除直接照明中已经固定的阶梯。Surface Cache 卡片照明也使用这些源点，因而还可能把同类结构带入反弹信号。

本轮保持相同 64 点预算，改为**固定的二维分层采样**：每个 8×8 单元仍恰好一个点，横、纵投影各覆盖 64 个互不重复的细子层；4×4 模式相应覆盖 16 个子层。对于单元 `(x,y)`、网格边长 `g`，两个格内子层分别为 `(5y+2x+3)%g`、`(3x+4y+3)%g`，在各子层中心取点。4 和 8 两种模式均保持中心对称、完整发光面积与每点原有面积权重。这样避免整排同时遮挡，让几何定义的半影过渡具有更细的采样分布。

它不随帧改变种子，不抖动源点，也不增加空间 RGB 模糊；静止场景继续复用同一张深度图集。CPU 的 `AreaLightSampleParameters` 与 GLSL helper 使用同一公式，以下三路必须采用完全相同的源点：

- CPU 生成的每源点六面深度 atlas。
- 不透明 PBR 的矩形灯照明及其深度阴影查询。
- Surface Cache `SCDirect` 的真实三角形 BVH 可见性积分。

`AreaLightsSettings.stratifiedSamples` 默认关闭，普通材质保持原中点积分；封闭方形顶灯场景默认开启。现有 256B `AreaLightsConstants` 的 `halfAxisU.w` 用符号编码模式，绝对值仍为 4 或 8；原 `halfAxisV.w` 的 GGX 过滤强度保持原义。atlas key 显式包含模式，Surface Cache 照明 key 包含带符号的常量，TAA / GI 历史兼容键也包含模式；切换会替换同尺寸 atlas、重照明并清除不兼容历史，之后恢复缓存复用。未增加渲染 pass、纹理、UBO 或射线数量：默认 64 源点仍对应 384 个深度面，256 配置下仍为 **24 MiB atlas**，常量缓冲仍为 **256B 面积灯 + 48B 阴影常量**。预算不变不代表耗时严格相同，实际成本以配对 GPU 测量为准。

运行时关闭阴影使用独立的 `AreaLightShadowSettings.enabled`。`SurfaceCacheGiSettings.areaLightShadows` 保留为材质 / 管线 opt-in：普通 PBR 的天空反射占用纹理10、静态探针占用 UBO12，不能在普通路径无条件清空它们。已 opt-in 的场景即使临时关闭阴影，仍在不透明场景前上传 `options.x=0` 并清对应 atlas 绑定，避免前一帧 GI 共用 UBO12 后留下错误解释的常量；重新启用可复用原 atlas。F1 的 `Recessed ceiling fixture` 提供 `Stratified emitter samples` 和 `Direct area shadows enabled` 两个开关，F5 仍用于直接光 / GI 对照。

### UE 职责与本次适配范围

[Epic 的 Lumen 说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-global-illumination-and-reflections-in-unreal-engine) 将漫反射间接照明与其他阴影系统配合使用；提高 `Final Gather Quality` 可减少噪声，同时增加 GPU 成本。本机 UE 5.7.1 的 [LumenScreenProbeGather.cpp:218](D:/UE5/engine/UE_5.7/Engine/Source/Runtime/Renderer/Private/Lumen/LumenScreenProbeGather.cpp:218) 也明确描述累积帧数与噪声 / 光照响应的取舍。直接软阴影则有独立的 VSM SMRT 路径：[VirtualShadowMapSMRTCommon.ush:44](D:/UE5/engine/UE_5.7/Engine/Shaders/Private/VirtualShadowMaps/VirtualShadowMapSMRTCommon.ush:44) 生成时空样本，[VirtualShadowMapProjectionSpot.ush:366](D:/UE5/engine/UE_5.7/Engine/Shaders/Private/VirtualShadowMaps/VirtualShadowMapProjectionSpot.ush:366) 按帧索引使用它们。这里仅参考分开诊断直接阴影与间接 GI 的职责，没有移植 SMRT。

本次修正的是本地 64 源点共行 / 共列造成的结构性台阶，不能据此声称消除了全部 Lumen 随机噪声、所有遮挡方向的采样误差或几何轮廓锯齿。原接触补追、曲率过滤与成熟 GI 历史算法未扩大核或更改射线预算；接触阴影细节仍由真实几何可见性和原有 GI 检查保护。

### 验证与复现

480×300、三个固定视图的 Vulkan 分图诊断已通过：[旧中点日志](../../build/wall_transition_midpoint.log)、[新分层日志](../../build/wall_transition_stratified.log)。各配置预热24帧、观察8个静止帧；保存实际显示变换后的总光 / 直接光，以及独立的未过滤补追、过滤补追、时域 GI、probe raw GI 和风险 mask。GI HDR 预览使用另外标明的 tone4 显示，不与总光 LDR 的数值直接相减。此诊断检查有限值与分层输出，没有高采样画质改善断言；480 图用于定位和低分辨率回归，不作为最终高分辨率性能或误差结论。

同机位总图：[旧8×8中点](../../build/wall_transition_midpoint_corner_lamp_total_renderer_ldr.png) · [新固定分层](../../build/wall_transition_stratified_corner_lamp_total_renderer_ldr.png)。

[面积灯 GPU 单元日志](../../build/wall_transition_area_gpu.log) 已验证默认中点模式、正负4/8编码、实际GPU循环16/64点数、每格占用、两轴投影子层、中心对称与常量布局；两种模式对独立512×512密积分的最大相对误差为 **0.003790**，低于0.008门限，GL错误为0。它覆盖未遮挡积分与编码，不等同于遮挡 atlas 的端到端真值测试。

另有 [CPU 采样剖面数据](../../build/wall_shadow_sampling_reference.json) 和 [剖面图](../../build/wall_shadow_sampling_profiles.png)：使用场景实际盒体的精确 AABB 可见性及密集矩形光积分，隔离样本位置造成的阶梯。这是 sampling-only oracle，不读取 GPU 深度 atlas，也不是 GI 画质真值；其数值改善比例不能用作最终渲染的降噪百分比。

```text
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --procedural --surface-cache-wall-transition-test --surface-cache-wall-transition-legacy --render-width 1280 --render-height 800 --screenshot build\wall_transition_midpoint_high.png
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --procedural --surface-cache-wall-transition-test --render-width 1280 --render-height 800 --screenshot build\wall_transition_stratified_high.png
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --software-rays --procedural --surface-cache-wall-transition-performance --render-width 1280 --render-height 800
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --software-rays --procedural --surface-cache-wall-transition-performance --render-width 2560 --render-height 1440
```

性能专项仅比较采样模式，保留相同接触 GI 设置，分别测 `gi_on` 和 `direct_only`。每种模式按 AB / BA / AB / BA 四对顺序运行，每批预热16帧、计时16帧，每配置64个新鲜整帧 GPU 时间戳；禁用 validation、呈现和图像回读。报告均值、中位数、P10 / P90及四个配对批均值的95% t区间；没有增加过滤组，因此不能将旧接触过滤耗时变化归给这项采样修改。

#### 1280×800正式三视图

[新分层采样](../../build/wall_transition_final.log) 与 [旧中点配置](../../build/wall_transition_legacy_final.log) 均已完成 corner_close / corner_wide / corner_lamp 三个1280×800视图。各视图预热24帧、检查8个静止帧，保留总光、带阴影直接光、无面积阴影直接光及上述独立GI阶段。总光对照可见原先宽平行台阶明显减少，墙顶过渡更连续；这属于实际渲染图的观察，没有把空间高通能量当作相对真值误差或声称所有随机颗粒消失。

高分辨率同机位总图：[旧中点](../../build/wall_transition_legacy_final_corner_lamp_total_renderer_ldr.png) · [新分层](../../build/wall_transition_final_corner_lamp_total_renderer_ldr.png)；直接光对照：[旧阴影](../../build/wall_transition_legacy_final_corner_lamp_direct_area_shadows_ldr.png) · [新阴影](../../build/wall_transition_final_corner_lamp_direct_area_shadows_ldr.png)。

两批缓存断言全部通过：atlas字节数均为 **25,165,824（24 MiB）**；改变采样模式会重建同尺寸atlas并清除GI历史，下一帧稳定复用；运行时关闭 / 重新启用面积阴影也复用兼容atlas。诊断仍不包含独立高采样遮挡atlas真值，CPU采样oracle的改善比例不作为这些GPU图的误差下降比例。

#### 仅改变源点分布的GPU开销

RTX 4060 Laptop、Debug、Vulkan软件射线、corner_lamp视角，测量关闭validation、呈现及图像回读。下表为完整帧GPU时间；相同射线和atlas预算、相同接触GI设置，每配置64帧，按上文四对批次计算差值及95%区间：[1280×800日志](../../build/wall_transition_perf_1280.log)、[2560×1440日志](../../build/wall_transition_perf_2560.log)。静止复用atlas，切换模式后重建atlas与初次卡片重照明的成本位于预热阶段，不包含在稳态均值中。

| 分辨率 / 显示模式 | 旧中点均值 ms | 新分层均值 ms | 新减旧 ms | 配对95%区间 ms |
| --- | ---: | ---: | ---: | --- |
| 1280×800 / GI开启 | 161.170 | 162.237 | +1.067 | [-3.876, 6.010] |
| 1280×800 / 仅直接光 | 3.988 | 4.243 | +0.256 | [-0.056, 0.567] |
| 2560×1440 / GI开启 | 449.073 | 451.640 | +2.567 | [0.262, 4.873] |
| 2560×1440 / 仅直接光 | 13.642 | 14.189 | +0.547 | [-0.344, 1.438] |

2560×1440、GI开启的本批差值约 **+0.572%**，区间未跨零，支持存在小幅稳态额外成本。其余三项区间跨零，当前批次未充分区分变化与批间波动，不能据此称为“无成本”。这是源点分布修改的配对整帧开销，未增加过滤组，不能与前两轮独立接触过滤开销混算。GI开启的绝对耗时仍高，以上离屏GPU时间不代表全屏交互FPS，也不能直接外推其他视角、Release构建或硬件光追性能。

#### 当前回归范围

[Vulkan灯框回归](../../build/wall_transition_lamp_vk.log) 已通过480×300、3视图、13配置、256-spp共享Surface Cache参考，`improve_assertions=1`；六项参数历史重置、灯强线性和关灯零能量检查均通过，后两项最大RGB误差为0。此项防止源点修改破坏原有接触修正，仍不是路径追踪真值。

[OpenGL灯框结构回归](../../build/wall_transition_lamp_gl.log) 已通过320×200、3视图、13配置，`reference_spp=0`、`improve_assertions=0`；四帧小幅运动复用254,391个pixel-frame，六项参数历史重置、灯强线性和关灯检查通过。此批没有独立高采样参考，直接光RGB8读取也全为0，因此只报告结构兼容通过，不能据其宣称OpenGL直接阴影画质与Vulkan等价。上方独立面积灯GPU单元另在真实OpenGL积分中验证了两种采样模式与正负4/8编码。

## 2026-10-19：椭球 / 方柱残噪与独立 GPU 计时

**历史范围：** 本节与下一节保留前两轮接触 GI 修改的记录，使用当时的8×8中点面积灯配置；这些改善比例和绝对耗时不能直接作为上方分层源点版本的当前验收。

上一轮椭球 / 方柱的“最终空间高通残差下降 5.66%”是相对一张有限采样参考图的指标，并不表示最终图像只减少了同样比例的可见颗粒。旧 1024-spp 参考的半图差表明，6–35 cm 外带的参考噪声已经占据最终高通残差的大部分；旧逐像素参考还使用 `p+n*rayBias`，与生产补追的源三角形锚定 / 间隙限幅起点不同。继续比较这个百分比，容易把参考噪声或起点差异当成降噪瓶颈。下方保留上一轮原始记录；本轮以相同安全起点的独立多 seed 参考、参考噪声底和接触剖面共同评估。

新的默认路径保留独立 R2 接触序列和每个选中像素 8 条射线，增加以下处理：

- **补上探针遗漏的接收点接触风险。** 原探针短命中 / 视差风险之外，从每个可见接收点沿 8 个屏幕方向检查 6 环位置，半径为 1、2、4、8、16、32 像素。只考察世界距离小于 `Contact risk distance` 的有效几何，用法线变化与中点切平面残差识别不连续近面；两面同时处于对方背半空间的凸边候选跳过。距离项在风险距离 D 的 15% 以内保持满权，从 15% 至 100% 以 `1-smoothstep(0.15D,D,d)` 宽范围渐变至零；默认 D=10 cm 时，核心 1.5 cm 内距离项为 1，之后平滑衰减到 10 cm，避免探针与真实接收点修正结果在选区边缘突然切换。它仅决定额外发射真实接收点射线，不根据亮度选择或借用邻面光照。屏外和被完全遮住的邻面不能靠该屏幕补充检测发现，原探针风险和完整场景补追仍保留。
- **用同表面的软风险边缘填补小孔。** 增加一个 3×3 几何兼容 mask 扩展 pass，在已验证的同表面邻点中取 `max(center,0.5*axisNeighbors,0.25*diagonalNeighbors)`。它只提高周围弱风险，不降低接触核心；法线、反照率、中点切平面及世界距离检查阻止跨几何边界扩展。此步骤不读取或混合 RGB，每个新增接收点仍独立进行安全起点认证和真实射线追踪。原始 mask 暂存在既有 unfiltered 目标中，扩展后写入专用 mask，再由后续追踪覆盖临时内容，因此不增加纹理。
- **给曲面可用的过滤支持。** 三轮支持半径由 1 逐步增加为 2、4 像素，受 `Contact guided max radius` 限制。同一表面的弦位移以两端法线的中点切平面衡量，避免把椭球平滑曲率当成平面断裂；法线、反照率、世界距离、中心有效性和逐像素路径检查继续阻止跨边界取样。
- **扩大支持时保护近处接触。** 实际补追按完整射线预算累计几何命中距离倒数，包括黑色、背面和缓存未知命中，得到调和命中距离。新增的接收点命中距离倒数仅在 guided 模式累加，旧模式不承担这项统计开销。更远的过滤步长使用中心与邻点距离的较小值计算命中视差，默认上限 10°；远处光照不能因为距离长而压过近接触。每个方向遇到视差或几何拒绝时，只保留已经逐步验证的较近端点，至少保留合法的 1 像素支持，避免极近接触完全失去降噪。调和距离是本实验的标量适配，不是 UE 的逐方向命中图集。
- **先过滤实际测量，再混合一次。** 新增内部 measurement 图保存实际接收点 RGB，以及打包在有限浮点 alpha 中的 readable count / 调和距离。原始计数和距离在三轮中保持不变，最后一次才与探针基础信号按中心 mask 混合；公开的未过滤 / 过滤结果仍输出真实中心可读率。未知起点保持未知，已知黑色保留，完整射线归一化分母不变，mask 为零的像素严格保持基础信号。

相对上一轮三轮小核版本，增加一张全分辨率 RGBA32F measurement 图：1280×800 为 **15.625 MiB**，2560×1440 为 56.25 MiB；这不包含上一轮已经增加的 scratch 图。默认 contact 从旧版 6 个 pass 增为 **8 个**：几何风险、原始 mask、软风险 mask 扩展、接收点追踪、measurement resolve、三轮过滤。measurement resolve 纳入过滤计时，mask 扩展纳入 trace 计时。关闭过滤但保留新 mask 时执行 4 个 pass，全接收点参考模式执行 3 个 pass。每个选中像素的射线数仍为 8，但接收点风险检测及软扩展扩大选区，因此总射线数和分类开销可能增加，不能把“8 条不变”理解成整帧成本不变。

F1 的新增参数为 `Contact hit-distance / curved filter`、`Receiver screen contact detection`、`Contact guided max radius`（默认 4）和 `Contact filter hit angle (degrees)`（默认 10）。关闭前两个开关可比较上一轮三轮过滤；独立接触序列和连续覆盖率过滤仍保持开启。新参数全部进入 GI 历史兼容键；`referenceReceiver` 是质量专项使用的全接收点安全起点参考模式。

参考的本机 UE 5.7.1 实现如下，接收点风险掩码和标量曲率过滤是本实验的适配，不是逐行移植：

- [LumenScreenProbeGather.cpp:294](D:/UE5/engine/UE_5.7/Engine/Source/Runtime/Renderer/Private/Lumen/LumenScreenProbeGather.cpp:294)：明确将短程全分辨率 AO 用于补充降采样 Screen Probes 缺少的高频接触遮蔽。
- [LumenShortRangeAOHardwareRayTracing.usf:47](D:/UE5/engine/UE_5.7/Engine/Shaders/Private/Lumen/LumenShortRangeAOHardwareRayTracing.usf:47) 读取实际接收点材质坐标、深度和法线；[同文件:83](D:/UE5/engine/UE_5.7/Engine/Shaders/Private/Lumen/LumenShortRangeAOHardwareRayTracing.usf:83) 从接收点位置沿余弦半球发射局部可见性射线。这里借鉴接收点补充近处细节的职责，实际实现仍追踪完整 GI 距离并读取 Surface Cache 辐射率。
- [LumenScreenSpaceBentNormal.usf:637](D:/UE5/engine/UE_5.7/Engine/Shaders/Private/Lumen/LumenScreenSpaceBentNormal.usf:637) 是屏幕短程 AO 入口，[同文件:675](D:/UE5/engine/UE_5.7/Engine/Shaders/Private/Lumen/LumenScreenSpaceBentNormal.usf:675) 调用接收点 horizon 搜索，[同文件:693](D:/UE5/engine/UE_5.7/Engine/Shaders/Private/Lumen/LumenScreenSpaceBentNormal.usf:693) 写出可选 ShortRangeGI。
- [LumenScreenProbeFiltering.usf:358](D:/UE5/engine/UE_5.7/Engine/Shaders/Private/Lumen/LumenScreenProbeFiltering.usf:358) 将邻点命中距离限制为不超过中心命中距离，再计算视差角，防止远处光照覆盖接触阴影。[LumenScreenProbeGatherTemporal.usf:629](D:/UE5/engine/UE_5.7/Engine/Shaders/Private/Lumen/LumenScreenProbeGatherTemporal.usf:629) 分别混合短程 AO / GI 历史。本实验继续将补追后的完整 RGB 信号送入既有几何验证时域，没有移植完整 UE Short Range AO / GI 管线。

### 曲面接触质量专项（最终八 pass）

```text
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --procedural --surface-cache-curved-contact-test --surface-cache-curved-contact-view 0 --surface-cache-curved-contact-reference-seeds 128 --render-width 1280 --render-height 800 --screenshot build\curved_contact_final.png
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --procedural --surface-cache-curved-contact-test --surface-cache-curved-contact-reference-seeds 32 --render-width 640 --render-height 400 --screenshot build\curved_contact_views.png
```

`--surface-cache-curved-contact-view` 可指定 0（objects）、1（near）、2（detail），默认 3 覆盖三个固定视图。参考 seed 支持 32 / 64 / 128，对应每 seed 32 条射线、共 1024 / 2048 / 4096 spp；参考关闭空间与时域过滤，使用生产补追同一套源锚定 / 间隙限幅起点和完整距离追踪。它共享单次反弹 Surface Cache，仍不是路径追踪真值。参考完全不可用的接收点从质量 ROI 排除并单独统计；这不影响全屏未知中心、alpha 和 mask 外不变检查，部分未知射线仍保留完整分母。

每个视图分别在椭球和方柱侧按真实几何距离统计 0–1、1–3、3–6、6–15、15–35 cm 五档；椭球外部距离求解析椭球的最近点，避免按最小半径缩放球 SDF 把长轴处厘米档划错。对照为上一轮三轮过滤和新默认，两者均使用独立接触方向、连续覆盖率和相同探针相位。每种配置预热 12 帧、测量 12 帧，分别报告未过滤、当前帧过滤和最终时域结果的总 / 误亮 / 误暗 MSE、偏差、残差高通、时间方差，以及 selected / unselected 子集。两种配置选区可以不同；仅在 mask 相同的像素检查实际接收点射线结果一致，同时要求上游探针 GI 哈希完全一致。

参考平均图由两个独立的等规模 seed 集合组成。两半均值之差的平方误差乘 1/4，估计完整参考的噪声 MSE；对同一兼容邻域做高通后再乘 1/4，估计参考高通噪声底。日志同时保留 raw 和扣除该噪声底后的 corrected 指标；`corrected_*_identifiable` 仅在旧误差高于估计噪声底 1.25 倍时为 1。接近参考噪声底的 corrected 比率不稳定，标记不支持识别时不能据此宣称降噪百分比，也不能把减法后的数值当作确定的路径追踪误差。

`paired_half_reference` 另用两个独立半参考分别比较新旧结果：由完整均值加 / 减半个两半差值重建各半参考，在同一几何 ROI、同一高通算子下报告当前帧过滤和最终时域阶段的 MSE / HP MSE，以及 `mse_after_minus_before` / `hp_after_minus_before` 配对差值。每个配对使用相同参考，其共同噪声底在期望差值中抵消；这组日志用于检查回退方向及其对半参考的敏感性。不能仅因参考噪声底高就推断配对差不显著，应同时查看两个独立半参考下差值的方向和幅度。

细节检查比较两侧 0–3 cm 接触核心亮度和 15–35 cm 外带的对比度。核心校验以同安全起点的共享缓存参考为准：限制新增过亮误差 `max(0,new-reference)-max(0,old-reference)`，同时限制绝对核心误差增长 `abs(new-reference)-abs(old-reference)`，避免把原先过暗的结果向参考纠正误判为接触细节丢失，也阻止继续压暗核心来通过检查。两项容差仍为 `0.03*max(referenceNear,abs(referenceContrast))`，原对比度 guard 及聚合 MSE / HP 的 1.02 上限保持不变；日志额外记录新旧绝对核心误差与过亮误差增量。专项另外检查每像素中心 alpha、未知中心、mask 外严格不变、参数变更清除历史、灯强线性、关灯零能量及小幅运动历史复用；运动检查只验证有限值和历史可用性，没有量化运动中的真值误差或拖影。

#### 1280×800、objects、4096 spp

包含软风险 mask 扩展的最终八 pass 配置已通过该专项，见 [最终高采样质量日志](../../build/curved_contact_padded_final.log)。比较上一轮配置与最终配置，在同一几何定义的 0–35 cm 接收区域测量，预热 12 帧后评估 12 个静止帧。下表报告未扣除参考噪声底的 raw 指标：

同视角总图：[上一轮配置](../../build/curved_contact_padded_final_objects_previous_total.png) · [最终八 pass 配置](../../build/curved_contact_padded_final_objects_improved_total.png)。

| 阶段 | 亮度 MSE 下降 | 残差 HP MSE 下降 | 静止帧间方差下降 |
| --- | ---: | ---: | ---: |
| 当前帧过滤 | 33.47% | 48.11% | 23.45% |
| 最终时域 | 36.65% | 2.19% | 26.93% |

最终静态高频残差是温和改善，不能将当前帧过滤的 48.11% 当作最终图像的降噪幅度。最终 `corrected_hp_ratio=0.799621`，但估计参考 HP 噪声底占旧误差的 **89.0865%**，`corrected_hp_identifiable=0`，不能据此宣称约 20% 的最终静态降噪。两个独立 2048-spp 半参考的最终配对 HP 差值都为负，分别为 `-8.91799e-9` 与 `-1.17831e-8`，支持温和改善的方向。

接触核心与外带的定义同上，数值为线性亮度。椭球核心向参考靠近，接触剖面对比也更接近参考：

| 接收侧 | 核心亮度：旧 → 新（参考） | 核心绝对误差：旧 → 新 | 外带减核心对比：旧 → 新（参考） |
| --- | --- | --- | --- |
| 椭球 | 0.0486003 → 0.0511120（0.0518745） | 0.00327424 → 0.000762518 | 0.0447392 → 0.0422036（0.0408870） |
| 方柱 | 0.0379425 → 0.0379160（0.0378963） | 0.0000462624 → 0.0000197526 | 0.0137564 → 0.0137691（0.0139664） |

两侧核心误差及对比度 guard、中心 alpha、22,079,285 个 mask-zero pixel-frame 不变、144 个未知中心 pixel-frame 保持未知、小幅运动历史复用和五项参数历史重置均通过。安全参考完全不可用的 3 个方柱接收点已从质量 ROI 排除，未计作黑色真值；灯强加倍最大 RGB 误差和关灯最大 RGB 均为零。

#### 640×400、三视图、1024 spp

相同最终八 pass 配置也已通过三个视图专项，见 [最终三视图质量日志](../../build/curved_contact_padded_views.log)。下表比较上一轮配置与最终配置，统计同一几何定义的 0–35 cm 接收区域，使用未扣除参考噪声底的 raw 指标；HP 为残差高通平方误差，时域方差来自预热后的 12 个静止帧：

| 视图 | 当前过滤 HP MSE 下降 | 当前过滤时域方差下降 | 最终 MSE 下降 | 最终 HP MSE 下降 | 最终时域方差下降 |
| --- | ---: | ---: | ---: | ---: | ---: |
| objects | 28.54% | 28.11% | 11.87% | 0.41% | 30.90% |
| near | 42.17% | 30.79% | 0.53% | 1.52% | 34.76% |
| detail | 30.03% | 18.18% | 1.19% | 1.62% | 23.38% |

当前帧过滤的空间高通误差下降约 **29%–42%**，最终静止帧间方差下降约 **23%–35%**；最终静态 raw HP MSE 只下降 **0.41%–1.62%**，不能把当前帧过滤的改善幅度作为最终空间残噪的改善幅度。三个视图的两个独立 512-spp 半参考都给出最终 HP 下降方向，例如 objects 的配对 HP 差值为 `-1.30462e-8` 与 `-1.73670e-9`。最终 HP 误差中估计参考噪声底占比为 87.4%–97.8%，三个视图均标记 `corrected_hp_identifiable=0`；扣除噪声底后的百分比不稳定，应与上方高采样单视图记录分别解读，本表只报告上述 raw 结果和配对变化方向。

接触核心为 0–3 cm，剖面对比度为 15–35 cm 外带减核心，数值均为线性亮度。这里只列实际生成完整剖面日志的项目，near 椭球侧及 detail 视图未产生这项完整剖面统计：

| 视图 / 接收侧 | 核心亮度：旧 → 新（参考） | 外带减核心对比：旧 → 新（参考） |
| --- | --- | --- |
| objects / 椭球 | 0.063629 → 0.050808（0.052345） | 0.029980 → 0.042756（0.040481） |
| objects / 方柱 | 0.038556 → 0.038197（0.037880） | 0.012247 → 0.012562（0.013984） |
| near / 方柱 | 0.039791 → 0.039738（0.039722） | 0.014623 → 0.014673（0.014592） |

上述已有统计的核心绝对误差均下降，objects 椭球的核心绝对误差从 0.0112847 降至 0.00153652，暗接触剖面对比得到保留。全专项的中心 alpha、mask 外不变、60 个未知中心 pixel-frame 保持未知、小幅运动历史复用及五项参数历史重置检查通过；灯强加倍最大 RGB 误差与关灯最大 RGB 均为零。本次参考 ROI 未排除未知接收点，更新后的参考核心误差 guard 通过。

1280×800 的高采样 objects 结果与 640×400 的三视图结果互为补充，后者额外覆盖 near / detail；两组记录都对应最终八 pass 配置。独立性能结果见下节，上一轮质量记录完整保留在历史章节。

#### CPU 与跨后端回归

- CPU 的 `surface_cache_test` 与 `rhi_frame_command_buffer_test` 均通过，分别见 [Surface Cache 设置 / 有效性验证](../../build/curved_contact_cpu.log) 和 [RHI 命令缓冲测试日志](../../build/curved_contact_frame_cpu.log)；后者成功退出时不输出文本，退出码为 0。
- [Vulkan 灯框专项](../../build/curved_contact_padded_lamp_vk.log) 通过：480×300、三个视图、13 个配置、256-spp 参考，`improve_assertions=1`；六项参数历史重置、灯强线性和灯灭检查均通过，最大 RGB 误差 / 灯灭值均为 0。
- [OpenGL 灯框兼容专项](../../build/curved_contact_padded_lamp_gl.log) 通过：320×200、三个视图、13 个配置，`reference_spp=0`、`improve_assertions=0`，仅作结构与兼容验证，不据此宣称 OpenGL 质量改善百分比；六项参数历史重置、线性误差 0、灯灭值 0 均通过。
- OpenGL GLSL 将两处 helper 的保留字形参 `packed` 重命名为 `encoded`，修复编译兼容，算法与 UBO 布局未变。[重命名后 Vulkan 曲面烟测](../../build/curved_contact_glfix_vk_smoke.log) 亦通过：320×200、objects、1024 spp，五项参数历史重置、线性误差 0、灯灭值 0；已验收的高采样质量及独立性能算法保持一致。

### 曲面接触性能专项（最终八 pass）

```text
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --software-rays --procedural --surface-cache-curved-contact-performance-test --surface-cache-curved-contact-view 0 --render-width 1280 --render-height 800
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --software-rays --procedural --surface-cache-curved-contact-performance-test --surface-cache-curved-contact-view 0 --render-width 2560 --render-height 1440
```

性能专项禁止 `--validation`，关闭呈现、AA、UI 和图像 / 诊断计数回读。三个配置为新掩码但关闭过滤（`filter_off`）、上一轮三轮过滤（`previous`）、新默认（`improved`）；关闭过滤时直接采用未过滤结果，没有仅为对照保留的复制 / resolve pass，过滤 scope 为空。三个配置驻留同进程，六轮分别采用不同顺序，每个批次预热 8 帧、计时 16 帧，每种配置共 96 个 GPU 样本。性能专项一次测一个视图，完整视图比较应分别指定 0 / 1 / 2。

同时记录三种 GPU 时间：整帧；contact trace 组（探针几何风险、接收点 mask 分类、同表面软风险扩展及实际接收点追踪）；contact filter 组（新增 measurement resolve 和三轮过滤）。两组独立时间戳都在 `EndRendering()` 之后写入，起止均采用 `BOTTOM_OF_PIPE`，包含对应最终 attachment STORE，并避免把尚未完成的生产者工作归给下一组；没有新增提交或计时用 draw。分组 query 只由 `MeasureFrames` 显式启用，普通交互帧不插入这些标记。Vulkan 使用当前队列的 `timestampValidBits` 处理差值回绕，乘设备 `timestampPeriod` 转为毫秒；每帧重置可用性，仅读取已写入的 query 对，并校验返回的 frameIndex，不能复用旧帧结果。

日志报告每组样本的均值、中位数、P10 / P90，以及同轮新旧批次差值和 4096 次重采样的 bootstrap 95% 区间。filter 组差值反映过滤组耗时（含 resolve）的变化，trace 组反映分类及追踪覆盖组的变化；它们是运行管线中的分组时间跨度，并非独占执行时间，不能直接称为端到端整帧净成本。完整方案的变化应看整帧配对差值。相同每像素射线预算不代表相同选区或总追踪成本。整帧变化若落在批间波动范围内，应保留不确定性，不能据此宣称新增过滤免费。

#### 1280×800、objects

最终八 pass 配置的性能专项已通过，见 [最终 1280×800 性能日志](../../build/curved_contact_padded_perf_1280.log)。设备为 RTX 4060 Laptop GPU，Vulkan 软件射线、Debug、objects 视角；validation、呈现、UI 和图像回读关闭。每种配置记录 96 个当前完成帧的有效时间戳，六轮批次配对。下表单位均为 ms，括号为逐帧 P10–P90：

| 配置 | 整帧均值（P10–P90） | trace 组均值（P10–P90） | 过滤组耗时（含 resolve）：均值（P10–P90） |
| --- | ---: | ---: | ---: |
| 新掩码，过滤关闭 | 231.424（210.242–246.353） | 82.120（74.610–88.860） | 0.002784（0.001024–0.004608） |
| 上一轮三轮过滤 | 218.051（199.216–232.474） | 62.433（57.476–67.711） | 6.103（3.150–10.566） |
| 最终八 pass | 232.372（209.813–249.621） | 81.255（75.330–87.665） | 3.148（1.935–4.466） |

同轮配对差值均为“最终八 pass 配置减对照”，单位为 ms；区间来自六组批次配对的 bootstrap 95% 区间：

| 对照 | 范围 | 配对均值差 | 95% 区间 |
| --- | --- | ---: | ---: |
| 新掩码，过滤关闭 | 整帧 | +0.948218 | [-0.967334, 2.89268] |
| 新掩码，过滤关闭 | trace 组 | -0.865973 | [-1.63289, -0.207936] |
| 新掩码，过滤关闭 | 过滤组（含 resolve） | +3.14571 | [2.73043, 3.70549] |
| 上一轮三轮过滤 | 整帧 | +14.3209 | [9.0954, 18.9052] |
| 上一轮三轮过滤 | trace 组 | +18.8218 | [17.4607, 20.1999] |
| 上一轮三轮过滤 | 过滤组（含 resolve） | -2.95402 | [-3.54942, -2.28514] |

最终过滤组（含 resolve）平均 **3.14849 ms**，相对完全关闭过滤的同组区间增加 **3.14571 ms**；关闭配置没有过滤 draw，约 0.0028 ms 是这次空时间戳范围的基线。这一分组差值不能称为实际整帧净开销：新掩码下开启过滤的整帧差为 **+0.948218 ms**，95% 区间 **[-0.967334, 2.89268]** 跨零，当前批次不足以可靠量出该端到端差值，也不能据此宣称过滤免费。

相对上一轮三轮过滤，最终过滤组少约 **2.954 ms**，但包含软风险分类和扩大真实接收点追踪的完整方案，整帧增加约 **14.321 ms**，95% 区间 **[9.095, 18.905]**；trace 组增加约 **18.822 ms**。整帧还包含其他阶段，分组跨度不是独占执行时间，不要求等于两个局部分组差值之和。

#### 2560×1440、objects

最终八 pass 配置的高分辨率性能专项也已通过，见 [最终 2560×1440 性能日志](../../build/curved_contact_padded_perf_2560.log)。设备、Debug / Vulkan 软件射线、关闭 validation / 呈现 / 回读及六组批次配对方法与 1280×800 相同，每种配置均有 96 个当前完成帧的有效样本。下表单位为 ms，括号为逐帧 P10–P90：

| 配置 | 整帧均值（P10–P90） | trace 组均值（P10–P90） | 过滤组耗时（含 resolve）：均值（P10–P90） |
| --- | ---: | ---: | ---: |
| 新掩码，过滤关闭 | 760.910（708.892–818.957） | 350.826（333.577–364.558） | 0.00170667（0–0.004096） |
| 上一轮三轮过滤 | 565.943（515.249–619.745） | 152.043（139.226–164.778） | 6.153（3.400–9.524） |
| 最终八 pass | 785.184（719.111–842.705） | 353.961（337.898–372.638） | 13.184（9.137–17.356） |

同轮配对差值仍为“最终八 pass 配置减对照”，单位为 ms：

| 对照 | 范围 | 配对均值差 | bootstrap 95% 区间 |
| --- | --- | ---: | ---: |
| 新掩码，过滤关闭 | 整帧 | +24.2738 | [16.0499, 33.7727] |
| 新掩码，过滤关闭 | trace 组 | +3.13553 | [0.0241173, 6.66724] |
| 新掩码，过滤关闭 | 过滤组（含 resolve） | +13.1823 | [11.0089, 15.3065] |
| 上一轮三轮过滤 | 整帧 | +219.241 | [209.006, 230.878] |
| 上一轮三轮过滤 | trace 组 | +201.919 | [197.708, 205.327] |
| 上一轮三轮过滤 | 过滤组（含 resolve） | +7.03082 | [5.28826, 8.77338] |

2560×1440 下最终过滤组（含 resolve）平均 **13.184 ms**，相对关闭过滤的同组跨度增加 **13.1823 ms**。相同新 mask 下开启过滤的实际整帧差为 **+24.2738 ms**，95% 区间 **[16.0499, 33.7727]** 全部为正；1280×800 的整帧差不确定结论不能概括到这个分辨率。

完整方案相对上一轮由 **565.943 ms** 增至 **785.184 ms**，增加 **219.241 ms（约 38.74%）**，95% 区间 **[209.006, 230.878]**。分组跨度中，trace 组增加 **201.919 ms**，明显大于过滤组增加的 **7.031 ms**，新增成本主要表现在接触检测与实际接收点补追组。分组耗时仍非独占执行时间，也不能称过滤免费。

上述2560×1440数据是显式指定尺寸、固定 objects 视角的高分辨率离屏专项，并非本场景默认启动配置或交互呈现帧率。本场景应使用 `RunSealedGiRoom.cmd`，默认1280×800；`RunGiRoom.cmd` 的2560×1440配置属于另一条旧 `--gi-room` / lumenGi路径。两种分辨率必须分别使用实测结果，不能按像素数线性外推。

## 历史：2026-10-19 接触补追区域的白噪声条带

墙角、天花板交界及椭球与方柱接触附近的颗粒条带，来自实际接收点补追的低采样信号。探针的三轮方向辐射率过滤发生在补追之前，不能过滤随后写入的 8 条/像素补追结果。旧补追仅经过一次小核过滤，还直接复用探针的 8 帧方向相位；静止后反复采到相同方向，形成固定的逐像素误差。`maxFrames=10` 是有限历史 EMA，继续等待不等于无限增加独立样本。

当前保留默认 8 条补追射线，改进如下：

- 补追使用独立的逐像素扰动及固定点 R2 二维时域位移，保持每帧径向分层。仅成功提交的 GI 帧推进序列；参数变更、camera cut 或历史失效从相位零重启。探针本身的方向相位与原始 GI 不变。
- 补追后默认执行三轮小核过滤，再进入原有时域。每一轮仍检查法线、反照率、双向切平面、世界距离和逐像素路径，不跨几何边界取样。只增加一张全分辨率 RGBA32F 暂存图，1280×800 约 15.63 MiB，2560×1440 约 56.25 MiB。默认 contact 由 4 个 pass 变为 6 个，射线预算不增加。
- 用连续的可读率置信权重取代相邻 alpha 必须在 0.001 内相等的条件。8 条射线的一条未知样本会让 alpha 相差 0.125，补追 mask 与探针结果混合也会连续改变 alpha；这不应被当作几何边界。未知中心与未知邻居仍不参与恢复，每轮原样保留中心 alpha，不改变射线归一化分母或丢弃已知黑色。

参考本机 UE 5.7.1 的 `LumenScreenProbeFiltering.cpp:18–45` 的三轮小核过滤，以及 `LumenScreenProbeFiltering.usf:355–368,446–462` 对接触遮挡和低时域支持的处理。这里对完整 RGB 补追积分做过滤，属于本实验的适配，未移植 UE 的逐方向 hit-distance 过滤或完整 Short Range GI；不能把这一局部修正称作完整 UE 降噪器。

新增 F1 参数为 `Contact current filter passes`（1–3，默认 3）、`Independent contact ray sequence` 和 `Contact coverage-weighted filter`（均默认开），三个参数均进入 GI 历史兼容键。将三者分别设为 1 / 关 / 关可恢复旧补追过滤和方向序列，便于同进程比较。

专项入口：

```text
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --procedural --surface-cache-contact-noise-test --render-width 1280 --render-height 800 --screenshot build\contact_noise_final.png
```

专项覆盖与反馈截图相同的绿/黄墙和天花板交界、椭球与方柱接触。区域由可见世界几何定义，包含距交界 0–6 cm、6–35 cm 和内部 40–95 cm；对照旧版、仅独立序列、当前默认。参考为每像素 32 seed × 32 射线（1024 spp），共享 Surface Cache，仍有采样误差与起点策略差异，不是路径追踪真值。每种配置预热 12 帧，评估其后 16 帧；分别记录总误差、误亮、误暗、去除参考真实变化后的空间高通残差及帧间方差。固定重复纹理可以有很低的帧间方差，因此不能仅凭这个指标判断画质。

专项同时核对上下游采样隔离、旧 8 相位确实重复而新序列不重复、中心 alpha、mask 外严格不变、小幅相机运动历史复用，并用无回读/无呈现的 ABBA 批次比较 GPU 耗时。灯框 fixture 另验证灯灭零能量、光强线性和新增参数清除历史。两个专项各有独立职责，接触 fixture 的有效区域不自动证明所有未知状态均被实际触发。

最终 1280×800、RTX 4060 Laptop、Debug、Vulkan validation、AA=None 结果见 `build/contact_noise_final.log`。下表比较旧配置与当前默认，范围均为距几何交界 0–35 cm 的同一组像素；百分比为相对 1024-spp 共享缓存参考的误差下降，不能等同于路径追踪误差或主观噪声幅度的同幅下降。

| 区域 | 当前补追过滤 MSE 下降 | 最终时域 MSE 下降 | 最终空间高通残差下降 |
| --- | ---: | ---: | ---: |
| 墙角 / 天花板 | 49.51% | 32.18% | 27.72% |
| 椭球 / 方柱接触 | 8.88% | 1.66% | 5.66% |

墙角最近 0–6 cm 的最终误亮平方误差下降 49.25%，曲面最近区域下降 0.22%；曲面该区域平均亮度偏差由 +0.001582 变为 +0.001644，仍有系统误差，不能宣称完全恢复真值。曲面 0–6 cm 与 6–35 cm 的帧间方差分别下降 31.13% 和 21.84%；墙角内部 40–95 cm 控制区逐项不变。两个视图各 16 个成熟帧的 32 / 48 个未知中心 pixel-frame 保持未知，所有像素中心 alpha 与 mask 外信号检查通过。小幅运动只验证历史复用和有限值，未量化运动中的参考误差或拖影。修正仍有残余噪声，较小分辨率的最近曲面区域也可能出现轻微参考误差波动，不能据此宣称所有视角都改善相同比例。

ABBA 整帧 GPU 毫秒为旧 157.723、新 165.339、新 161.261、旧 173.042；均值旧 165.383、新 163.300。批间波动明显大于均值差，不能据此宣称提速或没有性能成本，也无法可靠量出新增两轮过滤的净耗时。这是该墙角视图的整条管线计时，不是过滤 pass 的单独时间，不代表完整实时 GI 达到 60 FPS。

CPU 设置检查与 Vulkan 灯框回归分别见 `build/contact_noise_cpu.log`、`build/contact_noise_lamp.log`，灯框三视角继续优于纯探针重建，光强加倍最大 RGB 误差及灯灭最大 RGB 都为零，6 项参数历史重置检查通过。OpenGL 的高负载离屏预热会只等待 CPU 提交，连续排队 12 个 32 射线帧后再回读，曾触发 10 秒回读超时；兼容 fixture 改为默认 320×200，并在每个 GL 帧后实际回读确认 GPU 完成。最终 `build/contact_noise_opengl_final.log` 的三视角、13 配置结构回归通过，包含 32 射线、光强线性、灯灭和历史重置。Vulkan 质量 fixture、生产帧提交及上述 1280×800 数据不受这一测试调度调整影响。OpenGL 仅作结构检查，不据此宣称与 Vulkan 具有相同参考精度或性能。

## 2026-10-19：灯框接触阴影被探针重建扩散

仰视灯框的黑色暗瓣主要是空间表示误差，而非缺失探针或随机方向不足。实测一个位于灯框外约 0.45 mm 的天花板探针，64 条射线中 31 条在 10 cm 内命中黑框，入射积分约 0.07995；这些是可读取、起点有效的真实黑色遮挡。错误发生在把这一窄接触阴影借给相隔数厘米的同平面像素。法线与切平面深度权重无法区分这种局部半球可见性变化。继续增加该探针的射线，只会更稳定地保持原来的暗值。

另外，天花板底面与灯框底面法线相同、仅相差约 3 cm，按视深归一化的几何容差会允许跨层共享。粗网格原来的扩展双线性公式也会在切换单元时留下小幅权重跳变，但单独修正它对这次暗瓣收益有限。

当前处理如下：

- 基础探针按真实像素结点插值，包括被视口裁剪的末行/列；自适应探针继续使用实际位置 B2 核。`Continuous probe interpolation` 可关闭作旧公式对照。
- 根据真实命中距离、像素到探针的切向距离及原重建权重，识别无法可靠共享接触照明的区域。短命中倒数按全部射线预算平均，不根据 RGB 或“哪个探针更亮”选择样本。
- 平行层识别使用像素尺度容差，并给曲面法线变化留出几何余量；发现混层风险时请求实际接收点追踪，而非删除较暗探针。
- 在风险区域的真实 GBuffer 表面位置重新进行完整距离的余弦半球 GI 追踪。起点仍经过源三角形锚定和法线方向间隙限幅；合法黑色与未知射线均保留完整分母。
- 补追后增加小半径保边过滤，再进入原有时域累积。检查法线、反照率、双向切平面、世界距离及采样路径，不跨过不兼容几何。未选择的像素保持原信号，未知中心不借邻居补亮，可读率保留中心值。

这是本实验对有限探针分辨率的局部修正，不是 UE 普通 ScreenProbeGather 的逐行移植。UE 5.7.1 的 `LumenScreenProbeGather.usf:1153–1178` 对重建查询使用蓝噪声抖动及切平面验证；`LumenScreenProbeFiltering.usf:356–368` 在静止区域仍保留近命中角限制。查询抖动不能撤销已经扩散的窄阴影；再乘 Short Range AO 也不能恢复被错误压暗的像素，因此这里没有靠删除近黑色命中或叠加 AO 补偿。

F1 的 **GI contact reconstruction** 可调：

| 参数 | 默认 | 作用 |
| --- | --- | --- |
| Actual receiver contact rays | 开 | 关闭可恢复纯探针重建，省去当前默认八个 contact pass。 |
| Contact rays per pixel | 8，范围 8–32 | 每个选中像素的真实半球射线数；增加可降低细颗粒方差，但开销较高。 |
| Contact risk distance | 0.1 世界单位 | 识别短程几何风险的距离；**不截断补追射线**，补追仍使用总 GI 的 Ray distance。增大可能扩大追踪区域。 |
| Contact parallax angle | 10° | 位置偏移相对命中距离的容限；减小会更保守、追踪更多像素。 |
| Contact risky weight threshold | 0.05 | 原重建中危险贡献的占比阈值；减小扩大补追区域。 |
| Contact parallel layer validation | 开 | 对附近平行层的歧义增加实际接收点追踪。 |
| Contact current filter radius | 1，范围 1–2 | 首轮 / 非引导路径的支持半径；引导路径逐轮扩大至 guided max radius，每个方向逐像素检查，仍是中心与 8 个端点，不是完整方形核。 |
| Contact current filter strength | 1，范围 0–1 | 0 为严格保留未过滤补追信号；增大减少细颗粒，但可能软化像素尺度接触变化。 |
| Contact current filter passes | 3，范围 1–3 | 小核迭代次数；默认三轮，每轮都重新验证几何路径。 |
| Independent contact ray sequence | 开 | 使用独立于探针 8 相位循环的接触射线序列。 |
| Contact coverage-weighted filter | 开 | 用连续可读率置信权重支持同一表面邻居，中心可读率不变。 |
| Contact hit-distance / curved filter | 开 | 先滤实际接收点测量，用中点曲率兼容与近命中视差约束扩展支持，最后混合一次。 |
| Receiver screen contact detection | 开 | 用接收点周围的近处几何不连续补充探针遗漏的接触风险。 |
| Contact guided max radius | 4，范围 1–4 | 引导路径三轮支持半径的上限，默认 1 / 2 / 4 像素。 |
| Contact filter hit angle (degrees) | 10°，范围 1–45° | 超过 1 像素的额外支持使用该近命中视差上限；更近的合法支持保留。 |

所有参数进入 GI 历史兼容键。原 `ReadbackSurfaceCacheRawGi`、`ReadbackSurfaceCacheSpatialGi` 保持纯探针阶段；新增 ContactProbeRisk、ContactMask、ContactUnfilteredGi、ContactGi 回读，最后一项为实际进入时域的修正信号。安全起点认证失败仍保留 unknown，不宣称所有表面均无覆盖缺口。

原 `surfaceCacheRays` / `surfaceCacheProbeRayBudget` 仍描述探针预算，不含新增的接收点补追。灯框测试另以有效 mask 像素数 × 补追射线数报告 `max_extra_rays`；起点认证失败会减少实际发射数量，不能把该上限当作精确活动射线计数。

验证入口：

```text
output\sdf-banquet\Debug\bin\material_lab_demo.exe --surface-cache-lamp-test --vulkan --validation --software-rays --procedural --render-width 1280 --render-height 800 --screenshot build\lamp_check.png
```

`build/lamp_filtered.log` 为 RTX 4060 Laptop、Debug、Vulkan validation、1280×800、最终 AA=None 的三视角对照。该批的 `baseline` 是 16 射线/0.3 距离高质量配置，`economy8_10cm` 是验收后选用的当前默认 8 射线/0.1 距离。参考为每像素 8 seed × 32 射线，共 256 spp；共享 Surface Cache，仍有蒙特卡洛误差，不是路径追踪真值。

| 灯框外 0–35 cm 天花板区域，线性 GI 亮度 MSE | 纯探针 | 当前默认配置 |
| --- | ---: | ---: |
| 视角 0 | 0.000361307 | 0.000205190 |
| 视角 1 | 0.000330591 | 0.000200628 |
| 视角 2 | 0.000339385 | 0.000199533 |

三视角总误差减少约 39%–43%，暗误差减少约 18%–28%。视角 0 中，距灯框 3–8 cm 的平均亮度偏差从 -0.00839 变为 -0.000177；距灯框 0–1 cm 的误亮从 +0.07744 减至 +0.01021，真实窄接触阴影得到保留。受测天花板区域未新增起点认证缺口；全图仍有 5–9 个选中像素认证失败，位于其他几何边界。常量诊断绕过真实起点有效性，只验证归一化；另行记录真实可读率，不能将两者混为无缺口证明。

该批旧默认配置的补追范围在三个视角分别约占全图 14.8%、12.7%、11.7%。同进程 ABBA、每批 4 个无呈现/无回读计时帧中，关闭 contact 约 101 ms GPU，旧 8 射线 / 单轮过滤配置约 173 ms，高质量配置约 244 ms。这是加入三轮过滤之前的首次接触修正记录，不能替代上面的当前默认验收。画质修正仍有明显开销，尚未达到完整实时 GI 的性能目标；这些是整条管线开关差值，不能称作没有性能代价。8 射线节省了开销，但细颗粒噪声没有完全消失，过滤也不能保证亚像素接触阴影完全无损。

旧探针、空间、方向、传输与时域单项 fixture 显式关闭 contact，以保留各自独立验证职责。灯框 fixture 单独检查原始探针信号不受开关影响、未选像素严格复制、中心可读率保持、光强线性、灯灭零能量、参数变更清除历史及小幅镜头运动的历史复用；运动检查不等于已量化运动中的参考误差或拖影。

## 2026-10-18：远处细边缘与运动重建噪声

固定像素间距的探针，在远处对应更大的世界空间间距；角落射线又容易很快命中附近几何。原过滤器只从相邻大网格选代表，在默认 10° 命中角限制下，许多候选不能参与。同一网格内虽有自适应探针，却没有进入此邻居表，所以近看干净、远看仍有细碎斑点。当前保留原 8 条远邻槽，再加入最多 8 条局部代表，按实际投影间距连续加权；不放宽命中角，不跳过 BVH，不按光照明暗选择探针。

物体移动原本会通过模型矩阵与缓存世代让整幅历史失效，墙面也从一帧重新累积。现在保存成功提交帧的实例值快照，只有场景、模型源、对象次序、材质、灯光与绘制结构都相同，且变换变化全部属于可移动物体时才保留兼容历史。移动接收面仍进行几何重投影验证，不兼容时拒绝历史；没有物体速度缓冲，细小或切向运动可能通过容差。物体还会影响远端墙面的间接阴影，因此没有只清除物体包围盒；当前对全图采用动态帧数上限与裁剪，并在停止后短暂继续响应。这比完整的逐射线光照依赖追踪粗糙，仍需检查拖影。

新暴露、重投影失败或移动边缘的历史很短，单靠裁剪旧历史不能消除当前帧的低采样噪声。新增低历史恢复在时域 Pass 内对当前 GI 做小范围双边过滤，检查法线、双向切平面距离、世界距离、反照率和可读性，沿采样路径逐像素拒绝跨薄遮挡借光。达到默认 4 帧后停止，不能把空间样本当成更多历史帧。它不会消除真正无历史区域所需的新采样，只能减轻显露过程中的噪声。

参考 UE 5.7.1 的区别：普通 ScreenProbeGather 在 `LumenScreenProbeGatherTemporal.usf:585–604` 根据运动光照比例缩短历史，并传播上一帧快速更新状态；`LumenScreenProbeFiltering.usf:351–375,448–463` 在时域减弱时增强空间过滤。当前没有 UE 的逐探针运动光照比例、对象速度与依赖缓冲，使用严格变换认证加保守全局响应。低历史当前帧双边恢复参考的是 `LumenReSTIRGather.usf:1577–1623` 的消遮挡/方差过滤及 `LumenReflectionDenoiserSpatial.usf:73–79` 的短历史空间恢复思想，属于本地适配，不是普通 ScreenProbeGather 的原样移植。UE 普通探针级 TemporalFilter 默认关闭，不能将缺少它作为本问题的唯一原因。

对照入口：`--surface-cache-recovery-test`，Vulkan、9 个姿态、每姿态 8 seed × 32 条逐像素射线参考，共 256 spp。参考共享 Surface Cache，只隔离探针重建及过滤误差，不能视为路径追踪真值。远近视图瞄准同一世界角落；另测相机连续移动、物体移动后的旧地面区域和远端静态接收面。物体对照固定一个方向相位以核对原始信号一致，不能用这段的帧差宣称生产 8 相位的噪声方差。灯灭零光、未知状态、历史实际复用和 ABBA 性能分别检查。直射图单独保存，避免把面积光源积分或阴影采样条带归因于 GI。

```text
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --surface-cache-recovery-test --screenshot build\gi_recovery.png
Module\MaterialLab\RunSurfaceCacheGi.cmd --surface-cache-no-local-neighbors
Module\MaterialLab\RunSurfaceCacheGi.cmd --surface-cache-no-young-recovery
Module\MaterialLab\RunSurfaceCacheGi.cmd --surface-cache-reset-on-object-motion
```

本轮验收记录：`build/gi_recovery_quality.log`，RTX 4060 Laptop、Debug、Vulkan validation、1280×800、间距 32、64 射线/探针、最终 AA=None。相机每帧转向 0.015 rad 并平移 0.05，共 6 帧；物体沿 X 移动 1.2，俯视相机能同时看见旧足迹、新暴露地面及远端静态面。除原 1 相位匹配射线消融外，另比较 8 相位、4 个基础 seed 的实际物体运动；后者相位会因旧版全清而不同，所以不宣称逐帧射线相同。

| 指标（线性 GI 亮度 MSE） | 原行为 → 当前 | 解读 |
| --- | ---: | --- |
| 远处静止角落区域 | 2.96437e-5 → 2.96940e-5 | 约增加 0.17%，没有显著改善，不能称为已消除远角残斑。 |
| 远视图相机运动，全图 | 9.73798e-5 → 9.65996e-5 | 约降低 0.80%。 |
| 近视图相机运动，全图 | 4.93439e-5 → 4.92885e-5 | 约降低 0.11%，边际收益。 |
| 物体运动，8 相位 / 4 seed，全图 | 7.30546e-5 → 7.07155e-5 | 约降低 3.20%。 |
| 同上，减去参考物理变化后的帧间残差 | 1.60568e-5 → 1.46631e-5 | 约降低 8.68%；参考本身仍有采样误差。 |

不能将平滑度单独当作正确性：同一物体测试的未经参考校正帧差从 3.27422e-5 增至 4.04614e-5，包含真实几何/光照变化和不同射线相位。旧足迹的总 MSE 从 3.09070e-5 降至 2.98009e-5，但暗误差从 1.27737e-5 增至 1.82037e-5；当前原始射线的暗误差也增加，不能把所有差异归因于历史拖影，也不能声称每个暗区都有改善。默认动态上限与低历史恢复阈值同为 4，成功保留为 4 帧的像素依赖探针空间过滤和时域，不额外执行当前帧恢复。全局动态判定仍粗糙，后续更精细的运动光照比例/依赖信息及速度重投影才能进一步减少不必要的历史限制。

关键保护检查：30 帧 A/B 原始信号完全匹配；相机连续 12 帧均复用历史；单相位物体 4 次运动全部保留历史，累计 2,273,520 个静态接收像素复用；137,488 条不可读取射线保留原状态；唯一灯光关闭后 raw / spatial / temporal RGB 均为零。`build/gi_recovery_temporal.log` 另验证失败提交不会推进值快照、移动和 3 帧停止期均限制为 4 帧、随后恢复长历史；`build/gi_recovery_spatial.log` 的独立双精度三角形可见性检查中，经过过滤的穿几何邻居数为 0。CPU 两项检查通过；OpenGL 的 `build/gi_recovery_opengl_structural.log` 通过几何重建及灯灭保护，不将该后端不可用的 GPU 时间戳当作性能通过。

同进程静态 ABBA 四批 GPU 耗时为 124.305 / 129.059 / 126.514 / 122.645 ms，原行为均值 123.475 ms，当前 127.787 ms，增加约 4.312 ms（3.49%）；每批预热 12 帧、计时 4 帧，无回读与呈现。它主要量到新增邻居表/探针过滤，成熟历史不会执行低历史恢复，不能代表运动恢复的全部开销。整套基础软件 GI 在此 Debug 配置仍未达到实时 60 FPS。

隔离截图 `build/gi_recovery_far_direct.png` 在关闭 GI 时仍有墙面宽条带，因此这部分属于直射/阴影路径。GI 独立图已较平滑，当前局部邻居收益很小。规则面积光积分、阴影采样和灯具遮挡应在直射路径独立改进，继续加大 GI 模糊无法修正这部分；本轮保留其诊断证据，没有宣称解决所有条带。

## 数据与更新

每个对象保留六个正交方向的单层深度卡片，固定常驻页，默认每页 32×32。对象共享同一网格的局部捕获结果，切换相机或 MDF 精度不重新捕获。每张卡片保存逆变换、投影范围、页地址、表面误差容差、对象身份和材质。图集分为位置/有效性、法线/卡片身份、线性漫反射率三个平面；直接出射辐射率另存。

卡片捕获使用网格已有的三角形 BVH。世界 BVH 排序后的三角形到对象的映射用于精确选择卡片，避免只凭重叠包围盒借用另一个对象的光照。图集查询先检查朝向、捕获有效性、深度和距离，确定覆盖锚点，再默认对同对象、同卡片的照明做双线性读取；各 tap 仍检查深度、法线与切平面，不跨卡片寻找更亮的值。`Geometry-aware cache bilinear` 可恢复原最近样本读取作对照。

对象或材质变化更新对应卡片；光源变化重新计算直接光照。移动遮挡物也会使缓存光照失效，以更新其他物体上的阴影。静止时复用已经确定的直接光照，不把新旧帧混合。光照缓存的复用属于更新调度，不是时域滤波。

单层六方向捕获无法覆盖深腔、内壳和完全被外层遮挡的表面。未覆盖表面在 Coverage / Cache 视图中标为洋红色，参与 GI 时贡献零能量；有效黑色与缺失覆盖分开。后续可增加自适应卡片和多层捕获，不能靠放宽深度容差掩盖缺失。

## 光传输边界

默认基础 GI 复用之前研究的屏幕自适应表面探针：从 GBuffer 放置基础探针，对缺少几何覆盖的深度边缘、薄物体和曲面补充探针；光线由探针发射，像素从兼容的表面探针重建间接光。逐像素软件 BVH 路径保留为可切换的参考。

缓存存储 `L_direct = emission + rho * E_direct / PI`。探针用余弦半球 PDF 积分，原标量 `m0 = E_indirect / PI = mean(L_hit)` 始终保留；默认另存方向系数，在接收面法线处求值后再按几何权重重建。不提前乘探针落点的材质反射率，最终像素只乘自己的 `rho_receiver`。这样红墙上的探针不会把红墙反射率再次带到白色接收面。可读取黑色、离开场景的射线仍保留在总样本数中；缺失卡片不贡献能量，也不通过缩小分母补亮。

探针射线经过屏幕 HZB 追踪，再用 Mesh / Global SDF 补充屏外命中，并从 Surface Cache 读取单次反弹光照。探针 GI 不从上一帧最终 GI 画面中取光；屏幕命中也转换为缓存查询，避免将曝光、镜面或显示用颜色反弹到场景。

重建保留四个基础探针角点，并汇集有限支持范围内所有几何兼容的自适应表面代表。自适应权重按探针真实投影位置计算正值 B2 核，结合双向平面距离、法线及默认开启的屏幕线段可见性；不会把覆盖修补点假设在名义子格中心，也不按每像素最近胜出集合切换。所有 RGB 与可读性 alpha 使用相同的正权重归一化。这属于从探针到接收面的光场重建，不是对已经产生的 GI 图像做降噪。

默认启用下文的探针空间过滤与 GI 时域累积；分别用 H / T 或 `--surface-cache-no-spatial` / `--surface-cache-no-temporal` 关闭。两项都关闭且放置抖动关闭时恢复固定 seed 的原始单帧结果。默认放置相位固定为 0，时域模式只循环改变射线方向 seed；`Temporal placement jitter` 是独立且默认关闭的实验开关。原始探针积分与像素重建结果仍单独保留，不从历史画面向场景反弹光照。天空、镜面反射、透明材质传输与多次反弹仍不属于此单次漫反射版本。

## 曲率分配与凹缝射线起点

当前 GI 默认启用几何曲率加密与每探针起点短扫掠。屏幕 tile 仍作索引，4 / 8 / 16 个预留槽与每探针方向数不变；曲率加密增加的是活跃代表，不保证实际 GPU 开销不变。它不会通过放宽法线、对称切平面或遮挡门限混合不同表面。

原先均匀探针已覆盖的连续表面，只在几何交线附近激活额外代表。现在复用子格扫描，检查相对代表的法线变化、对称切平面误差，以及粗探针支持的加权误差。归一化误差通过 `smoothstep(0.5,1.5,error)` 连续激活现有主代表槽，预算处贡献为一半，避免越过阈值时突然加入满权重的随机光照。覆盖修补与真实交线仍优先满足原义务。代表仍是实际 GBuffer 像素；没有将世界位置平均到曲面内部。B2 核形状与双表面容量保持原定义，重建与空间滤波邻居权重都乘同一激活度。曲率预算控制分配而非射线随机性，代表像素和几何分类仍可能随镜头变化。

F1 → GI 新参数：

| 参数 | 默认 | 用途 |
| --- | --- | --- |
| Refine surface curvature | 开 | 连续曲面即使已有粗覆盖，也按误差预算加密；需要 Refine geometric intersections 开启 |
| Curvature normal budget (degrees) | 10 | 法线变化预算；越低越容易加密 |
| Curvature plane budget (pixels) | 0.75 | 切平面误差预算，按该表面世界像素足迹换算；越低越容易加密 |
| Constrain probe ray origins | 开 | 每个活跃探针只做一次三角形 BVH 短扫掠，屏幕与世界追踪复用同一起点 |
| Origin clearance fraction | 0.5 | 沿法线偏移遇到邻面时，仅使用扣除数值余量后安全距离的该比例；范围 0.05–0.95 |
| Origin numeric clearance | 0.00002 | 邻面端点的数值退让；另有随世界坐标增长的浮点精度下限，与几何 ray bias 分离 |
| Show actual ray origins | 关 | 无深度测试的起点叠加：青绿未限制、橙色已限制、紫红近距离/背面不确定、红色异常。标记中心是实际追踪起点，没有普通探针小球的显示法线偏移 |

起点图集为每探针一个 RGBA32F，xyz 是起点，w 为 0 非活跃、1 完整数值校正后偏移、2 已限制、3 近距离/背面不确定。负数为不可读原因：−1 数据或遍历异常，−2 无源面候选，−3 锚点误差超限，−4 朝向不符，−5 外侧数值余量不足。`ReadbackSurfaceCacheProbeOrigins()` 只读本帧已生成的结果；功能关闭时不返回旧图集。先沿主视线在极短窗口确认真实源三角形，修正深度重建可能略入自身表面的误差，再从该锚点做法线扫掠；两次 BVH 查询均为每探针一次，所有方向复用。源面校正严格限制两个法向平面误差；沿视线的校正量按夹角调整，并限制在四分之一投影足迹内，避免掠射表面被等向距离门限误拒绝。锚点校验失败、遍历耗尽或无法同时满足源面外数值余量与邻面距离约束时保持不可读。atlas 表面位置与法线不变，w=1 不表示 xyz 与旧偏移逐位相同。短扫掠会限制偏移跨越可检测的邻面，**不等于完整闭合体内部认证**，也不会修复已穿插的模型。默认椭球与高方柱保持原布局，未通过移开模型隐藏问题。

上一轮 GBuffer 位置由 Depth32F 反解，设备深度的一次浮点步进在远处或掠射视角不对应固定的世界误差。该版起点校验把该设备深度相邻的两个可表示浮点值分别反投影，以它们相对已存位置的世界位移，估计当地的深度量化及投影往返误差。沿存储法线与真实源三角形法线分别投影这段位移，加入各自的平面误差预算；沿视线允许量仍受原四分之一表面像素足迹限制。它只调整源面锚点的局部接受精度，不放大 ray bias、外侧数值 clearance 或邻面扫掠距离，也不放宽 −4 的正背面验证。修复前的五路消融中，wall 静止四帧累计 152 个 −3 误拒绝、首次移动 22 个；修复后两项均为零，仍保留 4 / 1 个背向源面的 −4。下面的直接位置捕获进一步移除了探针源位置对设备深度反解的依赖。

安全 GI 的屏幕命中还会以屏幕距离为有限查询上界，验证最近三角形；按真实对象与几何正背面读取 Surface Cache。未确认命中则继续 SDF，遍历耗尽保持不可读。空间滤波的探针对遮挡检查复用同一安全起点；命中角度也从这两个实际起点计算，避免把由偏移起点测量的距离与未偏移表面位置混用。几何相容权重仍由原始接收表面的位置和法线决定。保留真实背面、近命中、已知黑色和全射线数归一化，不用删除遮挡或抬高黑色来消除暗斑。

这份三角形扫掠及曲率预算是本实验的适配。UE 5.7.1 仍采用基础屏幕探针加自适应分配，并使用方向照明与接收像素的短程 AO / GI 配合；本版的一阶半球拟合、三点屏幕深度可见性仍是近似，不宣称已经完整移植 UE 的方向 gather、SH 或 Short Range GI。

离线入口：`--surface-cache-geometry-test`；A/B 可用 `--surface-cache-no-safe-origins`、`--surface-cache-no-curvature`，实际起点叠加可用 `--surface-cache-origin-overlay`。专项使用 20 / 45 / 60 / 90 / 120 度 V 槽，以及球体/方块的分离、接触与相交布局；参考来自独立双精度三角形求交与固定微小数值偏移，不以生产起点偏移作为真值。

### 2026-10-18：几何与曲率专项验收

最终深度精度修正后的独立专项见 `build/acute_geometry_geometry_depth.log`。测试为 Vulkan、256×192、32 像素基础间距、16 个候选槽、每探针 64 射线，关闭空间与时域过滤，以 0.06 世界单位的较大 ray bias 对近缝施压。八种布局中，旧偏移出现的 19 个跨入邻闭合体的起点全部被限制；safe 与 safe+curvature 的闭合接收物体失效数 `invalid_solid`、实际邻体内部起点数及独立判黑射线返回亮色的违例数均为零。保留 44–79 个背向单面诊断 sheet 的不可读起点，没有把它们伪装为可读黑色。有效前面、背面和空射线都被测试覆盖，关发光后最大原始 GI 为零，常量重建最大误差不超过 `2.98e-7`。这些是该组布局的独立 oracle 结果，不等于任意模型内部认证或每个像素均有探针；V60 仍有 2 个覆盖洞，三个球场景分别有 12 / 22 / 3 个洞。

仅打开曲率分配、其余安全起点设置相同时：

| 球体与方块关系 | 球面仿射重建 MSE 降低 | 球面残差梯度 RMS / P99 降低 | 实际 GI 球面 MSE 降低 |
| --- | ---: | ---: | ---: |
| 分离 | 27.06% | 10.14% / 4.73% | 0.32% |
| 接触 | 26.58% | 9.19% / 5.63% | 0.27% |
| 相交 | 27.19% | 7.97% / 6.53% | 0.53% |

**约 27% 是解析仿射光场的重建误差改善，不是实际 GI 降噪幅度。** 实际 GI 在这项独立曲率开关对照中仅改善约 0.3–0.5%；每场景增加 8 个活跃代表，总活跃数增加约 3.5–3.6%。球面解析残差按同一物体内部的相邻像素计算，避免用真实几何边缘的跳变代替曲面块色误差。

五个相隔四分之一像素的平移姿态中，关闭屏幕线段可见性、空间与时域过滤以隔离分配重建：仿射残差帧差 RMS / P99 降低 31.27% / 35.85%，但最大值 `0.0159164` 未下降。固定 seed=11 的实际 GI 帧差 RMS / P99 只降低 3.50% / 3.78%，且包含真实表面取样及物理光照变化，不能直接当作纯随机噪声方差。连续激活还由独立全探针归一 oracle 检查：实际激活度 `0.457871` 的探针在 1132 个像素上，选定探针脉冲与数学权重的最大误差 `1.74349e-6`；强行使用满权重的反事实最大偏差为 `0.190234`。这验证激活度确实参与重建，没有声称曲率误差估计本身已成为完整物理误差界。

### 五路消融与剩余暗尾部

`--surface-cache-feature-ablation` 在同进程比较 legacy、safe_only、curvature_only、current、no_screen_visibility。每个机位每项先预热 16 帧，再测静止 4 帧与首次移动 1 帧；两个机位共 10 项、50 个测量帧，全部复用有效历史。它不替代连续移动验收。初版日志为 `build/acute_geometry_ablation.log`，深度精度修正后的日志为 `build/acute_geometry_ablation_depth.log`，最终空间命中角度起点基准修正后的日志为 `build/acute_geometry_ablation_angle.log`。

最终 angle 批次中，current 相对同批 legacy 的曲面 ROI 如下；三个数字依次是 raw / spatial / temporal，负号表示误差下降：

| 机位与状态 | 总 MSE 变化 | dark MSE 变化 |
| --- | --- | --- |
| wall 静止 | −41.18% / −37.16% / −36.13% | +20.94% / +31.84% / +44.03% |
| wall 首次移动 | −39.18% / −30.49% / −35.74% | +34.42% / +45.98% / +35.18% |
| oval 静止 | −31.09% / −27.49% / −28.22% | −5.79% / +6.14% / +12.15% |
| oval 首次移动 | −33.15% / −29.68% / −27.87% | +6.11% / +15.57% / +16.15% |

总误差主要因偏亮尾部减少而改善，**暗尾部仍有回退，不能宣称黑斑已全面解决**。曲率单独开启多数情况下减小 dark MSE；safe_only 则增大暗尾部，连起点完全有效的 oval 静止情况也如此，因此问题不全是深度容差误拒绝。关闭三点屏幕线段可见性相对 current 的 dark MSE 变化不足约 0.3%，不是这批退化的主要解释。Depth32F 修正把 wall 静止/首次移动可读性从 `0.986514 / 0.987973` 恢复到 `0.999870 / 0.999754`，全边缘 ROI 的 dark MSE 下降约 2.1–3.3%，但曲面 ROI 数值基本不变。空间过滤还可能放大局部暗偏差，需继续用方向测量与表面邻域的独立对照定位，不能仅凭总 MSE 改善结案。

depth → angle 的最后小修正统一了空间命中角度计算与真实追踪起点的基准。其 legacy 和 raw 质量字段保持不变；四组曲面静止/首次移动的 spatial、temporal dark MSE 相对 depth 降低约 0.06–0.19%，总 MSE 降低约 0.01–0.14%。这是小幅改善，不能用它解释或抹去上表仍存在的暗尾部回退；Depth32F 对 −3 误拒绝的修复归因也保持独立。

五路与完整 edge 的 256 spp 逐像素参考共享现有 Surface Cache，并从 `p + n * rayBias` 使用原 BVH 参考路径起射；它避开屏幕假命中，但极薄或锐角处仍有旧起点偏移的边界，不是完整路径追踪真值。旧屏幕错误亮命中被移除后，可能暴露此前被抵消的局部暗误差；汇总 MSE 不能证明逐像素抵消关系。上述八布局的独立三角形 oracle 才是这轮起点和有效黑色语义的正确性证据。

### 复现入口与开销

```text
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --surface-cache-geometry-test --msaa 1
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --surface-cache-feature-ablation --render-width 1280 --render-height 800 --msaa 1
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --surface-cache-feature-perf --render-width 1280 --render-height 800 --msaa 1
```

feature-perf 在同进程按 legacy / current / current / legacy 的 ABBA 顺序测量，每批 12 帧，不含图像回读与呈现。RTX 4060 Laptop、Debug、1280×800、64 射线/探针下，最终 `build/acute_geometry_perf_angle.log` 四批 GPU 为 `76.3616 / 98.3036 / 99.0239 / 77.1136 ms`；legacy 均值 `76.7376 ms`，current 均值 `98.66375 ms`，增加约 **28.57%（约 29%）**。这是整组曲率分配、活跃射线变化、三角形起点及安全命中验证的管线开关对照，不是单一 shader 的隔离成本；固定容量没有增长不等于实际执行免费。本批次未达到 60 FPS。较早的 `build/acute_geometry_feature_perf.log` 是修正前的另一批测量，不能将跨批次绝对耗时差当作本次小修正的性能收益。

## 2026-10-18：源位置精度与整探针失效处理

探针使用的接收面位置默认改为直接光栅化捕获：复用材质的法线捕获管线、相同的 alpha/cutout 覆盖和不透明深度，在 RGBA32F 中保存插值后的 view position，再由放置阶段变换到世界空间。原来的深度反解仍可作 A/B 对照。直接捕获减少了深度量化及反投影条件数带来的源面误差；它没有改变几何法线、扩大安全偏移，也没有取消源三角形短查询或邻面扫掠。位置纹理复用原存储，仅另建共享深度的 framebuffer。

重建同时区分**整颗探针的发射起点无效**与**有效起点发出的部分方向不可读**。整起点认证失败的代表现在从光照加权分子和几何归一化分母中同时移除，避免把“没有合法测量”当成满权重黑色，拉暗周围区域。像素只在原有有限支持范围内，使用通过法线、对称切平面和可见性检查的其他代表；不会扩大搜索到另一面或跳过遮挡。没有足够可用支持时仍输出不可读，未新增保证填洞的射线或强制补亮。

该排除条件只查看起点图集的有效性，**不按 RGB、readability alpha 或亮度筛选探针**。有效黑色仍占完整权重；有效起点的部分方向缺卡片、超出距离场或遍历耗尽时，积分仍保留全部射线数作分母，不因删去未知方向而增亮。几何覆盖与常量/仿射等合成诊断保留各自定义，不能把它们的覆盖显示当作实际光传输全部有效的证明。

F1 → GI 提供两项按需回读，按钮点击后更新的是最近一次完成帧的统计，界面显示 `Last inspected` / `Last rays`；镜头继续移动时这些数字不会自动刷新，以免每帧回读造成停顿：

| 按钮 | 显示内容 | 如何判断 |
| --- | --- | --- |
| Inspect origin validity | 活跃、完整偏移、已限制、不确定，以及 −1～−5 五类整起点失败数 | `source / precision / facing / clearance` 分别对应无源候选、误差超限、朝向不符和外侧余量不足；这些才是起点认证统计 |
| Inspect ray status counts | 射线状态 6 / 16 / 14 / 15 的数量 | 分别为源数据/投影校验异常、起点不可用、距离场范围不足、遍历预算耗尽；状态 16 应结合起点统计看，不能将所有射线异常归为同一种问题 |

普通 P 探针标记中心显示的是选定方向的一条射线状态，红色不等于整颗探针的所有方向失效。`Show actual ray origins` 是另一项独立叠加：青绿为完整偏移，橙色为已限制，紫红为近距离/背面不确定，红色为整起点无效；它没有深度测试，可能透过前景显示，不能仅凭屏幕颜色认定探针属于眼前的平墙。

专项入口及深度反解对照：

```text
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --surface-cache-origin-test --msaa 1
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --surface-cache-origin-test --surface-cache-depth-positions --msaa 1
```

`build/probe_validity_origin_depth.log` 与 `build/probe_validity_origin_raster.log` 各覆盖 16 个姿态，包括 640×400、1280×800、2560×1440、近距离掠射墙面/天花板及移动后的机位；每探针 16 条射线，关闭空间与时域过滤。独立双精度三角形 oracle 在远离几何边界的平墙内区测得以下**捕获源位置到真实源平面的误差**，不是把安全偏移后的起点要求在平面上：

| 指标 | Depth32F 反解 | 直接位置捕获 |
| --- | ---: | ---: |
| 平墙内区样本数 | 15626 | 15627 |
| 平墙内区无效起点数 | 0 | 0 |
| 内区源平面误差 RMS（世界单位） | 3.768e−5 | 4.6522e−7 |
| 内区源平面绝对误差最大值（世界单位） | 2.24717e−4 | 3.46452e−6 |

两组有效起点进入其他闭合体、无效起点射线契约违例均为零。本轮几何、空间过滤与时域回归也通过。**深度反解基线在这 16 个姿态的平墙内区同样没有失效，未复现截图中的普通平墙误拒绝，不能据此宣称每个红点均已根治。** 边界仍有 −4 朝向不符等保守拒绝，直接捕获后的一个近掠射姿态也保留 2 个边界 −3；它们未被隐藏或改成可读黑色。实际剩余暗斑还可能来自有限方向采样、支持缺口与近处几何差异，须结合起点和射线计数继续定位。

`--surface-cache-transport-test` 独立验证重建规则，最终日志为 `build/probe_validity_transport_final.log`。1280×800、固定 seed=17、每探针 16 方向，关闭方向模型、空间/时域过滤及屏幕线段可见性，以隔离标量重建算术。CPU 从独立三角形确定接收材质，遍历全部活跃代表的实际位置 B2 核及四个粗角点，核对 66,494 个接收像素；RGBA 最大差 `2.60428e-4` 是 GPU 浮点与双精度 oracle 的数值差，不是画质 MSE。一个失效起点原本影响 124 个像素，它们的 RGB 也确实变化：与保留旧分母的反事实相比，RGB 向量最大差 `0.00312751`，并非只修改 alpha。45 个部分 unknown 探针仍按完整方向数积分，独立逐射线核对最大差 `8.61152e-8`；关灯后 2,174 个有效全黑探针和 1,024,000 个可读接收像素继续保留，RGB 为零。这证明该测试中的失效分母修正和已知黑色/部分未知语义，不等于证明所有视角均无暗斑或漏光。

源位置捕获的额外成本可用 `--surface-cache-source-perf --render-width 1280 --render-height 800` 测量。它在其余设置不变时，同进程按 depth / raster / raster / depth 顺序预热并各测 12 帧；`build/probe_validity_source_perf.log` 四批 GPU 耗时为 `111.135 / 111.889 / 112.895 / 112.511 ms`，均值 `111.823 / 112.392 ms`，该批约增加 `0.569 ms`（0.51%）。环境仍为 RTX 4060 Laptop、Debug、64 射线/探针，不含回读和呈现；波动不可忽略，这不是整套修复的隔离成本，也不能与上一批绝对耗时直接比较。

当前版本的几何、空间、时域、实际位置重建和方向模型 GPU 回归均通过；OpenGL 为结构检查通过，未将其等同独立高采样画质验收。`build/probe_validity_edge_final.log` 还记录 640×400、12 个 256 spp 参考姿态、64 个连续移动帧全部复用历史，关灯 raw/temporal 为零；它是本版本的运动回归，不用于沿用上一版 1280×800 的改善百分比。CPU 的 PBR 与 Surface Cache 两项回归通过。

## 当前表面代表、方向重建与局部可见性

自适应容量仍为每基础 tile 的 4 / 8 / 16 个候选槽，但现在每个空间子格最多占用两个槽，分别表示两个几何不兼容的可见表面。对应布局为 2×1 / 2×2 / 4×2 个子格，每格两个代表。默认 32 像素基础间距与 8 个槽意味着 2×2 个 16×16 像素子格，而不是旧版 4×2 个单表面子格。代表由实际 GBuffer 像素选出，保留各自位置与法线，不把前景和背景平均成一个悬空点。基础覆盖薄弱的表面和真实交线可以请求代表；均匀且已有覆盖的子格不必消耗两个探针的射线。

自适应重建采用按真实投影位置计算的可分离二次 B 样条核 `B2(dx / step.x) * B2(dy / step.y)`。单轴核在 `|t| < 0.5` 时为 `0.75 - t²`，在 `0.5 ≤ |t| < 1.5` 时为 `0.5 * (1.5 - |t|)²`，更远处为零。默认步长为 16 像素，单轴支持半径为 24 像素。搜索窗口包含完整核范围，候选离开范围时贡献连续降为零，不依赖最近四点、整体替换或交线元数据的周期遮罩；四个存储桶只汇总贡献，不限制参与探针数量。

正值归一化能保持常量场；规则、完整采样位置上的 B2 也有线性精度。但实际代表会因几何分组、覆盖缺口和屏幕边界偏离规则格，几何及可见性拒绝又会截断支持，因此不保证任意不规则分布上的仿射场精确重建。每格最多两个表面，第三层或更多互不兼容表面、极细物体和强曲率区域仍可能缺覆盖。增加候选数或降低基础间距可以增加预算，不能通过混入其他表面来保证填满所有像素。

`Probe segment visibility` 在像素与探针分别沿法线偏移后的线段上，于 1/4、1/2、3/4 位置检查当前 GBuffer 深度。确定的前景遮挡拒绝借用，深度容差带平滑减权；越界、背景和无效深度保持不确定，不凭空产生黑色遮挡。放置覆盖判断和最终重建使用同一函数与捕获纹理，避免分配器说“已覆盖”而接收端又拒绝。它独立于空间过滤的探针对 BVH 可见性检查：前者检查像素到探针，后者检查探针到邻探针。

三点检查只是屏幕空间近似：可能漏掉采样点之间的薄遮挡，无法读取隐藏层或屏外几何，前景投影也可能造成保守误拒绝。它会依赖当前视角，并可能增加真实支持缺口；不能宣称消除全部漏光，也不能替代逐像素短程 GI。原有法线、双向平面距离和未知覆盖规则保持独立，不通过放宽几何门限补偿被拒绝样本。

`Directional probe reconstruction` 为每探针增加横向四个 RGBA texel：第一个是原 `m0 / readability`，其余分别为 RGB 的世界坐标方向系数 `bx / by / bz`。系数来自现有余弦半球射线的中心化方向矩；按颜色通道限制一阶模型的幅度，避免负方向瓣。接收面求值为 `m0 + (2/3) * b · (n_receiver - n_probe)`，原法线处保持原标量积分，readability alpha 也保持原值。基础探针、自适应代表和覆盖回退都在逐探针聚合前求值，过滤后路径使用过滤后的射线重新拟合。

这是受限的一阶半球拟合，不是完整球面 SH。探针未采到背向半球，不能把缺失方向当作测量后直接除以 `cos_probe` 外推；模型的非负限制也不代表所有方向已有真实覆盖。它可以改善不同接收法线的方向表达，但同法线、不同位置的遮挡差异仍然存在，不能单靠方向系数修复墙角过遮挡。当前仍没有完整逐像素短程补追和多次反弹。原标量图集及 API 保留；方向模型为原始及启用的空间过滤结果各增加一次小图集积分，不新增主追踪射线，但会增加系数读取、图集和执行开销。

F1 的 GI 组中，以下开关相互独立；改变估计器配置会重建 GI 历史：

| 参数 | 当前默认 | 用途与限制 |
| --- | --- | --- |
| Directional probe reconstruction | 开启 | 按接收法线求值受限方向模型；关闭保留原标量 `E/PI` 对照。 |
| Probe segment visibility | 开启 | 开启像素到探针的三点屏幕深度检查，不替代空间过滤的 BVH 配对检查。 |
| Temporal placement jitter | 关闭，实验性 | 自动改变基础放置相位及依赖覆盖判定的自适应选择。它独立于默认 8 个射线方向相位，可能增加重建波动，不作为默认降噪保证。 |
| Adaptive candidates / tile | 8，可选 4 / 8 / 16 | 固定槽预算中每子格最多两个表面；增加容量可能增加活动探针和射线。 |
| Probe spacing (pixels) | 32，面板 4～128 | 同时影响基础密度、子格尺寸和实际位置核半径。减半间距大约增加四倍基础槽。 |
| Refine geometric intersections | 开启 | 启用交线主动补点和当前连续核重建路径；关闭可观察旧重建路径，传统缺覆盖补点仍有预算。 |
| Intersection refinement band (tiles) | 0.5，面板 0.25～1.5 | 控制交线主动分配的范围，受检测范围限制；不再充当逐像素粗细混合遮罩，也不是 B2 核半径。 |

在 `GI inspection = Indirect GI` 且使用表面探针时，`Probe reconstruction check` 还提供 `Actual-position affine`：逐探针按实际世界位置赋值 `clamp(0.45 + 0.04 * (x + 2y + 0.5z), 0.05, 0.95)`，再进行真实加权，用于检查名义中心与实际位置是否混用；不将目标像素的解析值直接写入结果。`Selected probe support` 则仅让 `Support diagnostic probe` 选定的探针为 1，其余有效探针为合法黑色 0，显示实际支持范围和权重。原 `Constant illumination` 与 `Base / adaptive weights` 继续可用。合成诊断绕过接收反射率与 GI 强度，暂停该视图的空间和时域过滤，不改写真实射线或积分图集；退出后重新建立真实历史。

### 2026-10-18：本轮实际位置重建验收

对照对象是上一版固定细格版本（`build/edge_mosaic_fixed.log`），不是最初的马赛克版本。Vulkan validation、1280×800、32 像素间距、每探针 64 射线，场景及空间 / 时域参数相同。两机位共 12 个姿态的 256 spp 逐像素单次反弹参考，96 项数值完全一致，两张静态参考 PNG 的 SHA256 也相同。参考共享当前 Surface Cache，不是完整路径追踪真值或 UE 实测。

| 机位 | 阶段 | 时域亮度 MSE 降低 | 暗误差降低 | 亮误差降低 |
| --- | --- | ---: | ---: | ---: |
| 墙顶 | 静止 | 34.22% | 21.99% | 39.13% |
| 墙顶 | 首移动帧 | 30.95% | 12.04% | 38.58% |
| 墙顶 | 连续移动 | 25.88% | 8.90% | 31.11% |
| 椭球 / 后墙 | 静止 | 27.21% | 8.78% | 33.29% |
| 椭球 / 后墙 | 首移动帧 | 28.61% | 17.40% | 32.92% |
| 椭球 / 后墙 | 连续移动 | 47.67% | 19.87% | 56.36% |

两机位各连续渲染 32 个运动帧，有对应参考的 10 个检查点参与误差比较；64 帧全部复用 GI 历史。未经过时空过滤的原始 GI，连续移动 MSE 也分别降低 27.30% / 45.07%，改进发生在重建源头。完整日志为 `build/adaptive_surface_final_quality.log`，逐项结果为 `build/adaptive_surface_final_compare.csv`。

不能据此声明光斑完全消除：真实 GI 的 16 像素纵向相位残差在部分阶段增加。例如墙顶连续移动由 1.90950e-7 增至 3.41329e-7，椭球静止由 2.70697e-7 增至 1.10339e-6。合成粗细标签的周期性改善不能代替该真实残差指标。静态及首移动帧的真实孔洞仍为零；连续移动墙顶由 72 降至 13 / 7,158,630 样本，椭球则由 0 增至 102 / 5,182,433 样本，约 0.00197%。低支持计数的质量总和尺度随核改变，不能直接解释为置信度改善比例。关灯 raw / temporal 为零，可读取黑色保留；真实可读率约 99.97%～99.99%。

结构回归在 4 / 8 / 16 槽与五个四分之一像素平移位置核对真实 GBuffer 来源、双表面槽、常量归一化和独立全局 B2 参考；选定探针在实际半径外及不兼容表面贡献为零。方向模型另核对实际射线积分、原法线恒等、非负界、未知覆盖、开关恢复、过滤后数据来源、历史失效与关灯归零。Vulkan 的既有空间过滤、时域和密闭房间回归、PBR CPU 回归及 OpenGL 的结构诊断均通过；OpenGL 的独立高采样参考限制沿用下文历史记录，不将结构诊断视为该参考验收。

为减少固定支持窗口扫描空槽，当前每个基础 tile 生成一枚活动槽位掩码，再跳过未生成的子格 / 代表。16 位掩码在 RGBA32F 中精确保存；所有活跃样本的访问顺序及权重不变。该优化前后两机位的常量、粗细标签、raw 和 temporal 共八张 PNG 逐字节一致。它新增一个很小的 tile 图集和汇总 pass，不增加 GI 追踪射线。

局部可见性的独立俯视检查找到 5 个可读的像素 / 基础探针对，原始变换后三角形的双精度求交确认线段被遮挡；关闭检查时这些探针有贡献，开启后所选探针贡献均归零。这只证明该明确遮挡子集，不证明三点检查能识别任意薄遮挡。最终日志为 `build/adaptive_surface_structure_release.log`。

同一 1280×800 密闭房间、64 射线 / 探针、64 个面积光采样，Vulkan validation，预热后各测 12 帧，不呈现、不限帧、不回读图像，包含覆盖诊断。重新成对测得：

| 版本 | GPU ms | 含提交与等待的整帧 ms |
| --- | ---: | ---: |
| 上一版固定细格 | 84.74 | 90.17 |
| 当前表面代表与方向 / 可见性检查 | 95.20 | 100.69 |

当前配置增加了约 12.3% GPU、11.7% 整帧开销，不能宣称这次画质改进免费或达到 60 FPS。另一批次的当前版本为 87.51 / 93.45 ms，体现了本机运行波动；不同批次差值不等于隔离的 shader 耗时。成对日志为 `build/adaptive_surface_perf_{before,after}_verified.log`，掩码前后的单批次记录为 `build/adaptive_surface_perf.log`、`build/adaptive_surface_mask_perf.log`。关闭方向模型或局部可见性可作开销对照，但会改变估计器。

最终审查还补齐了冻结分配对 normal bias / relative thickness 的兼容性检查与 GI 历史参数键；冻结位置不会掩盖遮挡参数变更。针对性回归确认参数改变会刷新分配并拒绝旧历史，配置稳定后继续复用冻结结果。发布构建与最终结构、方向、时域、CPU 日志为 `build/adaptive_surface_release_build.log`、`build/adaptive_surface_{structure,directional,temporal,cpu}_release.log`。方向纹理关闭或未采集时，读回 API 明确返回错误，不提供旧帧系数。

## Spatial Filtering 与自适应覆盖修正

执行顺序是“屏幕追踪与 SDF 补追 → 探针方向辐射率空间过滤 → 探针积分 → 几何兼容重建 → 时域累积 → 直射合成”。空间过滤处理每个探针的入射方向样本，发生在乘接收面反射率之前，因此不会把红墙材质直接模糊到白色物体。原始射线、原始 `E/PI` 和未过滤的像素 GI 仍分别保存，可与过滤后、时域前的 GI 对照。

本版参考 Lumen 的固定相邻探针模板、相对深度指数权重和近处命中距离限制。每一轮检查上下左右的邻居，半径为 2 时再检查两格处；重复迭代逐步传播兼容的低噪声估计，不使用逐轮扩大的 à-trous 步长。命中角度过大时不混合，且将邻居命中距离限制到当前命中距离，防止远处的亮样本洗掉近处接触阴影。几何配对和可选 BVH 可见性检查只对探针对执行一次，射线单元复用配对结果。

与 UE 的完整实现存在明确差异：UE 使用世界方向的等面积球面单元，本版使用法线朝向的余弦半球分层单元，对于法线几乎相同（点积大于 `1 - 1e-6`）且使用同一个切线基分支的平面探针，按相同分层单元一一对应，保留独立抖动的分层平均意义，避免最近方向查找重复使用或遗漏某些角度测量造成平面均值偏差。对不同法线或切线基的邻居，则把当前世界方向反变换到邻居的局部余弦半球，在最近四个分层单元中按实际射线方向选择角度最近的测量；选择在读取有效性之前完成，不会因最近样本不可读而另选更亮或有效的样本。空间命中角度另按 UE 的共同世界锥方向计算，即使用“邻居起点 + 当前方向 × 被限制的命中距离”，避免把独立随机方向的抖动错误地当作几何视差；10° 门限只约束空间视差，权重不依赖亮度。曲面间的最近方向重采样仍是近似，并非严格保持角度积分或能量的重采样，可能带来小量偏差；尚未实现 UE 将原始追踪散射到共享八面体方向图集的完整步骤。本版额外增加法线硬门限、双向表面平面距离检查和探针间的 BVH 可见性检查。自适应候选也可以替换不兼容的基础邻居。不可读取的射线继续保持不可读取，原始状态、命中位置、方向与追踪诊断不变；空间过滤不能替缺失卡片编造光照。已知黑色是有效测量，参与归一化过滤，避免只平均亮样本造成整体偏亮。

按 F1 打开 GI 组：

| 参数 | 默认 / 面板范围 | 含义与调节建议 |
| --- | --- | --- |
| Spatial filtering | 开启 / H | 开关探针方向辐射率过滤。配合 T 可分别比较单帧过滤和时域累积。 |
| Spatial passes | 3 / 1～4 | 固定相邻模板的迭代次数。越大越平滑、开销越高，也更容易削弱真实的局部变化；建议先用 2～3。 |
| Spatial half kernel | 1 / 1～2 个探针格 | 1 检查四邻居，2 再检查距离两格的四邻居。增大探针间距时，世界空间实际过滤范围也会变大。 |
| Spatial strength | 1 / 0～1 | 每轮原始值与邻域归一化结果的混合比例。0 精确保留原始单帧结果；细节变软时先尝试 0.5～0.8。 |
| Spatial position weight scale | 1000 / 100～10000 | 权重为 `exp2(-scale * relativeDepthDifference²)`。越大越少混合不同深度探针；减少时过滤更强，但更可能损失接触细节。 |
| Spatial hit angle | 10° / 1～60° | 接受邻居命中方向的最大夹角。减小保护近处遮挡，增大允许更多过滤；建议 5～15°，不建议先靠大角度消除光斑。 |
| Spatial normal threshold | 30° / 5～60° | 配对探针允许的法线夹角。较小保护墙角和曲面轮廓，过严会让曲面残留更多噪声。 |
| Spatial plane / view depth | 0.01 / 0.001～0.05 | 两个探针在彼此法线方向上的位移，相对较近的视深度的上限。越小越能阻止跨层混合；薄板漏光时先减小。 |
| Spatial pair visibility test | 开启 | 对偏移到表面外的探针对之间发射软件 BVH 遮挡射线；穿过物体的配对不共享光照。关闭可减少这项工作，但薄遮挡或近距离并行表面更可能串光。 |

调整过滤开关或参数会清空旧 GI 历史，下帧重新累积，避免混用不同估计器。空间过滤会引入有限邻域近似，不能同时保证无噪声和保留所有细小光照变化；优先增加真实采样与完善覆盖，再调过滤强度。

此前已经启用自适应探针，但候选只在每格少量离散位置检查几何，薄柱和球轮廓可能落在这些位置之间；重建的自适应搜索还可能沿用了抖动基础网格的四角位置，遗漏实际覆盖当前像素的单元。因此“开着自适应”仍不等于每个有效像素都拥有兼容探针。

当前补充候选按固定 4 / 8 / 16 槽预算划分为 2 / 4 / 8 个子格，每格最多保留两个不兼容表面的实际代表。接收端按上节实际位置 B2 核的完整范围累计所有兼容自适应贡献，不再仅在基础覆盖不足时选择 top-four 胜出集合。法线、双向平面和默认三点可见性共同约束重建；有限预算仍允许真实缺口，不通过放宽边界权重掩盖它们。

B 或 `GI inspection → Probe geometric coverage` 显示独立的几何覆盖：绿色为基础探针支持，橙色为自适应探针补充，洋红为有效接收面没有兼容探针，黑色为背景。它与 Cache coverage（是否存在卡片）以及 P 叠加显示的射线命中颜色分别回答不同问题。不可读取射线不代表接收像素没有探针；合法黑色光照也不等于覆盖缺失。

P 叠加视图中，探针标记的外环现在区分蓝色基础探针和橙色自适应探针；内侧仍显示射线命中 / 不可读取等状态色。选中的探针外环优先显示白色。这样可以直接确认自适应探针是否实际生成，不必从混杂的射线状态颜色推断探针类型。

发现洋红区域时，先确认 `Adaptive placement` 开启，再将 `Adaptive candidates / tile` 从 8 提到 16；仍有缺口可把 `Probe spacing` 从 32 降至 16。增加候选数会增加预留槽和潜在射线预算，减半间距大约将基础槽数增至四倍。不要用更大的空间过滤角度或更松的平面阈值把其他物体的光照填进缺口。

面板的 `Inspect active probe counts` 按钮回读当前探针，显示实际活动的基础 / 自适应数量；统计里的 `Probe slots` 和 `Software GI rays` 仍是预留容量上限。实际数量只在按下按钮时刷新，避免为了显示计数每帧等待 GPU。

```text
Module\MaterialLab\RunSurfaceCacheGi.cmd --surface-cache-spatial-passes 3 --surface-cache-spatial-radius 1
Module\MaterialLab\RunSurfaceCacheGi.cmd --surface-cache-no-temporal --surface-cache-view indirect
Module\MaterialLab\RunSurfaceCacheGi.cmd --surface-cache-no-spatial --surface-cache-no-temporal --surface-cache-view indirect
Module\MaterialLab\RunSurfaceCacheGi.cmd --surface-cache-view probe-coverage
```

## Temporal Accumulation

当前沿用 2026-10-16 调整的 10 帧总历史、8 个方向相位、8 帧运动历史上限和每帧 3 像素的帧数缩短阈值。Sigma 6 的历史裁剪同时覆盖超过 0.1 像素的缓慢相机运动；几何边缘另保留 2 帧历史和更严格的裁剪。当时 1280×800、每探针 64 射线的 Vulkan 完整转向、平移与组合轨迹验证通过；其数据是旧重建版本的参数调节依据，不替代新增方向与表面代表版本的运动验收。

时域环节位于“自适应探针积分 → 像素重建”之后、“间接光与直射合成”之前，累积的是线性 HDR 间接漫反射辐射率。它与最终图像 TAA 完全分开；直射、UI、曝光后的显示结果不进入 GI 历史。

历史资源独立不代表运动判定已经消除 TAA 抖动：当前 GBuffer 使用最终图像 TAA 的抖动投影，开启 TAA 时，静止相机的投影抖动也可能超过 0.1 像素，触发缓慢运动裁剪或边缘保护。因此当前默认及主要画质对照采用最终图像 AA=None；已有 TAA 共存检查验证历史复用，未证明此组合具备相同的长期收敛效果。

当前像素的世界位置投影到上一帧，检查相邻四个历史像素的世界位置误差、法线和有效性；只在同一表面上做双线性重投影。遮挡后重新露出的像素、越界、无效几何从当前帧重新开始。每像素保存有效累计帧数，达到上限之前采用逐步平均，达到上限后继续以 `1 / MaxFrames` 的权重加入当前样本，并非永久冻结结果。默认每 8 帧循环一次方向采样，给累积器提供不同的蒙特卡洛样本。

本版用相机重投影后的像素移动量区分历史帧数限制与历史裁剪：超过 3 像素才将普通表面历史限制至 8 帧，超过 0.1 像素就允许较宽的 Sigma 6 裁剪，避免慢速移动时长历史滞后于视角相关的探针重建。历史裁剪只限制旧值；2026-10-18 新增的低历史恢复另过滤当前样本。运动中的几何边缘仍用短历史与较紧裁剪，静止边缘正常累积。经严格认证的可移动物体纯变换现在可以保留兼容历史，材质、光源、拓扑或无法认证的修改仍清除整幅历史。

UE 默认关闭 `RejectBasedOnNormal` 和 `FastUpdateModeUseNeighborhoodClamp`，45° 是可启用法线检查的角度默认值。本版始终检查法线，并默认开启运动历史裁剪；这是为当前重建误差采用的更保守策略。UE 的快速更新程度来自命中运动物体的光照比例（`LumenScreenProbeGatherTemporal.usf:585–596`），并非简单的相机屏幕位移。本版尚无物体速度与光照运动缓冲，局部几何边缘保护也是本实现的安全调整，不是 UE 的同名完整算法。总历史 10 帧与方向相位 8 保留 UE 默认值；运动阈值、运动上限、Sigma 6 与边缘保护参数则根据本版实测调节。历史使用未经曝光的线性辐射率，显示曝光变化无需进行历史预曝光补偿。

相机切换、手动重置、开关 GI / 时域、切入卡片检查视图、分辨率、基础 seed、探针间距、射线数和时域参数变化都重建历史；正常相机移动通过逐像素检查复用兼容表面。总光、间接光与 History length 三个视图共享同一份历史。

按 F1 打开面板，在 GI 组调节下列参数：

| 参数 | 默认 / 面板范围 | 含义与调节建议 |
| --- | --- | --- |
| Temporal accumulation | 开启 | T 切换；关闭时查看固定 seed 的原始单帧误差，逐像素参考路径不使用此历史。 |
| Max accumulated frames | 10 / 1～64 | 越大静止噪声越少、响应越慢。1 只使用当前帧，开启低历史恢复时仍可空间过滤；调高前应同时检查运动轮廓和接触细节，不能只看静止方差。 |
| Temporal direction phases | 8 / 1～64 | 独立射线方向相位的循环长度。1 重复同一批射线，无法靠时域减少其采样误差；增大历史时可同步比较更多相位，但不保证整体画质更好。 |
| History distance / view depth | 0.01 / 0.001～0.05 | 接收表面与历史位置的相对误差基准，另有 0.002 世界单位下限和有上限的像素足迹余量；表面法线方向的误差仍受基准约束。减小可减少跨几何拖影，但会拒绝更多历史、增加闪烁。 |
| History normal threshold | 45° / 5～90° | 可接受的最大法线夹角。薄物体或曲面串光可调到 15～30°；太严会使曲面频繁重新累积。 |
| Moving history frames | 8 / 1～最大累计帧数 | 超过屏幕位移阈值时，普通运动表面的历史上限。1～2 反应快，但相机会使大面积表面恢复较强噪声；建议先比较 6～8，并保留边缘保护。 |
| Motion threshold (pixels) | 3 / 0.1～32 | 控制普通运动表面何时缩短历史帧数，不控制已启用的缓慢运动裁剪。按实际 GI 渲染分辨率的每帧位移计量，也受帧率影响。 |
| History neighborhood clamp | 开启 | 控制普通表面的历史裁剪。关闭可能更平稳，但可能滞后于当前重建；几何边缘保护是独立开关。 |
| Clamp slow camera motion | 开启 | 在普通历史裁剪开启时，将裁剪扩展到每帧位移超过 0.1 像素的表面，修正低于 3 像素阈值的慢速重建变化。关闭恢复仅快速更新区域裁剪；不同时关闭边缘保护。 |
| History clamp sigma | 6 / 1～8 | 普通运动区域兼容邻域统计区间的宽度。越大保留历史越多；太小会把历史拉回当前噪声。边缘保护另使用不大于 3 的 Sigma，不随该值无限放宽。 |
| Geometry edge protection | 开启 | 在移动轮廓、薄物体附近及历史支持不足时采用短历史与更紧裁剪。关闭用于对照；不增加射线，但会增加运动时的几何读取。 |
| Edge history frames | 2 / 1～总历史与运动上限的较小值 | 边缘区域的独立历史上限。1 响应最快但噪声更高；2 为当前默认。调高可减轻边缘噪声，也可能重新出现轮廓拖尾。 |
| Local adaptive probe neighbors | 开启 / Spatial 组 | 原有相邻大网格邻居之外，加入同一网格内本子单元及相邻子单元的有效代表，最多 8 个。按真实投影位置连续加权，并保留 BVH 可见性及命中角限制。用于减少远处细边缘的局部采样孤立。 |
| Young history spatial recovery | 开启 | 对历史不足的当前 GI 做小范围双边恢复。成熟历史不额外读取邻居；不会增加历史帧数，也不填亮未知像素。 |
| Recovery radius (pixels) | 3 / 1～4 | 当前帧 9 点核的半径。每条采样路径逐像素检查几何、材质和可读性；增大会更平滑，也增加读取和暂时软化细节的风险。 |
| Recovery below frames | 4 / 2～16 | 有效历史达到此值后完全停止恢复；低于它时平滑衰减强度。新暴露与完全重置帧也覆盖。 |
| Recovery strength | 0.75 / 0～1 | 低历史当前信号与兼容邻域平均的混合量，不按亮度剔除黑样本。0 用于隔离对照。 |
| Retain history on movable transforms | 开启 | 仅认证同一场景中可移动对象的纯变换。保存已提交帧的实例值快照；接收点仍逐像素检查历史位置与法线。未知映射、静态物体、模型源、灯光或材质变化仍重置。 |
| Dynamic lighting history frames | 4 / 1～16 | 认证运动及收敛期的历史上限。当前为全图保守响应，尚无 UE 的逐像素运动光照比例；太大会拖影，太小会增加噪声。 |
| Dynamic lighting clamp sigma | 2 / 0.5～6 | 无论相机是否移动，运动光照期间都对旧历史进行邻域裁剪。越小响应更快，也越容易回到当前采样误差。 |
| Dynamic settling frames | 3 / 0～16 | 物体停止后的若干成功提交帧继续动态响应，帮助旧位置的间接阴影更新。失败或丢弃帧不推进计数。 |
| Reset GI history | 按钮 / G | 清除旧历史，下帧从一帧开始；用于观察收敛过程。 |

V 切换到 History length：蓝色表示短历史，逐渐转绿，再到达到上限的红色；黑色为无效表面。新露出表面从一帧开始，静止表面逐渐达到上限。通过 T 开关比较光斑的时间稳定性；时域只能降低变化中的采样方差，不能修复 SDF / 卡片覆盖错误、几何串光或方向分布偏差。

```text
Module\MaterialLab\RunSurfaceCacheGi.cmd --surface-cache-view indirect --surface-cache-history-frames 10 --surface-cache-history-directions 8
Module\MaterialLab\RunSurfaceCacheGi.cmd --surface-cache-view history
Module\MaterialLab\RunSurfaceCacheGi.cmd --surface-cache-no-temporal --surface-cache-view indirect
```

### 相机移动时的噪声与调节

旧默认是 10 帧总历史、2 帧运动历史、每帧 1 像素阈值并开启历史邻域裁剪。相机轻微转动就可能让整片静态墙面超过 1 像素，使当前帧权重从 10% 升为 50%；裁剪还可能把已收敛历史拉回局部当前帧噪声。停下后历史重新增加，画面才逐渐稳定。这个现象既包含新露出区域必须重新采样的正常部分，也包含旧快速更新参数对静态相机运动过于保守的部分。

当前默认（日志名 `balanced_edges`）不放宽深度 0.01 和法线 45° 的历史拒绝条件。普通快速运动区域的当前样本权重由旧上限 2 帧时的 50% 降至上限 8 帧时的 12.5%；轮廓附近仍采用 2 帧历史。较宽的 Sigma 6 裁剪还覆盖超过 0.1 像素的缓慢运动，边缘则使用不大于 3 的 Sigma。旧参数对照显式关闭缓慢运动裁剪与边缘保护，保留原来的快速运动裁剪算法。参数修改自身会重置一次历史，比较前应先等待收敛。

边缘保护只在重投影位移超过每帧 0.1 像素时工作。有效双线性历史权重合计不足 0.9，或当前像素两像素范围内出现背景、法线不连续、双向表面平面距离过大时，触发边缘短历史和兼容表面裁剪。法线点积门限取历史门限与 0.9 的较大值；平面距离门限为 `max(0.002, min(历史基准容差, 0.002 * 视深度))`。这些是局部保护的固定实现值，不会通过放宽主历史检查来填补缺口。静止边缘仍可正常累积；关闭普通历史裁剪不会同时关闭几何边缘保护。

该边缘检查没有增加纹理、主追踪射线或全屏 Pass。当前时域参数块为 272 字节，另含低历史恢复和动态响应参数。运动且历史支持完整的像素最多检查 24 个几何邻居；发现不连续时提前退出，历史支持已不足时跳过检测。裁剪另使用 3×3 兼容辐射率样本，低历史恢复增加小范围读取。实际开销需单独计时。

曾比较关闭裁剪的长历史候选：小尺寸压力图中更稳定，正式分辨率却增加轮廓误差，因此没有采用。仅在快速移动时裁剪也会遗漏慢速组合运动的重建变化，当前另用缓慢运动裁剪修正。不能只根据小图、单条路径或时间方差选择参数，也不能把低闪烁等同于低误差。

连续运动回归入口为 `--surface-cache-motion-test`，覆盖纯转向、纯平移及组合路径。每条路径预热 24 帧，再向外运动 8 帧、原路返回 8 帧、停下 8 帧。9 个独立相机位置各用 4 个独立 seed、每探针 256 条射线建立未过滤参考。相同方向相位的配置逐帧核对原始 GI 一致，避免随机序列差异污染对照；SDF 资源准备完成后才开始比较。

未指定分辨率时，测试采用 320×200、间距 8、16 射线压力配置；正式大小通过 `--render-width 1280 --render-height 800` 指定，使用间距 32 与默认 64 射线。两种尺寸均只比较旧参数和当前默认两组。运动阈值按测试高度相对 800 同比缩放，但边缘两像素范围与 0.1 像素触发值保持实际像素单位，所以小图不能代替正式分辨率验证。`--surface-cache-motion-tune` 单独执行转向路径，对比旧参数和三组边缘方案。

误差在曝光前的线性 HDR 间接光上逐位置对照参考。平面内部记录亮度 MSE，将上一帧误差重投影到同一表面后记录误差变化 MSE；薄物体和曲面轮廓另记录边缘 MSE 与绝对误差第 95 百分位。不将正常的相机画面变化算作闪烁。背景、缺少兼容探针及不可读取当前 GI 的像素不能借历史填亮；这组旧测试按物体移动全清执行，新增的认证移动路径单独覆盖几何拒绝、远端光照响应和动态历史限制。

2026-10-16 正式接受的记录为 `build/surface_cache_motion_verified_vulkan.log`：RTX 4060 Laptop、优化 Debug 构建、Vulkan validation、显式软件追踪，实际 1280×800、间距 32、每探针 64 射线，最终图像 AA=None。以下分别列各运动路径，避免合计均值掩盖某一路径退化。

| 路径 | 平面亮度 MSE：旧 → 新 | 误差变化 MSE：旧 → 新 | 几何边缘 MSE：旧 → 新 |
| --- | ---: | ---: | ---: |
| 转向 | 6.43641e-6 → 5.49241e-6 | 1.05452e-5 → 7.15123e-6 | 1.79526e-5 → 1.80067e-5 |
| 平移 | 6.59991e-6 → 5.28071e-6 | 1.08108e-5 → 6.89229e-6 | 1.71856e-5 → 1.72313e-5 |
| 转向与平移组合 | 5.87106e-6 → 5.43570e-6 | 8.98195e-6 → 6.71156e-6 | 1.71810e-5 → 1.72376e-5 |
| 合计 | 6.30236e-6 → 5.40299e-6 | 1.01119e-5 → 6.91816e-6 | 1.74397e-5 → 1.74918e-5 |

三条路径的平面 MSE 分别降低约 14.7%、20.0%、7.4%，误差变化 MSE 分别降低约 32.2%、36.2%、25.3%；合计分别降低约 14.3% 和 31.6%。几何边缘 MSE 合计变化约 +0.30%，边缘绝对误差 P95 由 0.00889104 变为 0.00889758（约 +0.074%），与旧方案基本相当。运动区间平面有效历史均值由 2.33 增至 8.83 帧；72 个对应帧的原始 GI 最大差为零。背景、不可读取区域、历史重置和灯光 / 物体变化检查均通过。这些是有限测试房间的结果，不表示任意场景绝无拖影。

OpenGL 另通过 320×200、16 射线的三条连续运动路径回归，逐路径与合计检查均通过，72 个对应帧的原始 GI 最大差同样为零，记录见 `build/surface_cache_motion_verified_opengl.log`。这是另一后端的小尺寸正确性回归，不等于在 OpenGL 下完成 1280×800 画质或性能验证。

原有独立时域回归也在 Vulkan 与 OpenGL 通过，覆盖静止收敛、正常重投影、重新露出区域、历史失效与 TAA 共存。该基线显式关闭新增边缘保护和缓慢运动裁剪，确认保留旧路径兼容性，不将其静止方差收益当作新参数的运动收益。记录为 `build/surface_cache_motion_temporal_vulkan.log`、`build/surface_cache_motion_temporal_opengl.log`；TAA 共存检查的范围仍受上文限制。

更平稳仍有响应代价：停下后的前 8 帧区间，新配置平面 MSE 为 2.81537e-6，旧配置为 2.19167e-6，新配置约高 28.5%；同一区间误差变化 MSE 则由 3.71544e-7 降至 8.97926e-8，降低约 75.8%。因此不宣称所有时刻误差都更低；若更重视停下后的快速响应，可比较降低运动历史上限，同时保留边缘保护。

同一日志另独立测量连续相机运动开销：各配置同位置预热 24 帧，再测 12 个连续移动帧，不呈现、不限帧、不回读图像，保留 GI 历史链。下表为整条渲染管线，非隔离着色器时间。

| 配置 | GPU ms | 含提交与等待的整帧 ms |
| --- | ---: | ---: |
| 旧参数 | 27.15 | 41.46 |
| 当前参数，关闭边缘保护 | 27.38 | 41.39 |
| 当前参数，开启边缘保护 | 26.73 | 37.27 |

这些结果处于同一耗时量级，受 GPU 频率和提交调度波动影响，不能据该批次差值宣称边缘保护加速或没有开销。它仍有上述额外几何读取；GPU 约 27 ms、整帧约 37～41 ms，也没有达到稳定 60 FPS。截图前缀为 `build/surface_cache_motion_verified_`，包含各路径的运动峰值与停下结果；先前探索日志不代替这份正式验收记录。

```text
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --surface-cache-motion-test --render-width 1280 --render-height 800 --surface-cache-probe-rays 8 --screenshot build\surface_cache_motion.png
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --surface-cache-motion-tune --render-width 1280 --render-height 800
output\sdf-banquet\Debug\bin\material_lab_demo.exe --surface-cache-motion-test
```

## 探针射线与条带光斑

旧逐像素参考在 1 条射线时使用固定半径的确定性积分点，倾角恒为 45°，只改变方位角。这覆盖一个圆锥而非完整余弦半球，会形成与遮挡形状相关的亮暗条带；仅旋转 seed 或添加蓝噪声不能消除这种方向分布偏差。

探针路径用二维分层采样：默认 8×8 个角度单元，每个单元内部两个维度都独立抖动，再通过 `r=sqrt(u)`、`phi=2*PI*v` 映射到法线朝向的余弦半球。不同探针和不同采样单元具有独立散列扰动，避免相邻探针重复同一条固定圆锥。可切换 4×4 / 8×8 / 16×16，即每探针 16 / 64 / 256 条射线。

这修正方向分布，但不承诺无过滤的有限采样完全没有光斑：16 条射线仍可能把积分方差重建成较宽的亮暗块；几何采样不足、卡片缺失和 32×32 卡片的光照离散也可能产生残余变化。观察 `indirect` 视图，先提高每探针射线数，再减小探针间距，分别检查角度积分误差和空间重建误差；不要把真实的漫反射亮度变化一并抹平。

## 密闭陶瓷房间与方形顶灯

双击 `Module/MaterialLab/RunSealedGiRoom.cmd`，或在现有程序后添加 `--surface-cache-sealed-room`。这是一套独立场景，原有红绿墙房间和茶壶宴会继续保留。

新场景默认收起文字 HUD 和调节面板，便于完整观察顶灯。F1 打开调节面板；文字帮助可在面板 `Help / scene controls` 中开启。

房间内径为 8×7，高 4.5 世界单位。四面厚墙分别为珊瑚红、翡翠绿、钴蓝和赭黄；地面为浅米灰，天花板为暖白。顶棚由四片实体围出中央方孔，孔内有封闭背板、内衬、黑色边框和朝下的 1.8×1.8 方形漫射灯面。六个陶瓷球体、椭球、立方体及棱柱摆放在灯下。相机恢复为之前 Surface Cache 场景的 55° 视角，默认正面机位；1/2/3 分别切换正面、侧面和近景，抬头可查看顶灯。右键转向使用原始鼠标输入，保持原有 0.0025 弧度/鼠标单位的灵敏度，WASD 与 Q/E 移动；相机限制在房间内。

唯一光源为与灯面一致的单面矩形面积灯，默认线性辐射率为 `(24, 23.04, 21.36)`。太阳、点光、聚光、天空和环境补光均关闭。L 开关顶灯；F1 的 GI 面板提供 `Ceiling lamp` 和 `Ceiling emitted radiance`（0～80），同时更新可见灯面与真实光源。F5 关闭或开启间接 GI 时，直接面积光和缓存软阴影仍保留；V 可只看间接光、卡片光照、历史与覆盖。

陶瓷是非金属介质，线性漫反射率约 0.80～0.86、粗糙度 0.16～0.24；面积灯的直接 GGX 高光表现光滑釉面，彩墙通过现有单次漫反射 GI 染色。镜面间接反射及多次漫反射仍未实现，反射率高不代表将金属度设为 1。

新增矩形灯默认使用 8×8（可配置 4×4）确定性面积求积，卡片照明对每个采样点使用共用三角形 BVH 检查可见性。直接 PBR 软阴影使用各采样点的六面深度图 atlas，避免对每个画面像素重复遍历三角形。默认 64×6 个 128² 深度瓦片合计 24 MiB；只有几何、光源位置或形状变化才重建，相机移动、灯光颜色和亮度变化会复用它。4 tap 深度比较带有有界接收平面修正，瓦片内部合并为一次 textureGather 读取，边界保留独立读取以免跨面取样。它仍是有限阴影图分辨率和面积求积近似，接触处可能有偏差。此专用材质用于当前全不透明房间；普通 PBR 场景保留原路径。

灯面可见发光，而其卡片 emission 为零，由解析面积灯负责直接光输运，避免把同一盏灯经随机发光命中再次计入 GI。灯光变化会使卡片照明与时域历史失效，阴影几何缓存则不因亮度变化而重新生成。关闭 GI 后直接面积光与阴影仍保留。

独立检查与截图入口：

```text
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --sealed-gi-room-test --render-width 1280 --render-height 800 --screenshot build\sealed_gi_room.png
```

该入口检查密闭几何、灯面可达、间接光正能量、GI 开关对照，以及关闭唯一光源后的全黑结果，并输出 `_total`、`_indirect`、`_direct`、`_lamp_off` 图片。

2026-10-17 初版 76° 对角机位验证：Vulkan validation、显式软件追踪、1280×800 下通过 299 条密闭检查射线、9 条灯面可达路径及六个陶瓷材质检查；GI 开关改变约 100 万像素。关灯后原始 GI、时域 GI、卡片照明与最终图像全部为零。相机位移、恢复机位及灯光亮度变化均不重建阴影图，移动方块后更新 384 个面，随后继续复用。旧房间 Surface Cache、独立时域与材质 CPU 回归通过。日志为 `build/sealed_gi_room_final_vulkan.log`、`build/sealed_gi_baseline_vulkan.log`、`build/sealed_gi_temporal_vulkan.log`。

OpenGL 另通过 640×400 同一场景检查，包括关灯全黑和阴影缓存更新；记录为 `build/sealed_gi_room_final_opengl.log`。这是跨后端正确性检查，不代表 OpenGL 完成了上述分辨率的性能验收。

上述初版机位在 RTX 4060 Laptop、优化 Debug 构建、64 射线/探针与 64 个面积光采样下，静态预热后 12 帧平均 GPU 65.11 ms、含提交与等待 70.71 ms，不含呈现、限帧或图像回读。这是画质优先的观察场景，尚未达到稳定 60 FPS；启动器的 60 仅是上限。相比最初逐像素 BVH 面积阴影方案（16 个面积采样，GPU 322.99 ms）明显降低开销，但两者采样数不同，不能视作同画质性能对照。Gather 优化前后的直接光与完整 GI 截图 SHA256 完全一致。

恢复 55° 正面机位后，1280×800 的同一 Vulkan 场景检查再次通过，记录与截图为 `build/sealed_gi_camera_vulkan.log`、`build/sealed_gi_camera_total.png`。该机位 12 帧平均 GPU 45.64 ms、含提交与等待 51.84 ms；可见内容已变化，不能据此视作同画面渲染算法加速。

## 历史：墙顶交线与固定细格修复

本节保存先前“固定细格双线性重建”的设计和验收，已被上文“实际位置 B2 核＋每格双表面代表＋受限方向模型”替代。以下旧版参数语义、四顶点实现、误差与耗时只描述当时版本；当前算法及新增验证入口以上文和“验证入口”为准。

嵌入顶灯会让墙顶产生真实的遮挡暗带，单次反弹也会使未受直射的天花板方向较暗。需要修正的是这些局部暗值被稀疏探针扩展成整个网格大小的块状阴影；本轮不增加环境补光，也不消除真实暗角。

当时参考 UE 5.7.1 的 `LumenScreenProbeGather.usf::CalculateUpsampleInterpolationWeights` 与 `SurfaceCache/LumenSurfaceCacheSampling.ush`，增加以下处理：

- Surface Cache 的照明在已覆盖的同一个对象、同一张卡片内做双线性采样，并检查深度、法线和切平面，不跨页寻找更亮的值。有效黑色参与完整权重；真正不可读的照明仍保持不可读。
- 仅在 GI 路径中，根据相邻 GBuffer 表面的法线和平面变化检测几何交线，并验证交线落在双方有限的世界空间邻域内。分离的前景椭球和后墙即使无限平面相交，也不直接触发真实交线细化。薄物体和曲面缺覆盖补点仍优先。
- 主动细化采用跨基础 tile 共享的固定细格，按照实际取整后的子格中心做双线性插值。每个顶点独立检查几何兼容性和细化包络；缺少或不兼容的细顶点仅将自己的权重留给基础场。取消最近四象限竞争、四点整体有效门槛、四点最小支持度及基础探针的方形查询遮罩，避免 8×16 像素周期的细粗切换。
- 几何支持不足时，兼容候选以连续紧支撑核累计后归一化，不再按 top-four 胜出集合整体替换。有效黑色和不可读 alpha 均使用相同权重，不以可读数量或亮度重新选择探针。`0.01` 表示充分几何覆盖，`0.0001` 表示仍可用的低置信度估计；原法线和双向平面硬拒绝保持不变。

上述固定细格属于本实验对 UE 按角点处理自适应贡献思路的适配，并非完整 GPU 压缩调度的逐行移植。当时正常细化路径只查询四个固定细顶点，几何覆盖不足时才搜索邻近候选；只存标量 `E/PI`，未实现方向重建、逐像素短程 GI 校正及多次反弹。空间过滤和 GI 时域继续处理剩余方差；该次改动未增加 UBO 大小、长期图像历史、全屏 Pass 或射线图集容量。

该历史版本 F1 的 GI 面板参数如下，当前面板的新增开关及分配范围语义见上文：

| 参数 | 默认 / 范围 | 用途 |
| --- | --- | --- |
| Geometry-aware cache bilinear | 开启 | 切换深度 / 法线约束的卡片照明双线性采样，关闭可对照原 nearest 读取。 |
| Refine geometric intersections | 开启 | 交线补点与连续重建的总开关；关闭保留旧估计器作为对照。 |
| Intersection detection (probe tiles) | 0.75 / 0.25～2 | 几何交线检测范围，以基础探针格为单位。增大覆盖更宽区域，也会激活更多探针。 |
| Intersection blend width (tiles) | 0.5 / 0.25～1.5 | 细化区域到基础场的过渡宽度，按固定顶点的交线置信度计算，不再将查询点距最远顶点的距离作为整组开关。有效宽度不超过检测范围。 |
| Intersection normal angle | 45° / 15～75° | 触发交线检测的法线差异角。减小会在较缓曲面上增加细化。 |

在 `GI inspection = Indirect GI` 且使用探针时，`Probe reconstruction check` 可切换：

- `Real lighting`：正常间接光，默认值。
- `Constant illumination`：所有有效探针赋照度 1，绕过接收材质和 GI 强度，用于检查归一化及真正的支持缺口。
- `Base / adaptive weights`：基础探针赋 0.25，自适应探针赋 0.75，隔离粗细混合比例与光线噪声。几何接缝两侧可以有真实标签差，不能将所有相邻跳变都当作错误。

合成诊断自动暂停该视图的空间和时域过滤，不覆盖真实射线或照度图集；退出后重建真实 GI 历史。真实黑色样本仍参与正常估计器。覆盖修补点可在子格中偏移，固定基函数仍是有限空间近似，不等同于精确的逐像素可见性校正。

`--surface-cache-edge-test` 新增轮廓回归。深度 / 法线断层两侧 16 像素均计入，包含球体、椭球、箱体、墙面、紧贴接缝及低支持区域，不按覆盖状态剔除难点。两个机位各建立 6 个姿态的 256 spp 同缓存逐像素参考，并连续渲染 32 帧平移与转向、原路返回；静态、首移动及有对应参考的运动检查点分别报告原始、空间和时域结果。正常方向相位保持一致，参考仍只验证当前单次反弹近似，不是独立路径追踪真值。

`--surface-cache-edge-diagnostics` 是只运行合成诊断和静态实际光照的快入口，不执行高采样参考或运动验收。两入口均检查常量保持、凸组合范围、关灯归零和合法黑色；正式 1280×800 的两个固定机位还限制双色诊断中大于 0.1 的相邻跳变比例低于 2%。这是包含真实几何接缝的保守回归界限，不等于任意场景没有跳变。原马赛克版本在两机位均超过此界限。Vulkan 全尺寸与 OpenGL 回归通过 opt-in GPU CTest 注册，保持串行执行。

### 历史 2026-10-18：固定细格版轮廓马赛克修复验收

1280×800、Vulkan validation、RTX 4060 Laptop、优化 Debug 构建。原马赛克版本与固定细格版本使用相同的场景、射线预算、放置相位和过滤参数；两组共 12 个参考姿态的数值及静态参考图一致。参考为同一个完成光照缓存上的 256 spp 逐像素单次反弹，不能作为 UE Lumen 或独立路径追踪真值。下面统计深度 / 法线边界两侧全部接收区域的时域输出，不排除曲面、低支持或接缝。连续运动每个机位实际渲染 32 帧，在有相应高采样参考的 10 个检查点计算误差。

| 机位 | 阶段 | 亮度 MSE 降低 | 负向亮度误差降低 |
| --- | --- | ---: | ---: |
| 墙顶交线 | 静止 | 38.64% | 50.80% |
| 墙顶交线 | 移动首帧 | 40.75% | 49.98% |
| 墙顶交线 | 连续移动 | 36.73% | 49.52% |
| 椭球 / 后墙 | 静止 | 43.98% | 28.09% |
| 椭球 / 后墙 | 移动首帧 | 44.58% | 23.58% |
| 椭球 / 后墙 | 连续移动 | 28.95% | 17.73% |

未经过空间或时域的原始 GI，连续运动 MSE 也分别下降 35.20% 和 30.52%，说明改进来自重建本身。双色诊断中大于 0.1 的相邻跳变数量分别由 11,364 降至 6,532、9,550 降至 2,670，减少 42.52% / 72.04%；占相邻配对的 1.48% / 0.84%，通过上述 2% 回归界限。常量诊断没有支持缺口，归一化 MSE 约 1.4～1.6e-15。真实几何接缝仍可能产生 0.5 的标签跳变，不能以最大跳变归零作为目标。

64 个运动帧全部复用 GI 历史。两个静态机位及移动首帧的真实支持缺口为零；墙顶机位连续运动的 7,158,630 个边缘像素样本中，缺口由 54 增至 72（约 0.0010%），椭球机位保持为零。关灯后的原始及累积 GI 均为零，同时保留 1,024,000 个可读取的合法黑色像素。仍有不足：墙面单独统计的负向误差在部分阶段增加约 5～14%，曲面均值偏差也未完全消除；总 MSE 下降不代表每个区域、每种误差都改善。宽光照偏差、有限采样及覆盖修补点的子格位置近似仍需要后续的方向重建和短程校正。

同一 1280×800 密闭房间，64 射线 / 探针、64 个面积光采样，预热后各测 12 帧，包含覆盖诊断，不呈现、不限帧、不做图像回读：

| 版本 | GPU ms | 含提交与等待的整帧 ms |
| --- | ---: | ---: |
| 原马赛克版本 | 88.31 | 94.14 |
| 固定细格版本 | 80.00 | 85.10 |

整条管线批次耗时约下降 9.4% / 9.6%，活动探针分布也有变化，不能将差值归为单个重建着色器的耗时。当前画质配置仍未达到 60 FPS；查看器默认不采集覆盖诊断，绝对耗时可能不同。前后日志为 `build/edge_mosaic_before.log`、`build/edge_mosaic_fixed.log`，逐项比较为 `build/edge_mosaic_fixed_compare.csv`，性能记录为 `build/edge_mosaic_perf_{before,after}.log`。最终合成结构检查见 `build/edge_mosaic_verified_diagnostics.log`。

Vulkan 的既有空间过滤、时域累积和密闭房间回归通过，未报告 validation 错误；日志为 `build/edge_mosaic_{spatial_regression,temporal_regression,perf_after}.log`。641×401 的非整格尺寸结构诊断也通过，见 `build/edge_mosaic_odd_edges.log`。PBR CPU 回归通过。OpenGL 640×400 的常量、粗细混合、真实静态 GI 和关灯检查通过，但独立逐像素参考的两组 seed 均值未产生可测差异，尚未通过参考独立性检查；本轮不据此声明 OpenGL 的高采样画质验收通过。`--surface-cache-edge-test` 完整参考入口因此限定 Vulkan，OpenGL 使用 `--surface-cache-edge-diagnostics`，对应 GPU CTest 也只检查该结构路径。完整画质与运动数据来自 Vulkan。

最终二进制的重建诊断分别记录在 `build/edge_mosaic_final_vulkan.log`（1280×800）与 `build/edge_mosaic_opengl_verified.log`（640×400），构建与 CPU 日志为 `build/edge_mosaic_final_build.log`、`build/edge_mosaic_cpu.log`。

`--surface-cache-corner-test` 对比无过滤估计器；`--surface-cache-corner-filtered-test` 对比实际 Spatial + Temporal 路径。两者均使用同一完成光照缓存上的独立逐像素三角形 BVH 参考，比较两个机位、墙顶两侧的同平面区域。后者固定探针放置相位，采用 1024 spp 参考，并单独记录镜头移动首帧、8×8 完整同平面块的误差和每组帧耗时。参考不进入实际渲染历史。测试仍检查关灯全黑、几何覆盖及背景无假光。

`--surface-cache-corner-sweep` 另比较 0.75 / 1 / 1.25 的作用范围，普通回归只运行原版、仅交线细化、仅卡片过滤、两者组合四组。正式 1280×800 对照采用两个机位、32 像素基础间距、每探针 64 条射线、3 轮空间过滤及原有 GI 时域参数。误差在 HDR 间接光上计算；暗块指标是完整同平面 8×8 块相对参考的负向亮度误差平方，不能将其下降百分比理解为整体 GI 误差下降。

交互查看器默认不计算未显示的全屏探针覆盖诊断，按 B 切入覆盖视图时自动恢复。公共设置 `collectProbeCoverage` 仍默认开启，兼容诊断调用；关闭后，覆盖回读明确返回未采集错误，不会返回旧帧。下文历史交线性能对照关闭此独立诊断，光照、空间过滤和时域累积保持开启；本节新的密闭房间性能对照则包含覆盖诊断，各自配置单独列明。局部加密依然会增加有效射线和重建成本，并非免费降噪。

以下是固定细格修复之前的更早历史结果，见 `build/surface_cache_corner_accepted.log`（Vulkan validation、RTX 4060 Laptop、优化 Debug），不代表当前实际位置核版本。两个机位共 235,787 个交线区域像素，每组统计四个连续帧；天花板有 2,032 个完整 8×8 块样本，墙壁有 9,880 个。

| 指标 | 原版 | 改进后 | 变化 |
| --- | ---: | ---: | ---: |
| 天花板负向块误差 | 4.96788e-7 | 2.26941e-7 | 降低 54.3% |
| 天花板像素负向误差 | 5.98244e-6 | 4.79305e-6 | 降低 19.9% |
| 全部区域亮度 MSE | 8.18700e-5 | 8.15534e-5 | 基本持平，降低 0.39% |
| 两个收敛机位之间的重投影误差变化 MSE | 1.64032e-5 | 1.38705e-5 | 降低 15.4%，不是连续运动指标 |
| 静止相位误差变化 MSE | 9.48247e-8 | 6.49680e-8 | 降低 31.5% |
| GPU / 整帧 ms | 55.38 / 61.03 | 87.24 / 93.17 | 边缘补采样仍有明显开销 |

以上是本场景的局部误差对照，不能称作整体误差降低 54%。天花板总块误差约增加 0.47%，墙壁总块误差降低约 53.6%，但墙壁很小的负向块误差从 9.94e-9 增至 3.72e-8。镜头移动首帧的整体 MSE 增加约 2.7%，负向误差降低约 13.3%；因此也不能称作每一帧、每一种误差都改善。角落仍有真实遮挡和单次反弹近似造成的暗区。

性能各测 12 帧，不呈现、不限帧、不回读图像；不是交互 FPS。该历史场景每帧有效自适应探针均值从 141.5 增至 877，容量不变，新增有效射线使整帧尚不满足 60 FPS。面板可关闭交线细化以取舍成本，卡片过滤可单独保留。曾尝试裁掉未组成相邻局部四点格的细探针，虽然减少射线，却增加整体偏亮误差，因此未采用。参考图及改进前后间接光图为 `build/surface_cache_corner_accepted_{reference,before,after}_{0,1}.png`；这些图只显示间接光，灯面本身呈黑属于该诊断视图的正常表现。

最终构建、PBR CPU 回归、Vulkan 空间过滤 / 时域回归、OpenGL 320×200 交线回归以及 Vulkan 1280×800 密闭场景回归均通过。空间回归保留了 982 条不可读取射线的状态，元数据误差为零；独立几何检查的穿遮挡配对为零。新交线入口另验证诊断关闭后不会返回旧覆盖图或重置 GI 历史，关灯后仍存在可读取的合法黑色样本。密闭场景关灯后的 raw、temporal、atlas 均为零。日志为 `build/surface_cache_corner_accepted_{spatial,temporal,opengl,sealed}.log`。小尺寸 OpenGL 检查没有完整天花板 8×8 块样本，不能用它代替正式分辨率的天花板质量指标。

## 启动和观察

双击 `Module/MaterialLab/RunSurfaceCacheGi.cmd`，默认以 Vulkan、1280×800、间距 32 像素的自适应探针和每探针 64 条射线运行红绿墙测试房间，启用 3 轮探针空间过滤、10 帧 GI 时域累积与 8 个方向相位，限制 60 FPS。普通快速更新历史上限为 8 帧、阈值为每帧 3 像素，启用 Sigma 6 历史裁剪与 2 帧几何边缘保护。启动器显式关闭原生光追；60 是上限，不是性能承诺。主光沿敞开的房间正面照入，使用 CSM 计算可见表面的太阳直射阴影，卡片直接光照使用软件 BVH 遮挡。关闭天空和环境补光。房间、卡片与实际渲染使用相同的固定线性漫反射材质；茶壶的三个 raster LOD 在此模式均为相同灰色，不将精度调试色注入 GI。

| 操作 | 按键 |
| --- | --- |
| 总光 / 卡片光照 / 间接光 / 卡片覆盖 / 图集 / 累计帧数 / 探针覆盖 | V，或调节面板的 GI inspection |
| 空间过滤开关 / 探针几何覆盖 | H / B |
| 时域累积开关 / 清除历史 | T / G |
| 开关基础 GI | F5 |
| 16 / 64 / 256 条射线 / 探针 | F7；逐像素参考模式另调 1 / 4 / 8 / 16 |
| 自适应探针 / 逐像素参考 | F6，或面板 Gather from surface probes |
| 自适应补充 / 间距 16、32、64 像素 | F2 / F3 |
| 探针与追踪命中叠加显示 | P |
| 更换独立射线 seed / 冻结探针位置 | J / Space |
| 预设视角 / 重置场景 | 1 / 2 / 3，R |
| 移动物体 | 左右方向键 |
| 相机移动 / 转向 | WASD、Q/E、按住右键转向，Shift 加速 |
| 调节面板 / 退出 | F1 / Esc |

```text
Module\MaterialLab\RunSurfaceCacheGi.cmd --surface-cache-view indirect --surface-cache-probe-rays 16 --surface-cache-probe-spacing 16
Module\MaterialLab\RunSurfaceCacheGi.cmd --surface-cache-reference --surface-cache-view indirect --surface-cache-rays 16
Module\MaterialLab\RunSurfaceCacheGi.cmd --sdf-clipmaps
```

第三条复用 12 个导入茶壶、三张桌子和四级 Clipmap。它需要启用 Model / Assimp 的宴会构建。Surface Cache 不随相机或茶壶 raster/MDF 档位重捕获；移动选中的近处茶壶只更新其卡片，再使直接光照失效以重新计算遮挡。`--surface-cache-probe-rays` 的参数是方阵边长，不是总射线数。

## 验证入口

```text
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --surface-cache-test --screenshot build\surface_cache_quality.png
output\sdf-banquet\Debug\bin\material_lab_demo.exe --surface-cache-test
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --surface-cache-probe-test --screenshot build\surface_cache_probes.png
output\sdf-banquet\Debug\bin\material_lab_demo.exe --surface-cache-probe-test
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --surface-cache-temporal-test --screenshot build\surface_cache_temporal.png
output\sdf-banquet\Debug\bin\material_lab_demo.exe --surface-cache-temporal-test
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --surface-cache-spatial-test --screenshot build\surface_cache_spatial.png
output\sdf-banquet\Debug\bin\material_lab_demo.exe --surface-cache-spatial-test
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --surface-cache-adaptive-test
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --validation --software-rays --surface-cache-directional-test
output\sdf-banquet\Debug\bin\material_lab_demo.exe --vulkan --surface-cache-performance-test --render-width 1280 --render-height 800 --msaa 1
```

新增 `--surface-cache-adaptive-test` 调用 `SurfaceCacheAdaptiveSurfaceQuality`（`Test/adaptive_surface_test.inl`）。它独立于旧轮廓画质入口，检查 4 / 8 / 16 槽配置、同子格多表面代表、常量场、实际世界位置仿射场、真实位移探针的 impulse 支持范围和亚像素相机变化。B2 核检查使用实际探针位置的独立 CPU 权重参考，仿射场相对目标像素的误差另行报告，不要求不规则样本达到零误差。核结构检查先关闭屏幕可见性以隔离职责；独立的可见性 A/B 部分再与原三角形的双精度求交对照，不能把三点屏幕近似当作精确几何 oracle。

新增 `--surface-cache-directional-test` 调用 `SurfaceCacheDirectionalQuality`（`Test/surface_cache_directional_test.inl`），固定 320×200 与每探针 64 射线。它回读实际 GPU 系数、探针法线和射线，检查 `m0/readability` 与原标量积分一致、非负方向界、原法线保持、不同接收法线求值、开关 A→B→A 和旧图集不可读保护；空间阶段的系数均值另与实际过滤后射线独立积分比较。故意不相关的卡片捕获提供真正未知覆盖，关灯提供有效黑色；两者的 alpha 不得混淆。方向开关变化还应使 GI 历史失效。

启用 `MATERIAL_LAB_GPU_TESTS` 且 Vulkan 后端可用时，新增 CTest 名称分别为 `material_lab_surface_cache_adaptive_vulkan_gpu_test` 与 `material_lab_surface_cache_directional_vulkan_gpu_test`，均使用 validation、显式软件追踪并串行执行。这些结构、积分与状态检查不替代真实场景的成对画质、连续运动和性能验收，也不复用本节历史表作为新版本结果。

原有卡片、探针与时域质量入口显式关闭新增的空间过滤；卡片与探针质量入口还关闭 GI 时域，保持原来的独立单帧基线。质量入口使用独立小房间，检查无光全黑且黑色卡片仍有效、关闭屏幕追踪后的缓存反弹、光强加倍时原始 HDR 反弹加倍、A→B→A 相机与光源变化后结果一致、静态帧不更新卡片光照、移动单物体只更新六张卡片，以及故意提供不相关卡片时显示洋红缺失覆盖但不注入 GI 能量。卡片太阳光另以独立双精度逐三角形遮挡测试和 `rho * sunlight * cosine / PI` 核对，覆盖受光、被遮挡和屏外地板样本，不复用 GPU BVH 或卡片查询。

新增空间质量入口单独关闭时域，用 8 个独立 seed、每探针 256 条射线建立离线参考，再检查另外 6 个 seed 的 16 射线结果：比较平面内部亮度误差、16×16 宽区块误差以及独立识别的几何边界误差。还检查强度为零时等于原始结果、无光无能量、光强翻倍线性、不可读取射线不被填充，以及状态、命中和方向元数据完全不变。自适应覆盖在三个视角下比较均匀与补充探针的真实几何缺口，分别要求薄柱和球面拥有有效支持；缺少兼容探针的像素和背景不因过滤获得虚构能量。探针对的防漏光检查使用实际准备网格及实例变换，以双精度逐三角形求交独立验证每个被接受的配对，不复用任何 BVH 遍历；对照关闭可见性检查时确实穿过遮挡的配对。最后检查空间输出进入时域，以及修改过滤参数会清空旧历史。

### 2026-10-16：空间过滤验证与开销（相机运动调参前）

2026-10-16 最终空间回归在 RTX 4060 Laptop 的 Vulkan validation 与本机 Intel OpenGL 均通过，显式软件追踪。下表列 Vulkan 结果。噪声误差测试关闭时域与最终图像 AA，比较线性 HDR GI；参考只在测试端由独立 seed 的高射线结果构成。256×192 的间距 24 是粗网格压力配置，另测间距 8，使屏幕探针数与 1280×800、间距约 40 的查看器接近。它不等同于在实际 1280×800 分辨率运行，也不覆盖所有 SDF / 卡片共同偏差。

| 指标 | 未过滤 | 空间过滤 | 变化 |
| --- | ---: | ---: | ---: |
| 粗网格平面像素亮度 MSE | 1.06050e-4 | 9.86998e-5 | 下降 6.93% |
| 粗网格 16×16 区块平均亮度 MSE | 9.23442e-5 | 8.82018e-5 | 下降 4.48% |
| 粗网格几何边界亮度 MSE | 1.45041e-4 | 1.43145e-4 | 下降 1.31% |
| 较密网格平面像素亮度 MSE | 8.82197e-5 | 4.44170e-5 | 下降 49.65% |
| 较密网格 16×16 区块平均亮度 MSE | 4.70363e-5 | 3.20082e-5 | 下降 31.95% |

较密网格使用 8 个独立参考 seed、每探针 256 条射线，评估另外 12 个 seed、每探针 16 条射线。相对参考误差和宽区块误差必须分别下降，边界误差另设上限；不以更换 seed 或放宽几何阈值代替算法修正。时域与空间的降低噪声作用不能直接相加为绝对画质收益：时域主要减小随帧变化的误差，空间混合引入邻域近似，仍会保留真实的几何亮度变化，并可能带来小量偏差。

同一优化 Debug 构建、RTX 4060 Laptop、1280×800、Vulkan validation、间距 32、每探针 64 射线，预热后各测 12 帧，不呈现、不限帧、不回读图像。表中是整条管线的开关对照，包含射线方向相位等变化，不能把批次差值完全等同于隔离的过滤着色器耗时。

| 场景 / 配置 | GPU ms | 含提交与等待的整帧 ms |
| --- | ---: | ---: |
| 房间：空间关、时域关 | 19.70 | 24.18 |
| 房间：仅空间过滤 | 21.22 | 27.31 |
| 房间：仅时域累积 | 21.34 | 26.24 |
| 房间：空间过滤 + 时域 | 22.31 | 27.42 |
| 茶壶宴会：空间关、时域关 | 40.66 | 46.68 |
| 茶壶宴会：仅空间过滤 | 42.96 | 49.14 |
| 茶壶宴会：仅时域累积 | 43.48 | 49.84 |
| 茶壶宴会：空间过滤 + 时域 | 46.01 | 54.16 |

单独开启空间过滤的 GPU 批次差值约为房间 1.52 ms、茶壶宴会 2.31 ms。默认 3 轮过滤对应统计中的 6 个空间 pass：1 次探针对几何 / 可见性表、3 次方向辐射率过滤、1 次过滤后探针积分、1 次过滤后像素重建。配对可见性不是对每条角度射线重复发射；原始积分和未过滤 GI 另行保留用于诊断。射线预算仍为 576,000 条的预留容量上限，过滤不增加主 GI 追踪射线数。日志为 `build/surface_cache_spatial_perf_room.log` 与 `build/surface_cache_spatial_perf_banquet.log`，最终时空组合效果图为 `build/surface_cache_spatial_room_final.png`、`build/surface_cache_spatial_banquet_final.png`。默认房间和宴会整帧时间仍未达到稳定 60 FPS，不能只用 GPU 时间倒数当作交互帧率。

三个视角的覆盖压力测试（间距 64、每格候选 16）中，真实缺口由 34,501 个降为 644 个；默认间距 32、候选 8 时，147,456 个有效接收像素中的缺口由 15,662 个降为 525 个，减少约 96.6%，剩余约占有效像素的 0.36%，但没有归零。这仍是 256×192 的测试图像，并非 1280×800 查看器的实时缺口数量或活动探针数。默认三个测试视角合计实际活动基础探针 144 个、自适应探针 283 个，这是活动数而非预留容量。独立逐三角形可见性核对中，关闭配对可见性检查时存在 9 个穿过遮挡的配对；开启后为 0，同时保留了 244 个被独立证实可见的配对。552 条活动但不可读取的射线保持原样，元数据误差与光强加倍误差均为零。较密网格的一帧中，12,833 条可读射线有 4,427 条被空间邻域改变。日志与覆盖图见 `build/surface_cache_spatial_vulkan.log`、`build/surface_cache_spatial_opengl.log`、`build/surface_cache_spatial_coverage_default.png`。这些有限场景的检查不等于任意场景下绝无漏光或无覆盖缺口。

加入空间环节后，Vulkan 原有时域、探针与逐像素参考回归继续通过，分别见 `build/surface_cache_spatial_temporal_regression.log`、`build/surface_cache_spatial_probe_regression.log`、`build/surface_cache_spatial_reference_regression.log`。这些旧入口显式关闭空间过滤，保留独立验证职责：时域稳定区域 49,071 个像素的时间方差由 `2.93636e-4` 降至 `4.20130e-6`，均值能量误差约 0.084%；探针测试实际活动 176 个（其中自适应 96 个），11,264 条射线的积分最大误差约 `1.47e-7`，接收面反射率只乘一次、16 / 64 / 256 射线收敛继续成立；参考路径的 72 张卡片、光强线性和 Mesh / Global 分阶段补追也通过。这些分别是未过滤的既有路径验证，不将其收益再叠加到空间过滤的参考误差百分比上。

分阶段追踪分别运行 Mesh-only 与 Global-only 两组：先关闭 Surface Cache 记录原本不可读取的几何命中，再开启缓存，要求原来的 11 / 12 状态变为 9 / 10，保留相同世界命中位置，且源像素坐标为 -1（从卡片读取）。测试还要求实际屏外命中得到补充，并保持未命中、覆盖缺失、步数耗尽和非法起点等状态的分类。

探针质量入口读取实际 GPU 射线方向，以独立解析积分核对余弦分布的均值、二阶与四阶矩、球冠概率和切向对称性；固定 45° 圆锥会直接失败。还核对 `E/PI` 与实际射线辐射率的积分、已知黑色分母、自适应探针对最终图像的实际贡献、光强线性、固定 seed 的 A→B→A 无历史依赖、角度分辨率与空间间距对应的射线调度，以及缓存视图不追踪无用 GI 射线。

对条带与宽光斑另做量化收敛检查：固定相机、表面探针位置和光照，用 8 个独立 seed、每探针 256 条射线的结果在测试端形成参考，再用另外 4 个 seed 分别测量 16 / 64 / 256 条射线。检测平面上的探针入射光误差、最终 HDR 像素亮度误差，以及分表面的 16×16 区块平均误差，要求随着射线增加而下降。区块统计只用于离线误差分析，不写回渲染器；参考消除了真实几何亮度渐变对“光斑”的混淆，但无法证明所有档位共同存在的 SDF、卡片或重建偏差已经消除。

时域质量入口读取实际 raw GI、累积 GI 和每像素累计帧数，检查方向相位逐帧改变、静止方差降低、帧数上限、单帧模式、相机切换与重新露出表面的拒绝、正常平移的历史复用，以及光照、几何、参数、分辨率和模式切换后的历史失效。比较在最终曝光和色调映射之前进行，主对照检查关闭最终图像 TAA；另有两帧显式打开 TAA，验证其相机抖动不会清除独立的 GI 历史。

2026-10-16 首次时域版本的 Vulkan 回归（相机运动调参前，256×192 小房间、每探针 16 条射线、8 帧历史 / 8 个方向相位）中，48,456 个稳定接收像素的逐帧 RGB 方差由 `0.000361598` 降为 `0.000004925`，下降约 98.64%。这是同一表面上随方向相位变化的时间方差，不代表相对于真实光传输的绝对误差下降同样比例。相机平移 0.12 世界单位后，47,617 个像素复用了通过几何检查的历史，817 个像素从一帧重新开始。原有关闭时域的探针 / 卡片质量测试继续作为单帧对照。

Vulkan 与 OpenGL 的完整时域回归均通过，稳定区域的平均能量相对同批原始方向相位均值偏差约 0.09%。测试另覆盖聚光灯位置与方向改变、接收面材质改变时清空历史，以及 TAA 相机抖动下独立 GI 历史的复用；关闭时域后的累计帧数视图不调度无用探针射线。记录见 `build/surface_cache_temporal_vulkan.log`、`build/surface_cache_temporal_opengl.log`，单帧回归见 `build/surface_cache_temporal_probe_regression.log`、`build/surface_cache_temporal_reference_regression.log`。

以下为 2026-10-16 首次时域版本、相机运动调参前的历史计时，不替代当前默认参数的运行测量。同一优化 Debug 构建、RTX 4060 Laptop、1280×800、Vulkan validation、显式软件追踪，间距 32 像素、每探针 64 条射线，各 12 个无呈现/不限帧/无图像回读的计时帧：

| 场景 | 关闭时域 GPU / 整帧 ms | 开启时域 GPU / 整帧 ms |
| --- | ---: | ---: |
| 红绿墙房间 | 15.52 / 20.74 | 17.49 / 23.52 |
| 茶壶宴会 + Clipmap | 30.22 / 38.16 | 31.23 / 40.24 |

这是整条管线的开关对照，时域模式也会更换射线相位，差值不是完全隔离的时域着色器耗时。新增四个全屏 pass，不增加每帧探针射线预算。历史采用双缓冲颜色/计数、世界位置、法线/深度及一个可读覆盖输出，共七张 RGBA32F 纹理；1280×800 时约 109 MiB，调整最大累计帧数不会按帧数增加纹理数量。整帧结果仍未达到稳定 60 FPS，物体动态更新仍有现有 CPU 几何/SDF 更新成本。性能记录为 `build/surface_cache_temporal_perf_room.log`、`build/surface_cache_temporal_perf_banquet.log`，默认模式截图为 `build/surface_cache_temporal_{room,banquet}_final.png`。

性能入口预热后分别测量无 GI、16 / 64 / 256 条探针射线、64 条射线额外启用时域、较密的 16 像素探针间距、逐像素参考的 1 / 4 / 8 条射线、缓存视图与动态对象更新。每个静态项目测量 12 帧，批次内不呈现、不限帧、不回读图像；GPU 时间和包含提交等待的整帧时间分别记录。加上 `--sdf-clipmaps` 可测量茶壶宴会，不能把房间结果代替高精度模型负载。`surfaceCacheRays` 和 `surfaceCacheProbeRayBudget` 是含非活动预留槽的配置上限；实际有效探针与射线数需通过诊断回读单独统计。

## 历史：标量自适应探针的光斑收敛记录

以下为早期标量积分与旧覆盖分配的采样收敛记录，保留作实现演进对照，不代表当前实际位置核和方向模型的画质收益。

`build/surface_cache_probe_vulkan.log` 的 320×240 房间测试中，101 个有效探针包含 21 个自适应探针，共发射 6,464 条有效射线（默认每探针 64 条）。实际 GPU 方向的 `E[cos(theta)]=0.66718`、`E[cos²(theta)]=0.500166`、`E[cos⁴(theta)]=0.333363`，分别符合余弦 PDF 的 `2/3`、`1/2`、`1/3`；不存在所有射线固定在 45° 的圆锥。积分最大误差为 `1.24e-7`。保持缓存入射光不变、将接收地板反射率减半后，26,618 个地板像素的间接光恰好减半，探针入射积分保持不变。

固定位置，针对 69 个平面探针、58,788 个墙壁/天花板/地板像素和 287 个分表面的 16×16 区块，以独立 seed 的离线高采样参考测得：

| 每探针射线 | 探针 RGB 均方误差 | 平面 HDR 亮度均方误差 | 区块平均亮度均方误差 |
| --- | ---: | ---: | ---: |
| 16 | 6.82550e-4 | 1.27535e-4 | 1.27382e-4 |
| 64 | 1.26987e-4 | 2.79871e-5 | 2.70075e-5 |
| 256 | 1.99783e-5 | 3.69185e-6 | 3.82548e-6 |

在该场景与参考下，64 条射线相对 16 条的宽区块误差下降约 79%，256 条相对 64 条再下降约 86%。这是多个 seed 的统计收敛结果，不表示每一个 seed、每一个像素都会单调改善，也不证明卡片分辨率和几何覆盖的共同误差已经消失。测试端形成的参考和区块统计不参与交互渲染，未加入时空过滤。

Vulkan（RTX 4060 Laptop，validation、显式软件追踪）和 OpenGL（Intel）均通过以上探针回归，包括冻结位置后改变相机时强制重建屏幕格子。逐像素参考、旧 Mesh / Global SDF 追踪与三项 CPU 回归也通过。记录见 `build/surface_cache_probe_{vulkan,opengl,reference_regression,legacy_sdf}.log`。

## 先前无过滤探针版本性能记录

同一优化 Debug 构建、RTX 4060 Laptop、1280×800、Vulkan validation、显式软件追踪；静态批次各 12 帧，未呈现、限帧或回读图像：

| 路径 | 房间 GPU / 整帧 ms | 茶壶宴会 GPU / 整帧 ms |
| --- | ---: | ---: |
| 直接光，无探针和 GI | 1.34 / 4.26 | 3.41 / 7.58 |
| 16 射线/探针，间距 32 | 10.97 / 16.72 | 11.29 / 18.07 |
| 默认 64 射线/探针，间距 32 | 16.05 / 21.28 | 25.59 / 32.61 |
| 256 射线/探针，间距 32 | 40.65 / 46.20 | 82.66 / 89.35 |
| 64 射线/探针，间距 16 | 42.70 / 48.42 | 64.77 / 71.87 |
| 逐像素参考，1 射线 | 12.75 / 17.51 | 19.36 / 25.98 |
| 逐像素参考，4 射线 | 40.85 / 45.46 | 63.84 / 70.62 |
| 逐像素参考，8 射线 | 74.38 / 78.92 | 118.99 / 125.36 |
| 卡片光照查看，无 GI 射线 | 2.29 / 6.62 | 3.47 / 9.59 |

默认探针配置的 GPU 耗时约为 8 射线逐像素参考的 22%，但两者是不同的空间/角向近似，不能称作等质量加速；默认探针结果也仍有有限采样方差。整帧结果说明当前默认房间与茶壶宴会尚未达到稳定 60 FPS。当前四 texel 射线诊断布局会重复执行求交，固定候选槽也保留了无效容量，后续可通过紧凑调度和多输出/计算着色器降低开销。

动态单帧（64 射线/探针）房间为 17.68 / 99.57 ms，宴会为 27.21 / 305.00 ms，包含几何、SDF、上传、卡片重打光；卡片 CPU 更新分别为 0.14 / 3.70 ms，不代表稳定移动平均。静态帧卡片更新计数均为零。完整记录见 `build/surface_cache_probe_perf_room.log` 与 `build/surface_cache_probe_perf_banquet.log`，截图为 `build/surface_cache_probes_{room,banquet}_final.png`。

## 先前逐像素参考版本的验证与性能记录

五项 CPU 回归通过。Vulkan（显式软件追踪、validation）和 OpenGL 的完整新 GI 测试通过：72 张卡片、46,824 个有效捕获样本；32 个受光、32 个阴影、28 个离屏地板参考样本的最大直射光误差为 `1.52e-8`。原始 GI 光强翻倍误差和 A→B→A 历史依赖误差均为零。Mesh-only 与 Global-only 各有 241 条原本不可读取的命中从卡片读到光照，合计 372 次位于屏幕外；世界命中位置保持不变。有效黑色、缺失覆盖、动态更新也通过。

日志：`build/surface_cache_cpu_regressions.log`、`build/surface_cache_quality_vulkan.log`、`build/surface_cache_quality_opengl.log`。OpenGL 在本机 Intel 显卡上验证，性能表仅使用 NVIDIA Vulkan 测量。旧 SDF 软件补追回归与 PBR GPU 回归也通过，日志为 `build/surface_cache_legacy_sdf_vulkan.log`、`build/surface_cache_pbr_regression.log`。

同一优化 Debug 构建（`/O2 /Ob2`，Debug CRT）、RTX 4060 Laptop、1280×800、Vulkan validation、禁用原生光追：

| 路径 | 房间 GPU / 整帧 ms | 茶壶宴会 GPU / 整帧 ms |
| --- | ---: | ---: |
| 直接光，无 GI | 1.03 / 5.55 | 2.26 / 11.27 |
| 原始 GI，1 条射线/像素 | 15.59 / 22.38 | 17.49 / 34.42 |
| 原始 GI，4 条射线/像素 | 33.76 / 44.24 | 60.23 / 77.08 |
| 原始 GI，8 条射线/像素 | 65.02 / 75.06 | 117.99 / 132.80 |
| 卡片光照查看，无 GI 射线 | 1.82 / 10.34 | 2.68 / 16.97 |
| 动态更新一次，8 条射线/像素 | 64.71 / 137.71 | 109.32 / 301.85 |

日志：`build/surface_cache_perf_room_final.log`、`build/surface_cache_perf_banquet_final.log`。静态时卡片光照更新数均为 0；房间/宴会常驻卡片容量分别为 73,728 / 98,304 texels。动态项只有一帧，包含几何 refit、上传、卡片更新、全部卡片重打光和 8 条射线的积分，不代表稳定移动平均；单独卡片 CPU 更新约 1.45 / 4.27 ms。计数器 `surfaceCacheRays` 是视口像素数乘采样数的配置上限，背景像素会在着色器中提前退出。

这些先前数值说明数据结构与离屏读取已经可用，缓存查看成本较轻，但逐像素软件追踪参考路径没有达到完整实时 GI 的性能目标。当时默认采用 1 条射线方便交互；4/8 条用于质量检查。当前默认已将昂贵的光照射线调度到屏幕探针，当前性能见上节，不能沿用这份旧表。本阶段未用降低 SDF/模型精度或时空滤波掩盖此开销。

卡片直接光支持方向光、点光、聚光和矩形面积灯；天空漫反射尚未接入缓存，开启该组合会给出明确校验错误。主画面点光的直射仍继承现有 PBR 路径，独立阴影尚未接入；默认太阳场景使用 CSM，新密闭陶瓷场景为矩形灯单独启用缓存深度图软阴影，卡片中的支持光源则使用软件阴影射线。六向单层卡片、固定页和完整 BVH refit 也是后续优化项，不等同于 UE 的虚拟分页与调度实现。
