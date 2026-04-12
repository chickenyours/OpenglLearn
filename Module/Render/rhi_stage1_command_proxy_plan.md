# 第一阶段改造方案：RHI 前端命令代理 + 专职渲染线程 + 委托后门

## 目标

本阶段的目标不是一次性做完整渲染架构，而是先把当前 **RHI“调用即执行”** 的模式，改造成 **“前端发布命令 -> 渲染线程顺序代理执行”** 的模式。

当前先采用如下约束：

1. **所有命令顺序提交、顺序执行**
2. **只有一个总的 `RHICommandQueue`**
3. **只有一个专职渲染线程**
4. **暂不做多队列、多线程并行翻译**
5. **暂不做复杂资源状态追踪**
6. **保留一个“委托后门”接口，允许直接写图形库 API 验证功能**

这个阶段的重点是先把边界划清楚，让后续渲染管线、资源系统、材质系统、快照系统都能在这个骨架上继续演化。

---

## 一、当前代码存在的问题

当前代码里的 `Device` 接口抽象层次还不够清晰：

- 前端调用接口和后端立即执行混在一起
- 上层一旦拿到对象，实际上就能间接触发底层 API
- `Buffer` / `Texture` / `InputLayout` 还是“对象方法驱动”
- OpenGL 后端直接在对象方法里执行 `gl*`

例如：

- `GLBuffer::Initialize / Update / Map / CopyTo` 直接执行 OpenGL
- `GLTexture::Create / SetData / Bind` 直接执行 OpenGL
- `GLInputLayout::Initialize / Bind` 直接执行 OpenGL

这意味着：

- 当前调用线程就是图形 API 执行线程
- 上层无法只做“命令发布”
- 后续无法自然插入专职渲染线程
- 很难做统一调度、批量执行、调试追踪

此外，当前 `TextureAsset` / `RHITexture` / `GLTexture` 的语义也没有完全分开，CPU 资源与 GPU 资源有混叠倾向，后续线程代理后容易让“谁拥有 GPU 对象、谁负责生命周期”变得混乱。

---

## 二、第一阶段总体设计

第一阶段固定采用三层：

### 1. RHI 前端层
给上层模块调用。

职责：

- 接收请求
- 分配逻辑句柄
- 组装命令
- 投递到命令队列
- 不直接执行图形库 API

### 2. 命令层
统一表示所有 RHI 操作。

职责：

- 定义命令类型
- 定义命令负载
- 进入统一队列
- 保证提交顺序

### 3. 后端执行层
只在专职渲染线程中运行。

职责：

- 持有图形上下文
- 查表找到真实后端对象
- 把命令翻译成 OpenGL 等后端 API
- 执行委托后门

---

## 三、第一阶段的核心原则

### 原则 1：不要“每个接口一个队列”
本阶段所有命令进入一个总队列：

- `RHICommandQueue`

不要按以下方式拆：

- 不要每个接口一个队列
- 不要每种资源一个线程
- 不要“渲染管线队列”和“资源上传队列”先分开

因为第一阶段的核心目标是 **先把“调用即执行”改成“命令代理执行”**，而不是做复杂调度系统。

---

### 原则 2：总队列不是“渲染管线队列”
不是只有渲染管线会发命令。

这些也可能发命令：

- 资源系统
- 材质系统
- ECS 渲染提取阶段
- 测试代码
- 初始化代码
- 调试代码

因此总入口应该叫：

- `RHICommandQueue`
- 或 `RenderThreadCommandQueue`

而不是“渲染管线队列”。

---

### 原则 3：前端只拿句柄，不拿真实后端对象
后续上层不应该直接接触：

- `GLBuffer*`
- `GLTexture*`
- `GLInputLayout*`

而应该只拿：

- `BufferHandle`
- `TextureHandle`
- `InputLayoutHandle`

真实后端对象只存在于渲染线程内部的资源表中。

---

### 原则 4：命令入队粒度可以细，但执行以“批”为单位
前端可以一条一条入队，但渲染线程每次醒来后，应批量取出多条命令顺序执行。

不要一条命令唤醒一次线程。

---

### 原则 5：先允许顺序执行，不做复杂优化
当前假设：

