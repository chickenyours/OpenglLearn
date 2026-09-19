#pragma once

#include <array>

#include "Terrain/Public/terrain_components.h"

namespace Terrain {

class TerrainMesher {
public:
    static void Build(const ChunkBlocks& blocks, const BlockRenderRegistry& registry, ChunkMesh& out) {
        for(auto& layer : out.layers) layer.Clear();
        for(int y = 0; y < SectionSize; ++y) for(int z = 0; z < SectionSize; ++z) for(int x = 0; x < SectionSize; ++x) {
            const BlockId id = blocks.Get(x, y, z);
            const BlockRenderInfo& block = registry.Get(id);
            if(!block.visible) continue;
            for(std::size_t face = 0; face < 6; ++face) {
                const glm::ivec3 neighbor = glm::ivec3(x, y, z) + directions_[face];
                bool occluded = false;
                if(InBounds(neighbor)) occluded = registry.Get(blocks.Get(neighbor.x, neighbor.y, neighbor.z)).occludes;
                if(!occluded) EmitFace(glm::vec3(x, y, z), face, block, out.layers[static_cast<std::size_t>(block.layer)]);
            }
        }
        out.sourceRevision = blocks.revision;
        ++out.meshRevision;
    }

private:
    static bool InBounds(glm::ivec3 p) {
        return p.x >= 0 && p.y >= 0 && p.z >= 0 && p.x < SectionSize && p.y < SectionSize && p.z < SectionSize;
    }

    static void EmitFace(glm::vec3 base, std::size_t face, const BlockRenderInfo& block, CpuSubmesh& out) {
        const std::uint32_t first = static_cast<std::uint32_t>(out.vertices.size());
        for(std::size_t i = 0; i < 4; ++i) {
            out.vertices.push_back(Vertex{base + corners_[face][i], normals_[face], uvs_[i], block.tile[face]});
        }
        constexpr std::array<std::uint32_t, 6> pattern{0, 1, 2, 0, 2, 3};
        for(const auto index : pattern) out.indices.push_back(first + index);
    }

    inline static constexpr std::array<glm::ivec3, 6> directions_{
        glm::ivec3{-1,0,0}, {1,0,0}, {0,-1,0}, {0,1,0}, {0,0,-1}, {0,0,1}};
    inline static const std::array<glm::vec3, 6> normals_{
        glm::vec3{-1,0,0}, {1,0,0}, {0,-1,0}, {0,1,0}, {0,0,-1}, {0,0,1}};
    inline static const std::array<glm::vec2, 4> uvs_{glm::vec2{0,0}, {1,0}, {1,1}, {0,1}};
    inline static const std::array<std::array<glm::vec3,4>,6> corners_{
        std::array<glm::vec3,4>{glm::vec3{0,0,1}, {0,0,0}, {0,1,0}, {0,1,1}},
        std::array<glm::vec3,4>{glm::vec3{1,0,0}, {1,0,1}, {1,1,1}, {1,1,0}},
        std::array<glm::vec3,4>{glm::vec3{0,0,0}, {0,0,1}, {1,0,1}, {1,0,0}},
        std::array<glm::vec3,4>{glm::vec3{0,1,1}, {0,1,0}, {1,1,0}, {1,1,1}},
        std::array<glm::vec3,4>{glm::vec3{1,0,0}, {0,0,0}, {0,1,0}, {1,1,0}},
        std::array<glm::vec3,4>{glm::vec3{0,0,1}, {1,0,1}, {1,1,1}, {0,1,1}}
    };
};

} // namespace Terrain
