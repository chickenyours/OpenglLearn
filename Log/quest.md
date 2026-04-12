# Render 模块：RHI Buffer 设计与落地任务书

本文档用于指导“项目 AI”按照当前项目结构与代码风格，补全 **RHI Buffer 抽象层** 与 **OpenGL 后端基础实现**。目标是：

- **Public/RHI** 提供跨图形 API 的通用 Buffer 抽象与描述结构。
- **Private/Platform/Opengl** 提供当前阶段可运行的 OpenGL 基础实现。
- 接口声明尽量完整，为后续扩展到 D3D12 / Vulkan / Metal 预留空间。
- 当前阶段至少要能支持常见缓冲区能力：  
  - 顶点数据缓冲
  - 索引数据缓冲
  - 常量/统一参数缓冲
  - 结构化读写数据缓冲
  - 拷贝/上传/回读相关缓冲语义
- **不要把 OpenGL 特有概念直接塞进 RHI 公共接口**。例如：
  - `VAO` 不是通用 Buffer，属于 **输入布局/顶点输入状态对象** 范畴，不属于本次 buffer 核心抽象对象。
  - `EBO/UBO/SSBO` 可以在 OpenGL 后端中映射到通用 Buffer 用途，但公共 RHI 中应以更通用的名字描述。

---

## 1. 目录与命名要求

严格遵循当前目录格式与风格：

```text
Render/
├─ docs/
├─ Private/
│  ├─ Platform/Opengl/
│  │  ├─ gl_device.h
│  │  ├─ gl_device.cpp
│  │  ├─ gl_texture.h
│  │  ├─ gl_texture.cpp
│  │  ├─ gl_texture_convert.h
│  │  ├─ gl_texture_convert.cpp
│  │  ├─ gl_buffer.h
│  │  ├─ gl_buffer.cpp
│  │  ├─ gl_input_layout.h          // 如需要
│  │  ├─ gl_input_layout.cpp        // 如需要
│  │  └─ ...
│  └─ render_module.cpp
└─ Public/
   ├─ RHI/
   │  ├─ rhi_common.h
   │  ├─ RHI_device.h
   │  ├─ RHI_texture.h
   │  ├─ RHI_buffer.h
   │  └─ RHI_input_layout.h         // 如需要
   └─ render_module.h
```

### 命名风格
沿用现有风格：

- 公共 RHI 头文件：
  - `RHI_buffer.h`
  - `RHI_input_layout.h`
- OpenGL 私有实现：
  - `gl_buffer.h`
  - `gl_buffer.cpp`

注意：当前已有文件名中同时存在 `rhi_common.h` 和 `RHI_device.h` / `RHI_texture.h`。新文件保持这种已有风格，不要擅自统一改名。

---

## 2. 设计原则

### 2.1 RHI 层只表达“资源语义”，不暴露 OpenGL 名词
公共接口中不要直接出现这些词作为核心抽象：

- VBO
- EBO
- UBO
- SSBO
- VAO

应改为更通用的抽象：

- Vertex Buffer
- Index Buffer
- Constant Buffer
- Structured Buffer / Storage Buffer
- Indirect Buffer
- Staging / Upload / Readback Buffer

### 2.2 Buffer 与 Input Layout 分离
`VAO` 并不是 Buffer，而是 OpenGL 中“顶点输入状态缓存对象”。

因此建议：

- **RHI_buffer.h**：只负责 Buffer 资源本体
- **RHI_input_layout.h**：负责顶点属性布局描述
- OpenGL 后端里用 `gl_input_layout.*` 负责 VAO 或 VAO 风格对象

### 2.3 当前阶段先做“声明较完善 + 基础实现可用”
当前阶段要求：

- 公共接口描述完整，能体现未来扩展方向
- OpenGL 至少完成基本创建/更新/绑定/拷贝
- 不要求一次性做完复杂 barrier、持久映射、多队列同步等重型内容
- 但枚举和接口要为这些能力预留扩展位

---

## 3. 建议新增的公共 RHI 文件

