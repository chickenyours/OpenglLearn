#pragma once

#include <cstdint>
#include <vector>

namespace Render::RHI {

    enum class VertexElementFormat {
        Float1,
        Float2,
        Float3,
        Float4,

        UInt1,
        UInt2,
        UInt3,
        UInt4,

        UByte4_Norm
    };

    enum class VertexInputRate {
        PerVertex,
        PerInstance
    };

    struct VertexAttributeDesc {
        uint32_t location = 0;
        uint32_t binding = 0;
        VertexElementFormat format = VertexElementFormat::Float3;
        uint32_t offset = 0;
    };

    struct VertexBufferBindingDesc {
        uint32_t binding = 0;
        uint32_t stride = 0;
        VertexInputRate inputRate = VertexInputRate::PerVertex;
    };

    struct InputLayoutDesc {
        std::vector<VertexAttributeDesc> attributes;
        std::vector<VertexBufferBindingDesc> bindings;
    };

    class InputLayout {
    public:
        explicit InputLayout(const InputLayoutDesc& desc) : desc_(desc) {}
        virtual ~InputLayout() = default;

        const InputLayoutDesc& GetDesc() const noexcept { return desc_; }

        virtual bool Check() const noexcept = 0;

    protected:
        InputLayoutDesc desc_;
    };

}
