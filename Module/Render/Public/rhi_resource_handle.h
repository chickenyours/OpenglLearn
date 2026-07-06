#pragma once


#include <cstdint>
#include <vector>
#include <queue>
#include <optional>
#include <utility>

namespace Render{
    template <typename T>
    struct RenderResourceHandle
    {
        uint32_t id = UINT32_MAX;
        uint32_t version = 0;

        bool IsValid() const
        {
            return id != UINT32_MAX;
        }

        bool operator==(const RenderResourceHandle& other) const {
            return other.id == this->id && other.version == this->version;
        }
        bool operator!=(const RenderResourceHandle& other) const {
            return other.id != this->id || other.version != this->version;
        }
    };
}