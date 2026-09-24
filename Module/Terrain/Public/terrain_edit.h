#pragma once

// World block access + user edit overlay (Terrain interaction phase).
//
// Two concerns live here:
//   1. WorldEditStore: the sparse "terrain edit overlay" that makes user changes
//      outlive chunk unload/reload. It is owned and mutated by the main thread
//      only; workers never see it (the generation commit applies it, and the
//      meshing scheduler snapshots it).
//   2. TerrainWorld: a main-thread lookup from world coordinates to the live
//      ChunkBlocks of the loaded chunk, plus SetBlockWorld() which writes the
//      block, records the override and marks the affected meshes dirty.
//
// Nothing here is called from a worker. It intentionally reads ChunkBlocks (the
// editable source of truth) and never re-runs the procedural generator, so the
// raycast / edits see the current world, not the pristine one.

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "engine/ECS/Scene/scene.h"
#include "Terrain/Public/terrain_components.h"

namespace Terrain {

// ---------------------------------------------------------------------------
// Floor-based world <-> chunk/local conversion (negative coordinates included)
// ---------------------------------------------------------------------------

inline int FloorDiv(int value, int divisor) {
    const int quotient = value / divisor;
    const int remainder = value % divisor;
    if(remainder != 0 && ((remainder < 0) != (divisor < 0))) return quotient - 1;
    return quotient;
}

inline int FloorMod(int value, int divisor) {
    return value - FloorDiv(value, divisor) * divisor;
}

inline glm::ivec2 WorldToChunk(int worldX, int worldZ) {
    return glm::ivec2(FloorDiv(worldX, SectionSize), FloorDiv(worldZ, SectionSize));
}

inline glm::ivec3 WorldToSection(int worldX, int worldZ) {
    const glm::ivec2 chunk = WorldToChunk(worldX, worldZ);
    return glm::ivec3(chunk.x, 0, chunk.y);
}

// Local coordinate inside the owning chunk column (y is already 0-based because
// a chunk spans the full SectionHeight).
inline glm::ivec3 WorldToLocal(int worldX, int worldY, int worldZ) {
    return glm::ivec3(FloorMod(worldX, SectionSize), worldY,
                      FloorMod(worldZ, SectionSize));
}

struct IVec2Hash {
    std::size_t operator()(const glm::ivec2& value) const noexcept {
        std::uint64_t x = static_cast<std::uint32_t>(value.x);
        std::uint64_t y = static_cast<std::uint32_t>(value.y);
        std::uint64_t h = (x << 32u) ^ y;
        h ^= h >> 33u;
        h *= 0xff51afd7ed558ccdULL;
        h ^= h >> 33u;
        return static_cast<std::size_t>(h);
    }
};

// ---------------------------------------------------------------------------
// Sparse, per-chunk edit overlay
// ---------------------------------------------------------------------------

struct BlockEdit {
    glm::ivec3 position{0};  // world position
    BlockId id = 0;
};

// Main-thread owned. Keyed by chunk column so (a) applying on generation commit
// is O(edits in one chunk) and (b) it serialises naturally by chunk later.
class WorldEditStore {
public:
    void Set(const glm::ivec3& position, BlockId id) {
        std::vector<BlockEdit>& edits = edits_[glm::ivec2(
            FloorDiv(position.x, SectionSize), FloorDiv(position.z, SectionSize))];
        for(BlockEdit& edit : edits) {
            if(edit.position == position) {
                edit.id = id;
                return;
            }
        }
        edits.push_back(BlockEdit{position, id});
    }

    bool TryGet(const glm::ivec3& position, BlockId& out) const {
        const auto found = edits_.find(glm::ivec2(
            FloorDiv(position.x, SectionSize), FloorDiv(position.z, SectionSize)));
        if(found == edits_.end()) return false;
        for(const BlockEdit& edit : found->second) {
            if(edit.position == position) {
                out = edit.id;
                return true;
            }
        }
        return false;
    }

