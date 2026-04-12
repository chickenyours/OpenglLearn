#pragma once

#include <memory>
#include <vector>
#include <string>

#include "Render/Public/RHI/rhi_handles.h"
#include "Render/Public/RHI/RHI_buffer.h"
#include "Render/Public/RHI/RHI_texture.h"
#include "Render/Public/RHI/RHI_input_layout.h"
#include "Render/Public/RHI/rhi_command.h"

namespace Render::RHI {

    class RenderThread;
    class Device;

    /**
     * @brief RHI 前端接口
     * 
     * 职责：负责 CPU 端资源加载和命令投递
     * - 加载纹理资产（TextureAsset）从文件
     * - 分配逻辑句柄（Handle）
     * - 组装 RHI 命令
     * - 投递命令到渲染线程队列
     * 
     * 注意：不直接执行 GPU 操作，GPU 操作由后端 Device 在渲染线程执行
     */
    class RHIFrontend {
    public:
        RHIFrontend();
        ~RHIFrontend();

        RHIFrontend(const RHIFrontend&) = delete;
        RHIFrontend& operator=(const RHIFrontend&) = delete;

        /**
         * @brief 设置后端设备指针
         * @param device 后端设备
         */
        void SetDevice(Device* device);

        /**
         * @brief 获取后端设备指针
         */
        Device* GetDevice() const;

        /**
         * @brief 启动渲染线程
         */
        bool Start();

        /**
         * @brief 停止渲染线程
         */
        void Stop();

        /**
         * @brief 判断是否已启动
         */
        bool IsRunning() const;

        // ==================== CPU 端资源加载（前端职责） ====================

        /**
         * @brief 从文件加载 2D 纹理资产（CPU 端操作）
         * @param imagePath 图像文件路径
         * @param generateMips 是否生成 mipmap
         * @param flipVertical 是否垂直翻转图像
         * @return 纹理资产对象，失败返回 nullptr
         * 
         * 注意：此方法只负责加载图像文件到 CPU 内存，不创建 GPU 资源
         */
        std::unique_ptr<TextureAsset> LoadTextureFromFile(
            const std::string& imagePath,
            bool generateMips = false,
            bool flipVertical = true
        );

        /**
         * @brief 从内存数据加载 2D 纹理资产（CPU 端操作）
         * @param imageData 图像数据指针
         * @param imageSize 图像数据大小
         * @param generateMips 是否生成 mipmap
         * @param flipVertical 是否垂直翻转图像
         * @return 纹理资产对象，失败返回 nullptr
         */
        std::unique_ptr<TextureAsset> LoadTextureFromMemory(
            const void* imageData,
            size_t imageSize,
            bool generateMips = false,
            bool flipVertical = true
        );

        // ==================== GPU 资源命令（通过命令队列投递） ====================

        /**
         * @brief 创建空的 GPU 纹理资源（命令投递模式）
         * @param desc 纹理描述
         * @return 纹理句柄
         *
         * 注意：此方法投递命令到渲染线程，由后端创建空 GPU 纹理
         * 适用场景：渲染管线、帧缓冲区等需要预先创建纹理占位
         */
        TextureHandle CreateEmptyTexture(const TextureDesc& desc);

        /**
         * @brief 创建 GPU 纹理资源（从 TextureAsset）
         * @param asset 纹理资产
         * @return 纹理句柄
         *
         * 注意：此方法投递命令到渲染线程，由后端创建 GPU 资源
         */
        TextureHandle CreateTextureFromAsset(const TextureAsset& asset);

        /**
         * @brief 上传纹理数据到 GPU
         * @param handle 纹理句柄
         * @param asset 纹理资产（包含像素数据）
         *
         * 注意：此方法投递命令到渲染线程，由后端执行 GPU 上传
         */
        void UploadTexture(TextureHandle handle, const TextureAsset& asset);

        /**
         * @brief 更新纹理数据（局部更新）
         * @param handle 纹理句柄
         * @param data 像素数据
         * @param size 数据大小
         * @param mipLevel Mipmap 层级
         * @param xOffset X 偏移
         * @param yOffset Y 偏移
         * @param zOffset Z 偏移
         * @param width 更新宽度
         * @param height 更新高度
         * @param depth 更新深度
         *
         * 注意：此方法投递命令到渲染线程，由后端执行局部更新
         */
        void UpdateTexture(
            TextureHandle handle,
            const void* data,
            size_t size,
            uint32_t mipLevel = 0,
            uint32_t xOffset = 0,
            uint32_t yOffset = 0,
            uint32_t zOffset = 0,
            uint32_t width = 0,
            uint32_t height = 0,
            uint32_t depth = 0
        );

        /**
         * @brief 销毁 GPU 纹理资源
         * @param handle 纹理句柄
         */
        void DestroyTexture(TextureHandle handle);

        // ==================== Buffer 操作 ====================

        /**
         * @brief 创建 GPU 缓冲区
         * @param desc 缓冲区描述
         * @param initialData 初始数据（可选）
         * @param initialDataSize 初始数据大小
         * @return 缓冲区句柄
         */
        BufferHandle CreateBuffer(const BufferDesc& desc, const void* initialData = nullptr, size_t initialDataSize = 0);

        /**
         * @brief 更新 GPU 缓冲区数据
         * @param handle 缓冲区句柄
         * @param data 新数据
         * @param size 数据大小
         * @param offset 偏移量
         */
        void UpdateBuffer(BufferHandle handle, const void* data, size_t size, uint64_t offset = 0);

        /**
         * @brief 销毁 GPU 缓冲区
         * @param handle 缓冲区句柄
         */
        void DestroyBuffer(BufferHandle handle);

        /**
         * @brief 拷贝 GPU 缓冲区数据
         * @param src 源缓冲区句柄
         * @param dst 目标缓冲区句柄
         * @param size 拷贝大小
         * @param srcOffset 源偏移
         * @param dstOffset 目标偏移
         */
        void CopyBuffer(BufferHandle src, BufferHandle dst, uint64_t size, uint64_t srcOffset = 0, uint64_t dstOffset = 0);

        // ==================== InputLayout 操作 ====================

        /**
         * @brief 创建 GPU 输入布局
         * @param desc 输入布局描述
         * @return 输入布局句柄
         */
        InputLayoutHandle CreateInputLayout(const InputLayoutDesc& desc);

        /**
         * @brief 销毁 GPU 输入布局
         * @param handle 输入布局句柄
         */
        void DestroyInputLayout(InputLayoutHandle handle);

        // ==================== 帧操作 ====================

        void BeginFrame();
        void EndFrame();

        // ==================== 委托后门 ====================

        /**
         * @brief 执行委托函数（后门）
         * @param delegate 委托函数
         */
        void EnqueueDelegate(BackendDelegate delegate);

        /**
         * @brief 获取渲染线程
         */
        RenderThread* GetRenderThread() { return renderThread_.get(); }

    private:
        std::unique_ptr<RenderThread> renderThread_;
        Device* device_ = nullptr;  // 后端设备指针（不拥有）
    };

} // namespace Render::RHI
