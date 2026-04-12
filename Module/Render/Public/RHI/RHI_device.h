#pragma once

#include <memory>
#include "Render/Public/RHI/RHI_texture.h"
#include "Render/Public/RHI/RHI_buffer.h"
#include "Render/Public/RHI/RHI_input_layout.h"

namespace Render::RHI {

    /**
     * @brief RHI 后端设备接口
     *
     * 职责：只负责 GPU 端操作和图形 API 调用
     * - 创建/销毁 GPU 纹理资源（RHITexture）
     * - 上传纹理数据到 GPU
     * - 创建/销毁 GPU 缓冲区（Buffer）
     * - 绑定缓冲区到渲染管线
     * - 创建/销毁输入布局（InputLayout）
     *
     * 注意：不负责 CPU 端资源加载（如 TextureAsset 加载），那是 RHIFrontend 的职责
     */
    class Device {
    public:
        virtual ~Device() = default;

        // ==================== Texture GPU 操作 ====================

        /**
         * @brief 创建空的 GPU 纹理资源（不初始化像素数据）
         * @param desc 纹理描述
         * @return GPU 纹理资源对象，失败返回 nullptr
         *
         * 注意：此方法创建的纹理未初始化像素数据，需要后续调用 UploadTexture 或手动填充
         * 适用场景：渲染管线、帧缓冲区、工具层需要预先创建纹理占位
         */
        virtual std::unique_ptr<RHITexture> CreateEmptyTexture(const TextureDesc& desc) = 0;

        /**
         * @brief 从 TextureAsset 创建 GPU 纹理资源
         * @param asset 纹理资产（包含像素数据和描述）
         * @return GPU 纹理资源对象，失败返回 nullptr
         *
         * 注意：此方法会同时创建 GPU 资源并上传像素数据
         */
        virtual std::unique_ptr<RHITexture> CreateTextureFromAsset(const TextureAsset& asset) = 0;

        /**
         * @brief 将纹理数据上传到 GPU
         * @param texture GPU 纹理资源
         * @param asset 纹理资产（包含像素数据）
         *
         * 注意：此方法执行实际的 GPU 上传操作
         */
        virtual void UploadTexture(RHITexture* texture, const TextureAsset& asset) = 0;

        /**
         * @brief 局部更新纹理数据（用于动态纹理更新）
         * @param texture GPU 纹理资源
         * @param data 像素数据指针
         * @param mipLevel Mipmap 层级
         * @param xOffset X 偏移
         * @param yOffset Y 偏移
         * @param zOffset Z 偏移
         * @param width 更新宽度
         * @param height 更新高度
         * @param depth 更新深度
         *
         * 注意：此方法用于更新已创建纹理的部分区域
         */
        virtual void UpdateTexture(
            RHITexture* texture,
            const void* data,
            uint32_t mipLevel = 0,
            uint32_t xOffset = 0,
            uint32_t yOffset = 0,
            uint32_t zOffset = 0,
            uint32_t width = 0,
            uint32_t height = 0,
            uint32_t depth = 0
        ) = 0;

        /**
         * @brief 销毁 GPU 纹理资源
         * @param texture GPU 纹理资源
         */
        virtual void DestroyTexture(RHITexture* texture) = 0;

        // ==================== Buffer GPU 操作 ====================

        /**
         * @brief 创建 GPU 缓冲区
         * @param desc 缓冲区描述
         * @param initialData 初始数据（可选）
         * @return 缓冲区对象，失败返回 nullptr
         */
        virtual Buffer* CreateBuffer(const BufferDesc& desc, const void* initialData = nullptr) = 0;

        /**
         * @brief 销毁 GPU 缓冲区
         * @param buffer 缓冲区对象
         */
        virtual void DestroyBuffer(Buffer* buffer) = 0;

        /**
         * @brief 拷贝缓冲区数据
         * @param src 源缓冲区
         * @param dst 目标缓冲区
         * @param size 拷贝大小
         * @param srcOffset 源偏移
         * @param dstOffset 目标偏移
         * @return 是否成功
         */
        virtual bool CopyBuffer(
            Buffer* src,
            Buffer* dst,
            uint64_t size,
            uint64_t srcOffset = 0,
            uint64_t dstOffset = 0
        ) = 0;

        /**
         * @brief 绑定顶点缓冲区
         * @param buffer 缓冲区
         * @param slot 槽位
         * @param offset 偏移
         * @return 是否成功
         */
        virtual bool BindVertexBuffer(Buffer* buffer, uint32_t slot, uint64_t offset = 0) = 0;

        /**
         * @brief 绑定索引缓冲区
         * @param buffer 缓冲区
         * @param format 索引格式
         * @param offset 偏移
         * @return 是否成功
         */
        virtual bool BindIndexBuffer(Buffer* buffer, IndexFormat format, uint64_t offset = 0) = 0;

        /**
         * @brief 绑定常量缓冲区
         * @param buffer 缓冲区
         * @param slot 槽位
         * @param range 范围（可选）
         * @return 是否成功
         */
        virtual bool BindConstantBuffer(Buffer* buffer, uint32_t slot, const BufferRange* range = nullptr) = 0;

        /**
         * @brief 绑定存储缓冲区
         * @param buffer 缓冲区
         * @param slot 槽位
         * @param range 范围（可选）
         * @return 是否成功
         */
        virtual bool BindStorageBuffer(Buffer* buffer, uint32_t slot, const BufferRange* range = nullptr) = 0;

        // ==================== InputLayout GPU 操作 ====================

        /**
         * @brief 创建输入布局
         * @param desc 输入布局描述
         * @return 输入布局对象，失败返回 nullptr
         */
        virtual InputLayout* CreateInputLayout(const InputLayoutDesc& desc) = 0;

        /**
         * @brief 销毁输入布局
         * @param layout 输入布局对象
         */
        virtual void DestroyInputLayout(InputLayout* layout) = 0;

        /**
         * @brief 绑定输入布局
         * @param layout 输入布局
         * @return 是否成功
         */
        virtual bool BindInputLayout(InputLayout* layout) = 0;
    };

    /**
     * @brief 创建 OpenGL 后端设备
     * @return OpenGL 设备对象
     */
    std::unique_ptr<Device> CreateOpenGLDevice();

} // namespace Render::RHI