---

## 3.1 `Public/RHI/RHI_buffer.h`

该文件负责定义：

- Buffer 类型/用途枚举
- 内存使用语义
- CPU 访问模式
- Buffer 描述结构
- Buffer View / Range 结构
- 基础 Buffer 抽象类

### 3.1.1 建议内容

#### A. BufferUsage
采用位标记，表示这块缓冲区可承担哪些用途。

建议：

```cpp
enum class BufferUsage : uint32_t {
    None            = 0,

    Vertex          = 1 << 0,
    Index           = 1 << 1,
    Constant        = 1 << 2,
    Structured      = 1 << 3,
    Storage         = 1 << 4,
    Indirect        = 1 << 5,

    TransferSrc     = 1 << 6,
    TransferDst     = 1 << 7,

    Readback        = 1 << 8
};
```

说明：

- `Constant`：对应 D3D ConstantBuffer / Vulkan UniformBuffer / OpenGL UBO
- `Structured`：强调结构化元素
- `Storage`：强调可供 shader 大量读写
- `Structured` 和 `Storage` 可以同时存在
- `Readback` 用于 CPU 回读语义
- `TransferSrc/TransferDst` 用于拷贝用途

同时实现和 `TextureUsage` 一样的位运算：

```cpp
inline BufferUsage operator|(BufferUsage a, BufferUsage b);
inline BufferUsage operator&(BufferUsage a, BufferUsage b);
```

---

#### B. MemoryUsage
用于表达资源更偏向哪种内存使用策略。

建议：

```cpp
enum class MemoryUsage {
    GpuOnly,
    CpuToGpu,
    GpuToCpu,
    CpuOnly
};
```

解释：

- `GpuOnly`：最适合静态 GPU 资源
- `CpuToGpu`：CPU 高频更新给 GPU 读
- `GpuToCpu`：GPU 写、CPU 回读
- `CpuOnly`：仅 CPU 侧中转或测试用

---

#### C. CpuAccessMode / MapMode
建议分开描述：

```cpp
enum class CpuAccessMode : uint32_t {
    None        = 0,
    Read        = 1 << 0,
    Write       = 1 << 1
};

inline CpuAccessMode operator|(CpuAccessMode a, CpuAccessMode b);
inline CpuAccessMode operator&(CpuAccessMode a, CpuAccessMode b);
```

以及：

```cpp
enum class MapMode {
    Read,
    Write,
    ReadWrite,
    WriteDiscard,
    WriteNoOverwrite
};
```

当前阶段 OpenGL 后端不一定全部支持，但接口先保留。

---

#### D. IndexFormat
建议加入索引格式：

```cpp
enum class IndexFormat {
    UInt16,
    UInt32
};
```

---

#### E. BufferDesc
建议至少包含：

```cpp
struct BufferDesc {
    uint64_t size = 0;
    uint32_t stride = 0;
    uint32_t usage = static_cast<uint32_t>(BufferUsage::None);

    MemoryUsage memoryUsage = MemoryUsage::GpuOnly;
    uint32_t cpuAccess = static_cast<uint32_t>(CpuAccessMode::None);

    bool createMapped = false;
};
```

说明：

- `size`：字节数
- `stride`：单元素字节数，适合 Vertex/Structured/Instance 数据
- `usage`：位组合
- `memoryUsage`：内存语义
- `cpuAccess`：CPU 访问权限
- `createMapped`：为以后 persistent map 或上传缓冲预留

可选增加：

```cpp
const char* debugName = nullptr;
```

但要看项目当前是否已经统一管理 debug 名称。若没有，可先不加，或以注释形式保留扩展说明。

---

#### F. BufferRange / BufferViewDesc
通用 API 都很需要“只绑定 buffer 某一段范围”的能力，因此建议至少有：

```cpp
struct BufferRange {
    uint64_t offset = 0;
    uint64_t size = 0;
};
```

以及：

```cpp
struct BufferViewDesc {
    uint64_t offset = 0;
    uint64_t size = 0;
    uint32_t stride = 0;
};
```

