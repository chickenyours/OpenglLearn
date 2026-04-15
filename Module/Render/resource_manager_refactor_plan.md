# ResourceManager 头文件拆分与结构改进方案

## 目标

当前 `ResourceManager` 相关代码把接口声明、模板声明、模板实现、模块实现细节、资源池实现、工厂函数实现、句柄实现全部放进了同一个头文件中，已经出现以下问题：

1. **职责过度集中**：一个文件同时承担接口层、策略层、模块层、模板实现层。
2. **编译依赖偏重**：任何包含此头文件的编译单元都会被迫引入 `<unordered_map> / <mutex> / <iostream>` 以及大量模板实现。
3. **句柄与管理器强耦合**：`ResourceHandle<T>` 的拷贝行为依赖 `ResourceManagerModule::GetInstance()`，导致句柄层反向依赖管理器单例。
4. **后续扩展困难**：未来若加入 `TextureAsset / MeshAsset / ShaderAsset / AudioAsset`、异步加载、热重载、CPU/GPU 双阶段生命周期，该头文件会快速膨胀。
5. **实现边界不清晰**：哪些是稳定接口，哪些是内部实现，当前没有清晰边界。

本次改进目标不是推翻当前设计，而是**在尽量保持现有接口可用的前提下，完成结构化拆分，为后续资源系统扩展打基础**。

---

## 当前文件问题归纳

根据现有实现，当前头文件至少包含以下职责：

- 基础接口：`ILoadable`、`ILoadFromConfig`
- 对外句柄：`ResourceHandle<T>`
- 加载参数对象：`LoadOptions<T>`
- 工厂函数：`FromConfig / FromPointer / FromGenerator / FromKey`
- 模块类：`ResourceManagerModule`
- 内部模板池：`ResourceManagerModule::ResourcePool<T>`
- 模板实现：`GetPool / Get / OnHandleRelease / TryReleaseAll ...`
- 句柄特殊行为实现：拷贝构造、赋值、析构

这说明当前文件实际上已经同时承担了：

- 公共 API 头
- 模板实现头
- 内部实现头
- 部分模块实现源文件的职责

需要拆分。

---

## 改进原则

本次重构遵循以下原则：

### 1. 稳定接口与重实现分离
稳定、变化少、被广泛依赖的内容放在轻量头文件中；模板实现和内部逻辑尽量转移到 `.inl` 或 `.cpp`。

### 2. 对外暴露最小化
外部只应看到：

- 资源基础接口
- 句柄类型
- 加载选项类型
- 管理器公开方法声明

不应默认暴露资源池实现细节。

### 3. 保持模板可见性
模板必须仍可实例化，因此模板实现移动到 `.inl`，由 `.h` 末尾 `#include` 进来，不直接放进单一大头中。

### 4. 优先做“低风险结构重构”
先拆文件、理清层级，再考虑进一步修改 `ResourceHandle` 的语义。

### 5. 尽量保持原接口兼容
外部调用方式尽量不变，例如：

```cpp
auto handle = ResourceManagerModule::GetInstance().Get(FromConfig<MyRes>("a.json"));
```

这类调用方式先不主动破坏。

---

## 推荐的新文件结构

建议将当前单一头文件拆分为以下结构：

```text
Resource/
├─ Public/
│  ├─ resource_loadable.h
│  ├─ resource_handle.h
│  ├─ resource_load_options.h
│  └─ resource_manager_module.h
│
├─ Private/
│  ├─ resource_handle.inl
│  ├─ resource_load_options.inl
│  ├─ resource_manager_module.inl
│  └─ resource_manager_module.cpp
```

如果项目没有严格 Public/Private 目录，也至少拆成：

```text
resource_loadable.h
resource_handle.h
resource_handle.inl
resource_load_options.h
resource_load_options.inl
resource_manager_module.h
resource_manager_module.inl
resource_manager_module.cpp
```

---

## 各文件职责建议

### 1. `resource_loadable.h`

只放基础接口：

- `ILoadable`
- `ILoadFromConfig`

要求：

- 只保留最小依赖
- 尽量不包含重量级标准库头

建议内容：

```cpp
#pragma once

#include <string>

namespace Resource {

class ILoadable {
public:
    virtual ~ILoadable() = default;
    ILoadable() = default;

    bool IsLoad() const { return isLoad_; }
    virtual void Release() = 0;

protected:
    bool isLoad_ = false;
};

class ILoadFromConfig : public ILoadable {
public:
    virtual bool LoadFromConfigFile(const std::string& configFile) = 0;
};

}
```

---

### 2. `resource_handle.h`

只放：

- `template<typename T> class ResourceHandle`
- 成员函数声明
- 少量 inline 访问器

不在此文件中放重实现。

要求：

- 只包含必要头
- 不直接包含资源池实现
- 如有可能，仅前置声明 `ResourceManagerModule`

