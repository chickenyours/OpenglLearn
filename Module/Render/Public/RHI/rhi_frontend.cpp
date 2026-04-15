#include "Render/Public/RHI/rhi_frontend.h"
#include "Render/Private/Runtime/render_thread.h"
#include "Render/Public/RHI/rhi_handle_allocator.h"
#include <iostream>

namespace Render::RHI {

    // 使用外部提供的 RenderThread
    RHIFrontend::RHIFrontend(RenderThread* renderThread)
        : renderThread_(renderThread), ownsRenderThread_(false) {
    }

    // 自己创建 RenderThread
    RHIFrontend::RHIFrontend()
        : renderThread_(nullptr), ownsRenderThread_(true) {
        renderThread_ = new RenderThread();
    }

    RHIFrontend::~RHIFrontend() {
        if (ownsRenderThread_ && renderThread_) {
            delete renderThread_;
            renderThread_ = nullptr;
        }
    }

    void RHIFrontend::SetDevice(Device* device) {
        device_ = device;
    }

    Device* RHIFrontend::GetDevice() const {
        return device_;
    }

    bool RHIFrontend::Start() {
        if (!renderThread_) {
            return false;
        }
        return renderThread_->Start();
    }

    void RHIFrontend::Stop() {
        if (renderThread_) {
            renderThread_->Stop();
        }
    }

    bool RHIFrontend::IsRunning() const {
        return renderThread_ && renderThread_->IsRunning();
    }

    // ==================== CPU 端资源加载（前端职责） ====================

    std::unique_ptr<TextureAsset> RHIFrontend::LoadTextureFromFile(
        const std::string& imagePath,
        bool generateMips,
        bool flipVertical
    ) {
        auto asset = std::make_unique<TextureAsset>();
        if (!asset->LoadFromImageFile2D(imagePath)) {
            return nullptr;
        }

        if (generateMips) {
            TextureDesc& desc = asset->GetDesc();
            desc.generateMips = true;
            desc.mipLevels = TextureAsset::CalculateMipLevels(desc.width, desc.height, desc.depth);
        }

        TextureDesc& desc = asset->GetDesc();
        desc.flipVerticalOnLoad = flipVertical;

        std::cout << "[RHIFrontend] Loaded texture from file: " << imagePath << std::endl;
        std::cout << "  - Width: " << desc.width << ", Height: " << desc.height << std::endl;
        std::cout << "  - Mip Levels: " << desc.mipLevels << std::endl;

        return asset;
    }

    std::unique_ptr<TextureAsset> RHIFrontend::LoadTextureFromMemory(
        const void* imageData,
        size_t imageSize,
        bool generateMips,
        bool flipVertical
    ) {
        std::cerr << "[RHIFrontend] LoadTextureFromMemory not yet implemented" << std::endl;
        return nullptr;
    }

    // ==================== GPU 资源命令（通过命令队列投递） ====================

    TextureHandle RHIFrontend::CreateEmptyTexture(const TextureDesc& desc) {
        TextureHandle handle = RHIHandleAllocator::Instance().AllocateTextureHandle();

        CreateEmptyTextureCmd cmd;
        cmd.handle = handle;
        cmd.desc = desc;

        if (renderThread_) {
            renderThread_->EnqueueCreateCommand(RHICommand(RHICommandType::CreateEmptyTexture, std::move(cmd)));
        }

        std::cout << "[RHIFrontend] CreateEmptyTexture enqueued, handle id=" << handle.id << std::endl;
        return handle;
    }

    TextureHandle RHIFrontend::CreateTextureFromAsset(const TextureAsset& asset) {
        TextureHandle handle = RHIHandleAllocator::Instance().AllocateTextureHandle();

        CreateTextureCmd cmd;
        cmd.handle = handle;
        cmd.desc = asset.GetDesc();

        if (renderThread_) {
            renderThread_->EnqueueCreateCommand(RHICommand(RHICommandType::CreateTexture, std::move(cmd)));
        }

        std::cout << "[RHIFrontend] CreateTextureFromAsset enqueued, handle id=" << handle.id << std::endl;
        return handle;
    }

    void RHIFrontend::UploadTexture(TextureHandle handle, const TextureAsset& asset) {
        if (!asset.Check()) {
            std::cerr << "[RHIFrontend] Cannot upload invalid texture asset" << std::endl;
            return;
        }

        UploadTextureCmd cmd;
        cmd.handle = handle;
        cmd.mipLevel = 0;
        cmd.xOffset = 0;
        cmd.yOffset = 0;
        cmd.zOffset = 0;
        cmd.width = asset.GetDesc().width;
        cmd.height = asset.GetDesc().height;
        cmd.depth = asset.GetDesc().depth;

        const auto& source = asset.GetSource();
        if (source.valid) {
            cmd.data.resize(source.GetSize());
            std::memcpy(cmd.data.data(), source.GetData(), source.GetSize());
        }

        if (renderThread_) {
            renderThread_->EnqueueCreateCommand(RHICommand(RHICommandType::UploadTexture, std::move(cmd)));
        }

        std::cout << "[RHIFrontend] UploadTexture enqueued, handle id=" << handle.id << std::endl;
    }