说明：

- Constant Buffer / Uniform Buffer 常常只绑定一个范围
- Structured Buffer / Storage Buffer 也可能绑定子区间

当前阶段如果不单独实现 `BufferView` 对象，也至少保留描述结构。

---

#### G. Buffer 抽象基类
参考当前 `Texture` 风格：

```cpp
class Buffer {
public:
    explicit Buffer(const BufferDesc& desc) : desc_(desc) {}
    virtual ~Buffer() = default;

    const BufferDesc& GetDesc() const noexcept { return desc_; }

    virtual bool Check() const noexcept = 0;

    virtual bool Update(const void* data, uint64_t size, uint64_t offset = 0) = 0;
    virtual void* Map(MapMode mode, uint64_t offset = 0, uint64_t size = 0) = 0;
    virtual void Unmap() = 0;

protected:
    BufferDesc desc_;
};
```

说明：

- `Check()` 沿用现有 Texture 风格
- `Update()` 用于 CPU 写入
- `Map()/Unmap()` 保留未来扩展
- `size==0` 可表示映射到缓冲尾部或整段，具体由实现决定，但要在注释中写清楚

### 3.1.2 还可补充的辅助函数
建议增加：

```cpp
inline bool HasBufferUsage(uint32_t flags, BufferUsage usage);
inline bool HasCpuAccess(uint32_t flags, CpuAccessMode mode);
```

这样便于后端检查。

---

## 3.2 `Public/RHI/RHI_input_layout.h`（建议新增）

### 为什么需要它
因为用户提到了 `VAO`。  
但 **VAO 不应该放进 Buffer 抽象**。  
更合理做法是补一个“输入布局”抽象文件。

### 建议内容

#### A. VertexElementFormat
建议定义一些基础顶点元素格式：

```cpp
enum class VertexElementFormat {
    Float1,
    Float2,
    Float3,
    Float4,

    UInt1,
    UInt2,
    UInt3,
    UInt4,

    UByte4_Norm
};
```

#### B. VertexInputRate
```cpp
enum class VertexInputRate {
    PerVertex,
    PerInstance
};
```

#### C. VertexAttributeDesc
```cpp
struct VertexAttributeDesc {
    uint32_t location = 0;
    uint32_t binding = 0;
    VertexElementFormat format = VertexElementFormat::Float3;
    uint32_t offset = 0;
};
```

#### D. VertexBufferBindingDesc
```cpp
struct VertexBufferBindingDesc {
    uint32_t binding = 0;
    uint32_t stride = 0;
    VertexInputRate inputRate = VertexInputRate::PerVertex;
};
```

#### E. InputLayoutDesc
可以使用固定数组或 `std::vector`。  
若项目当前尽量避免在公共头文件引入过多 STL，可采用简单定长方案或后续调整。

如果直接使用 STL：

```cpp
#include <vector>

struct InputLayoutDesc {
    std::vector<VertexAttributeDesc> attributes;
    std::vector<VertexBufferBindingDesc> bindings;
};
```

#### F. InputLayout 抽象类
```cpp
class InputLayout {
public:
    explicit InputLayout(const InputLayoutDesc& desc) : desc_(desc) {}
    virtual ~InputLayout() = default;

    const InputLayoutDesc& GetDesc() const noexcept { return desc_; }
    virtual bool Check() const noexcept = 0;

protected:
    InputLayoutDesc desc_;
};
```

---

## 4. RHI Device 需要补充的接口

需要在 `Public/RHI/RHI_device.h` 中增加 Buffer 相关工厂与操作接口。  
保持与项目现有 Device 设计一致，如果当前 Device 已是抽象基类，则在其上新增虚函数。

## 4.1 至少补充的工厂函数

```cpp
virtual Buffer* CreateBuffer(const BufferDesc& desc, const void* initialData = nullptr) = 0;
virtual void DestroyBuffer(Buffer* buffer) = 0;
```

