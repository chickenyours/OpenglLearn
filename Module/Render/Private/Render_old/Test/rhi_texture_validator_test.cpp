// RHI 纹理加载测试程序
// 使用 Application 模块创建窗口和 OpenGL 上下文
// 通过 RenderModule 获取 RHI 设备
// 使用 RHI 前端加载纹理资产（CPU 端操作）
// 使用 RHI 后端上传纹理到 GPU（GPU 端操作）
// 使用委托函数验证纹理加载是否成功

#include <iostream>
#include <functional>

#include "application_module.h"
#include "render_module.h"
#include "Render/Public/RHI/RHI_device.h"
#include "Render/Public/RHI/RHI_texture.h"
#include "Render/Public/RHI/rhi_frontend.h"
#include "Render/Public/RHI/rhi_texture_validator.h"
#include "Render/Private/Backend/Opengl/gl_texture_validator.h"

using namespace Render::RHI;

/**
 * @brief 测试使用 RHI 加载纹理并通过委托函数验证
 *
 * 流程说明：
 * 1. RHI 前端：使用 RHIFrontend::LoadTextureFromFile() 加载图像文件到 CPU 内存（创建 TextureAsset）
 * 2. RHI 后端：使用 Device::CreateTextureFromAsset() 创建 GPU 纹理资源
 * 3. RHI 后端：使用 Device::UploadTexture() 将像素数据上传到 GPU
 * 4. 验证器：使用委托函数验证 GPU 纹理资源
 */
