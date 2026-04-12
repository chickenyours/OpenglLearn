/**
 * @file rhi_empty_texture_test.cpp
 * @brief RHI 空纹理创建测试程序
 *
 * 测试内容：
 * 1. 使用 Device::CreateEmptyTexture() 创建空纹理（工具层使用）
 * 2. 使用 UpdateTexture() 局部更新纹理数据
 * 3. 验证空纹理创建和更新流程
 *
 * 适用场景：
 * - 渲染管线创建帧缓冲纹理
 * - 工具层预先创建纹理占位
 * - 动态纹理更新
 */

#include <iostream>
#include <vector>
#include <random>

#include <glad/glad.h>

#include "application_module.h"
#include "render_module.h"
#include "Render/Public/RHI/RHI_device.h"
#include "Render/Public/RHI/RHI_texture.h"
#include "Render/Private/Backend/Opengl/gl_texture.h"

using namespace Render::RHI;

/**
 * @brief 测试使用 Device 创建空纹理
 */
bool TestCreateEmptyTexture() {
    std::cout << "\n=== Test: Create Empty Texture (Tool Layer API) ===" << std::endl;

    // 1. 创建并启动 Application 模块
    Application::ApplicationModule appModule;
    Application::ApplicationConfig config;
    config.width = 800;
    config.height = 600;
    config.title = "RHI Empty Texture Test";
    config.majorVersion = 4;
    config.minorVersion = 6;
    appModule.SetConfig(config);

    std::cout << "[AppModule] Creating window and OpenGL context..." << std::endl;
    if (!appModule.Startup()) {
        std::cerr << "[AppModule] Failed to startup ApplicationModule!" << std::endl;
        return false;
    }
    std::cout << "[AppModule] ApplicationModule started successfully!" << std::endl;

    // 2. 创建并初始化 Render 模块
    Render::RenderModule renderModule;
    std::cout << "[RenderModule] Initializing RenderModule from ApplicationModule..." << std::endl;
    if (!renderModule.InitializeFromApplication(appModule)) {
        std::cerr << "[RenderModule] Failed to initialize RenderModule!" << std::endl;
        appModule.Shutdown();
        return false;
    }
    std::cout << "[RenderModule] RenderModule started successfully!" << std::endl;

    // 3. 获取 RHI 设备（后端）
    auto* device = renderModule.GetDevice();
    if (!device) {
        std::cerr << "[RHI] Failed to get RHI device!" << std::endl;
        renderModule.Shutdown();
        appModule.Shutdown();
        return false;
    }
    std::cout << "[RHI Backend] RHI device acquired." << std::endl;

    // 4. 【工具层 API】创建空纹理（不初始化像素数据）
    // 这是工具层（如渲染管线）使用的接口
    std::cout << "\n[Tool Layer] Creating empty texture (no pixel data)..." << std::endl;

    TextureDesc desc;
    desc.dimension = TextureDimension::Tex2D;
    desc.format = Format::RGBA8_UNorm;
    desc.width = 512;
    desc.height = 512;
    desc.mipLevels = 1;
    desc.usage = TextureUsage::ShaderResource | TextureUsage::RenderTarget;

    auto emptyTexture = device->CreateEmptyTexture(desc);
    if (!emptyTexture) {
        std::cerr << "[Tool Layer] Failed to create empty texture!" << std::endl;
        renderModule.Shutdown();
        appModule.Shutdown();
        return false;
    }
    std::cout << "[Tool Layer] Empty texture created!" << std::endl;
    std::cout << "  - Width: " << emptyTexture->GetWidth() << std::endl;
    std::cout << "  - Height: " << emptyTexture->GetHeight() << std::endl;
    std::cout << "  - Format: " << static_cast<int>(emptyTexture->GetFormat()) << std::endl;

    // 5. 【工具层 API】使用 UpdateTexture 更新纹理数据
    std::cout << "\n[Tool Layer] Updating texture data with UpdateTexture()..." << std::endl;

    // 生成测试数据（渐变红色）
    std::vector<uint8_t> pixelData(512 * 512 * 4);
    for (size_t y = 0; y < 512; ++y) {
        for (size_t x = 0; x < 512; ++x) {
            size_t idx = (y * 512 + x) * 4;
            pixelData[idx + 0] = static_cast<uint8_t>(x % 256);      // R
            pixelData[idx + 1] = static_cast<uint8_t>(y % 256);      // G
            pixelData[idx + 2] = 128;                                 // B
            pixelData[idx + 3] = 255;                                 // A
        }
    }

    // 使用 UpdateTexture 更新整个纹理
    device->UpdateTexture(
        emptyTexture.get(),
        pixelData.data(),
        0,      // mipLevel
        0, 0, 0,// xOffset, yOffset, zOffset
        512, 512, 1  // width, height, depth
    );
    std::cout << "[Tool Layer] Texture data updated!" << std::endl;

    // 6. 【工具层 API】局部更新纹理（更新左上角 128x128 区域）
    std::cout << "\n[Tool Layer] Partial update (top-left 128x128 region)..." << std::endl;

    std::vector<uint8_t> partialData(128 * 128 * 4);
    for (size_t y = 0; y < 128; ++y) {
        for (size_t x = 0; x < 128; ++x) {
            size_t idx = (y * 128 + x) * 4;
            partialData[idx + 0] = 255;  // R
            partialData[idx + 1] = 255;  // G
            partialData[idx + 2] = 255;  // B
            partialData[idx + 3] = 255;  // A
        }
    }

    device->UpdateTexture(
        emptyTexture.get(),
        partialData.data(),
        0,      // mipLevel
        0, 0, 0,// xOffset, yOffset, zOffset
        128, 128, 1  // width, height, depth
    );
    std::cout << "[Tool Layer] Partial update completed!" << std::endl;

    // 7. 验证 OpenGL 纹理
    auto* glTexture = dynamic_cast<Render::Backend::OpenGL::GLTexture*>(emptyTexture.get());
    if (glTexture) {
        GLuint handle = glTexture->GetHandle();
        std::cout << "\n[Verification] OpenGL Texture Handle: " << handle << std::endl;
        std::cout << "[Verification] glIsTexture: " << (glIsTexture(handle) ? "true" : "false") << std::endl;
    }

    // 8. 清理
    renderModule.Shutdown();
    appModule.Shutdown();

    std::cout << "\n[SUCCESS] Empty Texture Creation Test PASSED!" << std::endl;
    std::cout << "\n=== Tool Layer API Summary ===" << std::endl;
    std::cout << "1. CreateEmptyTexture() - Create texture without pixel data" << std::endl;
    std::cout << "2. UpdateTexture() - Update texture data (full or partial)" << std::endl;
    std::cout << "3. Suitable for: Render Pipeline, Frame Buffer, Tool Layer" << std::endl;

    return true;
}

int main() {
    std::cout << "=== RHI Empty Texture Test Suite ===" << std::endl;
    std::cout << "Testing Device::CreateEmptyTexture() and UpdateTexture()" << std::endl;
    std::cout << "Target: Tool Layer (Render Pipeline, Frame Buffer, etc.)" << std::endl;

    bool success = TestCreateEmptyTexture();

    return success ? 0 : 1;
}