    void RHIFrontend::UpdateTexture(
        TextureHandle handle,
        const void* data,
        size_t size,
        uint32_t mipLevel,
        uint32_t xOffset,
        uint32_t yOffset,
        uint32_t zOffset,
        uint32_t width,
        uint32_t height,
        uint32_t depth
    ) {
        if (!data || size == 0) {
            std::cerr << "[RHIFrontend] Cannot update texture with null data" << std::endl;
            return;
        }

        UpdateTextureCmd cmd;
        cmd.handle = handle;
        cmd.mipLevel = mipLevel;
        cmd.xOffset = xOffset;
        cmd.yOffset = yOffset;
        cmd.zOffset = zOffset;
        cmd.width = width;
        cmd.height = height;
        cmd.depth = depth;
        cmd.data.resize(size);
        std::memcpy(cmd.data.data(), data, size);

        if (renderThread_) {
            renderThread_->EnqueueCreateCommand(RHICommand(RHICommandType::UpdateTexture, std::move(cmd)));
        }

        std::cout << "[RHIFrontend] UpdateTexture enqueued, handle id=" << handle.id << std::endl;
    }

    void RHIFrontend::DestroyTexture(TextureHandle handle) {
        DestroyTextureCmd cmd;
        cmd.handle = handle;

        if (renderThread_) {
            renderThread_->EnqueueDestroyCommand(RHICommand(RHICommandType::DestroyTexture, std::move(cmd)));
        }
    }

    // ==================== Buffer 操作 ====================

    BufferHandle RHIFrontend::CreateBuffer(const BufferDesc& desc, const void* initialData, size_t initialDataSize) {
        BufferHandle handle = RHIHandleAllocator::Instance().AllocateBufferHandle();

        CreateBufferCmd cmd;
        cmd.handle = handle;
        cmd.desc = desc;

        if (initialData && initialDataSize > 0) {
            cmd.initialData.resize(initialDataSize);
            std::memcpy(cmd.initialData.data(), initialData, initialDataSize);
        }

        if (renderThread_) {
            renderThread_->EnqueueCreateCommand(RHICommand(RHICommandType::CreateBuffer, std::move(cmd)));
        }

        std::cout << "[RHIFrontend] CreateBuffer enqueued, handle id=" << handle.id << std::endl;
        return handle;
    }

    void RHIFrontend::UpdateBuffer(BufferHandle handle, const void* data, size_t size, uint64_t offset) {
        UpdateBufferCmd cmd;
        cmd.handle = handle;
        cmd.offset = offset;
        cmd.data.resize(size);
        std::memcpy(cmd.data.data(), data, size);

        if (renderThread_) {
            renderThread_->EnqueueCreateCommand(RHICommand(RHICommandType::UpdateBuffer, std::move(cmd)));
        }
    }

    void RHIFrontend::DestroyBuffer(BufferHandle handle) {
        DestroyBufferCmd cmd;
        cmd.handle = handle;

        if (renderThread_) {
            renderThread_->EnqueueDestroyCommand(RHICommand(RHICommandType::DestroyBuffer, std::move(cmd)));
        }
    }

    void RHIFrontend::CopyBuffer(BufferHandle src, BufferHandle dst, uint64_t size, uint64_t srcOffset, uint64_t dstOffset) {
        CopyBufferCmd cmd;
        cmd.src = src;
        cmd.dst = dst;
        cmd.size = size;
        cmd.srcOffset = srcOffset;
        cmd.dstOffset = dstOffset;

        if (renderThread_) {
            renderThread_->EnqueueCreateCommand(RHICommand(RHICommandType::CopyBuffer, std::move(cmd)));
        }
    }

    // ==================== InputLayout 操作 ====================

    InputLayoutHandle RHIFrontend::CreateInputLayout(const InputLayoutDesc& desc) {
        InputLayoutHandle handle = RHIHandleAllocator::Instance().AllocateInputLayoutHandle();

        CreateInputLayoutCmd cmd;
        cmd.handle = handle;
        cmd.desc = desc;

        if (renderThread_) {
            renderThread_->EnqueueCreateCommand(RHICommand(RHICommandType::CreateInputLayout, std::move(cmd)));
        }

        std::cout << "[RHIFrontend] CreateInputLayout enqueued, handle id=" << handle.id << std::endl;
        return handle;
    }

    void RHIFrontend::DestroyInputLayout(InputLayoutHandle handle) {
        DestroyInputLayoutCmd cmd;
        cmd.handle = handle;

        if (renderThread_) {
            renderThread_->EnqueueDestroyCommand(RHICommand(RHICommandType::DestroyInputLayout, std::move(cmd)));
        }
    }

    // ==================== 帧操作 ====================

    void RHIFrontend::BeginFrame() {
        if (renderThread_) {
            renderThread_->EnqueueCreateCommand(RHICommand(RHICommandType::BeginFrame, BeginFrameCmd{}));
        }
    }

    void RHIFrontend::EndFrame() {
        if (renderThread_) {
            renderThread_->EnqueueCreateCommand(RHICommand(RHICommandType::EndFrame, EndFrameCmd{}));
        }
    }

    // ==================== 委托后门 ====================

    void RHIFrontend::EnqueueDelegate(BackendDelegate delegate) {
        ExecuteDelegateCmd cmd;
        cmd.delegate = std::move(delegate);

        if (renderThread_) {
            renderThread_->EnqueueCreateCommand(RHICommand(RHICommandType::ExecuteDelegate, std::move(cmd)));
        }
    }

} // namespace Render::RHI
