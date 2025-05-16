#pragma once

namespace Resource{
    enum class MaterialType{
        GENERIC     = 1 << 0,
        BLINN_PHONG = 1 << 1,
        BPR         = 1 << 2,
        UNLIT       = 1 << 3
    };
}