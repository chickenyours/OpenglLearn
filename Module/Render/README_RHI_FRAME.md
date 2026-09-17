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

旧的 `SetBackgroundColor`、`Flip`、`async_CreateVertexBuffer` 和直接获取 command buffer pool 的用法保留，便于已有代码逐步迁移。
