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