注意：因为当前 `ResourceHandle<T>` 的拷贝实现依赖 `ResourceManagerModule`，实现部分可放到 `resource_handle.inl` 中，并由 `resource_manager_module.h` 或 `resource_handle.h` 末尾包含。

---

### 3. `resource_load_options.h`

只放：

- `LoadOptions<T>` 声明
- `FromConfig / FromPointer / FromGenerator / FromKey` 声明

实现放到 `resource_load_options.inl`。

这样后续如果要增加：

- 异步加载参数
- 资源域标签
- 生命周期策略
- 线程策略

可以集中改这里。

---

### 4. `resource_manager_module.h`

只放：

- `ResourceManagerModule` 类声明
- `template<typename T> class ResourcePool;` 前置声明
- `Get / OnHandleRelease / OnZeroRefRelease / TryReleaseAll / GetPool` 的声明

不在这里直接写完整模板实现逻辑。

该头应包含：

- `module.h`
- `resource_loadable.h`
- `resource_handle.h`
- `resource_load_options.h`

然后在文件末尾：

```cpp
#include "resource_manager_module.inl"
```

---

### 5. `resource_manager_module.inl`

这里只放模板相关实现：

- `ResourcePool<T>` 定义与实现
- `ResourceManagerModule::Get<T>()`
- `ResourceManagerModule::GetPool<T>()`
- `ResourceManagerModule::OnHandleRelease<T>()`
- `ResourceManagerModule::OnZeroRefRelease<T>()`
- `ResourceManagerModule::TryReleaseAll<T>()`

如果 `ResourceHandle<T>` 的模板实现必须依赖 manager，也可在此处包含 `resource_handle.inl`。

---

### 6. `resource_manager_module.cpp`

只放非模板实现：

- `ResourceManagerModule::GetInstance()`
- `Startup()`
- `Shutdown()`

如果现在这些函数尚未真正实现，也要在 `.cpp` 中留出位置，不要再回写到公共头里。

---

## 推荐的包含关系

建议依赖方向如下：

```text
resource_loadable.h
        ↑
resource_load_options.h
        ↑
resource_handle.h
        ↑
resource_manager_module.h
        ↓
resource_manager_module.inl
```

更准确地说：

- `resource_loadable.h` 为最底层
- `resource_load_options.h` 依赖 `resource_loadable.h`
- `resource_handle.h` 尽量只依赖基础头和标准库
- `resource_manager_module.h` 聚合外部 API
- `resource_manager_module.inl` 承载模板实现

目标是避免：

- 句柄层直接知道资源池细节
- 工厂函数层直接知道模块内部细节
- 所有内容彼此循环包含

---

## 对 `ResourceHandle<T>` 的额外改进建议

当前 `ResourceHandle<T>` 最大的问题不是文件位置，而是语义耦合。

### 当前问题

当前拷贝构造/拷贝赋值会通过：

```cpp
ResourceManagerModule::GetInstance().Get<T>(FromKey<T>(other.GetName()))
```

重新向管理器登记引用。

这会带来几个问题：

1. `ResourceHandle<T>` 依赖全局单例管理器。
2. handle 的“复制”实际上不是普通复制，而是带副作用的重新获取。
3. 后续若想支持多资源上下文、多 manager、多 scene，很难演化。
4. 测试和 mock 较困难。

### 本次建议

本轮重构先**不强制改语义**，只做文件拆分与边界整理。

但请在代码中增加注释，明确说明：

- `ResourceHandle<T>` 目前不是纯值语义对象
- 复制行为会触发 manager 引用恢复逻辑
- 后续建议改为共享控制块设计

### 后续可选方向

后续可以考虑将 `ResourceHandle<T>` 改为基于控制块的轻量句柄，例如：

```cpp
struct ResourceControlBlock {
    void* resource;
    std::atomic<size_t> refCount;
    std::function<void()> onZeroRef;
};
```

然后 `ResourceHandle<T>` 只持有控制块指针，不再反向调用 manager 单例。

但这一项属于后续演进，不是这次改动的硬要求。

---

## 对 `ResourcePool<T>` 的改进建议

当前 `ResourcePool<T>` 作为 `ResourceManagerModule` 的内部模板类是合理的，但建议注意以下几点：

### 1. 保持内部实现地位
`ResourcePool<T>` 不应成为对外 API 的公共概念，尽量只在 manager 内部可见。

### 2. 释放策略明确化
当前存在两套释放路径：

- 默认 `Release()` 后 erase
- `onZeroCallFunc` 自定义策略

建议增加注释说明：

- `onZeroCallFunc` 是“零引用时接管资源后续处理”的回调
- 如果回调不 erase，则资源会继续保留在池中
- `onGetRestoreCallFunc` 的触发时机是“池中已有资源再次被获取时”

