#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include <glm/glm.hpp>

#include "engine/ECS/Component/component.h"
#include "engine/ECS/Component/component_loader_registry.h"
#include "Render/Public/RHIResourceType/Buffer/vertex_buffer.h"
#include "Render/Public/rhi_resource_handle.h"

namespace Terrain {

inline constexpr int SectionSize = 16;
// A terrain chunk is a full column: 16x16 footprint, tall enough for a
// Minecraft-style vertical scale (oceans, plains, mountains, snow peaks).
inline constexpr int SectionHeight = 128;
inline constexpr std::size_t SectionVolume =
    static_cast<std::size_t>(SectionSize) * SectionSize * SectionHeight;
using BlockId = std::uint16_t;

enum class Face : std::uint8_t { NegativeX, PositiveX, NegativeY, PositiveY, NegativeZ, PositiveZ };
enum class Layer : std::uint8_t { Opaque, Cutout, Transparent };

struct BlockRenderInfo {
    bool visible = false;
    bool occludes = false;
    Layer layer = Layer::Opaque;
    std::array<std::uint16_t, 6> tile{};
    // Per-face vertex colors. Kept alongside the tile index so the same mesh
    // can feed either a textured or a flat-shaded (color) pipeline.
    std::array<glm::vec3, 6> color{};

    // Material tiles, split into the three voxel faces. These are expanded into
    // the legacy per-face `tile[6]` array by ApplyFaceTiles(), so existing code
    // that reads `tile[face]` keeps working unchanged.
    std::uint16_t topTile = 0;
    std::uint16_t sideTile = 0;
    std::uint16_t bottomTile = 0;

    void ApplyFaceTiles() {
        tile[static_cast<std::size_t>(Face::PositiveY)] = topTile;
        tile[static_cast<std::size_t>(Face::NegativeY)] = bottomTile;
        tile[static_cast<std::size_t>(Face::NegativeX)] = sideTile;
        tile[static_cast<std::size_t>(Face::PositiveX)] = sideTile;
        tile[static_cast<std::size_t>(Face::NegativeZ)] = sideTile;
        tile[static_cast<std::size_t>(Face::PositiveZ)] = sideTile;
    }
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
    // Terrain generation is a one-shot per chunk; the flag lets the generation
    // system skip chunks that are already filled in.
    bool generated = false;

    // Async generation bookkeeping. The main thread marks a chunk pending and
    // stamps a never-reused token before submitting a worker job; the commit
    // step only writes the result back when the token still matches. This makes
    // a stale result (chunk unloaded, entity id recycled, or a newer request)
    // harmless instead of corrupting the chunk.
    bool generationPending = false;
    std::uint64_t generationToken = 0;

    // Inclusive vertical bounds of non-air content. Maintained as blocks are
    // written so the mesher can skip the (usually large) empty top of the
    // 128-tall column instead of scanning every cell.
    int minNonAirY = SectionHeight;
    int maxNonAirY = -1;

    bool LoadFromMetaDataImpl(const Json::Value&, Log::StackLogErrorHandle) { return true; }