- 命令提交顺序 = 执行顺序
- 不做重排
- 不做并行翻译
- 不做复杂状态压缩
- 不做跨帧依赖管理

这样最适合第一阶段验证。

---

## 四、第一阶段的对象划分

---

### 4.1 `RenderThread`
专职渲染线程对象。

职责：

- 创建线程
- 持有图形上下文
- 维护运行循环
- 等待命令
- 批量执行命令
- 调用后端执行器
- 支持安全退出

建议接口：

```cpp
class RenderThread {
public:
    bool Start();
    void Stop();
    void WakeUp();

    bool Enqueue(RHICommand&& cmd);
    void DrainOnce();

private:
    void ThreadMain();

private:
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool running_ = false;
    bool stopRequested_ = false;

    RHICommandQueue queue_;
    std::unique_ptr<IRHIBackendExecutor> executor_;
};
```

线程循环建议：

1. 等待条件变量
2. 队列非空则批量取命令
3. 顺序执行
4. 没有命令则继续休眠
5. 收到停止请求则退出

---

### 4.2 `RHICommandQueue`
统一命令队列。

职责：

- 入队
- 批量出队
- 判断空/非空
- 当前阶段保证 FIFO

建议接口：

```cpp
class RHICommandQueue {
public:
    void Push(RHICommand&& cmd);
    void PopAll(std::vector<RHICommand>& out);
    bool Empty() const;

private:
    std::deque<RHICommand> commands_;
};
```

说明：

- 当前阶段单队列即可
- 后续若要升级，可在内部演化为多通道，但对外接口尽量保持不变

---

### 4.3 `RHIHandleAllocator`
逻辑资源句柄分配器。

职责：

- 为资源分配唯一逻辑句柄
- 句柄只代表“逻辑身份”，不代表真实后端对象已创建成功
- 后续可扩展 generation 防止悬挂句柄误用

建议句柄类型：

```cpp
using BufferHandle = uint64_t;
using TextureHandle = uint64_t;
using InputLayoutHandle = uint64_t;
```

更推荐：

```cpp
struct BufferHandle {
    uint32_t index = 0;
    uint32_t generation = 0;
};

struct TextureHandle {
    uint32_t index = 0;
    uint32_t generation = 0;
};

struct InputLayoutHandle {
    uint32_t index = 0;
    uint32_t generation = 0;
};
```

第一阶段也可以先简单用 `uint64_t`，后续再换。

---

### 4.4 `RHIBackendResourceTable`
后端资源表，只在渲染线程内使用。

职责：

- 保存逻辑句柄 -> 真实后端对象 的映射
- 创建成功后登记
- 销毁时删除
- 查询时返回后端对象

示意：

```cpp
class RHIBackendResourceTable {
public:
    void RegisterBuffer(BufferHandle handle, std::unique_ptr<GLBuffer> obj);
    GLBuffer* FindBuffer(BufferHandle handle);
    void RemoveBuffer(BufferHandle handle);

    void RegisterTexture(TextureHandle handle, std::unique_ptr<GLTexture> obj);
    GLTexture* FindTexture(TextureHandle handle);
    void RemoveTexture(TextureHandle handle);

    void RegisterInputLayout(InputLayoutHandle handle, std::unique_ptr<GLInputLayout> obj);
    GLInputLayout* FindInputLayout(InputLayoutHandle handle);
    void RemoveInputLayout(InputLayoutHandle handle);
};
```

注意：

- 这个表不暴露给上层
- 只允许渲染线程访问
- 这样可以避免多线程同步复杂度

---

### 4.5 `IRHIBackendExecutor`
后端命令执行器。

职责：

- 接收命令
- 操作资源表
- 调用真实 OpenGL API
- 执行委托后门

建议接口：

```cpp
class IRHIBackendExecutor {
public:
    virtual ~IRHIBackendExecutor() = default;
    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;
    virtual void Execute(const RHICommand& cmd) = 0;
};
```

OpenGL 对应：

```cpp
class GLBackendExecutor final : public IRHIBackendExecutor {
public:
    bool Initialize() override;
    void Shutdown() override;
    void Execute(const RHICommand& cmd) override;

private:
    RHIBackendResourceTable resources_;
};
```