如果项目已使用智能指针，则按项目风格改为智能指针返回。  
若当前模块风格更偏裸指针，则继续沿用现有风格。

---

## 4.2 建议补充的绑定与复制函数
这些接口是否放到 Device，取决于当前项目是否已经有 command list / context 抽象。  
如果还没有 command list，先挂在 Device 上。

建议至少包含：

```cpp
virtual bool CopyBuffer(
    Buffer* src,
    Buffer* dst,
    uint64_t size,
    uint64_t srcOffset = 0,
    uint64_t dstOffset = 0
) = 0;
```

以及与图形绑定相关的基础接口：

```cpp
virtual bool BindVertexBuffer(Buffer* buffer, uint32_t slot, uint64_t offset = 0) = 0;
virtual bool BindIndexBuffer(Buffer* buffer, IndexFormat format, uint64_t offset = 0) = 0;
virtual bool BindConstantBuffer(Buffer* buffer, uint32_t slot, const BufferRange* range = nullptr) = 0;
virtual bool BindStructuredBuffer(Buffer* buffer, uint32_t slot, const BufferRange* range = nullptr) = 0;
virtual bool BindStorageBuffer(Buffer* buffer, uint32_t slot, const BufferRange* range = nullptr) = 0;
```

说明：

- `BindStructuredBuffer` 和 `BindStorageBuffer` 是否分离，可看后端实际需要
- 若觉得当前阶段太多，可至少保留：
  - `BindVertexBuffer`
  - `BindIndexBuffer`
  - `BindConstantBuffer`
  - `BindStorageBuffer`

### 不建议现在放进 RHI Device 的东西
- 复杂 barrier 管理
- 多队列 ownership 转移
- descriptor heap/descriptor set 分配
- pipeline layout 校验

这些先别做重。

---

## 5. OpenGL 后端实现要求

---

## 5.1 `Private/Platform/Opengl/gl_buffer.h`

定义 `GLBuffer`，继承 `Render::RHI::Buffer`。

### 建议成员

```cpp
class GLBuffer final : public Render::RHI::Buffer {
public:
    explicit GLBuffer(const Render::RHI::BufferDesc& desc);
    ~GLBuffer() override;

    bool Initialize(const void* initialData = nullptr);

    bool Check() const noexcept override;

    bool Update(const void* data, uint64_t size, uint64_t offset = 0) override;
    void* Map(Render::RHI::MapMode mode, uint64_t offset = 0, uint64_t size = 0) override;
    void Unmap() override;

    unsigned int GetHandle() const noexcept;
    unsigned int GetTarget() const noexcept;

    bool BindAsVertexBuffer() const;
    bool BindAsIndexBuffer() const;
    bool BindAsConstantBuffer(uint32_t slot, uint64_t offset = 0, uint64_t size = 0) const;
    bool BindAsStorageBuffer(uint32_t slot, uint64_t offset = 0, uint64_t size = 0) const;

    bool CopyTo(GLBuffer* dst, uint64_t size, uint64_t srcOffset = 0, uint64_t dstOffset = 0) const;

private:
    unsigned int handle_ = 0;
    unsigned int target_ = 0;
    bool mapped_ = false;
};
```

说明：

- `GetTarget()` 是 OpenGL 内部语义，不进公共接口
- `BindAsXXX()` 是 GL 私有便捷函数
- 设备层可调用这些函数完成 RHI Device 的虚函数实现

---

## 5.2 OpenGL 目标映射策略

### 核心原则
OpenGL 的 `GL_ARRAY_BUFFER / GL_ELEMENT_ARRAY_BUFFER / GL_UNIFORM_BUFFER / GL_SHADER_STORAGE_BUFFER` 等目标，不应直接出现在 RHI 公共层中，只在 `gl_buffer.cpp` 做映射。

### 建议辅助函数
在 `gl_buffer.cpp` 中增加内部静态函数：

```cpp
static GLenum ChoosePrimaryTarget(const Render::RHI::BufferDesc& desc);
static GLenum ChooseUsageHint(const Render::RHI::BufferDesc& desc);
static GLbitfield ConvertMapMode(Render::RHI::MapMode mode);
```