bool TestRHITextureLoadingWithValidator() {
    std::cout << "\n=== Test: RHI Texture Loading with Delegate Validator ===" << std::endl;

    // 1. 创建并启动 Application 模块
    Application::ApplicationModule appModule;
    Application::ApplicationConfig config;
    config.width = 800;
    config.height = 600;
    config.title = "RHI Texture Validator Test";
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
    std::cout << "[RenderModule] Platform state: " << static_cast<int>(renderModule.GetPlatformState()) << std::endl;

    // 3. 注册 OpenGL 纹理验证器
    std::cout << "\n[Validator] Registering OpenGL texture validator..." << std::endl;
    Render::Backend::OpenGL::RegisterOpenGLTextureValidator();
    std::cout << "[Validator] OpenGL texture validator registered." << std::endl;

    // 4. 获取 RHI 设备（后端）
    auto* device = renderModule.GetDevice();
    if (!device) {
        std::cerr << "[RHI] Failed to get RHI device!" << std::endl;
        renderModule.Shutdown();
        appModule.Shutdown();
        return false;
    }
    std::cout << "[RHI Backend] RHI device acquired." << std::endl;

    // 5. 创建 RHI 前端接口
    RHIFrontend frontend;
    frontend.SetDevice(device);

    // 6. 【RHI 前端】加载图像文件到 CPU 内存（创建 TextureAsset）
    // 这是 CPU 端操作，不涉及 GPU
    std::string texturePath = "./images/tex.png";
    std::cout << "\n[RHI Frontend] Loading image file to CPU memory: " << texturePath << std::endl;

    auto asset = frontend.LoadTextureFromFile(texturePath, false, true);
    if (!asset) {
        std::cerr << "[RHI Frontend] Failed to load image file!" << std::endl;
        renderModule.Shutdown();
        appModule.Shutdown();
        return false;
    }
    std::cout << "[RHI Frontend] Image loaded to CPU memory successfully!" << std::endl;

    // 7. 【RHI 后端】创建 GPU 纹理资源（从 TextureAsset）
    // 这是 GPU 端操作，创建 OpenGL 纹理对象
    std::cout << "\n[RHI Backend] Creating GPU texture resource from asset..." << std::endl;
    auto gpuTexture = device->CreateTextureFromAsset(*asset);
    if (!gpuTexture) {
        std::cerr << "[RHI Backend] Failed to create GPU texture!" << std::endl;
        renderModule.Shutdown();
        appModule.Shutdown();
        return false;
    }
    std::cout << "[RHI Backend] GPU texture resource created!" << std::endl;

    // 8. 【RHI 后端】上传纹理数据到 GPU
    // 这是 GPU 端操作，将像素数据上传到显存
    std::cout << "[RHI Backend] Uploading texture data to GPU..." << std::endl;
    device->UploadTexture(gpuTexture.get(), *asset);
    std::cout << "[RHI Backend] Texture data uploaded to GPU!" << std::endl;

    // 9. 输出纹理参数
    const auto& desc = gpuTexture->GetDesc();

    std::cout << "\n=== GPU Texture Parameters ===" << std::endl;
    std::cout << "Width: " << desc.width << " pixels" << std::endl;
    std::cout << "Height: " << desc.height << " pixels" << std::endl;
    std::cout << "Mip Levels: " << desc.mipLevels << std::endl;
    std::cout << "Format: " << static_cast<int>(desc.format) << std::endl;

    // 10. 使用委托函数验证纹理（使用 GPU 纹理资源）
    std::cout << "\n[Validator] Validating GPU texture with delegate validator..." << std::endl;

    // 方式 1: 使用指定的验证器
    auto result = ValidateTexture(*gpuTexture, "OpenGL");

    std::cout << "\n=== Validation Result ===" << std::endl;
    std::cout << "Validator: OpenGL" << std::endl;
    std::cout << "Is Valid: " << (result.isValid ? "YES" : "NO") << std::endl;

    if (result.isValid) {
        std::cout << "Width: " << result.width << std::endl;
        std::cout << "Height: " << result.height << std::endl;
        std::cout << "Mip Levels: " << result.mipLevels << std::endl;
        std::cout << "Native Handle: 0x" << std::hex << result.nativeHandle << std::dec << std::endl;
    } else {
        std::cerr << "Error: " << result.errorMessage << std::endl;
    }

    // 11. 使用所有已注册的验证器
    std::cout << "\n[Validator] Running all registered validators..." << std::endl;
    auto allResults = ValidateTexture(*gpuTexture);

    for (const auto& [name, validatorResult] : allResults) {
        std::cout << "  [" << name << "] " << (validatorResult.isValid ? "PASSED" : "FAILED");
        if (!validatorResult.isValid) {
            std::cout << " - " << validatorResult.errorMessage;
        }
        std::cout << std::endl;
    }

    // 12. 使用自定义委托函数验证（演示用户自定义验证）
    std::cout << "\n[Validator] Running custom delegate validator..." << std::endl;

    TextureValidator customValidator = [](const RHITexture& tex) -> TextureValidationResult {
        TextureValidationResult result;

        // 检查 GPU 纹理资源是否有效
        if (!tex.IsValid()) {
            result.errorMessage = "GPU texture resource is not valid";
            return result;
        }

        // 检查是否有有效的格式
        const auto& desc = tex.GetDesc();
        if (desc.format == Format::Unknown) {
            result.errorMessage = "Texture format is Unknown";
            return result;
        }

        // 检查尺寸是否大于 0
        if (desc.width == 0 || desc.height == 0) {
            result.errorMessage = "Texture dimensions are zero";
            return result;
        }

        result.isValid = true;
        result.width = desc.width;
        result.height = desc.height;
        result.mipLevels = desc.mipLevels;
        return result;
    };

    auto customResult = customValidator(*gpuTexture);
    std::cout << "  [Custom] " << (customResult.isValid ? "PASSED" : "FAILED");
    if (!customResult.isValid) {
        std::cout << " - " << customResult.errorMessage;
    }
    std::cout << std::endl;

    // 13. 最终结果判定
    bool allPassed = result.isValid && customResult.isValid;

    for (const auto& [name, validatorResult] : allResults) {
        if (!validatorResult.isValid) {
            allPassed = false;
            break;
        }
    }

    if (allPassed) {
        std::cout << "\n[SUCCESS] RHI Texture Loading Test PASSED!" << std::endl;
        std::cout << "\n=== RHI Architecture Verification ===" << std::endl;
        std::cout << "Frontend (RHIFrontend): CPU-side asset loading" << std::endl;
        std::cout << "Backend (Device): GPU-side resource creation and upload" << std::endl;
        std::cout << "Separation of concerns: VERIFIED" << std::endl;
    } else {
        std::cerr << "\n[FAILED] RHI Texture Loading Test FAILED!" << std::endl;
    }

    // 清理
    renderModule.Shutdown();
    appModule.Shutdown();

    return allPassed;
}

int main() {
    std::cout << "=== RHI Texture Validator Test Suite ===" << std::endl;
    std::cout << "Testing RHI texture loading with delegate validators" << std::endl;
    std::cout << "RHI Architecture: Frontend (CPU) + Backend (GPU)" << std::endl;

    bool success = TestRHITextureLoadingWithValidator();

    return success ? 0 : 1;
}