---

## 五、命令系统设计

---

### 5.1 命令分类

第一阶段先分三类即可：

1. **资源命令**
2. **帧命令**
3. **委托命令**

---

### 5.2 资源命令

建议先覆盖当前已有能力：

#### Buffer
- CreateBuffer
- UpdateBuffer
- DestroyBuffer
- CopyBuffer

#### Texture
- CreateTexture
- UploadTexture
- DestroyTexture

#### InputLayout
- CreateInputLayout
- DestroyInputLayout

说明：

- 当前先不追求全量覆盖所有渲染状态
- 先把资源创建、更新、销毁链路跑通
- 这是第一阶段最稳的落点

---

### 5.3 帧命令

为了给后续渲染管线预留位置，建议先加：

- BeginFrame
- EndFrame

即使第一阶段内部先不做复杂逻辑，也保留命令种类。

这样以后扩展：

- BeginPass
- EndPass
- SetRenderTarget
- Draw
- Present

会更自然。

---

### 5.4 委托命令
这是你要求的“后门”。

用途：

- 允许测试代码直接写图形库 API
- 允许在过渡阶段用最短路径验证功能
- 允许暂未纳入正式 RHI 抽象的功能先跑通

但它有严格限制：

1. **只能在渲染线程执行**
2. **只能作为调试/验证/过渡机制**
3. **不能让上层长期依赖它替代正式 RHI**
4. **不能绕过资源表随意破坏正式资源生命周期**

---

## 六、命令结构建议

建议用枚举 + 负载结构体 + `std::variant`。

示意：

```cpp
enum class RHICommandType {
    CreateBuffer,
    UpdateBuffer,
    DestroyBuffer,
    CopyBuffer,

    CreateTexture,
    UploadTexture,
    DestroyTexture,

    CreateInputLayout,
    DestroyInputLayout,

    BeginFrame,
    EndFrame,

    ExecuteDelegate
};
```

负载示意：

```cpp
struct CreateBufferCmd {
    BufferHandle handle;
    Render::RHI::BufferDesc desc;
    std::vector<std::byte> initialData;
};

struct UpdateBufferCmd {
    BufferHandle handle;
    uint64_t offset = 0;
    std::vector<std::byte> data;
};

struct DestroyBufferCmd {
    BufferHandle handle;
};

struct CopyBufferCmd {
    BufferHandle src;
    BufferHandle dst;
    uint64_t size = 0;
    uint64_t srcOffset = 0;
    uint64_t dstOffset = 0;
};

struct CreateTextureCmd {
    TextureHandle handle;
    Render::RHI::TextureDesc desc;
};

struct UploadTextureCmd {
    TextureHandle handle;
    uint32_t mipLevel = 0;
    uint32_t xOffset = 0;
    uint32_t yOffset = 0;
    uint32_t zOffset = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 0;
    std::vector<std::byte> data;
};

struct DestroyTextureCmd {
    TextureHandle handle;
};

struct CreateInputLayoutCmd {
    InputLayoutHandle handle;
    Render::RHI::InputLayoutDesc desc;
};

struct DestroyInputLayoutCmd {
    InputLayoutHandle handle;
};

struct BeginFrameCmd {};
struct EndFrameCmd {};
```

然后统一封装：

```cpp
using RHICommandPayload = std::variant<
    CreateBufferCmd,
    UpdateBufferCmd,
    DestroyBufferCmd,
    CopyBufferCmd,
    CreateTextureCmd,
    UploadTextureCmd,
    DestroyTextureCmd,
    CreateInputLayoutCmd,
    DestroyInputLayoutCmd,
    BeginFrameCmd,
    EndFrameCmd,
    ExecuteDelegateCmd
>;

struct RHICommand {
    RHICommandType type;
    RHICommandPayload payload;
};
```

---

## 七、委托后门设计

---

### 7.1 设计目标

委托后门不是为了替代正式 RHI，而是为了：

- 快速验证 OpenGL 后端功能
- 在正式命令尚未补齐时，允许插入直接图形库调用
- 方便早期测试程序迁移到渲染线程中运行

