#include "Render/Public/Pipeline/probe_sdf_lod.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
std::vector<Render::ProbeTriangle> Octahedron() {
    std::vector<Render::ProbeTriangle> triangles;
    for (int x : {-1, 1}) for (int y : {-1, 1}) for (int z : {-1, 1})
        triangles.push_back({glm::vec3(x, 0, 0), glm::vec3(0, y, 0), glm::vec3(0, 0, z)});
    return triangles;
}
// Exact signed distance to the L1 unit ball. Outside, project onto the unit
// simplex by soft-thresholding; inside, its nearest face is a known plane.
float OctahedronDistance(glm::vec3 p) {
    const auto q = glm::abs(p); const float sum = q.x + q.y + q.z;
    if (sum <= 1) return (sum - 1) / std::sqrt(3.f);
    std::array<float, 3> values{q.x, q.y, q.z}; std::sort(values.begin(), values.end(), std::greater<float>());
    float total = 0, threshold = 0;
    for (std::size_t i = 0; i < values.size(); ++i) {
        total += values[i]; const float candidate = (total - 1) / float(i + 1);
        if (values[i] > candidate) threshold = candidate;
    }
    return glm::length(q - glm::max(q - glm::vec3(threshold), glm::vec3(0)));
}
double MeanError(const Render::ProbeMeshSdf& mesh) {
    double result = 0; std::uint32_t samples = 0;
    for (int z = -9; z <= 9; ++z) for (int y = -9; y <= 9; ++y) for (int x = -9; x <= 9; ++x) {
        const glm::vec3 p = glm::vec3(x, y, z) * .0973f;
        result += std::abs(double(mesh.Sample(p)) - OctahedronDistance(p)); ++samples;
    }
    return result / samples;
}
bool SameTriangles(std::span<const Render::ProbeTriangle> a, std::span<const Render::ProbeTriangle> b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) if (a[i].a != b[i].a || a[i].b != b[i].b || a[i].c != b[i].c) return false;
    return true;
}
} // namespace
int main() {
    try {
        using Q = Render::ProbeSdfQuality;
        const auto geometry = Octahedron(); const auto medium = Render::BakeProbeMeshSdf(geometry, 40, true);
        Render::ProbeSdfLodCache cache; const auto asset = cache.RegisterMesh(geometry, true, medium);
        Check(cache.RegisterMesh(geometry, true, medium) == asset && cache.Statistics().assets == 1, "Identical mesh instances did not share the quality cache");
        Check(cache.Ready(asset, Q::Medium) == medium && cache.Statistics().completedBakes[1] == 0, "Seeded medium tier was rebaked");
        const auto highRequest = cache.Resolve(asset, Q::High, medium);
        Check(highRequest.pending && highRequest.mesh == medium && highRequest.activeQuality == Q::Medium && !highRequest.changed, "Pending high quality did not retain the active medium mesh");
        // Desired quality changes before the earlier high request completes.
        // Its eventual completion must populate a cache, not override desire.
        const auto lowRequest = cache.Resolve(asset, Q::Low, medium);
        Check(lowRequest.pending && lowRequest.mesh == medium && !lowRequest.changed, "Pending low quality unexpectedly removed the active mesh");
        std::vector<std::jthread> requests;
        for (int thread = 0; thread != 6; ++thread) requests.emplace_back([&] { for (int i = 0; i != 64; ++i) { cache.Request(asset, Q::High); cache.Request(asset, Q::Low); } });
        requests.clear(); Check(cache.WaitIdle(), "Asynchronous quality cache failed to become idle");
        const auto low = cache.Ready(asset, Q::Low), high = cache.Ready(asset, Q::High);
        Check(low && high && low->Resolution() == 24 && medium->Resolution() == 40 && high->Resolution() == 64, "Actual requested resolution metadata is incorrect");
        Check(SameTriangles(low->Triangles(), geometry) && SameTriangles(high->Triangles(), geometry), "Distance quality changed source triangle geometry");
        Check(low->Closed() && medium->Closed() && high->Closed(), "A quality tier lost the closed mesh sign");
        Check(highRequest.mesh == medium && lowRequest.mesh == medium, "Late asynchronous completion mutated a published selection");
        const auto lowResult = cache.Resolve(asset, Q::Low, medium);
        Check(lowResult.mesh == low && lowResult.activeQuality == Q::Low && !lowResult.pending && lowResult.changed, "Latest requested low tier was overridden by stale high completion");
        const auto bakes = cache.Statistics().completedBakes;
        Check(bakes[0] == 1 && bakes[1] == 0 && bakes[2] == 1, "Duplicate requests caused duplicate quality bakes");
        auto previous = low;
        for (int switchIndex = 0; switchIndex != 30; ++switchIndex) {
            const auto desired = Q(switchIndex % 3); const auto result = cache.Resolve(asset, desired, previous);
            Check(!result.pending && result.mesh == cache.Ready(asset, desired) && result.activeQuality == desired, "Cached switch failed to activate the desired immutable tier");
            Check(result.changed == (result.mesh != previous), "Quality selection changed flag is inaccurate"); previous = result.mesh;
        }
        Check(cache.Statistics().completedBakes == bakes && cache.Statistics().queued == 0 && cache.Statistics().working == 0, "Cached switches triggered rebaking");
        const double lowError = MeanError(*low), mediumError = MeanError(*medium), highError = MeanError(*high);
        Check(mediumError < lowError && highError < mediumError && highError < lowError * .65, "Higher distance resolution did not improve independently measured error");
        Check(low->InterpolationError() > medium->InterpolationError() && medium->InterpolationError() > high->InterpolationError(), "Higher quality did not tighten the conservative interpolation bound");

        Check(Render::SelectProbeSdfDistanceQuality(2.5f, Q::Medium) == Q::Medium && Render::SelectProbeSdfDistanceQuality(2.39f, Q::Medium) == Q::High, "Near threshold hysteresis is missing");
        Check(Render::SelectProbeSdfDistanceQuality(3.4f, Q::High) == Q::High && Render::SelectProbeSdfDistanceQuality(3.61f, Q::High) == Q::Medium, "High tier did not retain its hysteresis band");
        Check(Render::SelectProbeSdfDistanceQuality(7.5f, Q::Medium) == Q::Medium && Render::SelectProbeSdfDistanceQuality(7.61f, Q::Medium) == Q::Low, "Far threshold hysteresis is missing");
        Check(Render::SelectProbeSdfDistanceQuality(6.7f, Q::Low) == Q::Low && Render::SelectProbeSdfDistanceQuality(6.39f, Q::Low) == Q::Medium, "Low tier did not retain its hysteresis band");
        Check(Render::SelectProbeSdfDistanceQuality(8, Q::High) == Q::Low && Render::SelectProbeSdfDistanceQuality(1, Q::Low) == Q::High, "Large camera moves cannot skip an intermediate tier");
        Q current = Q::Medium;
        for (int frame = 0; frame != 100; ++frame) { current = Render::SelectProbeSdfDistanceQuality(3 + (frame % 2 ? .08f : -.08f), current); Check(current == Q::Medium, "Distance jitter caused tier flicker"); }

        Render::ProbeSdfLodCache unseeded; const auto queuedAsset = unseeded.RegisterMesh(geometry, true);
        Check(unseeded.WaitIdle() && unseeded.Ready(queuedAsset, Q::Medium) && unseeded.Statistics().completedBakes[1] == 1, "Unseeded registration did not asynchronously prepare medium quality");
        const std::array<Render::ProbeTriangle, 1> degenerate{{{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}};
        const auto failedAsset = unseeded.RegisterMesh(degenerate, false);
        Check(unseeded.WaitIdle(), "Failed asynchronous bake did not finish");
        const auto failure = unseeded.Resolve(failedAsset, Q::Medium);
        Check(failure.failed && !failure.pending && !failure.mesh && !unseeded.Error(failedAsset, Q::Medium).empty(), "Bake failure was not represented without replacing valid cache assets");
        unseeded.Request(queuedAsset, Q::Low); Check(unseeded.WaitIdle() && unseeded.Ready(queuedAsset, Q::Low), "Worker did not recover after a failed asset bake");
        std::cout << "Mesh SDF quality cache passed: same geometry 24/40/64; mean analytic distance error " << lowError << " / " << mediumError << " / " << highError
                  << "; immutable pending selections, asynchronous deduplication, no stale selection, cached switching, hysteresis and worker failure recovery.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
