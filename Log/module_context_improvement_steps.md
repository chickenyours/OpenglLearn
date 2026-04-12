# module_context 改进步骤说明

## 1. 改进背景

当前模块系统里，`ModuleManager` 已经具备了比较完整的全局模块管理能力：

- 通过 `module_include_gen.h` 统一定义所有模块类型和固定槽位。
- 通过 `GetModule<T>()` 按类型查询模块。
- 通过 `QueryRaw(index)` 按槽位查询模块。
- 通过 `ModuleHostAPI` 把“上下文指针 + 槽位查询函数”暴露给插件使用。  
  这些能力已经在 `module_manager.h/.cpp` 中具备。fileciteturn0file2 fileciteturn0file3

但是 `module_context.h` 目前还是一个未完成的空壳，只定义了一个：

```cpp
template <typename ...Modules>
class ModuleContextManager{
public:
    ModuleManager* manager;
    ModuleContextManager(){
    }
};
```

它还没有真正承担“主程序和插件共享模块上下文”的职责。fileciteturn0file1

因此，需要把 `module_context` 体系补全，让它成为：

- **主程序中的全局模块访问快照**
- **插件中的统一模块访问入口**
- **跨 DLL / 跨 CRT 时共享固定槽位语义的桥梁**

---

## 2. 现有问题

### 2.1 `ModuleManager` 已有全局槽位，但 `ModuleContext` 没接上

`module_include_gen.h` 已经定义了模块总数、每个模块类型对应的固定索引，以及类型到槽位的映射关系，例如：

- `Application::ApplicationModule -> 0`
- `Render::RenderModule -> 1`
- `Resource::ResourceManagerModule -> 2`  
  这些映射是整个系统共享模块语义的基础。fileciteturn0file4

但 `module_context.h` 没有真正利用这些槽位定义。

---

### 2.2 插件和主程序缺少统一的“上下文载体”

虽然 `ModuleHostAPI` 已经允许插件通过索引回调查询模块：

```cpp
struct ModuleHostAPI {
    void* context = nullptr;
    void* (*query_by_index)(void* context, std::size_t index) noexcept = nullptr;
};
```

但是缺少一个统一封装层，导致：

- 主程序里直接用 `ModuleManager`
- 插件里可能直接用 `ModuleHostAPI`
- 两边调用风格不统一
- 插件很容易重复写一套查询代码  
  这些问题都会增加接口分裂和维护成本。fileciteturn0file2

---

### 2.3 `module_context_gen.h` 没承担“生成上下文类型列表”的职责

目前 `module_context_gen.h` 几乎为空。fileciteturn0file0

但从设计目标看，它更适合承担：

- 在编译期生成“本上下文要初始化哪些模块类型”的列表
- 让主程序和插件都能通过统一的生成结果得到相同的上下文模板类型

也就是把“上下文初始化哪些模块”这个问题，从手写改成自动生成。

---

## 3. 改进目标

这次改进的目标可以分成四条：

### 3.1 建立统一的 `ModuleContext`

设计一个非模板基类 `ModuleContext`，内部只保存：

- 一个按全局槽位编号存储的 `void* slots_[kModuleCount]`

这样所有模块访问最终都归一到同一个结构上，而不是每个插件自己定义一套成员变量。

---

### 3.2 建立模板化的 `ModuleContextManager<Modules...>`

模板参数 `Modules...` 表示“本上下文需要初始化哪些模块类型”。

初始化时：

- 根据类型参数拿到全局槽位编号
- 向 `ModuleManager` 或 `ModuleHostAPI` 查询模块指针
- 把结果写入 `slots_[]`

这样既保留了模板类型安全，又不会破坏全局槽位一致性。

---

### 3.3 支持两种初始化来源

上下文应同时支持：

1. **从主程序 `ModuleManager` 初始化**
2. **从插件 `ModuleHostAPI` 初始化**

这样主程序和插件都能使用同一套 `ModuleContext` 调用方式。

---

### 3.4 让 `module_context_gen.h` 负责生成上下文模板参数

例如定义：

```cpp
#define MODULE_CONTEXT_GEN_TYPES \
    Application::ApplicationModule, \
    Render::RenderModule, \
    Resource::ResourceManagerModule
```

然后统一生成：

```cpp
using GeneratedModuleContext = ModuleContextManager<MODULE_CONTEXT_GEN_TYPES>;
```

这样不同插件只要使用同一份生成规则，就能共享相同槽位语义。

---

## 4. 具体改进步骤

## 步骤一：保留 `ModuleManager` 作为唯一模块源

这一阶段不需要推翻 `ModuleManager`，因为它已经完成了最核心的事情：