### 目标选择建议
按优先级选主目标，例如：

1. Index -> `GL_ELEMENT_ARRAY_BUFFER`
2. Vertex -> `GL_ARRAY_BUFFER`
3. Constant -> `GL_UNIFORM_BUFFER`
4. Storage / Structured -> `GL_SHADER_STORAGE_BUFFER`
5. 否则默认 `GL_ARRAY_BUFFER` 或 `GL_COPY_READ_BUFFER`

注意：

- 同一 GL buffer object 本质上可绑定到多个 target
- `target_` 这里只作为“默认主要绑定目标”
- 真正执行绑定时，应按函数语义绑定到目标位，而不是死依赖 `target_`

---

## 5.3 OpenGL usage hint 建议
将 RHI 描述映射到：

- `GL_STATIC_DRAW`
- `GL_DYNAMIC_DRAW`
- `GL_STREAM_DRAW`

大致策略：

- `GpuOnly` + 无 CPU 写：`GL_STATIC_DRAW`
- `CpuToGpu`：`GL_DYNAMIC_DRAW`
- 高频流式更新：未来可扩展 `GL_STREAM_DRAW`
- `GpuToCpu` / `CpuOnly`：先用 `GL_DYNAMIC_READ` / `GL_STREAM_READ` 之类近似映射

只需近似，不求绝对完美。

---

## 5.4 必须完成的实现功能

### A. 创建
- `glGenBuffers`
- `glBindBuffer`
- `glBufferData`
- 支持 `initialData`

### B. 销毁
- `glDeleteBuffers`

### C. Update
优先用：

- `glBindBuffer`
- `glBufferSubData`

并做越界检查：

- `offset + size <= desc_.size`

### D. Map / Unmap
优先使用：

- `glMapBufferRange`
- `glUnmapBuffer`

如果当前环境不支持，也可退回到 `glMapBuffer`，但优先 `Range`

### E. Bind
至少支持：

- 顶点缓冲绑定
- 索引缓冲绑定
- 常量缓冲绑定到 binding point
- 存储缓冲绑定到 binding point

建议分别用：

- `glBindBuffer(GL_ARRAY_BUFFER, handle_)`
- `glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, handle_)`
- `glBindBufferBase(GL_UNIFORM_BUFFER, slot, handle_)`
- `glBindBufferRange(GL_UNIFORM_BUFFER, slot, handle_, offset, size)`
- `glBindBufferBase(GL_SHADER_STORAGE_BUFFER, slot, handle_)`
- `glBindBufferRange(GL_SHADER_STORAGE_BUFFER, slot, handle_, offset, size)`

### F. Copy
使用：

- `glBindBuffer(GL_COPY_READ_BUFFER, src)`
- `glBindBuffer(GL_COPY_WRITE_BUFFER, dst)`
- `glCopyBufferSubData(...)`

---

## 6. OpenGL Device 需要补充的实现

如果当前 `gl_device.h/.cpp` 已是 RHI Device 的 OpenGL 后端实现，则需在其中补充 Buffer 相关实现。

## 6.1 建议新增内容
在 `gl_device.h` 中补充 override：

```cpp
Render::RHI::Buffer* CreateBuffer(const Render::RHI::BufferDesc& desc, const void* initialData = nullptr) override;
void DestroyBuffer(Render::RHI::Buffer* buffer) override;

bool CopyBuffer(
    Render::RHI::Buffer* src,
    Render::RHI::Buffer* dst,
    uint64_t size,
    uint64_t srcOffset = 0,
    uint64_t dstOffset = 0
) override;

bool BindVertexBuffer(Render::RHI::Buffer* buffer, uint32_t slot, uint64_t offset = 0) override;
bool BindIndexBuffer(Render::RHI::Buffer* buffer, Render::RHI::IndexFormat format, uint64_t offset = 0) override;
bool BindConstantBuffer(Render::RHI::Buffer* buffer, uint32_t slot, const Render::RHI::BufferRange* range = nullptr) override;
bool BindStorageBuffer(Render::RHI::Buffer* buffer, uint32_t slot, const Render::RHI::BufferRange* range = nullptr) override;
```