    // Applies every override that lands inside `section` to `blocks`.
    // Returns the number of edits applied (0 when the chunk has none).
    std::size_t Apply(const glm::ivec3& section, ChunkBlocks& blocks) const {
        const auto found = edits_.find(glm::ivec2(section.x, section.z));
        if(found == edits_.end()) return 0;

        std::size_t applied = 0;
        for(const BlockEdit& edit : found->second) {
            const glm::ivec3 local = WorldToLocal(
                edit.position.x, edit.position.y, edit.position.z);
            if(!ChunkBlocks::InBounds(local.x, local.y, local.z)) continue;
            blocks.Set(local.x, local.y, local.z, edit.id);
            ++applied;
        }
        return applied;
    }

    const std::vector<BlockEdit>* EditsForChunk(const glm::ivec3& section) const {
        const auto found = edits_.find(glm::ivec2(section.x, section.z));
        return found == edits_.end() ? nullptr : &found->second;
    }

    std::size_t EditCount() const {
        std::size_t count = 0;
        for(const auto& entry : edits_) count += entry.second.size();
        return count;
    }

    void Clear() { edits_.clear(); }

private:
    std::unordered_map<glm::ivec2, std::vector<BlockEdit>, IVec2Hash> edits_;
};

// ---------------------------------------------------------------------------
// Live world lookup / edit API
// ---------------------------------------------------------------------------

struct BlockEditResult {
    bool foundChunk = false;   // owning chunk is loaded
    bool changed = false;      // a live ChunkBlocks was modified
    int dirtyChunks = 0;       // meshes invalidated (self + boundary neighbours)
};

class TerrainWorld {
public:
    void SetScene(ECS::Core::Scene* scene) { scene_ = scene; }

    WorldEditStore& Edits() { return edits_; }
    const WorldEditStore& Edits() const { return edits_; }

    // Registry of loaded chunk entities, maintained by the streaming system.
    void RegisterChunk(const glm::ivec3& section, ECS::EntityID entity) {
        chunks_[glm::ivec2(section.x, section.z)] = entity;
    }
    void UnregisterChunk(const glm::ivec3& section) {
        chunks_.erase(glm::ivec2(section.x, section.z));
    }
    void ClearChunks() { chunks_.clear(); }

    ChunkBlocks* FindBlocks(const glm::ivec3& section) {
        if(scene_ == nullptr) return nullptr;
        const auto found = chunks_.find(glm::ivec2(section.x, section.z));
        if(found == chunks_.end()) return nullptr;
        auto handle = scene_->GetActiveComponent<ChunkBlocks>(found->second);
        return handle.Get();  // nullptr when the entity is dead
    }

    // The raycast / debug read path. Returns Air for unloaded or not-yet-filled
    // chunks, and never regenerates procedurally.
    BlockId GetBlockWorld(int worldX, int worldY, int worldZ) {
        ChunkBlocks* blocks = FindBlocks(WorldToSection(worldX, worldZ));
        if(blocks == nullptr || !blocks->generated) return 0;
        const glm::ivec3 local = WorldToLocal(worldX, worldY, worldZ);
        if(!ChunkBlocks::InBounds(local.x, local.y, local.z)) return 0;
        return blocks->Get(local.x, local.y, local.z);
    }

    // Breaks/places a block. Records the override first (so it survives a
    // reload), then writes the live chunk and invalidates the affected meshes.
    BlockEditResult SetBlockWorld(const glm::ivec3& position, BlockId id) {
        BlockEditResult result;
        edits_.Set(position, id);

        const glm::ivec3 section = WorldToSection(position.x, position.z);
        ChunkBlocks* blocks = FindBlocks(section);
        result.foundChunk = blocks != nullptr;
        if(blocks == nullptr || !blocks->generated) return result;

        const glm::ivec3 local = WorldToLocal(position.x, position.y, position.z);
        if(!ChunkBlocks::InBounds(local.x, local.y, local.z)) return result;

        blocks->Set(local.x, local.y, local.z, id);
        result.changed = true;
        ++result.dirtyChunks;  // self

        // A block on a horizontal border exposes/hides a face in the neighbour.
        if(local.x == 0) {
            if(MarkChunkDirty(glm::ivec3(section.x - 1, 0, section.z))) ++result.dirtyChunks;
        }
        if(local.x == SectionSize - 1) {
            if(MarkChunkDirty(glm::ivec3(section.x + 1, 0, section.z))) ++result.dirtyChunks;
        }
        if(local.z == 0) {
            if(MarkChunkDirty(glm::ivec3(section.x, 0, section.z - 1))) ++result.dirtyChunks;
        }
        if(local.z == SectionSize - 1) {
            if(MarkChunkDirty(glm::ivec3(section.x, 0, section.z + 1))) ++result.dirtyChunks;
        }
        return result;
    }

