#pragma once
#include "materials.h"
#include "engine/ECS/Component/component_loader_registry.h"
#include <vector>
namespace PixelSandbox {
inline constexpr int ChunkSize = 16;
inline constexpr int FieldScale = 4;
struct AirCell { float pressure = 0, vx = 0, vy = 0; };
// One world entity owns contiguous storage. Pixel identity travels in Cell;
// no ECS lookup, allocation, virtual call or neighbor list is needed per pixel.
struct GridStorage : ECS::Component::Component<GridStorage> {
    std::vector<Cell> cells;
    std::vector<AirCell> air, nextAir;
    bool LoadFromMetaDataImpl(const Json::Value&, Log::StackLogErrorHandle) { return false; }
};
struct ChunkRegion : ECS::Component::Component<ChunkRegion> {
    int x = 0, y = 0;
    std::uint8_t awake = 4;
    bool LoadFromMetaDataImpl(const Json::Value&, Log::StackLogErrorHandle) { return false; }
};
} // namespace PixelSandbox
