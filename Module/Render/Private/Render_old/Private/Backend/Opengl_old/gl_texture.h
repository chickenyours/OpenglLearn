#pragma once

#include <cstdint>
#include "Render/Public/RHI/RHI_texture.h"

namespace Render::Backend::OpenGL {

    /**
     * @brief OpenGL 纹理实现类
     * 
     * 继承层次：
     * TextureAsset (CPU 端资产，包含像素数据) 
     *   ↓
     * RHITexture (GPU 资源抽象)
     *   ↓
     * GLTexture (OpenGL 具体实现)
     */
    class GLTexture final : public Render::RHI::RHITexture {
    public:
        explicit GLTexture(const Render::RHI::TextureDesc& desc);
        ~GLTexture() override;

        GLTexture(const GLTexture&) = delete;
        GLTexture& operator=(const GLTexture&) = delete;

        GLTexture(GLTexture&& other) noexcept;
        GLTexture& operator=(GLTexture&& other) noexcept;

        // RHITexture 接口实现
        bool Create() override;
        void* GetNativeHandle() const noexcept override;
        bool SetData(
            const void* data,
            uint32_t mipLevel = 0,
            uint32_t xOffset = 0,
            uint32_t yOffset = 0,
            uint32_t zOffset = 0,
            uint32_t uploadWidth = 0,
            uint32_t uploadHeight = 0,
            uint32_t uploadDepth = 0
        ) override;

        // OpenGL 特有方法
        void Destroy();
        void GenerateMips();
        void Bind(uint32_t unit) const;

        uint32_t GetHandle() const noexcept { return handle_; }
        uint32_t GetTarget() const noexcept { return target_; }

    private:
        bool ValidateDesc() const noexcept;
        void SetupDefaultSamplerState();

    private:
        uint32_t handle_ = 0;
        uint32_t target_ = 0;
    };

}