即：

> 委托后门是“过渡验证通道”，不是长期架构主通道。

---

### 7.2 推荐形式

建议定义一个委托命令：

```cpp
struct BackendDelegateContext {
    void* backendExecutor = nullptr;
    void* nativeDevice = nullptr;
    void* nativeContext = nullptr;
};

using BackendDelegate = std::function<void(BackendDelegateContext&)>;

struct ExecuteDelegateCmd {
    BackendDelegate delegate;
};
```

然后在后端执行器里，当收到 `ExecuteDelegate` 时：

```cpp
void GLBackendExecutor::Execute(const RHICommand& cmd) {
    ...
    if (cmd.type == RHICommandType::ExecuteDelegate) {
        auto& data = std::get<ExecuteDelegateCmd>(cmd.payload);

        BackendDelegateContext ctx{};
        ctx.backendExecutor = this;
        ctx.nativeDevice = nullptr;
        ctx.nativeContext = GetCurrentGLContextPointer();

        data.delegate(ctx);
        return;
    }
    ...
}
```

---

### 7.3 使用方式示例

```cpp
frontend.EnqueueDelegate([](BackendDelegateContext& ctx) {
    glClearColor(0.2f, 0.3f, 0.4f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
});
```

这样可以确保：

- `gl*` 语句在渲染线程执行
- 不会在错误线程调用 OpenGL
- 早期功能验证非常方便

---

### 7.4 委托后门的限制建议

必须明确写进代码规范：

1. 禁止业务层长期依赖委托后门
2. 新增正式 RHI 能力后，应逐步替换对应委托代码
3. 委托内部尽量不要持久化保存后端对象裸指针
4. 委托只能假设自己运行在渲染线程
5. 委托造成的资源副作用，应尽量纳入资源表体系管理

---

## 八、前端接口建议

---

### 8.1 前端接口职责

前端接口做这些事：

- 分配句柄
- 复制必要数据到命令对象
- 把命令入队
- 必要时唤醒渲染线程
- 不等待图形 API 立即完成

---

### 8.2 建议前端接口

```cpp
class RHIFrontend {
public:
    BufferHandle CreateBuffer(const Render::RHI::BufferDesc& desc, const void* initialData, size_t initialDataSize);
    void UpdateBuffer(BufferHandle handle, const void* data, size_t size, uint64_t offset = 0);
    void DestroyBuffer(BufferHandle handle);

    TextureHandle CreateTexture(const Render::RHI::TextureDesc& desc);
    void UploadTexture(TextureHandle handle, const void* data, size_t size,
                       uint32_t mipLevel = 0,
                       uint32_t xOffset = 0,
                       uint32_t yOffset = 0,
                       uint32_t zOffset = 0,
                       uint32_t width = 0,
                       uint32_t height = 0,
                       uint32_t depth = 0);
    void DestroyTexture(TextureHandle handle);

    InputLayoutHandle CreateInputLayout(const Render::RHI::InputLayoutDesc& desc);
    void DestroyInputLayout(InputLayoutHandle handle);

    void BeginFrame();
    void EndFrame();

    void EnqueueDelegate(BackendDelegate delegate);
};
```

---

### 8.3 入队策略

前端每次入队时：

- 把数据复制进命令对象
- 命令进入 `RHICommandQueue`
- 当队列从空变非空时唤醒渲染线程

当前阶段先不做复杂优化。

---

## 九、资源对象层如何改

这是第一阶段非常重要的一点。

---

### 9.1 不再让上层直接操作 `Buffer*`
当前 `Buffer` 抽象是：

```cpp
class Buffer {
public:
    virtual bool Check() const noexcept = 0;
    virtual bool Update(const void* data, uint64_t size, uint64_t offset = 0) = 0;
    virtual void* Map(MapMode mode, uint64_t offset = 0, uint64_t size = 0) = 0;
    virtual void Unmap() = 0;
};
```

这类接口适合“立即执行风格”，不适合“前端只发命令”的风格。

第一阶段建议：

- 上层不再拿 `Buffer*`
- 上层只拿 `BufferHandle`
- `Update / Map / Unmap` 这种行为改成前端函数或命令
- 真正的 `GLBuffer` 只在后端执行器里使用

