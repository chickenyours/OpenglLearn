#pragma once
#include <cstdint>
#include <cstddef>

namespace Render::Device::Command{

    enum class CommandId : uint16_t {
        CreateTexture = 1,
        DestroyTexture = 2,
        UpdateTexture = 3,
        Draw = 4,
    };

    struct CommandHeader {
        CommandId id;
        uint16_t size;
    };

}