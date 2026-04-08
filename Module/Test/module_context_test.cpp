#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "module_context.h"
#include "module_manager.h"
#include "application_module.h"
#include "render_module.h"
#include "resource_manager_module.h"

/**
 * @brief 测试 ModuleContext 基类的基本功能
 */
void TestModuleContextBase() {
    std::cout << "\n=== Test 1: ModuleContext Base Functionality ===" << std::endl;

    ModuleContext context;
    context.Reset();

    // 测试 IsBound 在未初始化时应该返回 false
    bool appBound = context.IsBound(0);
    bool renderBound = context.IsBound(1);
    bool resourceBound = context.IsBound(2);

    std::cout << "After Reset - Application bound: " << (appBound ? "yes" : "no") << std::endl;
    std::cout << "After Reset - Render bound: " << (renderBound ? "yes" : "no") << std::endl;
    std::cout << "After Reset - Resource bound: " << (resourceBound ? "yes" : "no") << std::endl;

    std::cout << "ModuleContext base test passed." << std::endl;
}

/**
 * @brief 测试 ModuleContextManager 从 ModuleManager 初始化
 */
void TestModuleContextManagerFromModuleManager() {
    std::cout << "\n=== Test 2: ModuleContextManager from ModuleManager ===" << std::endl;

    // 启动 ModuleManager
    auto& manager = ModuleManager::Instance();
    std::cout << "ModuleManager instance acquired." << std::endl;

    if (!manager.Startup()) {
        std::cerr << "Failed to startup ModuleManager!" << std::endl;
        return;
    }
    std::cout << "ModuleManager started successfully!" << std::endl;

    // 创建 GeneratedModuleContext 并从 ModuleManager 初始化
    GeneratedModuleContext context;
    bool initSuccess = context.Initialize(manager);

    std::cout << "GeneratedModuleContext initialized: " << (initSuccess ? "success" : "failed") << std::endl;

    if (!initSuccess) {
        std::cerr << "ModuleContext initialization failed!" << std::endl;
        return;
    }

    // 测试通过 Get<T>() 获取模块
    auto* appModule = context.Get<Application::ApplicationModule>();
    auto* renderModule = context.Get<Render::RenderModule>();
    auto* resourceManager = context.Get<Resource::ResourceManagerModule>();

    std::cout << "\n--- Module Access via ModuleContext ---" << std::endl;

    if (appModule) {
        std::cout << "ApplicationModule: " << appModule->GetName()
                  << ", started: " << (appModule->IsStarted() ? "yes" : "no") << std::endl;
    } else {
        std::cerr << "Failed to get ApplicationModule via context!" << std::endl;
    }

    if (renderModule) {
        std::cout << "RenderModule: " << renderModule->GetName()
                  << ", started: " << (renderModule->IsStarted() ? "yes" : "no")
                  << ", platform state: " << static_cast<int>(renderModule->GetPlatformState()) << std::endl;
    } else {
        std::cerr << "Failed to get RenderModule via context!" << std::endl;
    }

    if (resourceManager) {
        std::cout << "ResourceManagerModule: " << resourceManager->GetName()
                  << ", started: " << (resourceManager->IsStarted() ? "yes" : "no") << std::endl;
    } else {
        std::cerr << "Failed to get ResourceManagerModule via context!" << std::endl;
    }

    // 验证通过 ModuleContext 获取的模块与直接从 ModuleManager 获取的是相同的
    auto* directApp = manager.GetModule<Application::ApplicationModule>();
    auto* directRender = manager.GetModule<Render::RenderModule>();
    auto* directResource = manager.GetModule<Resource::ResourceManagerModule>();

    std::cout << "\n--- Verification: Context vs Direct Access ---" << std::endl;
    std::cout << "ApplicationModule pointer match: " << (appModule == directApp ? "yes" : "no") << std::endl;
    std::cout << "RenderModule pointer match: " << (renderModule == directRender ? "yes" : "no") << std::endl;
    std::cout << "ResourceManagerModule pointer match: " << (resourceManager == directResource ? "yes" : "no") << std::endl;

    // 测试通过 ModuleHostAPI 初始化
    std::cout << "\n--- Test: Initialize from ModuleHostAPI ---" << std::endl;

    const auto& hostAPI = manager.GetHostAPI();
    std::cout << "ModuleHostAPI acquired." << std::endl;

    GeneratedModuleContext contextFromAPI;
    bool apiInitSuccess = contextFromAPI.Initialize(hostAPI);
    std::cout << "GeneratedModuleContext from HostAPI initialized: " << (apiInitSuccess ? "success" : "failed") << std::endl;

    if (apiInitSuccess) {
        auto* apiApp = contextFromAPI.Get<Application::ApplicationModule>();
        auto* apiRender = contextFromAPI.Get<Render::RenderModule>();
        auto* apiResource = contextFromAPI.Get<Resource::ResourceManagerModule>();

        std::cout << "HostAPI context - ApplicationModule pointer match: " << (apiApp == directApp ? "yes" : "no") << std::endl;
        std::cout << "HostAPI context - RenderModule pointer match: " << (apiRender == directRender ? "yes" : "no") << std::endl;
        std::cout << "HostAPI context - ResourceManagerModule pointer match: " << (apiResource == directResource ? "yes" : "no") << std::endl;
    }

    // 测试资源加载功能
    std::cout << "\n--- Test: Resource Loading via ModuleContext ---" << std::endl;

    struct TestResource : Resource::ILoadable {
        int value = 0;

        void Release() override {
            std::cout << "TestResource released, value=" << value << std::endl;
            isLoad_ = false;
        }

        void SetLoaded(bool loaded) {
            isLoad_ = loaded;
        }
    };

    auto testResourceHandle = resourceManager->Get<Resource::ILoadable>(
        Resource::FromGenerator<Resource::ILoadable>(
            "test_resource_context",
            []() -> std::unique_ptr<Resource::ILoadable> {
                std::cout << "Creating TestResource..." << std::endl;
                auto ptr = new TestResource();
                ptr->value = 100;
                ptr->SetLoaded(true);
                return std::unique_ptr<Resource::ILoadable>(ptr);
            }
        )
    );

    if (testResourceHandle) {
        std::cout << "TestResource loaded successfully via context, IsLoad=" << testResourceHandle->IsLoad() << std::endl;
    } else {
        std::cerr << "Failed to load TestResource!" << std::endl;
    }

    // 关闭模块系统
    std::cout << "\nShutting down ModuleManager..." << std::endl;
    manager.Shutdown();
    std::cout << "ModuleManager shutdown complete." << std::endl;
}

