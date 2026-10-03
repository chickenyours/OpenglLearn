# 通用 RHI 帧渲染接口

这一版 `Module/Render` 保持为引擎底层通用模块。RHI 不包含区块、体素、地形、LOD 或材质业务类型；上层渲染器只需把自己的可见物转换为通用的资源绑定和 draw 命令。

## 提供的能力

- `BeginFrame` / `EndFrame`：帧边界、默认帧缓冲尺寸、颜色/深度/模板清理和呈现。
- `RHIFrameEncoder`：校验 Begin → Record → End 顺序的通用录制接口。
- `SetViewport` / `SetScissor`：视口与裁剪状态。
- `PipelineSpec`：拓扑、剔除、正面方向、线框、深度比较/写入和透明/加法混合。
- `CreateMeshBufferDesc` / `UpdateMeshBufferDesc`：创建或原位刷新顶点和可选的 16/32 位索引；支持 Static、Dynamic、Stream usage。
- `BindTexture`：按纹理槽绑定 2D 纹理。GLSL 可使用显式 `layout(binding = N)`。
- `UniformBufferSpec`：动态 UBO 创建、帧内小块更新和 binding point 绑定。
- `Draw` / `DrawIndexed`：带范围、base vertex 和 instance count 的通用绘制。
- 所有异步上传命令自行持有上传数据，调用者返回后即可释放临时数组。
- 句柄带版本号；删除 mesh、UBO、texture 和 pipeline 后旧句柄失效。

## 通用录帧示例

```cpp
Render::RHICommand::BeginFrame begin{};
begin.frameIndex = frameIndex;
begin.framebufferWidth = width;
begin.framebufferHeight = height;
begin.clearColor = glm::vec4(0.45f, 0.68f, 0.92f, 1.0f);

auto frame = device->BeginFrame(begin);
frame.UpdateUniformBuffer(frameUbo, frameConstants);
frame.BindUniformBuffer(frameUbo, 0);

for (const DrawPacket& packet : visiblePackets) {
    frame.BindPipeline(packet.pipeline);
    frame.BindMesh(packet.mesh);
    frame.BindTexture(packet.texture, 0);
    frame.UpdateUniformBuffer(drawUbo, packet.constants);
    frame.BindUniformBuffer(drawUbo, 1);
    frame.DrawIndexed({
        .indexCount = packet.indexCount,
        .firstIndex = packet.firstIndex,
        .baseVertex = packet.baseVertex,
        .instanceCount = packet.instanceCount
    });
}

frame.End();
device->async_SubmitFrameCommands(frame.GetCommandBuffer());
```

`UpdateUniformBuffer` 的单条帧命令最多内联 256 字节，适合相机、光照和单次 draw 常量。更大的、低频变化的数据应通过纹理或独立 GPU buffer 资源上传。

## 沙盒地形作为验收场景

沙盒/体素地形在上层可按以下方式映射，不需要污染 RHI：

| 上层概念 | 通用 RHI 能力 |
|---|---|
| 网格化后的可见面 | indexed mesh buffer |
| 区域流入/流出 | 异步创建/删除 mesh handle |
| 相机、雾、太阳光 | frame UBO（binding 0） |
| 每次绘制的位置和参数 | draw UBO（binding 1） |
| 方块纹理图集 | texture slot |
| 不透明、裁切、半透明批次 | 不同 PipelineSpec + 上层排序 |
| 重复物件 | instanceCount |
| 调试碰撞面或网格 | PolygonMode::Line |

可见性剔除、距离排序、区块合批、贪心网格、LOD 选择和地形生成仍属于上层 renderer/world 系统。RHI 只执行已经准备好的通用 draw packet。

## 线程与生命周期

资源创建、删除和帧提交可以从任意生产线程进入队列，OpenGL 调用只发生在持有上下文的渲染线程。完成回调由 `Render::System::CallBackSystem::OnTick()` 拉回调用线程。关闭模块时会先排空已经排队的命令，然后在渲染线程释放上下文。

## 积压时的调度

