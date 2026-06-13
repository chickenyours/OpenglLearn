# Render 模块使用文档

## 1. 模块作用

`RenderModule` 是 `Module/Render` 模块的核心入口类，它：

- 继承自 `IModule` 接口，可被应用程序或 ECS 统一管理
- 管理图形库平台状态（窗口、上下文、函数加载器）
- 负责 OpenGL 函数指针的加载
- 在启动时检查环境合法性
- 提供 RHI 设备创建入口

## 2. 启动前置条件

在调用 `RenderModule::Startup()` 之前，应用程序必须确保：

1. **窗口系统已初始化**（如 GLFW、SDL 等）
2. **OpenGL Context 已创建**并成为当前线程的活动上下文
3. **已准备好 OpenGL 函数地址获取函数**（如 `glfwGetProcAddress`）

## 3. 与 Application 模块配合使用（推荐）

`RenderModule` 依赖 `ApplicationModule`，可以直接从 `ApplicationModule` 初始化：

```cpp
#include "Application/public/application_module.h"
#include "Render/Public/render_module.h"

int main() {
    // 1. 创建并启动 Application 模块
    Application::ApplicationModule appModule;
    if (!appModule.Startup()) {
        return -1;
    }

    // 2. 创建 Render 模块并从 Application 初始化
    Render::RenderModule renderModule;
    if (!renderModule.InitializeFromApplication(appModule)) {
        return -1;
    }

    // 3. 使用 Render 模块进行渲染
    auto* device = renderModule.GetDevice();
    // ... 渲染逻辑 ...

    // 4. 清理
    renderModule.Shutdown();
    appModule.Shutdown();
    return 0;
}
```

## 4. OpenGL Loader 设置方法

模块使用 `glad` 作为 OpenGL 函数加载器。应用程序需要通过 `SetGLProcAddressLoader()` 传入一个函数指针获取器。

### 函数签名

```cpp
using GLProcAddressLoader = void* (*)(const char* name);
```

### 常见平台的 Loader

| 平台 | Loader 函数 |
|------|-------------|
| GLFW | `glfwGetProcAddress` |
| SDL  | `SDL_GL_GetProcAddress` |
| Windows/WGL | 自定义加载函数 |

## 5. Startup / Shutdown 用法

### 基本用法（手动模式）

```cpp
#include "Render/Public/render_module.h"

Module::Render::RenderModule renderModule;

// 1. 设置平台状态
renderModule.SetPlatformReady(true);

// 2. 设置上下文状态
renderModule.SetContextReady(true);

// 3. 设置 OpenGL 函数加载器
renderModule.SetGLProcAddressLoader(&glfwGetProcAddress);

// 4. 启动模块
if (!renderModule.Startup()) {
    // 处理启动失败
    return -1;
}

// ... 使用模块 ...

// 5. 关闭模块
renderModule.Shutdown();
```

### 状态检查

```cpp
// 检查是否已启动
if (renderModule.IsStarted()) {
    // 模块已就绪
}

// 获取当前平台状态
auto state = renderModule.GetPlatformState();
switch (state) {
    case Render::GraphicsLibraryPlatformState::Ready:
        // 完全就绪
        break;
    case Render::GraphicsLibraryPlatformState::Error:
        // 发生错误
        break;
    // ... 其他状态
}
```

## 6. 与应用程序配合方式

应用程序负责创建窗口和 OpenGL 上下文，然后将其传递给 `RenderModule`。

### 使用 ApplicationModule 的完整示例

```cpp
#include "Application/public/application_module.h"
#include "Render/Public/render_module.h"

int main() {
    // 1. 创建 Application 模块并配置
    Application::ApplicationModule appModule;
    Application::ApplicationConfig config;
    config.width = 800;
    config.height = 600;
    config.title = "OpenGL Window";
    config.majorVersion = 4;
    config.minorVersion = 6;
    appModule.SetConfig(config);

    // 2. 启动 Application 模块（创建 GLFW 窗口和 OpenGL 上下文）
    if (!appModule.Startup()) {
        return -1;
    }

    // 3. 创建并初始化 Render 模块
    Render::RenderModule renderModule;
    if (!renderModule.InitializeFromApplication(appModule)) {
        return -1;
    }

    // 4. 主循环
    while (!glfwWindowShouldClose(appModule.GetWindow())) {
        glClear(GL_COLOR_BUFFER_BIT);
        // ... 渲染逻辑 ...
        glfwSwapBuffers(appModule.GetWindow());
        glfwPollEvents();
    }

    // 5. 清理
    renderModule.Shutdown();
    appModule.Shutdown();
    return 0;
}
```

## 7. 与 ECS 配合方式

ECS 系统不应直接依赖 OpenGL 具体类型，而应通过 `RenderModule` 或 `RHI::Device` 进行交互。

### 推荐模式

```cpp
// ECS 系统持有 RenderModule 的引用或指针
class RenderSystem {
public:
    RenderSystem(Render::RenderModule& renderModule)
        : renderModule_(renderModule) {}

    void Update() {
        auto* device = renderModule_.GetDevice();
        if (device) {
            // 通过 RHI 接口进行渲染操作
            // 不直接依赖 GLDevice、GLTexture 等具体类型
        }
    }

private:
    Render::RenderModule& renderModule_;
};
```

### ECS 集成步骤

1. 应用程序创建并启动 `ApplicationModule`
2. 应用程序创建并启动 `RenderModule`（通过 `InitializeFromApplication`）
3. ECS 调度系统通过模块对象获取 `Device`
4. ECS 系统只调用抽象接口，不依赖具体 OpenGL 类型

## 8. 示例代码

### 最小可用示例

```cpp
#include "Application/public/application_module.h"
#include "Render/Public/render_module.h"

int main() {
    Application::ApplicationModule app;
    app.Startup();

    Render::RenderModule renderModule;
    renderModule.InitializeFromApplication(app);

    // 获取设备并进行渲染
    auto* device = renderModule.GetDevice();
    // ...

    renderModule.Shutdown();
    app.Shutdown();
    return 0;
}
```

### 状态机示例

```cpp
Render::RenderModule module;

// 逐步设置状态
module.SetPlatformReady(true);
assert(module.GetPlatformState() == Render::GraphicsLibraryPlatformState::PlatformReady);

module.SetContextReady(true);
assert(module.GetPlatformState() == Render::GraphicsLibraryPlatformState::ContextReady);

module.SetGLProcAddressLoader(&glfwGetProcAddress);

// 启动后进入 Ready 状态
if (module.Startup()) {
    assert(module.GetPlatformState() == Render::GraphicsLibraryPlatformState::Ready);
}
```

## 9. 平台状态说明

| 状态 | 说明 |
|------|------|
| `Uninitialized` | 未初始化 |
| `PlatformReady` | 窗口系统已就绪 |
| `ContextReady` | OpenGL 上下文已就绪 |
| `FunctionLoaderReady` | 函数加载器已就绪 |
| `Ready` | 模块完全就绪 |
| `Error` | 发生错误 |

## 10. 注意事项

1. **不要重复调用 `Startup()`**：模块会检测 `started_` 状态，重复调用会直接返回 `true`
2. **`Shutdown()` 可安全重复调用**：内部有状态检查
3. **确保 Context 存在**：`glad` 加载必须在有效的 OpenGL 上下文中进行
4. **错误处理**：`Startup()` 失败时返回 `false`，不会抛出异常
5. **推荐依赖 ApplicationModule**：使用 `InitializeFromApplication()` 方法可以自动处理所有前置条件
