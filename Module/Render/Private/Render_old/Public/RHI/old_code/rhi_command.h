#pragma once

#include <cstdint>
#include <vector>
#include <variant>
#include <functional>
#include <memory>

#include "Render/Public/RHI/rhi_handles.h"
#include "Render/Public/RHI/RHI_buffer.h"
#include "Render/Public/RHI/RHI_texture.h"
#include "Render/Public/RHI/RHI_input_layout.h"

namespace Render::RHI {

    /**
     * @brief RHI 命令类型枚举
     */
    enum class RHICommandType {
        // Buffer 命令
        CreateBuffer,
        UpdateBuffer,
        DestroyBuffer,
        CopyBuffer,

        // Texture 命令
        CreateEmptyTexture,  // 创建空纹理
        CreateTexture,       // 从 Asset 创建纹理
        UploadTexture,
        UpdateTexture,       // 局部更新纹理
        DestroyTexture,

        // InputLayout 命令
        CreateInputLayout,
        DestroyInputLayout,

        // 帧命令
        BeginFrame,
        EndFrame,

        // 委托命令（后门）
        ExecuteDelegate
    };

    /**
     * @brief Buffer 命令负载
     */
    struct CreateBufferCmd {
        BufferHandle handle;
        BufferDesc desc;
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

    /**
     * @brief Texture 命令负载
     */
    struct CreateEmptyTextureCmd {
        TextureHandle handle;
        TextureDesc desc;
    };

    struct CreateTextureCmd {
        TextureHandle handle;
        TextureDesc desc;
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

    struct UpdateTextureCmd {
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

    /**
     * @brief InputLayout 命令负载
     */
    struct CreateInputLayoutCmd {
        InputLayoutHandle handle;
        InputLayoutDesc desc;
    };

    struct DestroyInputLayoutCmd {
        InputLayoutHandle handle;
    };

    /**
     * @brief 帧命令负载
     */
    struct BeginFrameCmd {};
    struct EndFrameCmd {};

    /**
     * @brief 委托后门上下文
     */
    struct BackendDelegateContext {
        void* backendExecutor = nullptr;   // 后端执行器指针
        void* nativeDevice = nullptr;       // 原生设备指针（OpenGL 为 nullptr）
        void* nativeContext = nullptr;      // 原生上下文指针（OpenGL 上下文）
    };

    /**
     * @brief 委托函数类型
     */
    using BackendDelegate = std::function<void(BackendDelegateContext&)>;

    struct ExecuteDelegateCmd {
        BackendDelegate delegate;
    };

    /**
     * @brief RHI 命令负载变体
     */
    using RHICommandPayload = std::variant<
        CreateBufferCmd,
        UpdateBufferCmd,
        DestroyBufferCmd,
        CopyBufferCmd,
        CreateEmptyTextureCmd,
        CreateTextureCmd,
        UploadTextureCmd,
        UpdateTextureCmd,
        DestroyTextureCmd,
        CreateInputLayoutCmd,
        DestroyInputLayoutCmd,
        BeginFrameCmd,
        EndFrameCmd,
        ExecuteDelegateCmd
    >;

    /**
     * @brief RHI 命令
     */
    struct RHICommand {
        RHICommandType type;
        RHICommandPayload payload;

        RHICommand() : type(RHICommandType::BeginFrame) {}

        RHICommand(RHICommandType cmdType, RHICommandPayload cmdPayload)
            : type(cmdType), payload(std::move(cmdPayload)) {}
    };

} // namespace Render::RHI
