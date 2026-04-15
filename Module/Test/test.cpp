#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "module_manager.h"
#include "application_module.h"
#include "render_module.h"
#include "resource_manager_module.h"

// OpenGL 渲染三角形的主程序代码
void RenderTriangle(GLFWwindow* window) {
    // 创建顶点着色器
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

    // 创建片段着色器
    const char* fragmentShaderSource = R"(
        #version 460 core
        in vec3 ourColor;
        out vec4 FragColor;
        void main() {
            FragColor = vec4(ourColor, 1.0);
        }
    )";

    // 编译顶点着色器
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, nullptr);
    glCompileShader(vertexShader);

    // 检查编译结果
    GLint success;
    GLchar infoLog[512];
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vertexShader, 512, nullptr, infoLog);
        std::cerr << "Vertex Shader Compilation Error: " << infoLog << std::endl;
        return;
    }

    // 编译片段着色器
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, nullptr);
    glCompileShader(fragmentShader);

    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(fragmentShader, 512, nullptr, infoLog);
        std::cerr << "Fragment Shader Compilation Error: " << infoLog << std::endl;
        return;
    }

    // 链接着色器程序
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

    // 三角形顶点数据 (位置 + 颜色)
    float vertices[] = {
        // 位置              // 颜色
         0.0f,  0.5f, 0.0f,  1.0f, 0.0f, 0.0f,  // 顶点上 (红色)
         0.5f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f,  // 顶点右下 (绿色)
        -0.5f, -0.5f, 0.0f,  0.0f, 0.0f, 1.0f   // 顶点左下 (蓝色)
    };

    // 创建 VAO 和 VBO
    GLuint VAO, VBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // 位置属性 (location = 0)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // 颜色属性 (location = 1)
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    // 渲染循环
    std::cout << "Starting render loop... Press ESC to exit." << std::endl;

    while (!glfwWindowShouldClose(window)) {
        // 输入处理
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, true);
        }

        // 清屏
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // 绘制三角形
        glUseProgram(shaderProgram);
        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);

        // 交换缓冲
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // 清理资源
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteProgram(shaderProgram);

    std::cout << "Render loop ended." << std::endl;
}

int main() {
    std::cout << "=== Module System Test ===" << std::endl;

    // 1. 获取 ModuleManager 实例并启动
    auto& manager = ModuleManager::Instance();

    std::cout << "ModuleManager instance acquired." << std::endl;

    // 2. 启动模块系统
    if (!manager.Startup()) {
        std::cerr << "Failed to startup ModuleManager!" << std::endl;
        return -1;
    }

    std::cout << "ModuleManager started successfully!" << std::endl;

    // 3. 获取 Application 模块
    auto* appModule = manager.GetModule<Application::ApplicationModule>();
    if (appModule == nullptr) {
        std::cerr << "Failed to get ApplicationModule!" << std::endl;
        return -2;
    }

    std::cout << "ApplicationModule acquired: " << appModule->GetName() << std::endl;
    std::cout << "ApplicationModule started: " << (appModule->IsStarted() ? "yes" : "no") << std::endl;

    // 4. 获取 Render 模块
    auto* renderModule = manager.GetModule<Render::RenderModule>();
    if (renderModule == nullptr) {
        std::cerr << "Failed to get RenderModule!" << std::endl;
        return -3;
    }

    std::cout << "RenderModule acquired: " << renderModule->GetName() << std::endl;
    std::cout << "RenderModule started: " << (renderModule->IsStarted() ? "yes" : "no") << std::endl;
    std::cout << "RenderModule platform state: "
              << static_cast<int>(renderModule->GetPlatformState()) << std::endl;

    // 5. 获取 ResourceManager 模块
    auto* resourceManager = manager.GetModule<Resource::ResourceManagerModule>();
    if (resourceManager == nullptr) {
        std::cerr << "Failed to get ResourceManagerModule!" << std::endl;
        return -5;
    }

    std::cout << "ResourceManagerModule acquired: " << resourceManager->GetName() << std::endl;
    std::cout << "ResourceManagerModule started: " << (resourceManager->IsStarted() ? "yes" : "no") << std::endl;

    // 测试资源加载功能 - 使用 FromGenerator 加载一个简单资源
    // 新设计：资源类型无需继承 ILoadable
    struct TestResource {
        int value = 0;
        
        TestResource() {
            std::cout << "Creating TestResource..." << std::endl;
            value = 42;
        }
        
        ~TestResource() {
            std::cout << "TestResource released, value=" << value << std::endl;
        }
    };

    auto testResourceHandle = resourceManager->Get<TestResource>(
        Resource::FromGenerator<TestResource>(
            "test_resource",
            []() -> std::unique_ptr<TestResource> {
                return std::make_unique<TestResource>();
            }
        )
    );

    if (testResourceHandle) {
        std::cout << "TestResource loaded successfully, value=" << testResourceHandle->value << std::endl;
    } else {
        std::cerr << "Failed to load TestResource!" << std::endl;
    }

    // 6. 获取窗口并渲染三角形
    GLFWwindow* window = appModule->GetWindow();
    if (window == nullptr) {
        std::cerr << "Failed to get window from ApplicationModule!" << std::endl;
        return -4;
    }

    std::cout << "Window acquired successfully!" << std::endl;

    // 7. 调用渲染函数绘制三角形
    RenderTriangle(window);

    // 8. 关闭模块系统
    std::cout << "Shutting down ModuleManager..." << std::endl;
    manager.Shutdown();
    std::cout << "ModuleManager shutdown complete." << std::endl;

    std::cout << "=== Test Complete ===" << std::endl;
    return 0;
}