    static constexpr std::size_t Index(int x, int y, int z) {
        return static_cast<std::size_t>((y * SectionSize + z) * SectionSize + x);
    }
    static constexpr bool InBounds(int x, int y, int z) {
        return x >= 0 && y >= 0 && z >= 0
            && x < SectionSize && y < SectionHeight && z < SectionSize;
    }
    BlockId Get(int x, int y, int z) const { return blocks[Index(x, y, z)]; }
    void MarkNonAir(int y) {
        if(y < minNonAirY) minNonAirY = y;
        if(y > maxNonAirY) maxNonAirY = y;
    }
    void Set(int x, int y, int z, BlockId id) {
        blocks[Index(x, y, z)] = id;
        if(id != 0) MarkNonAir(y);
        ++revision;
    }
    // Bump the revision without touching content. Used to invalidate a chunk's
    // mesh when a *neighbouring* block edit changes one of its exposed faces.
    void MarkDirty() { ++revision; }
};

// One-block-thick border layers just outside a chunk, handed to a meshing job.
//
// The main thread fills a layer only for neighbours that contain user edits, so
// border faces sample the live edited world instead of the pristine generator.
// Unedited/unloaded neighbours are simply left unset and the mesher falls back
// to the deterministic procedural cache (which equals the unedited neighbour).
//
// The layers are indexed by the in-plane coordinate `u` (z for the X faces, x
// for the Z faces) and y: value(u, y) is the neighbour block adjacent to the
// chunk's own block at that (u, y).
struct ChunkNeighborhood {
    static constexpr std::size_t SliceSize =
        static_cast<std::size_t>(SectionSize) * SectionHeight;
    using Slice = std::array<BlockId, SliceSize>;

    Slice negativeX{};
    Slice positiveX{};
    Slice negativeZ{};
    Slice positiveZ{};
    bool hasNegativeX = false;
    bool hasPositiveX = false;
    bool hasNegativeZ = false;
    bool hasPositiveZ = false;

    static constexpr std::size_t Index(int u, int y) {
        return static_cast<std::size_t>(u) * SectionHeight + y;
    }
    BlockId NegativeX(int u, int y) const { return negativeX[Index(u, y)]; }
    BlockId PositiveX(int u, int y) const { return positiveX[Index(u, y)]; }
    BlockId NegativeZ(int u, int y) const { return negativeZ[Index(u, y)]; }
    BlockId PositiveZ(int u, int y) const { return positiveZ[Index(u, y)]; }
};

struct Vertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f};
    glm::vec3 color{1.0f};
    glm::vec2 uv{0.0f};
    std::uint32_t tile = 0;
};

inline Render::VertexLayout TerrainVertexLayout() {
    Render::VertexLayout layout;
    layout.typeSlots = {
        Render::VertexFieldType::Vec3,
        Render::VertexFieldType::Vec3,
        Render::VertexFieldType::Vec3,
        Render::VertexFieldType::Vec2,
        Render::VertexFieldType::Int
    };
    return layout;
}

struct CpuSubmesh {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    void Clear() { vertices.clear(); indices.clear(); }
};

struct ChunkMesh : ECS::Component::Component<ChunkMesh> {
    std::array<CpuSubmesh, 3> layers;
    std::uint64_t sourceRevision = 0;
    std::uint64_t meshRevision = 0;

    // Async meshing bookkeeping, mirrors ChunkBlocks::generationPending/Token.
    // A meshing job owns an independent snapshot, never this component.
    bool meshPending = false;
    std::uint64_t meshToken = 0;

    bool LoadFromMetaDataImpl(const Json::Value&, Log::StackLogErrorHandle) { return true; }
};

struct UploadResult {
    std::mutex mutex;
    Render::RenderResourceHandle<Render::VertexBufferSpec> handle;
    bool done = false;
    bool succeeded = false;
    bool updating = false;
    bool cancelled = false; // chunk unloaded before the create callback arrived
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

// Marks the entity the chunk streamer centers the loaded region on.
struct TerrainViewer : ECS::Component::Component<TerrainViewer> {
    glm::vec3 position{0.0f};
    bool LoadFromMetaDataImpl(const Json::Value&, Log::StackLogErrorHandle) { return true; }
};

inline void RegisterTerrainComponents() {
    REGISTER_COMPONENT("terrain_chunk_location", ChunkLocation);
    REGISTER_COMPONENT("terrain_chunk_blocks", ChunkBlocks);
    REGISTER_COMPONENT("terrain_chunk_mesh", ChunkMesh);
    REGISTER_COMPONENT("terrain_chunk_render", ChunkRender);
    REGISTER_COMPONENT("terrain_viewer", TerrainViewer);
}

} // namespace Terrain