### 说明
- OpenGL 的顶点 buffer 真正使用时常与 VAO / 顶点属性布局联动，因此 `BindVertexBuffer` 当前阶段可先提供基础绑定能力
- 真正的 attribute pointer 设置应归 `InputLayout`/VAO 那层处理

---

## 7. 关于 VAO 的处理意见

用户点到了 `VAO`，这里明确给项目 AI：

## 7.1 不要把 VAO 当作 Buffer 实现进 `RHI_buffer.h`
因为它不表示线性内存资源，而是“输入状态对象”。

## 7.2 当前推荐做法
新增：

- `Public/RHI/RHI_input_layout.h`
- `Private/Platform/Opengl/gl_input_layout.h`
- `Private/Platform/Opengl/gl_input_layout.cpp`

### OpenGL 后端中的 `GLInputLayout`
内部可持有：

- VAO handle
- 顶点属性声明信息
- 各 binding 信息

并负责：

- `glGenVertexArrays`
- `glBindVertexArray`
- `glEnableVertexAttribArray`
- `glVertexAttribPointer`
- `glVertexAttribIPointer`
- `glVertexAttribDivisor`

### 当前阶段若不想把输入布局一次做全
那至少要在本文档里说明：

- VAO 不属于 buffer
- 本阶段只先完成 buffer 资源抽象
- 下一阶段再做 input layout / vertex state

---

## 8. 兼容其他底层 API 的映射语义

项目 AI 在写代码时，应保证 RHI 公共层的命名可以合理映射到：

### Direct3D 11/12
- Vertex -> Vertex Buffer
- Index -> Index Buffer
- Constant -> Constant Buffer
- Structured -> Structured Buffer
- Storage -> UAV/SRV 风格读写资源
- Transfer -> Copy Source / Destination

### Vulkan
- Vertex -> `VK_BUFFER_USAGE_VERTEX_BUFFER_BIT`
- Index -> `VK_BUFFER_USAGE_INDEX_BUFFER_BIT`
- Constant -> `VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT`
- Storage -> `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT`
- TransferSrc/Dst -> 对应 transfer flags
- Indirect -> `VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT`

### Metal
- 核心上都是 `MTLBuffer`
- 用途主要通过绑定点和 encoder 语义表达

因此公共层不要出现 OpenGL 独占命名。

---

## 9. 本阶段“必须完成”与“可暂缓”的边界

## 9.1 必须完成
1. `RHI_buffer.h`
2. `gl_buffer.h`
3. `gl_buffer.cpp`
4. `RHI_device.h` 增加 buffer 工厂和基础绑定接口
5. `gl_device.h/.cpp` 实现上述接口
6. 保证可创建以下语义的缓冲：
   - Vertex
   - Index
   - Constant
   - Storage / Structured
7. 支持：
   - 初始化数据
   - Update
   - Map / Unmap
   - Copy
   - 基础绑定

## 9.2 建议完成
1. `RHI_input_layout.h`
2. `gl_input_layout.h/.cpp`
3. 基础顶点布局抽象，解决 VAO 的合理归属

## 9.3 可以暂缓
1. 持久映射
2. 多线程上下文同步
3. barrier / resource state 跟踪系统
4. descriptor set / bind group 系统
5. ring buffer allocator
6. transient upload heap
7. indirect draw 详细命令结构

---

## 10. 推荐代码骨架

下面给项目 AI 一个建议骨架，最终实现可按项目现状微调。

---

## 10.1 `RHI_buffer.h` 建议骨架