---

### 9.2 `TextureAsset` 与 GPU 纹理分离
建议：

#### `TextureAsset`
只做 CPU 侧：

- 文件加载
- 原始像素
- 配置描述
- 不直接代表 GPU 对象

#### `TextureHandle`
给前端和上层使用

#### `GLTexture`
只存在于渲染线程资源表中，代表真实 GPU 对象

这样更清晰，也更适合线程代理模型。

---

### 9.3 `InputLayout` 同理
当前也不建议继续让上层拿 `InputLayout*` 做绑定，而是：

- 上层拿 `InputLayoutHandle`
- 发绑定命令或创建命令
- 后端查表执行

第一阶段哪怕先只做创建/销毁，也要把这个方向定下来。

---

## 十、线程模型建议

---

### 10.1 渲染线程启动
初始化阶段：

1. 主线程创建 `RenderThread`
2. 渲染线程内部建立图形上下文
3. 渲染线程初始化 `GLBackendExecutor`
4. 渲染线程进入等待循环

---

### 10.2 唤醒方式
当前采用：

- 条件变量
- 队列从空变非空时唤醒
- 停止请求时唤醒

说明：

- 当前先不做帧节拍
- 因为你已经明确说第一阶段主要验证命令代理执行
- 后续做渲染循环时再加帧驱动即可

---

### 10.3 执行方式
当前假设：

- 队列中命令按 FIFO 顺序执行
- 不重排
- 不打散
- 不跨线程并行翻译

这是第一阶段最重要的简化条件。

---

## 十一、推荐的最小目录改造

建议新增：

```text
Render/
├── Public/
│   ├── RHI/
│   │   ├── rhi_frontend.h
│   │   ├── rhi_handles.h
│   │   ├── rhi_command.h
│   │   ├── rhi_delegate.h
│   │   └── ...
│
├── Private/
│   ├── Runtime/
│   │   ├── render_thread.h
│   │   ├── render_thread.cpp
│   │   ├── rhi_command_queue.h
│   │   ├── rhi_command_queue.cpp
│   │   ├── rhi_handle_allocator.h
│   │   ├── rhi_handle_allocator.cpp
│   │   ├── rhi_backend_resource_table.h
│   │   └── rhi_backend_resource_table.cpp
│   │
│   ├── Backend/
│   │   └── Opengl/
│   │       ├── gl_backend_executor.h
│   │       ├── gl_backend_executor.cpp
│   │       ├── gl_buffer.h
│   │       ├── gl_buffer.cpp
│   │       ├── gl_texture.h
│   │       ├── gl_texture.cpp
│   │       ├── gl_input_layout.h
│   │       ├── gl_input_layout.cpp
│   │       └── ...
```

说明：

- 当前 `gl_buffer / gl_texture / gl_input_layout` 可以继续保留
- 但它们的使用者不再是上层，而是 `gl_backend_executor`

---

## 十二、第一阶段迁移步骤

建议严格按以下顺序执行。

---

### 步骤 1：先引入 Handle 类型
新增：

- `BufferHandle`
- `TextureHandle`
- `InputLayoutHandle`

并建立简单的句柄分配器。

先不做 generation 也可以。

---

### 步骤 2：引入 `RHICommand`
先把命令种类和负载结构搭起来。

当前至少包括：

- Create / Update / Destroy Buffer
- Create / Upload / Destroy Texture
- Create / Destroy InputLayout
- BeginFrame / EndFrame
- ExecuteDelegate

---

### 步骤 3：引入 `RHICommandQueue`
先做最简单的线程安全 FIFO 队列。

---

### 步骤 4：引入 `RenderThread`
先让它能：

- 启动
- 休眠
- 被唤醒
- 批量出队
- 调用执行器

---

### 步骤 5：引入 `IRHIBackendExecutor` 与 `GLBackendExecutor`
让 OpenGL 后端从“对象直接被上层调用”，变成“执行器收到命令后调用对象”。

---

### 步骤 6：把 `GLBuffer / GLTexture / GLInputLayout` 的使用收口到执行器
即：

- 上层不直接创建/销毁/调用它们
- 只让 `GLBackendExecutor` 调用这些类