- 内部持有所有模块实例
- 通过 `moduleTable_[]` 建立槽位表
- 提供按槽位查询与按类型查询
- 提供 `ModuleHostAPI` 给插件查询

因此 `ModuleManager` 仍然作为**系统唯一真实模块拥有者**。`ModuleContext` 不拥有模块对象，只缓存模块指针。fileciteturn0file2 fileciteturn0file3

### 这一阶段的设计原则

- 模块实例只存在于 `ModuleManager`
- `ModuleContext` 只是访问层，不负责生命周期
- 插件永远不直接 new 这些模块

---

## 步骤二：新增统一的 `ModuleContext` 基类

引入一个非模板基类：

```cpp
class ModuleContext {
public:
    ModuleContext() noexcept;

    void Reset() noexcept;
    bool IsBound(std::size_t index) const noexcept;

    template<typename T>
    T* Get() noexcept;

    template<typename T>
    const T* Get() const noexcept;

protected:
    void SetSlot(std::size_t index, void* ptr) noexcept;
    void* GetRaw(std::size_t index) noexcept;
    const void* GetRaw(std::size_t index) const noexcept;

private:
    void* slots_[ModuleIncludeGen::kModuleCount];
};
```

### 这样做的意义

1. 统一所有上下文的数据布局
2. 统一主程序/插件访问模块的方法
3. 模块访问最终都通过固定槽位完成

此时 `Get<T>()` 的行为是：

- 根据 `ModuleSlot<T>::kIndex` 找到槽位
- 返回对应指针

这就把“类型参数访问”和“全局槽位访问”结合起来了。槽位信息来自 `module_include_gen.h`。fileciteturn0file4

---

## 步骤三：新增模板 `ModuleContextManager<Modules...>`

在 `ModuleContext` 基础上，加入模板派生类：

```cpp
template<typename... Modules>
class ModuleContextManager : public ModuleContext {
public:
    bool Initialize(ModuleManager& manager) noexcept;
    bool Initialize(const ModuleHostAPI& api) noexcept;
};
```

### 它的职责

只负责“把模板参数列表中的模块填入上下文”。

例如：

```cpp
ModuleContextManager<
    Application::ApplicationModule,
    Render::RenderModule
> context;
```

初始化时只会填充：

- `Application::ApplicationModule` 对应槽位
- `Render::RenderModule` 对应槽位

其他槽位保持空指针。

### 这样做的优点

- 插件不必加载全部模块
- 只初始化自己声明依赖的模块
- 但底层槽位编号依然全局统一

这就是“模板裁剪初始化范围”和“全局共享槽位语义”之间的平衡。

---

## 步骤四：实现从 `ModuleManager` 初始化

实现：

```cpp
bool Initialize(ModuleManager& manager) noexcept;
```

逻辑是：

1. `Reset()` 清空所有槽位
2. 对 `Modules...` 做参数包展开
3. 针对每个 `T`：
   - 取 `ModuleIncludeGen::ModuleSlot<T>::kIndex`
   - 调用 `manager.GetModule<T>()`
   - 写入 `slots_[index]`
4. 若某个必须模块为空，则初始化失败

### 作用

主程序里可以直接构造一个上下文快照：

```cpp
GeneratedModuleContext context;
context.Initialize(ModuleManager::Instance());
```

后续业务代码统一从 `context.Get<T>()` 访问模块。

---

## 步骤五：实现从 `ModuleHostAPI` 初始化

实现：

```cpp
bool Initialize(const ModuleHostAPI& api) noexcept;
```

逻辑是：

1. `Reset()` 清空槽位
2. 对 `Modules...` 展开
3. 对每个 `T` 调用 `QueryModuleFromHostAPI<T>(api)`
4. 将结果放入对应槽位

### 作用

插件在入口中拿到主程序传来的 `ModuleHostAPI` 后，可以直接创建和主程序一致的上下文：

```cpp
GeneratedModuleContext context;
context.Initialize(host_api);
```

这样插件不需要理解主程序模块的内部结构，只要按类型参数获取指针即可。`QueryModuleFromHostAPI` 本身已经在 `module_manager.h` 中准备好了。fileciteturn0file2

---

## 步骤六：让 `module_context_gen.h` 生成上下文模板参数

把原先空白的 `module_context_gen.h` 改成“编译期上下文模块列表生成器”。

例如：

```cpp
#define MODULE_CONTEXT_GEN_TYPES \
    Application::ApplicationModule, \
    Render::RenderModule, \
    Resource::ResourceManagerModule
```

然后在 `module_context.h` 中写：