- 每轮每种资源命令最多处理 8 条；未消费的批次保留在消费端，同类型命令保持 FIFO。上传字节在队列之间移动，不再额外复制。
- 每条资源命令执行后检查帧队列，包含本轮开始后新提交的帧。帧内部命令连续执行；资源积压不再要求帧等待整个批次。
- 主线程每次最多执行 64 个完成回调，时间预算为 1 ms，使用 `try_pop`，不等待生产者，也不追赶不断新增的回调。
- 资源依赖必须通过完成回调建立，帧只能引用已经创建完成的资源；不同类型队列不提供全局 FIFO。
- 单条后端调用、完整帧和单个回调不可抢占，因此预算不等于严格的帧耗时保证。

地形侧每帧最多提交 4 个 layer 操作、4 MiB 上传数据，提交时间预算为 2 ms，同时最多保留 16 个尚未回调的上传。单个超大 mesh 允许独占一次预算，避免永久无法上传。生成和网格化各自每帧最多接收 4 个结果、调度 4 个任务，在途任务各最多 8 个；卸载每帧最多 8 个区块。CPU 重任务在配置好的 ECS worker pool 上执行，未配置 worker pool 时仍保留测试使用的同步回退。

回归验证：`render_scheduling_test` 使用记录后端验证积压中途到达的帧、分批队列 FIFO 和回调预算；`terrain_pipeline_test` 包含延迟回调、在途背压、积压排空及超大 mesh 上传测试。这些测试不测量真实 GPU 帧率。

## 防止旧帧积压与绘制期间同步

`terrain_render_demo` 使用 `async_SubmitLatestFrameCommands` 提交完整场景快照：渲染线程正在执行的帧不受影响，尚未执行的快照最多保留一个，新快照替换旧快照并回收其命令缓冲。相机不再等待历史帧逐一呈现。此接口的完成回调表示“执行完或被替换后已释放”，不保证实际呈现；不要在可替换帧中放置必须执行的资源操作或依赖上一帧的增量更新。原 `async_SubmitFrameCommands` 仍然按 FIFO 可靠执行，语义不变。

