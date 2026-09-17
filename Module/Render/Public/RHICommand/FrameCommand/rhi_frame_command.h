#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include <glm/glm.hpp>

#include "Render/Public/RHICommand/FrameCommand/rhi_command_dispatcher.h"
#include "Render/Public/RHIResourceType/rhi_resource_type.h"

namespace Render::RHICommand {

enum ClearFlagBits : uint8_t {
    ClearNone = 0,
    ClearColor = 1u << 0u,
    ClearDepth = 1u << 1u,
    ClearStencil = 1u << 2u
};

struct SetBackgroundColor {
    glm::vec4 color{0.0f, 0.0f, 0.0f, 1.0f};
};

struct Flip {};

struct SetVertexBuffer {
    RenderResourceHandle<VertexBufferSpec> buffer;
};

struct SetPipeline {
    RenderResourceHandle<PipelineSpec> pipeline;
};

struct Draw {
    uint32_t vertexCount = 0; // 0 means all vertices after firstVertex.
    uint32_t firstVertex = 0;
    uint32_t instanceCount = 1;
    uint32_t firstInstance = 0;
};

// Kept for source compatibility; new code should use Draw/DrawIndexed with an
// instanceCount greater than one.
struct DrawInstance {
    uint32_t instanceCount = 1;
};

struct DrawRect {};

struct BeginFrame {
    uint64_t frameIndex = 0;
    uint32_t framebufferWidth = 1;
    uint32_t framebufferHeight = 1;
    uint8_t clearFlags = ClearColor | ClearDepth;
    glm::vec4 clearColor{0.45f, 0.68f, 0.92f, 1.0f};
    float clearDepth = 1.0f;
    int32_t clearStencil = 0;
};

struct EndFrame {
    bool present = true;
};

struct SetViewport {
    int32_t x = 0;
    int32_t y = 0;
    uint32_t width = 1;
    uint32_t height = 1;
    float minDepth = 0.0f;
    float maxDepth = 1.0f;
};

struct SetScissor {
    bool enabled = false;
    int32_t x = 0;
    int32_t y = 0;
    uint32_t width = 1;
    uint32_t height = 1;
};

struct BindTexture {
    RenderResourceHandle<RHITextureSpec> texture;
    uint32_t slot = 0;
};

struct BindUniformBuffer {
    RenderResourceHandle<UniformBufferSpec> buffer;
    uint32_t binding = 0;
    uint32_t offset = 0;
    uint32_t size = 0; // 0 binds the remainder of the buffer.
};

inline constexpr size_t MaxInlineUniformBytes = 256;

struct UpdateUniformBuffer {
    RenderResourceHandle<UniformBufferSpec> buffer;
    uint32_t offset = 0;
    uint32_t size = 0;
    std::array<std::byte, MaxInlineUniformBytes> data{};
};

template <typename T>
UpdateUniformBuffer MakeUniformUpdate(
    RenderResourceHandle<UniformBufferSpec> buffer,
    const T& value,
    uint32_t offset = 0
) {
    static_assert(std::is_trivially_copyable_v<T>);
    static_assert(sizeof(T) <= MaxInlineUniformBytes);
    UpdateUniformBuffer result{};
    result.buffer = buffer;
    result.offset = offset;
    result.size = static_cast<uint32_t>(sizeof(T));
    std::memcpy(result.data.data(), &value, sizeof(T));
    return result;
}

struct DrawIndexed {
    uint32_t indexCount = 0; // 0 means all indices after firstIndex.
    uint32_t firstIndex = 0;
    int32_t baseVertex = 0;
    uint32_t instanceCount = 1;
    uint32_t firstInstance = 0;
};

// Existing command ids stay stable (0..5). New commands are appended so old
// captured buffers and tests remain readable.
using FrameCommandsSet = CommandTable<
    SetBackgroundColor,
    Flip,
    SetVertexBuffer,
    SetPipeline,
    Draw,
    DrawRect,
    BeginFrame,
    EndFrame,
    SetViewport,
    SetScissor,
    BindTexture,
    BindUniformBuffer,
    UpdateUniformBuffer,
    DrawIndexed,
    DrawInstance
>;

} // namespace Render::RHICommand
