#pragma once

// Portable extracted curve data, version 1, little endian, IEEE 754 floats.
// Header followed by guideOffsets, guideRest, guideRestOrientation,
// strandOffsets, renderRest, renderBindings, segments, without extra padding.
// Source lengths use metres. Derived rest quaternions are xyzw, rotate +Z onto
// the outgoing tangent, and use parallel transport to choose the free twist.
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace HairSimulation {

struct HairAssetHeader {
    char magic[8];
    std::uint32_t version, guideCount, nodeCount, strandCount, renderVertexCount, segmentCount;
    float boundsMin[3], boundsMax[3];
};
struct HairFloat4 { float x, y, z, w; };
struct HairRenderBinding {
    std::uint32_t guideNodes[4]; // First node of each interpolation interval; lane 3 unused.
    float weights[4];          // Three weights, normalized from source half floats.
    float lerps[4];            // Source interpolation factor between guideNodes[k] and +1.
};
static_assert(sizeof(HairAssetHeader) == 56);
static_assert(sizeof(HairFloat4) == 16 && sizeof(HairRenderBinding) == 48);

struct HairAsset {
    HairAssetHeader header{};
    std::vector<std::uint32_t> guideOffsets, strandOffsets, segments;
    // guideRest.w is arc length; renderRest.w is physical radius in metres.
    std::vector<HairFloat4> guideRest, guideRestOrientation, renderRest;
    std::vector<HairRenderBinding> renderBindings;
};

// All ranges and sizes are checked before allocation or GPU upload. A failed
// load leaves output unchanged, including failure after a valid header.
inline bool loadHairAsset(const std::filesystem::path& path, HairAsset& output, std::string& error) {
    auto fail = [&](const char* message) { error = message; return false; };
    if constexpr (std::endian::native != std::endian::little)
        return fail("Hair asset requires little-endian IEEE 754 host");
    static_assert(std::numeric_limits<float>::is_iec559);
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return fail("Cannot open hair asset");
    const auto size = file.tellg();
    if (size < static_cast<std::streamoff>(sizeof(HairAssetHeader)) || size > (1LL << 30))
        return fail("Invalid hair asset file size");
    file.seekg(0);
    HairAsset asset;
    if (!file.read(reinterpret_cast<char*>(&asset.header), sizeof(asset.header))) return fail("Truncated hair header");
    const auto& h = asset.header;
    if (std::memcmp(h.magic, "DHAIR001", 8) != 0 || h.version != 1) return fail("Unsupported hair asset version");
    if (h.guideCount == 0 || h.guideCount > 65535 || h.guideCount > h.strandCount ||
        h.nodeCount < 2 * h.guideCount || h.nodeCount > 1000000 ||
        h.strandCount == 0 || h.strandCount > 1000000 ||
        h.renderVertexCount < 2 * h.strandCount || h.renderVertexCount > 10000000 ||
        h.nodeCount > h.renderVertexCount || h.segmentCount != h.renderVertexCount - h.strandCount)
        return fail("Invalid hair asset counts");
    const std::uint64_t expected = sizeof(h) + 4ull * (h.guideCount + 1) + 32ull * h.nodeCount +
        4ull * (h.strandCount + 1) + 64ull * h.renderVertexCount + 4ull * h.segmentCount;
    if (expected != static_cast<std::uint64_t>(size)) return fail("Hair asset section sizes disagree with file size");
    for (int k = 0; k < 3; ++k)
        if (!std::isfinite(h.boundsMin[k]) || !std::isfinite(h.boundsMax[k]) || h.boundsMin[k] > h.boundsMax[k] ||
            std::abs(h.boundsMin[k]) > 100 || std::abs(h.boundsMax[k]) > 100)
            return fail("Invalid hair asset bounds");
    auto read = [&](auto& values, std::size_t count) {
        values.resize(count);
        return static_cast<bool>(file.read(reinterpret_cast<char*>(values.data()),
            static_cast<std::streamsize>(count * sizeof(values[0]))));
    };
    if (!read(asset.guideOffsets, h.guideCount + 1) || !read(asset.guideRest, h.nodeCount) ||
        !read(asset.guideRestOrientation, h.nodeCount) || !read(asset.strandOffsets, h.strandCount + 1) ||
        !read(asset.renderRest, h.renderVertexCount) || !read(asset.renderBindings, h.renderVertexCount) ||
        !read(asset.segments, h.segmentCount)) return fail("Truncated hair asset section");
    auto validOffsets = [](const auto& offsets, std::uint32_t total) {
        if (offsets.front() != 0 || offsets.back() != total) return false;
        for (std::size_t i = 1; i < offsets.size(); ++i)
            if (offsets[i] <= offsets[i - 1] || offsets[i] - offsets[i - 1] < 2) return false;
        return true;
    };
    if (!validOffsets(asset.guideOffsets, h.nodeCount) || !validOffsets(asset.strandOffsets, h.renderVertexCount))
        return fail("Invalid hair strand offsets");
    auto finite4 = [](const HairFloat4& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
    };
    std::vector<std::uint32_t> nodeEnds(h.nodeCount);
    for (std::uint32_t g = 0; g < h.guideCount; ++g) {
        float previousArc = -1.0f;
        for (auto n = asset.guideOffsets[g]; n < asset.guideOffsets[g + 1]; ++n) {
            const auto& p = asset.guideRest[n];
            const auto& q = asset.guideRestOrientation[n];
            const float norm = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
            if (!finite4(p) || p.w < previousArc || p.w < 0 || p.w > 1000 || !finite4(q) || std::abs(norm - 1) > 0.005f ||
                p.x < h.boundsMin[0] - 1e-5f || p.x > h.boundsMax[0] + 1e-5f ||
                p.y < h.boundsMin[1] - 1e-5f || p.y > h.boundsMax[1] + 1e-5f ||
                p.z < h.boundsMin[2] - 1e-5f || p.z > h.boundsMax[2] + 1e-5f)
                return fail("Invalid guide position, arc length or rest quaternion");
            previousArc = p.w;
            nodeEnds[n] = asset.guideOffsets[g + 1];
        }
    }
    for (std::uint32_t n = 0; n < h.renderVertexCount; ++n) {
        const auto& p = asset.renderRest[n];
        if (!finite4(p) || p.w < 0 || p.w > 1 || p.x < h.boundsMin[0] - 1e-5f || p.x > h.boundsMax[0] + 1e-5f ||
            p.y < h.boundsMin[1] - 1e-5f || p.y > h.boundsMax[1] + 1e-5f ||
            p.z < h.boundsMin[2] - 1e-5f || p.z > h.boundsMax[2] + 1e-5f)
            return fail("Invalid render rest position or width");
        const auto& binding = asset.renderBindings[n];
        float weight = 0;
        for (int k = 0; k < 3; ++k) {
            const auto first = binding.guideNodes[k];
            if (first >= h.nodeCount || first + 1 >= nodeEnds[first] ||
                !std::isfinite(binding.weights[k]) || binding.weights[k] < 0 || binding.weights[k] > 1 ||
                !std::isfinite(binding.lerps[k]) || binding.lerps[k] < 0 || binding.lerps[k] > 1)
                return fail("Invalid render guide binding");
            weight += binding.weights[k];
        }
        if (std::abs(weight - 1) > 1e-4f) return fail("Unnormalized render guide weights");
    }
    std::size_t index = 0;
    for (std::uint32_t s = 0; s < h.strandCount; ++s)
        for (auto n = asset.strandOffsets[s]; n + 1 < asset.strandOffsets[s + 1]; ++n)
            if (index >= asset.segments.size() || asset.segments[index++] != n)
                return fail("Segment index crosses a strand or is out of order");
    if (index != asset.segments.size()) return fail("Invalid hair segment count");
    output = std::move(asset);
    error.clear();
    return true;
}

} // namespace HairSimulation