```cpp
#pragma once

#include <cstdint>

namespace Render::RHI {

    enum class BufferUsage : uint32_t {
        None            = 0,
        Vertex          = 1 << 0,
        Index           = 1 << 1,
        Constant        = 1 << 2,
        Structured      = 1 << 3,
        Storage         = 1 << 4,
        Indirect        = 1 << 5,
        TransferSrc     = 1 << 6,
        TransferDst     = 1 << 7,
        Readback        = 1 << 8
    };

    inline BufferUsage operator|(BufferUsage a, BufferUsage b) {
        return static_cast<BufferUsage>(
            static_cast<uint32_t>(a) | static_cast<uint32_t>(b)
        );
    }

    inline BufferUsage operator&(BufferUsage a, BufferUsage b) {
        return static_cast<BufferUsage>(
            static_cast<uint32_t>(a) & static_cast<uint32_t>(b)
        );
    }

    enum class MemoryUsage {
        GpuOnly,
        CpuToGpu,
        GpuToCpu,
        CpuOnly
    };

    enum class CpuAccessMode : uint32_t {
        None    = 0,
        Read    = 1 << 0,
        Write   = 1 << 1
    };

    inline CpuAccessMode operator|(CpuAccessMode a, CpuAccessMode b) {
        return static_cast<CpuAccessMode>(
            static_cast<uint32_t>(a) | static_cast<uint32_t>(b)
        );
    }

    inline CpuAccessMode operator&(CpuAccessMode a, CpuAccessMode b) {
        return static_cast<CpuAccessMode>(
            static_cast<uint32_t>(a) & static_cast<uint32_t>(b)
        );
    }

    enum class MapMode {
        Read,
        Write,
        ReadWrite,
        WriteDiscard,
        WriteNoOverwrite
    };

    enum class IndexFormat {
        UInt16,
        UInt32
    };

    struct BufferDesc {
        uint64_t size = 0;
        uint32_t stride = 0;
        uint32_t usage = static_cast<uint32_t>(BufferUsage::None);

        MemoryUsage memoryUsage = MemoryUsage::GpuOnly;
        uint32_t cpuAccess = static_cast<uint32_t>(CpuAccessMode::None);

        bool createMapped = false;
    };

    struct BufferRange {
        uint64_t offset = 0;
        uint64_t size = 0;
    };

    struct BufferViewDesc {
        uint64_t offset = 0;
        uint64_t size = 0;
        uint32_t stride = 0;
    };

    inline bool HasBufferUsage(uint32_t flags, BufferUsage usage) {
        return (flags & static_cast<uint32_t>(usage)) != 0;
    }

    inline bool HasCpuAccess(uint32_t flags, CpuAccessMode mode) {
        return (flags & static_cast<uint32_t>(mode)) != 0;
    }

    class Buffer {
    public:
        explicit Buffer(const BufferDesc& desc) : desc_(desc) {}
        virtual ~Buffer() = default;

        const BufferDesc& GetDesc() const noexcept { return desc_; }

        virtual bool Check() const noexcept = 0;
        virtual bool Update(const void* data, uint64_t size, uint64_t offset = 0) = 0;
        virtual void* Map(MapMode mode, uint64_t offset = 0, uint64_t size = 0) = 0;
        virtual void Unmap() = 0;

    protected:
        BufferDesc desc_;
    };

}
```

---

## 10.2 `gl_buffer.h` 建议骨架

```cpp
#pragma once

#include "Render/Public/RHI/RHI_buffer.h"

namespace Render::Platform::Opengl {

    class GLBuffer final : public Render::RHI::Buffer {
    public:
        explicit GLBuffer(const Render::RHI::BufferDesc& desc);
        ~GLBuffer() override;

        bool Initialize(const void* initialData = nullptr);

        bool Check() const noexcept override;
        bool Update(const void* data, uint64_t size, uint64_t offset = 0) override;
        void* Map(Render::RHI::MapMode mode, uint64_t offset = 0, uint64_t size = 0) override;
        void Unmap() override;

        unsigned int GetHandle() const noexcept;
        unsigned int GetTarget() const noexcept;

        bool BindAsVertexBuffer() const;
        bool BindAsIndexBuffer() const;
        bool BindAsConstantBuffer(uint32_t slot, uint64_t offset = 0, uint64_t size = 0) const;
        bool BindAsStorageBuffer(uint32_t slot, uint64_t offset = 0, uint64_t size = 0) const;

        bool CopyTo(GLBuffer* dst, uint64_t size, uint64_t srcOffset = 0, uint64_t dstOffset = 0) const;

    private:
        unsigned int handle_ = 0;
        unsigned int target_ = 0;
        bool mapped_ = false;
    };

}
```

