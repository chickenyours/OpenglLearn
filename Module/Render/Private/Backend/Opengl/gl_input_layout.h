#pragma once

#include "Render/Public/RHI/RHI_input_layout.h"

namespace Render::Backend::OpenGL {

    class GLInputLayout final : public Render::RHI::InputLayout {
    public:
        explicit GLInputLayout(const Render::RHI::InputLayoutDesc& desc);
        ~GLInputLayout() override;

        GLInputLayout(const GLInputLayout&) = delete;
        GLInputLayout& operator=(const GLInputLayout&) = delete;

        GLInputLayout(GLInputLayout&& other) noexcept;
        GLInputLayout& operator=(GLInputLayout&& other) noexcept;

        bool Initialize();
        void Destroy();

        bool Create();

        bool Check() const noexcept override;

        bool Bind() const;
        void Unbind() const;

        uint32_t GetHandle() const noexcept { return handle_; }

    private:
        uint32_t handle_ = 0;
    };

}
