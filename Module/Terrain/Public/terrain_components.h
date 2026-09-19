#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include <glm/glm.hpp>

#include "engine/ECS/Component/component.h"
#include "Render/Public/RHIResourceType/Buffer/vertex_buffer.h"
#include "Render/Public/rhi_resource_handle.h"

namespace Terrain {

inline constexpr int SectionSize = 16;
inline constexpr std::size_t SectionVolume = SectionSize * SectionSize * SectionSize;
using BlockId = std::uint16_t;

enum class Face : std::uint8_t { NegativeX, PositiveX, NegativeY, PositiveY, NegativeZ, PositiveZ };
enum class Layer : std::uint8_t { Opaque, Cutout, Transparent };

struct BlockRenderInfo {
    bool visible = false;
    bool occludes = false;
    Layer layer = Layer::Opaque;
    std::array<std::uint16_t, 6> tile{};
};

class BlockRenderRegistry {
public:
    void Set(BlockId id, BlockRenderInfo info) {
        if(id >= entries_.size()) entries_.resize(static_cast<std::size_t>(id) + 1);
        entries_[id] = info;
    }
    const BlockRenderInfo& Get(BlockId id) const {
        static const BlockRenderInfo air{};
        return id < entries_.size() ? entries_[id] : air;
    }
private:
    std::vector<BlockRenderInfo> entries_;
};

struct ChunkLocation : ECS::Component::Component<ChunkLocation> {
    glm::ivec3 section{0};
    bool LoadFromMetaDataImpl(const Json::Value&, Log::StackLogErrorHandle) { return true; }
    glm::vec3 WorldOrigin() const { return glm::vec3(section * SectionSize); }
};

struct ChunkBlocks : ECS::Component::Component<ChunkBlocks> {
    std::array<BlockId, SectionVolume> blocks{};
    std::uint64_t revision = 1;
    bool LoadFromMetaDataImpl(const Json::Value&, Log::StackLogErrorHandle) { return true; }

    static constexpr std::size_t Index(int x, int y, int z) {
        return static_cast<std::size_t>((y * SectionSize + z) * SectionSize + x);
    }
    BlockId Get(int x, int y, int z) const { return blocks[Index(x, y, z)]; }
    void Set(int x, int y, int z, BlockId id) { blocks[Index(x, y, z)] = id; ++revision; }
};

struct Vertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f};
    glm::vec2 uv{0.0f};
    std::uint32_t tile = 0;
};

struct CpuSubmesh {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    void Clear() { vertices.clear(); indices.clear(); }
};

struct ChunkMesh : ECS::Component::Component<ChunkMesh> {
    std::array<CpuSubmesh, 3> layers;
    std::uint64_t sourceRevision = 0;
    std::uint64_t meshRevision = 0;
    bool LoadFromMetaDataImpl(const Json::Value&, Log::StackLogErrorHandle) { return true; }
};

struct UploadResult {
    std::mutex mutex;
    Render::RenderResourceHandle<Render::VertexBufferSpec> handle;
    bool done = false;
    bool succeeded = false;
    bool updating = false;
    std::uint64_t revision = 0;
    std::uint32_t indexCount = 0;
};

struct GpuSubmesh {
    Render::RenderResourceHandle<Render::VertexBufferSpec> handle;
    std::uint32_t indexCount = 0;
    std::uint64_t uploadedRevision = 0;
    std::shared_ptr<UploadResult> pending;
};

struct ChunkRender : ECS::Component::Component<ChunkRender> {
    std::array<GpuSubmesh, 3> layers;
    bool visible = true;
    float viewDepth = 0.0f;
    bool LoadFromMetaDataImpl(const Json::Value&, Log::StackLogErrorHandle) { return true; }
};

inline void RegisterTerrainComponents() {
    REGISTER_COMPONENT("terrain_chunk_location", ChunkLocation);
    REGISTER_COMPONENT("terrain_chunk_blocks", ChunkBlocks);
    REGISTER_COMPONENT("terrain_chunk_mesh", ChunkMesh);
    REGISTER_COMPONENT("terrain_chunk_render", ChunkRender);
}

} // namespace Terrain
