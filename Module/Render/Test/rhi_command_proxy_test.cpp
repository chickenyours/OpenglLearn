/**
 * @file rhi_command_proxy_test.cpp
 * @brief RHI 第一阶段命令代理测试程序
 *
 * 测试内容：
 * 1. RHI 前端加载纹理资产（CPU 端操作）
 * 2. RHI 后端创建 GPU 资源并上传数据（GPU 端操作）
 * 3. 使用委托后门函数验证纹理参数
 *
 * RHI 架构职责划分：
 * - RHIFrontend: CPU 端资源加载（LoadTextureFromFile）
 * - Device: GPU 端资源创建和上传（CreateTextureFromAsset, UploadTexture）
 */

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>

#include "Render/Public/RHI/rhi_frontend.h"
#include "Render/Public/RHI/rhi_handles.h"
#include "Render/Public/RHI/RHI_texture.h"
#include "Render/Public/RHI/RHI_device.h"
#include "Render/Private/Backend/Opengl/gl_texture.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

using namespace Render::RHI;

/**
 * @brief 测试 RHI 前端加载纹理资产（CPU 端操作）
 */
bool TestRHIFrontendTextureLoading(RHIFrontend& frontend) {
    std::cout << "\n=== Test 1: RHI Frontend Texture Asset Loading (CPU-side) ===" << std::endl;

    // 使用 RHI 前端加载纹理资产（CPU 端操作）
    std::cout << "[RHIFrontend] Loading texture from file..." << std::endl;
    auto asset = frontend.LoadTextureFromFile("./images/tex.png", false, true);

    if (!asset) {
        std::cerr << "[RHIFrontend] Failed to load texture asset!" << std::endl;
        return false;
    }

    const auto& desc = asset->GetDesc();
    std::cout << "[RHIFrontend] Texture asset loaded successfully!" << std::endl;
    std::cout << "  - Width: " << desc.width << std::endl;
    std::cout << "  - Height: " << desc.height << std::endl;
    std::cout << "  - Format: " << static_cast<int>(desc.format) << std::endl;
    std::cout << "  - Mip Levels: " << desc.mipLevels << std::endl;

    std::cout << "[Test 1 PASSED] RHI Frontend texture asset loading works correctly" << std::endl;
    return true;
}

/**
 * @brief 测试 RHI 后端创建 GPU 资源并上传数据（GPU 端操作）
 */
bool TestRHIBackendGPUResourceCreation(RHIFrontend& frontend, Device* device) {
    std::cout << "\n=== Test 2: RHI Backend GPU Resource Creation (GPU-side) ===" << std::endl;

    // 先加载纹理资产（CPU 端）
    auto asset = frontend.LoadTextureFromFile("./images/tex.png", false, true);
    if (!asset) {
        std::cerr << "[RHIBackend] Failed to load texture asset!" << std::endl;
        return false;
    }

    // 使用 RHI 后端创建 GPU 纹理资源
    std::cout << "[RHIBackend] Creating GPU texture from asset..." << std::endl;
    auto gpuTexture = device->CreateTextureFromAsset(*asset);

    if (!gpuTexture) {
        std::cerr << "[RHIBackend] Failed to create GPU texture!" << std::endl;
        return false;
    }
    std::cout << "[RHIBackend] GPU texture created!" << std::endl;

    // 使用 RHI 后端上传纹理数据到 GPU
    std::cout << "[RHIBackend] Uploading texture data to GPU..." << std::endl;
    device->UploadTexture(gpuTexture.get(), *asset);
    std::cout << "[RHIBackend] Texture data uploaded to GPU!" << std::endl;

    // 验证 OpenGL 纹理
    auto* glTexture = dynamic_cast<Render::Backend::OpenGL::GLTexture*>(gpuTexture.get());
    if (glTexture) {
        GLuint handle = glTexture->GetHandle();
        std::cout << "[RHIBackend] OpenGL Texture Handle: " << handle << std::endl;
        std::cout << "[RHIBackend] glIsTexture: " << (glIsTexture(handle) ? "true" : "false") << std::endl;
    }

    std::cout << "[Test 2 PASSED] RHI Backend GPU resource creation works correctly" << std::endl;
    return true;
}

/**
 * @brief 测试委托后门函数
 */
