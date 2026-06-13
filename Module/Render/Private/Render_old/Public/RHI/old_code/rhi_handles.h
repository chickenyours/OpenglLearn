#pragma once

#include <cstdint>

namespace Render::RHI {

    /**
     * @brief Buffer 逻辑句柄
     * 第一阶段使用简单 uint64_t，后续可扩展 generation 防止悬挂句柄
     */
    struct BufferHandle {
        uint64_t id = 0;

        bool operator==(const BufferHandle& other) const { return id == other.id; }
        bool operator!=(const BufferHandle& other) const { return id != other.id; }
        bool operator<(const BufferHandle& other) const { return id < other.id; }

        static BufferHandle Invalid() { return BufferHandle{0}; }
        bool IsValid() const { return id != 0; }
    };

    /**
     * @brief Texture 逻辑句柄
     */
    struct TextureHandle {
        uint64_t id = 0;

        bool operator==(const TextureHandle& other) const { return id == other.id; }
        bool operator!=(const TextureHandle& other) const { return id != other.id; }
        bool operator<(const TextureHandle& other) const { return id < other.id; }

        static TextureHandle Invalid() { return TextureHandle{0}; }
        bool IsValid() const { return id != 0; }
    };

    /**
     * @brief InputLayout 逻辑句柄
     */
    struct InputLayoutHandle {
        uint64_t id = 0;

        bool operator==(const InputLayoutHandle& other) const { return id == other.id; }
        bool operator!=(const InputLayoutHandle& other) const { return id != other.id; }
        bool operator<(const InputLayoutHandle& other) const { return id < other.id; }

        static InputLayoutHandle Invalid() { return InputLayoutHandle{0}; }
        bool IsValid() const { return id != 0; }
    };

} // namespace Render::RHI