---

## 10.3 `RHI_device.h` 建议补充骨架

在现有 device 抽象上补充：

```cpp
class Device {
public:
    virtual ~Device() = default;

    // 已有 texture 接口 ...

    virtual Buffer* CreateBuffer(const BufferDesc& desc, const void* initialData = nullptr) = 0;
    virtual void DestroyBuffer(Buffer* buffer) = 0;

    virtual bool CopyBuffer(
        Buffer* src,
        Buffer* dst,
        uint64_t size,
        uint64_t srcOffset = 0,
        uint64_t dstOffset = 0
    ) = 0;

    virtual bool BindVertexBuffer(Buffer* buffer, uint32_t slot, uint64_t offset = 0) = 0;
    virtual bool BindIndexBuffer(Buffer* buffer, IndexFormat format, uint64_t offset = 0) = 0;
    virtual bool BindConstantBuffer(Buffer* buffer, uint32_t slot, const BufferRange* range = nullptr) = 0;
    virtual bool BindStorageBuffer(Buffer* buffer, uint32_t slot, const BufferRange* range = nullptr) = 0;
};
```

注意把前置声明和头文件引用处理好，避免循环依赖。

---

## 11. 实现细节要求

## 11.1 参数校验
所有实现至少做这些检查：

- `desc.size > 0`
- `Update()` 时 `data != nullptr`
- `offset + size <= desc_.size`
- Map 时范围合法
- Bind 时 buffer 非空且 `Check()` 为真
- Copy 时源/目标均有效且范围合法

## 11.2 析构安全
- `GLBuffer` 析构时如果 `handle_ != 0` 则释放
- 避免重复删除
- 如果仍处于 mapped 状态，先尝试 `Unmap()`

## 11.3 不要在公共层 include OpenGL 头
OpenGL 头只存在于 `Private/Platform/Opengl/*`

## 11.4 不要让公共层与 GLFW 直接耦合
Buffer 与窗口系统无关，不要引入 GLFW 依赖。

---

## 12. 下一阶段建议（本次不必全做）

本次完成后，后续建议顺序：

1. `InputLayout / VAO` 抽象
2. `CommandList / Context` 抽象
3. `Pipeline / Shader Resource Binding` 抽象
4. 上传缓冲子分配器
5. Frame allocator / transient resource
6. 同步与资源状态跟踪

---

## 13. 给项目 AI 的最终执行要求

请项目 AI 按本文档完成以下内容：

### 必做文件
- `Render/Public/RHI/RHI_buffer.h`
- `Render/Private/Platform/Opengl/gl_buffer.h`
- `Render/Private/Platform/Opengl/gl_buffer.cpp`

### 需要修改
- `Render/Public/RHI/RHI_device.h`
- `Render/Private/Platform/Opengl/gl_device.h`
- `Render/Private/Platform/Opengl/gl_device.cpp`

### 建议新增
- `Render/Public/RHI/RHI_input_layout.h`
- `Render/Private/Platform/Opengl/gl_input_layout.h`
- `Render/Private/Platform/Opengl/gl_input_layout.cpp`

### 风格要求
- 与当前 `RHI_texture.h` 风格一致
- 公共层保持简洁、偏抽象
- 后端层完成 OpenGL 对应映射
- 当前阶段代码要“能编译、能创建、能更新、能绑定、能拷贝”

### 特别强调
- **VAO 不属于 Buffer**
- **EBO/UBO/SSBO 只作为 OpenGL 后端映射概念，不进入公共 RHI 命名**
- **公共命名必须面向多后端扩展**