### 3. 日志与行为分离
现在模板实现中大量使用 `std::cout / std::cerr`。建议后续替换为项目统一日志系统，避免资源模块直接绑定标准输出。

本次重构中至少先把日志集中，避免在多个拆分文件里继续扩散。

---

## 对 include 依赖的改进要求

### 当前问题
当前公共头中包含：

- `<unordered_map>`
- `<mutex>`
- `<iostream>`

这些都属于相对重的依赖。

### 改进要求
拆分后应做到：

- `resource_loadable.h` 不包含 `<unordered_map> / <mutex> / <iostream>`
- `resource_handle.h` 不包含 `<unordered_map> / <mutex>`
- `resource_manager_module.h` 尽量少包含 `<iostream>`
- 只有 `.inl` 或 `.cpp` 需要时再引入重头

目标是降低公共头传播成本。

---

## 建议的重构步骤

请项目 AI 按以下顺序执行，不要一次性大改所有逻辑。

### 第一步：创建新文件骨架
创建以下文件：

- `resource_loadable.h`
- `resource_handle.h`
- `resource_handle.inl`
- `resource_load_options.h`
- `resource_load_options.inl`
- `resource_manager_module.h`
- `resource_manager_module.inl`
- `resource_manager_module.cpp`

### 第二步：迁移基础接口
将 `ILoadable`、`ILoadFromConfig` 移入 `resource_loadable.h`。

### 第三步：迁移 `LoadOptions<T>`
将 `LoadOptions<T>` 声明和工厂函数声明移入 `resource_load_options.h`；
将工厂函数模板实现移入 `resource_load_options.inl`。

### 第四步：迁移 `ResourceHandle<T>`
将声明移入 `resource_handle.h`；
将析构、拷贝、移动、赋值等模板实现移入 `resource_handle.inl`。

### 第五步：迁移 `ResourceManagerModule`
将类声明与模板声明放入 `resource_manager_module.h`；
将 `ResourcePool<T>` 实现与 manager 的模板函数实现移入 `resource_manager_module.inl`；
将非模板函数实现放入 `resource_manager_module.cpp`。

### 第六步：修正包含关系
确保：

- `.h` 只声明必要内容
- `.inl` 只承载模板实现
- `.cpp` 承载非模板实现
- 删除旧大头中的重复实现

### 第七步：保持外部调用不变
外部现有调用代码尽量不改，只修正 include 路径。

### 第八步：编译验证
至少验证：

1. `FromConfig<T>()` 可正常实例化
2. `FromPointer<T>()` 可正常实例化
3. `ResourceManagerModule::Get<T>()` 正常返回句柄
4. `ResourceHandle<T>` 拷贝/移动行为与当前逻辑一致
5. 资源归零后的默认释放与自定义释放策略不变

---

## 本次重构明确“不做”的事

为了控制风险，本次重构不要求同时完成以下内容：

1. 不强制重写 `ResourceHandle<T>` 的引用语义。
2. 不强制引入 `shared_ptr` 取代当前逻辑。
3. 不强制实现异步加载。
4. 不强制加入 `TextureAsset` 等资产层对象。
5. 不强制替换日志系统，只建议预留接口。
6. 不强制将单例 manager 改造成多实例 manager。

也就是说，本次重点是：

**整理结构，不大改行为。**

---

## 额外建议：为资产层扩展预留位置

后续如果计划加入：

- `TextureAsset`
- `MeshAsset`
- `ShaderAsset`
- `MaterialAsset`

建议未来结构继续扩展为：

```text
Asset Layer
    ↓
Resource Metadata Layer
    ↓
CPU Runtime Resource Layer
    ↓
GPU Runtime Resource Layer
```

当前这个 `ResourceManager` 更适合作为：

- 运行时资源管理器
- 或资产加载与运行时对象之间的桥接层

因此现在先把头文件拆干净，是有必要的。

---

## 项目 AI 执行要求

请项目 AI 按以下方式修改：

1. **优先做文件拆分，不要先重写底层逻辑。**
2. **保证现有对外接口尽量兼容。**
3. **不要引入新的循环依赖。**
4. **模板实现必须保留可见性，使用 `.inl` 方案。**
5. **所有非模板实现必须迁移到 `.cpp`。**
6. **修改完成后保证可编译。**
7. **如果某处必须轻微调整接口，需在代码中增加清晰注释说明原因。**

---

## 最终预期结果

重构完成后，应达到以下效果：

- 公共头职责清晰
- 编译依赖下降
- 模板实现集中管理
- manager / handle / options / loadable 层次分明
- 为后续 TextureAsset 等资产体系扩展留出空间

如果项目 AI 需要进一步执行第二阶段优化，则在本次结构重构稳定后，再进入：

- `ResourceHandle` 去单例耦合
- 资源控制块化
- 异步加载与状态机
- 资源域与场景级资源管理
- CPU/GPU 生命周期分层