    bool MarkChunkDirty(const glm::ivec3& section) {
        ChunkBlocks* blocks = FindBlocks(section);
        if(blocks == nullptr || !blocks->generated) return false;
        blocks->MarkDirty();
        return true;
    }

    // Invalidates the 4 horizontal neighbours (used after a generated chunk
    // applies edits at its border).
    int MarkNeighborsDirty(const glm::ivec3& section) {
        int dirty = 0;
        if(MarkChunkDirty(glm::ivec3(section.x - 1, 0, section.z))) ++dirty;
        if(MarkChunkDirty(glm::ivec3(section.x + 1, 0, section.z))) ++dirty;
        if(MarkChunkDirty(glm::ivec3(section.x, 0, section.z - 1))) ++dirty;
        if(MarkChunkDirty(glm::ivec3(section.x, 0, section.z + 1))) ++dirty;
        return dirty;
    }

    // Snapshots the one-block border layer of each *edited* neighbour for a mesh
    // job. Unedited neighbours are left unset so the mesher uses the (equal)
    // procedural cache; this keeps the common streaming case allocation-free.
    void CollectNeighborhood(const glm::ivec3& section, ChunkNeighborhood& out) {
        out = ChunkNeighborhood{};

        const auto copyXSlice = [&](int neighborX, int neighborZ, int localX,
                                    ChunkNeighborhood::Slice& slice) {
            const glm::ivec3 neighborSection(neighborX, 0, neighborZ);
            const std::vector<BlockEdit>* edits = edits_.EditsForChunk(neighborSection);
            if(edits == nullptr || edits->empty()) return false;
            ChunkBlocks* blocks = FindBlocks(neighborSection);
            if(blocks == nullptr || !blocks->generated) return false;
            for(int z = 0; z < SectionSize; ++z) {
                for(int y = 0; y < SectionHeight; ++y) {
                    slice[ChunkNeighborhood::Index(z, y)] = blocks->Get(localX, y, z);
                }
            }
            return true;
        };

        const auto copyZSlice = [&](int neighborX, int neighborZ, int localZ,
                                    ChunkNeighborhood::Slice& slice) {
            const glm::ivec3 neighborSection(neighborX, 0, neighborZ);
            const std::vector<BlockEdit>* edits = edits_.EditsForChunk(neighborSection);
            if(edits == nullptr || edits->empty()) return false;
            ChunkBlocks* blocks = FindBlocks(neighborSection);
            if(blocks == nullptr || !blocks->generated) return false;
            for(int x = 0; x < SectionSize; ++x) {
                for(int y = 0; y < SectionHeight; ++y) {
                    slice[ChunkNeighborhood::Index(x, y)] = blocks->Get(x, y, localZ);
                }
            }
            return true;
        };

        out.hasNegativeX = copyXSlice(section.x - 1, section.z, SectionSize - 1,
                                      out.negativeX);
        out.hasPositiveX = copyXSlice(section.x + 1, section.z, 0, out.positiveX);
        out.hasNegativeZ = copyZSlice(section.x, section.z - 1, SectionSize - 1,
                                      out.negativeZ);
        out.hasPositiveZ = copyZSlice(section.x, section.z + 1, 0, out.positiveZ);
    }

    std::size_t LoadedChunkCount() const { return chunks_.size(); }

private:
    ECS::Core::Scene* scene_ = nullptr;
    std::unordered_map<glm::ivec2, ECS::EntityID, IVec2Hash> chunks_;
    WorldEditStore edits_;
};

} // namespace Terrain