```cpp
using GeneratedModuleContext = ModuleContextManager<MODULE_CONTEXT_GEN_TYPES>;
```

### 这样做的意义

- 主程序和插件都不再手写长模板参数
- 自动生成结果可由构建系统维护
- 一旦模块表变更，只需要重新生成这个头文件

这一步相当于把“使用哪些模块”也纳入代码生成流程。

---

## 步骤七：统一主程序和插件的调用方式

经过上面几步后，最终调用方式会变成：

### 主程序

```cpp
GeneratedModuleContext context;
if (!context.Initialize(ModuleManager::Instance())) {
    return false;
}

auto* app = context.Get<Application::ApplicationModule>();
auto* render = context.Get<Render::RenderModule>();
```

### 插件

```cpp
extern "C" bool PluginStartup(const ModuleHostAPI* host_api) {
    if (host_api == nullptr) {
        return false;
    }

    GeneratedModuleContext context;
    if (!context.Initialize(*host_api)) {
        return false;
    }

    auto* render = context.Get<Render::RenderModule>();
    return render != nullptr;
}
```

### 统一后的收益

- 主程序和插件写法一致
- 都通过 `Get<T>()` 获取模块
- 插件不需要关心 `QueryRaw(index)` 细节
- 类型安全和槽位共享同时保留

---

## 5. 最终结构关系

改进完成后，整体职责可以概括为：

### `module_include_gen.h`
负责：

- 定义全局模块列表
- 定义槽位编号
- 定义类型到槽位的映射关系  
  它是整个系统“全局模块 ABI 语义”的来源。fileciteturn0file4

### `module_manager.h/.cpp`
负责：

- 真正拥有模块实例
- 启动、关闭模块
- 建立槽位表
- 给主程序和插件提供查询能力  
  它是“模块实例拥有者”。fileciteturn0file2 fileciteturn0file3

### `module_context.h`
负责：

- 提供统一的上下文访问容器
- 缓存模块指针，不拥有对象
- 封装按类型获取模块的逻辑
- 支持从 `ModuleManager` 和 `ModuleHostAPI` 初始化

### `module_context_gen.h`
负责：

- 生成当前上下文所需模块类型列表
- 让主程序和插件都能用同一份生成结果构造上下文

---

## 6. 为什么这种方式适合跨插件共享

因为跨插件共享的真正关键，不是“类名一样”，而是：

1. **所有人看到的槽位编号一致**
2. **所有人都通过相同查询协议取模块**
3. **上下文只传递原始指针，不跨边界传递复杂容器实现**

你的现有设计里，`ModuleHostAPI + 固定槽位索引` 已经具备跨 DLL / 跨 CRT 的基础。fileciteturn0file2turn0file4

这次改进只是把这套能力从“底层能用”提升到“上层可统一使用”：

- 底层：`ModuleManager` / `ModuleHostAPI`
- 上层：`ModuleContext`

于是主程序和插件都能共享同一套模块上下文语义。

---

## 7. 后续可继续增强的方向

### 7.1 支持“可选模块”和“必需模块”区分

当前初始化可以默认要求所有模板参数模块都必须成功绑定。

后续可以扩展为：

- `Require<T>`：绑定失败则初始化失败
- `Optional<T>`：绑定失败则留空，允许继续运行

这样插件依赖声明会更灵活。

---

### 7.2 支持自动校验插件依赖

可以在插件初始化时，对其 `GeneratedModuleContext` 中声明的模块逐个检查，若宿主未提供则报错。

这可以形成一套更稳定的插件依赖检测机制。

---

### 7.3 支持更细粒度的代码生成

当前 `module_context_gen.h` 可以先生成全量模块列表。

后续可以改成：

- 主程序生成全量 `GeneratedModuleContext`
- 每个插件生成自己的最小依赖列表

这样既统一 ABI，又减少无意义初始化。

---

## 8. 总结

这次 `module_context` 的改进，本质上是在现有模块系统上补上“统一访问层”。

### 改进前

- `ModuleManager` 有全局槽位和查询能力
- `ModuleHostAPI` 有跨插件查询能力
- 但 `ModuleContext` 还是空壳
- 主程序和插件没有统一的上下文访问方式

### 改进后

- `ModuleManager` 仍然是唯一模块拥有者
- `ModuleContext` 成为统一模块访问容器
- `ModuleContextManager<Modules...>` 负责按模板参数初始化模块指针
- `module_context_gen.h` 负责生成上下文模板参数列表
- 主程序和插件都能基于同一套固定槽位共享模块上下文

这套结构最大的价值是：

**既保留了模板类型安全，又保留了跨插件共享时最重要的固定槽位 ABI 语义。**