---

### 步骤 7：加入委托后门
确保：

- 可以从前端发布 `ExecuteDelegate`
- 委托在渲染线程执行
- 允许直接写 `gl*` 做验证

---

### 步骤 8：把测试代码迁移到前端命令模型
先不要急着改完整引擎逻辑，只需要验证：

- 创建 Buffer
- 更新 Buffer
- 创建 Texture
- 上传 Texture
- 创建 InputLayout
- 执行委托里的 `glClear` / `glDraw*`

如果这些链路通了，第一阶段就算成功。

---

## 十三、第一阶段明确不做的内容

为了防止项目 AI 过度设计，这里明确列出当前不做：

1. 不做多队列调度
2. 不做命令重排
3. 不做状态合并优化
4. 不做 Vulkan 风格资源状态屏障系统
5. 不做复杂的 frame graph
6. 不做多窗口上下文管理
7. 不做 GPU fence 生命周期回收
8. 不做异步结果查询系统
9. 不做完整渲染管线图
10. 不做高阶 descriptor/bindless 体系

第一阶段只有一个目标：

> **把 RHI 从“直接执行”改成“前端发命令，渲染线程顺序代理执行”，并保留一个能直接写图形库 API 的委托后门。**

---

## 十四、第一阶段完成标准

满足以下条件即可认为阶段成功：

1. 上层不再直接调用 `GLBuffer / GLTexture / GLInputLayout`
2. 上层通过前端接口只发布命令
3. 渲染线程能够顺序消费命令
4. OpenGL 调用只发生在渲染线程
5. 资源通过 handle 管理
6. 委托后门可以在渲染线程安全执行 `gl*` 代码
7. 现有测试程序至少可以通过：
   - Buffer 创建/更新
   - Texture 创建/上传
   - InputLayout 创建
   - Delegate 中直接调用 OpenGL 验证渲染结果

---

## 十五、对项目 AI 的执行要求

项目 AI 在执行本方案时，应遵循：

1. **优先保证最小可运行**
2. **不要提前引入复杂扩展**
3. **不要把第一阶段写成完整最终版渲染架构**
4. **不要删除现有 OpenGL 资源类，可复用，但调用入口必须收口到执行器**
5. **代码风格尽量贴近当前项目结构**
6. **先保证命令顺序提交和顺序执行正确**
7. **委托后门必须可用**
8. **新增接口命名保持清晰，避免未来难以替换**
9. **必要时可以新增测试入口，优先验证线程代理执行链路**
10. **如果某些旧接口暂时无法完全移除，可先标记 deprecated，但不要继续扩散使用**

---

## 十六、推荐的最小示例流程

以下是第一阶段预期的调用流程。

### 业务线程
```cpp
auto vb = frontend.CreateBuffer(desc, vertices.data(), vertices.size_bytes());
auto tex = frontend.CreateTexture(texDesc);
frontend.UploadTexture(tex, pixels.data(), pixelsSize, 0, 0, 0, 0, width, height, 1);

frontend.EnqueueDelegate([](BackendDelegateContext& ctx) {
    glClearColor(0.1f, 0.2f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
});
```

### 渲染线程
- 取到 `CreateBuffer`
- 在资源表里创建 `GLBuffer`
- 取到 `CreateTexture`
- 在资源表里创建 `GLTexture`
- 取到 `UploadTexture`
- 调用 `GLTexture::SetData`
- 取到 `ExecuteDelegate`
- 执行内部 `glClearColor` 和 `glClear`

---

## 十七、结论

第一阶段不是去追求“完整渲染框架”，而是先把最危险、最混乱的一层拆开：

- 上层调用与后端执行分离
- 资源逻辑身份与后端真实对象分离
- 图形 API 执行线程与业务线程分离

最终形成这条链路：

> **上层模块 -> RHI 前端 -> RHICommandQueue -> RenderThread -> BackendExecutor -> OpenGL**

并保留：

> **Delegate 后门 -> 在渲染线程中直接执行图形库 API**

当前先默认：

- 命令提交顺序 = 命令执行顺序

这个假设完全适合作为第一阶段落地基线。