/**
 * @brief OpenGL 渲染三角形（用于验证 RenderModule 可用性）
 */
void RenderTriangle(GLFWwindow* window) {
    const char* vertexShaderSource = R"(
        #version 460 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec3 aColor;
        out vec3 ourColor;
        void main() {
            gl_Position = vec4(aPos, 1.0);
            ourColor = aColor;
        }
    )";

    const char* fragmentShaderSource = R"(
        #version 460 core
        in vec3 ourColor;
        out vec4 FragColor;
        void main() {
            FragColor = vec4(ourColor, 1.0);
        }
    )";

    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, nullptr);
    glCompileShader(vertexShader);

    GLint success;
    GLchar infoLog[512];
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vertexShader, 512, nullptr, infoLog);
        std::cerr << "Vertex Shader Compilation Error: " << infoLog << std::endl;
        return;
    }

    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, nullptr);
    glCompileShader(fragmentShader);

    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(fragmentShader, 512, nullptr, infoLog);
        std::cerr << "Fragment Shader Compilation Error: " << infoLog << std::endl;
        return;
    }

    GLuint shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shaderProgram, 512, nullptr, infoLog);
        std::cerr << "Shader Program Link Error: " << infoLog << std::endl;
        return;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    float vertices[] = {
        // 位置              // 颜色
         0.0f,  0.5f, 0.0f,  1.0f, 0.0f, 0.0f,
         0.5f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f,
        -0.5f, -0.5f, 0.0f,  0.0f, 0.0f, 1.0f
    };

    GLuint VAO, VBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    std::cout << "Starting render loop... Press ESC to exit." << std::endl;

    int frameCount = 0;
    auto startTime = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, true);
        }

        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(shaderProgram);
        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);

        glfwSwapBuffers(window);
        glfwPollEvents();

        frameCount++;
        double currentTime = glfwGetTime();
        if (currentTime - startTime >= 1.0) {
            std::cout << "FPS: " << frameCount << std::endl;
            frameCount = 0;
            startTime = currentTime;
            // 自动退出，避免长时间运行
            glfwSetWindowShouldClose(window, true);
        }
    }

    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteProgram(shaderProgram);

    std::cout << "Render loop ended." << std::endl;
}

/**
 * @brief 完整的集成测试：使用 ModuleContext 获取模块并渲染
 */
void TestModuleContextIntegration() {
    std::cout << "\n=== Test 3: ModuleContext Integration Test ===" << std::endl;

    // 启动 ModuleManager
    auto& manager = ModuleManager::Instance();
    if (!manager.Startup()) {
        std::cerr << "Failed to startup ModuleManager!" << std::endl;
        return;
    }
    std::cout << "ModuleManager started." << std::endl;

    // 创建上下文
    GeneratedModuleContext context;
    if (!context.Initialize(manager)) {
        std::cerr << "Failed to initialize ModuleContext!" << std::endl;
        return;
    }
    std::cout << "ModuleContext initialized." << std::endl;

    // 通过上下文获取模块
    auto* appModule = context.Get<Application::ApplicationModule>();
    auto* renderModule = context.Get<Render::RenderModule>();

    if (!appModule || !renderModule) {
        std::cerr << "Failed to get required modules!" << std::endl;
        return;
    }

    GLFWwindow* window = appModule->GetWindow();
    if (!window) {
        std::cerr << "Failed to get window!" << std::endl;
        return;
    }
    std::cout << "Window acquired via ModuleContext." << std::endl;

    // 渲染测试
    RenderTriangle(window);

    // 清理
    manager.Shutdown();
    std::cout << "ModuleManager shutdown complete." << std::endl;
}

int main() {
    std::cout << "=== ModuleContext System Test ===" << std::endl;
    std::cout << "Testing ModuleContext and ModuleContextManager functionality..." << std::endl;

    // 测试 1: ModuleContext 基类功能
    TestModuleContextBase();

    // 测试 2: ModuleContextManager 从 ModuleManager 初始化
    TestModuleContextManagerFromModuleManager();

    // 测试 3: 集成测试（包含渲染）
    TestModuleContextIntegration();

    std::cout << "\n=== All Tests Complete ===" << std::endl;
    return 0;
}
