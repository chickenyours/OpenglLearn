// Voxel raycast (3D DDA) verification:
//  - +X / -X axis hits
//  - diagonal hit
//  - starting inside a solid voxel (documented immediate hit)
//  - max-distance rejection
//  - negative world coordinates
//  - hitOriginSolid = false skips the starting voxel

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Terrain/Public/terrain_raycast.h"

using namespace Terrain;

namespace {

bool Expect(bool condition, const char* what) {
    std::printf("  %-56s %s\n", what, condition ? "ok" : "FAIL");
    return condition;
}

struct SolidSet {
    std::vector<glm::ivec3> cells;
    BlockId operator()(int x, int y, int z) const {
        for (const glm::ivec3& cell : cells) {
            if (cell.x == x && cell.y == y && cell.z == z) return 1;
        }
        return 0;
    }
};

glm::vec3 Normalize(glm::vec3 v) { return glm::normalize(v); }

} // namespace

int main() {
    int failures = 0;

    // +X
    {
        SolidSet solid{{{3, 1, 1}}};
        const VoxelRaycastHit hit = RaycastBlocks(
            glm::vec3(0.5f, 1.5f, 1.5f), glm::vec3(1.0f, 0.0f, 0.0f), 10.0f, solid);
        std::printf("  +X hit block (%d,%d,%d) normal (%d,%d,%d) dist %.2f steps %d\n",
                    hit.block.x, hit.block.y, hit.block.z,
                    hit.normal.x, hit.normal.y, hit.normal.z, hit.distance, hit.steps);
        failures += !Expect(hit.hit && hit.block == glm::ivec3(3, 1, 1),
                            "+X: hits (3,1,1)");
        failures += !Expect(hit.normal == glm::ivec3(-1, 0, 0), "+X: normal is -X");
        failures += !Expect(hit.previous == glm::ivec3(2, 1, 1), "+X: previous is (2,1,1)");
        failures += !Expect(std::abs(hit.distance - 2.5f) < 1e-4f, "+X: distance is 2.5");
    }

    // -X
    {
        SolidSet solid{{{0, 1, 1}}};
        const VoxelRaycastHit hit = RaycastBlocks(
            glm::vec3(3.5f, 1.5f, 1.5f), glm::vec3(-1.0f, 0.0f, 0.0f), 10.0f, solid);
        failures += !Expect(hit.hit && hit.block == glm::ivec3(0, 1, 1),
                            "-X: hits (0,1,1)");
        failures += !Expect(hit.normal == glm::ivec3(1, 0, 0), "-X: normal is +X");
    }

    // Diagonal
    {
        SolidSet solid{{{3, 3, 3}}};
        const VoxelRaycastHit hit = RaycastBlocks(
            glm::vec3(0.5f, 0.5f, 0.5f), Normalize(glm::vec3(1.0f, 1.0f, 1.0f)),
            10.0f, solid);
        failures += !Expect(hit.hit && hit.block == glm::ivec3(3, 3, 3),
                            "diagonal: hits (3,3,3)");
        const int normalComponents = std::abs(hit.normal.x) + std::abs(hit.normal.y)
            + std::abs(hit.normal.z);
        failures += !Expect(normalComponents == 1, "diagonal: normal is a single axis");
    }

    // Start inside solid -> immediate hit.
    {
        SolidSet solid{{{1, 1, 1}}};
        const VoxelRaycastHit hit = RaycastBlocks(
            glm::vec3(1.5f, 1.5f, 1.5f), glm::vec3(1.0f, 0.0f, 0.0f), 10.0f, solid);
        failures += !Expect(hit.hit && hit.block == glm::ivec3(1, 1, 1),
                            "start inside solid: immediate hit");
        failures += !Expect(hit.distance == 0.0f, "start inside: distance 0");
    }

    // hitOriginSolid = false skips the origin voxel.
    {
        SolidSet solid{{{0, 0, 0}}};
        const VoxelRaycastHit hit = RaycastBlocks(
            glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(1.0f, 0.0f, 0.0f), 5.0f, solid,
            false);
        failures += !Expect(!hit.hit, "hitOriginSolid=false: skips origin voxel");
    }

    // Max distance rejection.
    {
        SolidSet solid{{{5, 1, 1}}};
        const VoxelRaycastHit hit = RaycastBlocks(
            glm::vec3(0.5f, 1.5f, 1.5f), glm::vec3(1.0f, 0.0f, 0.0f), 3.0f, solid);
        failures += !Expect(!hit.hit, "reach limit: block beyond reach missed");
    }

    // Negative coordinates, axis aligned.
    {
        SolidSet solid{{{-2, 1, 1}}};
        const VoxelRaycastHit hit = RaycastBlocks(
            glm::vec3(0.5f, 1.5f, 1.5f), glm::vec3(-1.0f, 0.0f, 0.0f), 10.0f, solid);
        std::printf("  -X to negative coords hit (%d,%d,%d)\n",
                    hit.block.x, hit.block.y, hit.block.z);
        failures += !Expect(hit.hit && hit.block == glm::ivec3(-2, 1, 1),
                            "negative: hits (-2,1,1)");
        failures += !Expect(hit.normal == glm::ivec3(1, 0, 0), "negative: normal is +X");
    }

    // Negative voxel floor: origin at -0.5 floors to -1.
    {
        SolidSet solid{{{-5, -1, -1}}};
        const VoxelRaycastHit hit = RaycastBlocks(
            glm::vec3(-1.5f, -0.5f, -0.5f), glm::vec3(-1.0f, 0.0f, 0.0f), 10.0f, solid);
        failures += !Expect(hit.hit && hit.block == glm::ivec3(-5, -1, -1),
                            "negative voxel floor handled");
    }

    // Stepping is bounded: a 6 block reach touches only a handful of voxels.
    {
        SolidSet solid{};
        const VoxelRaycastHit hit = RaycastBlocks(
            glm::vec3(0.5f, 0.5f, 0.5f), glm::normalize(glm::vec3(1.0f, 0.3f, 0.2f)),
            6.0f, solid);
        std::printf("  reach-6 diagonal steps: %d\n", hit.steps);
        failures += !Expect(!hit.hit && hit.steps <= 12,
                            "reach 6 touches few voxels (cheap raycast)");
    }

    std::printf("%s\n", failures == 0 ? "raycast test OK" : "raycast test FAILED");
    return failures == 0 ? 0 : 1;
}
