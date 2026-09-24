#pragma once

// Voxel ray traversal (Amanatides & Woo 3D DDA).
//
// Casts a ray from a world-space origin along a direction and returns the first
// solid voxel within `maxDistance`. It walks the integer voxel grid one cell at
// a time, so a reach of N blocks costs O(N) steps instead of a brute-force
// fine-grained march.
//
// The caller supplies a block provider `(int x, int y, int z) -> BlockId`. For
// the interactive path this must read the *live* ChunkBlocks (see TerrainWorld),
// never the procedural generator, so raycasts agree with the edited world.

#include <cmath>
#include <limits>

#include <glm/glm.hpp>

#include "Terrain/Public/terrain_components.h"

namespace Terrain {

struct VoxelRaycastHit {
    bool hit = false;
    glm::ivec3 block{0};     // solid voxel that was hit (break target)
    glm::ivec3 previous{0};  // last empty voxel before it (future place target)
    glm::ivec3 normal{0};    // face normal, pointing back toward the ray origin
    BlockId blockId = 0;
    float distance = 0.0f;   // ray parameter at entry (world units)
    int steps = 0;           // voxels traversed (profiling)
};

// `getBlock` is anything callable as getBlock(int, int, int) -> BlockId, where 0
// means air. Starting inside a solid voxel counts as an immediate hit when
// `hitOriginSolid` is true (documented behaviour, tested explicitly).
template <typename BlockProvider>
VoxelRaycastHit RaycastBlocks(const glm::vec3& origin, const glm::vec3& direction,
                              float maxDistance, BlockProvider&& getBlock,
                              bool hitOriginSolid = true) {
    VoxelRaycastHit result;
    if (maxDistance <= 0.0f) return result;
    const float length = glm::length(direction);
    if (length < 1e-6f) return result;
    const glm::vec3 dir = direction / length;

    glm::ivec3 voxel(
        static_cast<int>(std::floor(origin.x)),
        static_cast<int>(std::floor(origin.y)),
        static_cast<int>(std::floor(origin.z)));

    const glm::ivec3 axisSign(
        dir.x > 0.0f ? 1 : (dir.x < 0.0f ? -1 : 0),
        dir.y > 0.0f ? 1 : (dir.y < 0.0f ? -1 : 0),
        dir.z > 0.0f ? 1 : (dir.z < 0.0f ? -1 : 0));

    if (hitOriginSolid) {
        const BlockId id = getBlock(voxel.x, voxel.y, voxel.z);
        if (id != 0) {
            result.hit = true;
            result.block = voxel;
            result.previous = voxel;
            result.blockId = id;
            result.normal = -axisSign;
            result.distance = 0.0f;
            return result;
        }
    }

    const float infinity = std::numeric_limits<float>::infinity();
    glm::vec3 tMax(infinity);
    glm::vec3 tDelta(infinity);
    for (int axis = 0; axis < 3; ++axis) {
        if (axisSign[axis] == 0) continue;
        const float start = origin[axis];
        const float cell = static_cast<float>(voxel[axis]);
        if (axisSign[axis] > 0) {
            tMax[axis] = (cell + 1.0f - start) / dir[axis];
        } else {
            tMax[axis] = (cell - start) / dir[axis];
        }
        tDelta[axis] = 1.0f / std::abs(dir[axis]);
    }

    constexpr int kMaxSteps = 512;  // safety bound; reach is ~6-8 blocks
    glm::ivec3 previous = voxel;
    for (int i = 0; i < kMaxSteps; ++i) {
        int axis = 0;
        if (tMax.x < tMax.y && tMax.x < tMax.z) {
            axis = 0;
        } else if (tMax.y < tMax.z) {
            axis = 1;
        } else {
            axis = 2;
        }

        const float t = tMax[axis];
        if (t > maxDistance) break;

        tMax[axis] += tDelta[axis];
        voxel[axis] += axisSign[axis];
        result.steps = i + 1;

        const BlockId id = getBlock(voxel.x, voxel.y, voxel.z);
        if (id != 0) {
            result.hit = true;
            result.block = voxel;
            result.previous = previous;
            result.blockId = id;
            result.distance = t;
            result.normal = glm::ivec3(0);
            result.normal[axis] = -axisSign[axis];
            return result;
        }
        previous = voxel;
    }

    return result;
}

} // namespace Terrain
