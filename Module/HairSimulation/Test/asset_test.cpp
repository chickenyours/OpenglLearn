#include "hair_asset.h"
#include <chrono>
#include <iostream>
#include <iterator>

#ifndef HAIR_ASSET_PATH
#define HAIR_ASSET_PATH "Asset/HairSimulation/diana_lod0.hairbin"
#endif

int main(int argc, char** argv) {
    using namespace HairSimulation;
    const std::filesystem::path source = argc > 1 ? argv[1] : HAIR_ASSET_PATH;
    HairAsset asset;
    std::string error;
    auto require = [](bool condition, const std::string& message) {
        if (!condition) throw std::runtime_error(message);
    };
    try {
        require(loadHairAsset(source, asset, error), "Load actual Diana asset: " + error);
        const auto& h = asset.header;
        require(h.guideCount == 120 && h.nodeCount == 3000 && h.strandCount == 17500 &&
            h.renderVertexCount == 436975 && h.segmentCount == 419475, "Extracted retail counts changed");
        // Independent source width audit: native ribbons offset +/-half of
        // packed full width. The portable render stream stores radius.
        double projectedArea=0;float maximumRadius=0;
        for(const auto& p:asset.renderRest)maximumRadius=std::max(maximumRadius,p.w);
        for(auto n:asset.segments){const auto& a=asset.renderRest[n];const auto& b=asset.renderRest[n+1];
            const double dx=double(b.x)-a.x,dy=double(b.y)-a.y,dz=double(b.z)-a.z;
            projectedArea+=std::sqrt(dx*dx+dy*dy+dz*dz)*(double(a.w)+b.w);
        }
        require(std::abs(maximumRadius-0.00007629510948f)<1e-10f&&std::abs(projectedArea-.8145702053)<1e-6,
            "Diana packed full width was confused with physical radius");
        for (std::uint32_t g = 0; g < h.guideCount; ++g) {
            const auto a = asset.guideOffsets[g], b = asset.guideOffsets[g + 1];
            require(b - a == 25, "Actual guide topology must have 25 points");
            require(asset.guideRest[a].w == 0, "Root arc must be zero");
            for (auto n = a; n + 1 < b; ++n) {
                const auto& p = asset.guideRest[n];
                const auto& next = asset.guideRest[n + 1];
                const auto& q = asset.guideRestOrientation[n];
                float dx = next.x-p.x, dy = next.y-p.y, dz = next.z-p.z;
                const float length = std::sqrt(dx*dx + dy*dy + dz*dz);
                require(length > 1e-8f, "Nondegenerate source guide edge");
                // Quaternion rotate(q, +Z), matches the GLSL solver convention.
                const float zx = 2*(q.x*q.z + q.w*q.y), zy = 2*(q.y*q.z - q.w*q.x), zz = 1-2*(q.x*q.x + q.y*q.y);
                require(std::abs(zx-dx/length) < 3e-5f && std::abs(zy-dy/length) < 3e-5f &&
                    std::abs(zz-dz/length) < 3e-5f, "Rest frame +Z must match actual outgoing tangent");
                require(std::abs((next.w-p.w)-length) < 1e-6f, "Guide arc metric must match source lengths");
            }
        }
        // Guides appear as the first 120 displayed strands. Their native
        // three-guide mapping must reproduce the source guide positions.
        for (std::uint32_t n = 0; n < h.nodeCount; ++n) {
            HairFloat4 reconstructed{};
            const auto& binding = asset.renderBindings[n];
            for (int k = 0; k < 3; ++k) {
                const auto& a = asset.guideRest[binding.guideNodes[k]];
                const auto& b = asset.guideRest[binding.guideNodes[k] + 1];
                const float t = binding.lerps[k], w = binding.weights[k];
                reconstructed.x += (a.x+(b.x-a.x)*t)*w;
                reconstructed.y += (a.y+(b.y-a.y)*t)*w;
                reconstructed.z += (a.z+(b.z-a.z)*t)*w;
            }
            const auto& expected = asset.renderRest[n];
            require(std::abs(reconstructed.x-expected.x) < 1e-6f && std::abs(reconstructed.y-expected.y) < 1e-6f &&
                std::abs(reconstructed.z-expected.z) < 1e-6f, "Actual guide interpolation identity");
        }
        std::ifstream file(source, std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(file)), {});
        const auto temporary = std::filesystem::temp_directory_path() /
            ("hair_asset_validation_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".hairbin");
        auto reject = [&](const std::vector<char>& damaged, const char* reason) {
            { std::ofstream out(temporary, std::ios::binary); out.write(damaged.data(), damaged.size()); }
            const auto priorCount = asset.guideRest.size();
            const bool accepted = loadHairAsset(temporary, asset, error);
            std::filesystem::remove(temporary);
            require(!accepted && !error.empty(), reason);
            require(asset.guideRest.size() == priorCount, "Failed loads preserve existing asset");
        };
        auto bad = bytes;
        bad[0] = 'X';
        reject(bad, "Reject bad magic");
        bad = bytes;
        bad.resize(56);
        reject(bad, "Reject truncated payload before allocating full counts");
        bad = bytes;
        const std::uint32_t overflowCount = 0xffffffffu;
        std::memcpy(bad.data()+offsetof(HairAssetHeader, renderVertexCount), &overflowCount, sizeof(overflowCount));
        reject(bad, "Reject integer overflow counts");
        bad = bytes;
        const std::size_t bindingStart = sizeof(HairAssetHeader) + 4ull*(h.guideCount+1) +
            32ull*h.nodeCount + 4ull*(h.strandCount+1) + 16ull*h.renderVertexCount;
        const std::uint32_t invalidNode = h.nodeCount;
        std::memcpy(bad.data()+bindingStart, &invalidNode, sizeof(invalidNode));
        reject(bad, "Reject out-of-bounds GPU guide binding");
        std::cout << "Diana asset: counts, frames, arc metric, native guide interpolation and malformed inputs passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
