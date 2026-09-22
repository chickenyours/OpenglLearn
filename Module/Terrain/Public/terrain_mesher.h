#pragma once

#include <array>
#include <cstdint>

#include <glm/glm.hpp>

#include "Terrain/Public/terrain_components.h"

namespace Terrain {

// Turns a chunk column of block ids into CPU submeshes. Faces are culled with a
// caller supplied neighbour sampler so that chunk borders are seamless: the
// system fills in-bounds lookups from the component and out-of-bounds ones from
// the deterministic generator.
class TerrainMesher {
public:
    template <typename NeighborSampler>
    static void Build(const ChunkBlocks& blocks,
                      const glm::ivec3& section,
                      const BlockRenderRegistry& registry,
                      ChunkMesh& out,
                      NeighborSampler&& sample) {
        for(auto& layer : out.layers) layer.Clear();

        const glm::vec3 origin =
            glm::vec3(section) * static_cast<float>(SectionSize);

        for(int y = 0; y < SectionHeight; ++y) {
            for(int z = 0; z < SectionSize; ++z) {
                for(int x = 0; x < SectionSize; ++x) {
                    const BlockId id = blocks.Get(x, y, z);
                    const BlockRenderInfo& block = registry.Get(id);
                    if(!block.visible) continue;

                    for(std::size_t face = 0; face < 6; ++face) {
                        const glm::ivec3 direction = directions_[face];
                        const BlockId neighborId = sample(
                            x + direction.x, y + direction.y, z + direction.z);
                        const BlockRenderInfo& neighbor = registry.Get(neighborId);

                        if(block.layer == Layer::Transparent) {
                            // Water only draws a face against air. Against land it
                            // would z-fight with the bank, against water it would
                            // blend on top of itself.
                            if(neighborId != static_cast<BlockId>(0)) continue;
                        } else if(neighbor.occludes) {
                            continue;
                        }

                        const float variation = BlockVariation(
                            static_cast<int>(origin.x) + x,
                            static_cast<int>(origin.y) + y,
                            static_cast<int>(origin.z) + z);
                        EmitFace(
                            glm::vec3(static_cast<float>(x),
                                      static_cast<float>(y),
                                      static_cast<float>(z)),
                            face,
                            block,
                            block.color[face] * variation,
                            out.layers[static_cast<std::size_t>(block.layer)]);
                    }
                }
            }
        }

        out.sourceRevision = blocks.revision;
        ++out.meshRevision;
    }

private:
    // Small deterministic tint so large flat areas are not a single color.
    static float BlockVariation(int x, int y, int z) {
        std::uint32_t h = static_cast<std::uint32_t>(x) * 0x8da6b343u;
        h ^= static_cast<std::uint32_t>(y) * 0x9e3779b9u;
        h ^= static_cast<std::uint32_t>(z) * 0xd8163841u;
        h ^= h >> 15u;
        h *= 0x2c1b3c6du;
        h ^= h >> 12u;
        return 0.92f + 0.12f
            * (static_cast<float>(h & 0x00ffffffu)
               / static_cast<float>(0x01000000u));
    }

    static void EmitFace(glm::vec3 base,
                         std::size_t face,
                         const BlockRenderInfo& block,
                         glm::vec3 color,
                         CpuSubmesh& out) {
        const std::uint32_t first = static_cast<std::uint32_t>(out.vertices.size());
        for(std::size_t i = 0; i < 4; ++i) {
            out.vertices.push_back(Vertex{
                base + corners_[face][i], normals_[face], color, uvs_[i], block.tile[face]});
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
