#include "Render/Public/RHI/rhi_frontend.h"
#include "Render/Private/Runtime/render_thread.h"
#include "Render/Public/RHI/rhi_handle_allocator.h"
#include <iostream>

namespace Render::RHI {

    RHIFrontend::RHIFrontend()
        : renderThread_(std::make_unique<RenderThread>()) {
    }

    RHIFrontend::~RHIFrontend() {
        Stop();
    }

    void RHIFrontend::SetDevice(Device* device) {
        device_ = device;
    }

    Device* RHIFrontend::GetDevice() const {
        return device_;
    }

    bool RHIFrontend::Start() {
        return renderThread_->Start();
    }

    void RHIFrontend::Stop() {
        renderThread_->Stop();
    }

    bool RHIFrontend::IsRunning() const {
        return renderThread_->IsRunning();
    }

    // ==================== CPU 端资源加载（前端职责） ====================

    std::unique_ptr<TextureAsset> RHIFrontend::LoadTextureFromFile(
        const std::string& imagePath,
        bool generateMips,
        bool flipVertical
    ) {
        // CPU 端操作：直接从文件加载图像数据到内存
        auto asset = std::make_unique<TextureAsset>();
        if (!asset->LoadFromImageFile2D(imagePath)) {
            return nullptr;
        }

        // 设置 mipmap 生成选项
        if (generateMips) {
            TextureDesc& desc = asset->GetDesc();
            desc.generateMips = true;
            desc.mipLevels = TextureAsset::CalculateMipLevels(desc.width, desc.height, desc.depth);
        }

        // 设置垂直翻转选项
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
        // CPU 端操作：从内存数据加载图像
        // 注意：当前 TextureAsset 没有直接从内存加载的接口
        // 这里预留接口，后续可扩展
        std::cerr << "[RHIFrontend] LoadTextureFromMemory not yet implemented" << std::endl;
        return nullptr;
    }

    // ==================== GPU 资源命令（通过命令队列投递） ====================

    TextureHandle RHIFrontend::CreateEmptyTexture(const TextureDesc& desc) {
        // 分配纹理句柄
        TextureHandle handle = RHIHandleAllocator::Instance().AllocateTextureHandle();

        // 投递创建空纹理命令到渲染线程
        CreateEmptyTextureCmd cmd;
        cmd.handle = handle;
        cmd.desc = desc;

        renderThread_->Enqueue(RHICommand(RHICommandType::CreateEmptyTexture, std::move(cmd)));

        std::cout << "[RHIFrontend] CreateEmptyTexture enqueued, handle id=" << handle.id << std::endl;
        return handle;
    }

    TextureHandle RHIFrontend::CreateTextureFromAsset(const TextureAsset& asset) {
        // 注意：此方法用于命令代理模式
        // 第一阶段简化处理：返回一个句柄，实际创建由后端在渲染线程执行
        // 完整实现需要在后端资源表中建立句柄到 GPU 纹理的映射

        TextureHandle handle = RHIHandleAllocator::Instance().AllocateTextureHandle();

        // 投递创建命令到渲染线程
        CreateTextureCmd cmd;
        cmd.handle = handle;
        cmd.desc = asset.GetDesc();

        // 注意：这里不复制 asset 数据，因为实际创建需要从资源表查找
        // 完整实现需要传递 asset 数据或者引用
        renderThread_->Enqueue(RHICommand(RHICommandType::CreateTexture, std::move(cmd)));

        std::cout << "[RHIFrontend] CreateTextureFromAsset enqueued, handle id=" << handle.id << std::endl;
        return handle;
    }

    void RHIFrontend::UploadTexture(TextureHandle handle, const TextureAsset& asset) {
        // 投递上传命令到渲染线程
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

        // 复制像素数据到命令
        const auto& source = asset.GetSource();
        if (source.valid) {
            cmd.data.resize(source.GetSize());
            std::memcpy(cmd.data.data(), source.GetData(), source.GetSize());
        }

        renderThread_->Enqueue(RHICommand(RHICommandType::UploadTexture, std::move(cmd)));

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
        // 投递局部更新命令到渲染线程
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

        renderThread_->Enqueue(RHICommand(RHICommandType::UpdateTexture, std::move(cmd)));

        std::cout << "[RHIFrontend] UpdateTexture enqueued, handle id=" << handle.id << std::endl;
    }

    void RHIFrontend::DestroyTexture(TextureHandle handle) {
        DestroyTextureCmd cmd;
        cmd.handle = handle;
        renderThread_->Enqueue(RHICommand(RHICommandType::DestroyTexture, std::move(cmd)));
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

        renderThread_->Enqueue(RHICommand(RHICommandType::CreateBuffer, std::move(cmd)));

        std::cout << "[RHIFrontend] CreateBuffer enqueued, handle id=" << handle.id << std::endl;
        return handle;
    }

    void RHIFrontend::UpdateBuffer(BufferHandle handle, const void* data, size_t size, uint64_t offset) {
        UpdateBufferCmd cmd;
        cmd.handle = handle;
        cmd.offset = offset;
        cmd.data.resize(size);
        std::memcpy(cmd.data.data(), data, size);

        renderThread_->Enqueue(RHICommand(RHICommandType::UpdateBuffer, std::move(cmd)));
    }

    void RHIFrontend::DestroyBuffer(BufferHandle handle) {
        DestroyBufferCmd cmd;
        cmd.handle = handle;
        renderThread_->Enqueue(RHICommand(RHICommandType::DestroyBuffer, std::move(cmd)));
    }

    void RHIFrontend::CopyBuffer(BufferHandle src, BufferHandle dst, uint64_t size, uint64_t srcOffset, uint64_t dstOffset) {
        CopyBufferCmd cmd;
        cmd.src = src;
        cmd.dst = dst;
        cmd.size = size;
        cmd.srcOffset = srcOffset;
        cmd.dstOffset = dstOffset;
        renderThread_->Enqueue(RHICommand(RHICommandType::CopyBuffer, std::move(cmd)));
    }

    // ==================== InputLayout 操作 ====================

    InputLayoutHandle RHIFrontend::CreateInputLayout(const InputLayoutDesc& desc) {
        InputLayoutHandle handle = RHIHandleAllocator::Instance().AllocateInputLayoutHandle();

        CreateInputLayoutCmd cmd;
        cmd.handle = handle;
        cmd.desc = desc;

        renderThread_->Enqueue(RHICommand(RHICommandType::CreateInputLayout, std::move(cmd)));

        std::cout << "[RHIFrontend] CreateInputLayout enqueued, handle id=" << handle.id << std::endl;
        return handle;
    }

    void RHIFrontend::DestroyInputLayout(InputLayoutHandle handle) {
        DestroyInputLayoutCmd cmd;
        cmd.handle = handle;
        renderThread_->Enqueue(RHICommand(RHICommandType::DestroyInputLayout, std::move(cmd)));
    }

    // ==================== 帧操作 ====================

    void RHIFrontend::BeginFrame() {
        renderThread_->Enqueue(RHICommand(RHICommandType::BeginFrame, BeginFrameCmd{}));
    }

    void RHIFrontend::EndFrame() {
        renderThread_->Enqueue(RHICommand(RHICommandType::EndFrame, EndFrameCmd{}));
    }

    // ==================== 委托后门 ====================

    void RHIFrontend::EnqueueDelegate(BackendDelegate delegate) {
        ExecuteDelegateCmd cmd;
        cmd.delegate = std::move(delegate);
        renderThread_->Enqueue(RHICommand(RHICommandType::ExecuteDelegate, std::move(cmd)));
    }

} // namespace Render::RHI