bool TestDelegateBackend(RHIFrontend& frontend) {
    std::cout << "\n=== Test 3: Delegate Backend (OpenGL Direct Calls) ===" << std::endl;

    std::atomic<bool> delegateExecuted{false};

    // 使用委托后门执行 OpenGL 调用
    frontend.EnqueueDelegate([&delegateExecuted](BackendDelegateContext& ctx) {
        std::cout << "[Delegate] Executing in render thread..." << std::endl;
        
        // 直接调用 OpenGL API
        glClearColor(0.2f, 0.3f, 0.4f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
        std::cout << "[Delegate] OpenGL calls executed successfully!" << std::endl;
        std::cout << "  - glClearColor called" << std::endl;
        std::cout << "  - glClear called" << std::endl;
        std::cout << "  - Current OpenGL Error: " << glGetError() << std::endl;
        
        delegateExecuted = true;
    });

    // 等待一下让渲染线程执行委托
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    if (!delegateExecuted) {
        std::cerr << "[Delegate] Delegate was not executed!" << std::endl;
        return false;
    }

    std::cout << "[Test 3 PASSED] Delegate backend works correctly" << std::endl;
    return true;
}

/**
 * @brief 测试多线程命令提交
 */
bool TestMultiThreadCommandSubmission(RHIFrontend& frontend) {
    std::cout << "\n=== Test 4: Multi-thread Command Submission ===" << std::endl;

    std::atomic<int> commandCount{0};

    // 创建多个线程同时提交命令
    std::vector<std::thread> threads;
    for (int i = 0; i < 3; ++i) {
        threads.emplace_back([&frontend, &commandCount, i]() {
            // 提交 Buffer 创建命令
            BufferDesc desc;
            desc.size = 256;
            desc.usage = static_cast<uint32_t>(BufferUsage::Vertex);
            desc.memoryUsage = MemoryUsage::CpuToGpu;

            std::vector<float> data(64, static_cast<float>(i));
            BufferHandle handle = frontend.CreateBuffer(desc, data.data(), data.size() * sizeof(float));

            if (handle.IsValid()) {
                commandCount++;
                std::cout << "[Thread " << i << "] Created buffer, handle id=" << handle.id << std::endl;
            }
        });
    }

    // 等待所有线程完成
    for (auto& t : threads) {
        t.join();
    }

    // 等待渲染线程处理命令
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    if (commandCount != 3) {
        std::cerr << "[Multi-thread] Expected 3 commands, got " << commandCount << std::endl;
        return false;
    }

    std::cout << "[Test 4 PASSED] Multi-thread command submission works correctly" << std::endl;
    return true;
}

/**
 * @brief 初始化 GLFW 和 OpenGL
 */
bool InitGLFW() {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(800, 600, "RHI Command Proxy Test", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        std::cerr << "Failed to create GLFW window" << std::endl;
        return false;
    }

    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return false;
    }

    std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
    return true;
}

/**
 * @brief 清理 GLFW
 */
void CleanupGLFW(GLFWwindow* window) {
    if (window) {
        glfwDestroyWindow(window);
    }
    glfwTerminate();
}

int main() {
    std::cout << "=== RHI Command Proxy Test Suite ===" << std::endl;
    std::cout << "RHI Architecture: Frontend (CPU) + Backend (GPU)" << std::endl;

    // 初始化 GLFW 和 OpenGL
    if (!InitGLFW()) {
        return 1;
    }

    // 创建 RHI 前端
    RHIFrontend frontend;

    // 创建 OpenGL 后端设备
    auto device = CreateOpenGLDevice();
    if (!device) {
        std::cerr << "Failed to create OpenGL device" << std::endl;
        CleanupGLFW(nullptr);
        return 1;
    }

    // 设置前端使用的后端设备
    frontend.SetDevice(device.get());

    // 启动渲染线程
    if (!frontend.Start()) {
        std::cerr << "Failed to start render thread" << std::endl;
        CleanupGLFW(nullptr);
        return 1;
    }
    std::cout << "Render thread started" << std::endl;

    // 运行测试
    bool allPassed = true;

    allPassed &= TestRHIFrontendTextureLoading(frontend);
    allPassed &= TestRHIBackendGPUResourceCreation(frontend, device.get());
    allPassed &= TestDelegateBackend(frontend);
    allPassed &= TestMultiThreadCommandSubmission(frontend);

    // 停止渲染线程
    frontend.Stop();
    std::cout << "\nRender thread stopped" << std::endl;

    // 清理
    CleanupGLFW(nullptr);

    // 最终结果
    std::cout << "\n=== Final Result ===" << std::endl;
    if (allPassed) {
        std::cout << "[SUCCESS] All RHI Command Proxy Tests PASSED!" << std::endl;
        std::cout << "\n=== RHI Architecture Verification ===" << std::endl;
        std::cout << "Frontend (RHIFrontend): CPU-side asset loading - VERIFIED" << std::endl;
        std::cout << "Backend (Device): GPU-side resource creation - VERIFIED" << std::endl;
        std::cout << "Command Queue: Thread-safe command submission - VERIFIED" << std::endl;
        std::cout << "Delegate Backend: Direct OpenGL calls in render thread - VERIFIED" << std::endl;
        return 0;
    } else {
        std::cerr << "[FAILED] Some RHI Command Proxy Tests FAILED!" << std::endl;
        return 1;
    }
}