OpenGL 完整覆写 UBO 时先 orphan 存储，再上传数据，避免逐个 draw 覆写同一 object UBO 时等待前一个 draw 读取完。部分更新保留原有数据。原理参考 [Khronos Buffer Object Streaming](https://wikis.khronos.org/opengl/Buffer_Object_Streaming)；驱动分配、单次上传与 swap 本身仍可能耗时。

地形提取使用相机视锥保守剔除整个区块柱，减少视野外 draw。区块卸载和 MeshUploadSystem 停止时释放 GPU 网格，同时取消尚未回调的创建；晚到的创建结果会立即销毁，避免移动过程中 GPU 资源持续增长。uploader 服务必须活到上传回调处理结束。后台结果队列只在锁内移动指针，主线程取结果使用 try-lock，区块数据复制和旧 mesh 释放移至锁外。

窗口标题显示实际执行帧的 FPS、主线程本次 CPU 时间、最近帧排队时间、渲染线程执行（含 present）时间及被替换帧数。`GetFrameStats()` 的执行时间是 CPU 墙钟时间，不是 GPU timestamp；这些统计也不是 GPU fence。

`terrain_render_demo --smoke-test` 创建隐藏窗口，自动移动相机运行 10 秒，不捕获鼠标，退出时输出帧数、缓冲数量、采样峰值和 GL 错误数量。2026-09-24 本机 Debug 构建实测：639 帧、替换 7 帧、2 个命令缓冲、采样 CPU/排队/执行峰值 11.21/16.08/7.03 ms、GL errors=0。隐藏窗口短测不能替代可见窗口长时间游玩测试。

新增回归覆盖 1000 个快照积压时只执行最新帧、回调恰好一次和缓冲复用，UBO 全量/部分更新路径、六个视锥面的剔除，以及创建回调前后卸载资源的两种情况。

旧的 `SetBackgroundColor`、`Flip`、`async_CreateVertexBuffer` 和直接获取 command buffer pool 的用法保留，便于已有代码逐步迁移。

## 上层材质系统（第一阶段）

材质是当前 Render 模块的上层子系统，没有新增 IModule，也没有新增材质专用 RHI
命令。原来的 `RenderItem.pipeline/texture`、资源创建和帧提交接口继续可用。
`RenderItem.material` 非空时优先使用材质快照；Terrain 的
`RenderSettings.materials` 也遵循相同的优先规则。

### 数据与线程边界

- `Public/Material/material.h`：不依赖 GL 的模板/schema、参数布局、不可变资产和实例。
- `Public/Material/material_runtime.h`：已完成 GPU 资源的绑定描述、不可变快照、
  可注入 ECS Context 的 `MaterialService`。服务和实例编辑属于主线程。
- `Public/Material/unlit_material.h`：Surface/Sprite 的 Unlit 模板、GLSL、顶点布局和
  PipelineSpec。两者共享参数与纹理协议，保留不同的顶点和深度/混合状态。
- `Private/Material/rhi_material_resources.h`：GPU 资源所有权转移和异步回收适配器。

`MaterialTemplate` 定义参数名、类型、默认值、范围、编辑分组、显示名称和逐对象覆盖权限；
`MaterialAsset` 保存一组不可变默认参数；`MaterialInstance` 保存独立可修改的参数。
`MaterialService` 持有实例，ECS 可以只保存带 generation 的 `MaterialHandle`。
`Capture()` 复制确定版本的值与绑定，后续编辑、替换或删除实例均不改变已提取的帧。
未修改的实例重复 Capture 会复用同一个快照；带 draw override 的 Capture 不修改实例。

当前支持 Float、Int、Bool、Vec2/3/4、Mat4，按 std140 打包，最大 256 字节。
Bool 编码为 uint32，GLSL 中用 uint 并通过 `!= 0u` 读取。Vec3 保留 16 字节，
请使用 `GenerateGLSLUniformBlock()` 自动生成匹配的 padding，避免手写布局分歧。
数值类型、非有限值、范围、重名、未知参数及未经允许的逐对象覆盖均会返回错误。
无参数的纯纹理模板不需要材质 UBO。

### 接入示例

先通过现有 RHI 异步接口创建 Shader、Pipeline、纹理和相同大小的参数 UBO；
必须等完成回调后再注册材质。`IsValid()` 不是 GPU 就绪检查。
下例中的句柄均来自已经成功执行的完成回调：

```cpp
using namespace Render::Material;
auto schema = MakeUnlitTemplate(Domain::Surface);
auto asset = MaterialAsset::Create(schema);

OwnedMaterialResources owned;
owned.pipelines = {pipelineHandle};
owned.uniformBuffers = {materialUbo}; // schema->ByteSize()，Unlit 为 48
owned.programs = {programHandle};
owned.sources = {vertexSourceHandle, fragmentSourceHandle};
owned.textures = {whiteTexture};
auto lifetime = RetainMaterialResources(device, std::move(owned));

MaterialPassResources bindings;
bindings.expectedTemplate = schema; // 编译该 Pipeline 使用的同一份 schema
bindings.pipeline = pipelineHandle;
bindings.parameterBuffer = materialUbo;
bindings.parameterBufferBytes = static_cast<uint32_t>(schema->ByteSize());
bindings.textures = {{0, whiteTexture}};
bindings.lifetime = lifetime;

MaterialService materials;
std::string error;
auto handle = materials.Register(asset, bindings, &error);
if (!handle.IsValid()) { /* report error */ }

Render::RenderItem item;
item.mesh = meshHandle;
item.material = materials.Capture(handle, {
    {"baseColor", glm::vec4(1, 0, 0, 1)}
}, &error);
if (item.material) renderWorld.Add(std::move(item));
```

一个 GPU 句柄只能转移给一个 owner。多个材质共享同一个 lifetime；共享纹理也可以用
独立 owner，再加入各材质 owner 的 `dependencies`。调用方不得再直接删除已转移的句柄。
required 纹理缺失会拒绝发布；调用方可显式提供白色或其他默认纹理。optional 纹理缺失会
录制解绑，Shader 必须自行支持这种路径，不能无条件采样未绑定纹理。

View/Object/Material UBO 的标准 binding 为 0/1/2。
第一阶段材质 UBO 固定 binding 2，纹理槽限定 0..15。发布时同时校验 expectedTemplate，
防止两个同字节数、不同参数布局的模板误用同一 Pipeline；这仍是调用方声明，不能代替
未来的驱动反射校验。SourceInstanceRevision 仅供诊断，不是快照唯一标识或缓存键。
内置 Unlit 使用纹理槽 0，参数为 baseColor、uvTransform（xy 缩放、zw 偏移）、alphaCutoff。
Surface 顶点为 position(vec3)、uv(vec2)、color(vec4)，Sprite 顶点 position 为 vec2。
逐精灵 tint 可以保留在顶点 color 中，避免因对象颜色不同而拆分批次。

### 排序与生命周期

ForwardRenderPipeline 在每个 RenderLayer 中先处理按场景排序的项目，再处理按提交顺序
绘制的项目。Sprite 域或 `RenderOrder::Submission` 自动保留组内顺序，A/B/A 材质序列
不会重排为 A/A/B。不透明 Surface 沿用现有状态排序，透明 Surface 沿用深度排序。
此层没有新增几何合批器；现有 IWanna 的 atlas/CPU batch 路径保持可用。

录制时每个快照通过通用 `RHIFrameEncoder::KeepAlive` 固定到 command buffer，
执行完、Latest 替换或 Cancel 后才释放。每一帧都包含完整材质参数更新，不能依赖可能
被替换的前一帧。材质快照只保证其声明的材质资源所有权；mesh、View/Object UBO
仍需由调用方管理，保证使用期间有效。

放弃未提交帧时调用 `encoder.Cancel()`；结束时先停止生产任务，清空服务、RenderWorld
中的未消费快照和调用方的 lifetime 引用，再 `device.StopAndRelease()`。
设备会回收遗留的未提交帧，排空已提交帧及其触发的删除命令。device/context 必须活到
所有 owner 释放；不要在设备销毁后释放包含其引用的材质 owner。
`RenderFrameService.recordSucceeded` 报告 ECS 管线录制是否成功，失败时应取消该帧。

`MaterialService::Replace` 只在候选资产和绑定校验成功后替换，失败保留旧材质；
这是发布/回退基础，目前没有文件监视、异步 Shader 编译服务、自动反射或 Shader Graph。
第一阶段也未迁移旧 ResourceManager，未加入 PBR、RenderTarget 或 Inspector。

### 兼容性补充与验证

RHI 仅增加通用能力：`async_DeleteShaderSource/ShaderProgram`、`KeepAlive`、`Cancel`、
`UpdateUniformBufferBytes`。帧命令编号和结构没有改变；Bytes 更新复制数据至原来的
256 字节命令，原有模板版本 `UpdateUniformBuffer<T>` 保留。其他后端若需要回收 Shader，
应实现两个新增删除方法；默认空实现用于保持派生类的源码兼容。

构建/运行：

```powershell
cmake --build build --target material_data_test material_runtime_test material_render_smoke terrain_pipeline_test render_scheduling_test rhi_frame_command_buffer_test terrain_render_demo
ctest --test-dir build --output-on-failure -R '^material_(data|runtime)_test$'
.\bin\material_render_smoke.exe
.\bin\terrain_pipeline_test.exe
.\bin\render_scheduling_test.exe
.\bin\rhi_frame_command_buffer_test.exe
.\bin\terrain_render_demo.exe --smoke-test
```

CPU/记录后端覆盖参数布局与隔离、失败发布、旧句柄、透明 A/B/A 顺序、纹理解绑、
共享 UBO 值恢复、Latest 帧自包含、执行/替换/取消/停止时的保活与重入回收。
`material_render_smoke` 使用隐藏 OpenGL 窗口编译真实 Surface/Sprite Shader 并读回像素；
它单独运行，不要求无窗口单元测试环境具备 GPU。Terrain demo 的地形和水面已经通过
材质 UBO 使用颜色、透明度/Fresnel 等参数，默认值沿用此前画面。

## HDR 材质管线与渲染目标

新增的 `MaterialRenderPipeline` 调度四级 CSM、平面反射、SSAO、HDR Forward、
多级 Bloom、色调映射和屏幕滤镜。实现与完整资源契约见
[README_MATERIAL_PIPELINE.md](README_MATERIAL_PIPELINE.md)，交互验证入口为
`material_lab_demo`。

RHI 增补 RGBA16F / Depth32F、可关闭 mipmap 的空纹理、RenderTarget 资源和
`SetRenderTarget`（追加帧命令 ID 15，旧 ID 不变）。切换目标设置 viewport，
重置 scissor/sRGB 和绘制状态缓存；FBO 与附件借用关系、帧保活和释放顺序由
`render_target_test` 验证。原 `BeginFrame` 默认 framebuffer 与直接 Forward 路径继续保留。
