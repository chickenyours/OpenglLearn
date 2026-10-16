#include "Render/Public/Pipeline/probe_sdf_lod.h"
#include <algorithm>
#include <bit>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace Render {
namespace {
std::size_t Tier(ProbeSdfQuality quality) {
    const auto result = std::size_t(quality);
    if (result >= ProbeSdfQualityResolutions.size()) throw std::invalid_argument("Invalid Mesh SDF quality tier");
    return result;
}
bool SameGeometry(std::span<const ProbeTriangle> a, std::span<const ProbeTriangle> b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) if (a[i].a != b[i].a || a[i].b != b[i].b || a[i].c != b[i].c) return false;
    return true;
}
std::uint64_t GeometryHash(std::span<const ProbeTriangle> triangles, bool closed) {
    std::uint64_t result = 14695981039346656037ull;
    const auto add = [&](std::uint32_t value) { for (int byte = 0; byte != 4; ++byte) { result ^= (value >> (byte * 8)) & 255; result *= 1099511628211ull; } };
    add(closed ? 1 : 0);
    for (const auto& triangle : triangles) for (const auto point : {triangle.a, triangle.b, triangle.c}) for (int axis = 0; axis != 3; ++axis) {
        if (!std::isfinite(point[axis])) throw std::invalid_argument("Nonfinite Mesh SDF source geometry");
        add(std::bit_cast<std::uint32_t>(point[axis] == 0 ? 0.f : point[axis]));
    }
    return result;
}
} // namespace
std::uint32_t ProbeSdfQualityResolution(ProbeSdfQuality quality) { return ProbeSdfQualityResolutions[Tier(quality)]; }
const char* ProbeSdfQualityName(ProbeSdfQuality quality) {
    constexpr std::array<const char*, 3> names{"LOW", "MEDIUM", "HIGH"}; return names[Tier(quality)];
}
ProbeSdfQuality SelectProbeSdfDistanceQuality(float distance, ProbeSdfQuality current, const ProbeSdfDistanceLodSettings& settings) {
    Tier(current);
    if (!std::isfinite(distance) || distance < 0 || !std::isfinite(settings.highDistance) || !std::isfinite(settings.lowDistance) ||
        !std::isfinite(settings.hysteresis) || settings.highDistance <= 0 || settings.lowDistance <= settings.highDistance ||
        settings.hysteresis < 0 || settings.hysteresis * 2 >= settings.lowDistance - settings.highDistance || settings.hysteresis >= settings.highDistance)
        throw std::invalid_argument("Invalid Mesh SDF distance selection or hysteresis");
    const float h = settings.hysteresis;
    if (current == ProbeSdfQuality::High) {
        if (distance > settings.lowDistance + h) return ProbeSdfQuality::Low;
        return distance > settings.highDistance + h ? ProbeSdfQuality::Medium : current;
    }
    if (current == ProbeSdfQuality::Low) {
        if (distance < settings.highDistance - h) return ProbeSdfQuality::High;
        return distance < settings.lowDistance - h ? ProbeSdfQuality::Medium : current;
    }
    if (distance < settings.highDistance - h) return ProbeSdfQuality::High;
    if (distance > settings.lowDistance + h) return ProbeSdfQuality::Low;
    return current;
}

struct ProbeSdfLodCache::Impl {
    enum class State { Empty, Queued, Working, Ready, Failed };
    struct Asset {
        std::uint64_t hash = 0;
        bool closed = false;
        std::shared_ptr<const std::vector<ProbeTriangle>> triangles;
        std::array<std::shared_ptr<const ProbeMeshSdf>, 3> meshes;
        std::array<State, 3> states{State::Empty, State::Empty, State::Empty};
        std::array<std::string, 3> errors;
    };
    struct Job { std::uint32_t asset; std::size_t tier; };
    mutable std::mutex mutex;
    mutable std::condition_variable condition;
    std::vector<Asset> assets;
    std::deque<Job> jobs;
    ProbeSdfLodStatistics statistics;
    std::array<std::uint32_t,3> resolutions;
    float surfaceExpansion;
    std::jthread worker;
    explicit Impl(std::array<std::uint32_t,3> sizes,float expansion) : resolutions(sizes),surfaceExpansion(expansion), worker([this](std::stop_token stop) { Work(stop); }) {}
    ~Impl() { worker.request_stop(); condition.notify_all(); if (worker.joinable()) worker.join(); }
    void ValidateAsset(std::uint32_t asset) const { if (asset >= assets.size()) throw std::out_of_range("Unknown Mesh SDF asset"); }
    void RequestLocked(std::uint32_t asset, std::size_t tier) {
        ValidateAsset(asset); ++statistics.requests;
        auto& item = assets[asset];
        if (item.states[tier] == State::Ready) { ++statistics.cacheHits; return; }
        if (item.states[tier] == State::Queued || item.states[tier] == State::Working) { ++statistics.deduplicatedRequests; return; }
        if (item.states[tier] == State::Failed) return;
        item.states[tier] = State::Queued; jobs.push_back({asset, tier}); ++statistics.queued; condition.notify_all();
    }
    void Work(std::stop_token stop) {
        while (true) {
            Job job{}; std::shared_ptr<const std::vector<ProbeTriangle>> triangles; bool closed = false;
            {
                std::unique_lock lock(mutex); condition.wait(lock, [&] { return stop.stop_requested() || !jobs.empty(); });
                if (stop.stop_requested()) return;
                job = jobs.front(); jobs.pop_front(); --statistics.queued; ++statistics.working;
                auto& asset = assets[job.asset]; asset.states[job.tier] = State::Working; triangles = asset.triangles; closed = asset.closed;
            }
            std::shared_ptr<const ProbeMeshSdf> result; std::string error;
            try { result = BakeProbeMeshSdf(*triangles, resolutions[job.tier], closed,surfaceExpansion); }
            catch (const std::exception& e) { error = e.what(); }
            catch (...) { error = "Unknown Mesh SDF bake failure"; }
            {
                std::lock_guard lock(mutex); auto& asset = assets[job.asset]; --statistics.working;
                if (result) {
                    asset.meshes[job.tier] = std::move(result); asset.states[job.tier] = State::Ready;
                    const auto& mesh = *asset.meshes[job.tier]; ++statistics.completedBakes[job.tier]; ++statistics.readyTiers[job.tier]; ++statistics.generation;
                    statistics.bakedVoxels += mesh.Statistics().voxels; statistics.triangleDistanceTests += mesh.Statistics().distanceTriangleTests;
                    statistics.bakeMilliseconds += mesh.Statistics().milliseconds; statistics.distanceBytes += mesh.Pixels().size() * sizeof(glm::vec4);
                } else { asset.states[job.tier] = State::Failed; asset.errors[job.tier] = std::move(error); ++statistics.failedBakes; }
                condition.notify_all();
            }
        }
    }
};
ProbeSdfLodCache::ProbeSdfLodCache(std::array<std::uint32_t,3> resolutions,float expansion) {
    if(resolutions[0]<12||resolutions[2]>128||resolutions[0]>=resolutions[1]||resolutions[1]>=resolutions[2])
        throw std::invalid_argument("Mesh SDF resolutions must increase within 12..128");
    if(!std::isfinite(expansion)||expansion<0||expansion>2)throw std::invalid_argument("Invalid Mesh SDF surface expansion");
    impl_=std::make_unique<Impl>(resolutions,expansion);
}
ProbeSdfLodCache::~ProbeSdfLodCache() = default;
std::uint32_t ProbeSdfLodCache::RegisterMesh(std::span<const ProbeTriangle> triangles, bool closed, std::shared_ptr<const ProbeMeshSdf> seed) {
    if (triangles.empty() || triangles.size() > 1048576) throw std::invalid_argument("Mesh SDF cache needs 1..1048576 source triangles");
    const auto hash = GeometryHash(triangles, closed);
    if (seed && (seed->Resolution() != impl_->resolutions[1] || std::abs(seed->SurfaceBias()-impl_->surfaceExpansion*seed->Spacing().x)>1e-6f || !SameGeometry(triangles, seed->Triangles()) || (seed->Closed() && !closed)))
        throw std::invalid_argument("Mesh SDF cache seed must match the configured medium resolution and geometry");
    std::lock_guard lock(impl_->mutex);
    for (std::size_t i = 0; i < impl_->assets.size(); ++i) if (impl_->assets[i].hash == hash && impl_->assets[i].closed == closed && SameGeometry(triangles, *impl_->assets[i].triangles))
        return std::uint32_t(i);
    if (impl_->assets.size() >= 1024) throw std::invalid_argument("Mesh SDF cache exceeds 1024 geometry assets");
    const auto index = std::uint32_t(impl_->assets.size()); Impl::Asset asset;
    asset.hash = hash; asset.closed = closed; asset.triangles = std::make_shared<const std::vector<ProbeTriangle>>(triangles.begin(), triangles.end());
    if (seed) {
        asset.meshes[1] = std::move(seed); asset.states[1] = Impl::State::Ready; ++impl_->statistics.seededAssets; ++impl_->statistics.readyTiers[1];
        impl_->statistics.distanceBytes += asset.meshes[1]->Pixels().size() * sizeof(glm::vec4); ++impl_->statistics.generation;
    }
    impl_->assets.push_back(std::move(asset)); ++impl_->statistics.assets;
    if (!impl_->assets.back().meshes[1]) impl_->RequestLocked(index, 1);
    return index;
}
void ProbeSdfLodCache::Request(std::uint32_t asset, ProbeSdfQuality quality) {
    const auto tier = Tier(quality); std::lock_guard lock(impl_->mutex); impl_->RequestLocked(asset, tier);
}
std::shared_ptr<const ProbeMeshSdf> ProbeSdfLodCache::Ready(std::uint32_t asset, ProbeSdfQuality quality) const {
    const auto tier = Tier(quality); std::lock_guard lock(impl_->mutex); impl_->ValidateAsset(asset); return impl_->assets[asset].meshes[tier];
}
ProbeSdfLodResult ProbeSdfLodCache::Resolve(std::uint32_t asset, ProbeSdfQuality desired, std::shared_ptr<const ProbeMeshSdf> previous) {
    const auto tier = Tier(desired); std::lock_guard lock(impl_->mutex); impl_->ValidateAsset(asset);
    const auto& item = impl_->assets[asset];
    std::size_t previousTier = 1;
    if (previous) {
        const auto found = std::find(item.meshes.begin(), item.meshes.end(), previous);
        if (found == item.meshes.end()) throw std::invalid_argument("Previous Mesh SDF belongs to another cache asset");
        previousTier = std::size_t(found - item.meshes.begin());
    }
    impl_->RequestLocked(asset, tier);
    ProbeSdfLodResult result; result.requestedQuality = desired; result.generation = impl_->statistics.generation;
    result.failed = item.states[tier] == Impl::State::Failed;
    result.pending = item.states[tier] == Impl::State::Queued || item.states[tier] == Impl::State::Working;
    if (item.meshes[tier]) { result.mesh = item.meshes[tier]; result.activeQuality = desired; }
    else if (previous) { result.mesh = previous; result.activeQuality = ProbeSdfQuality(previousTier); }
    else if (item.meshes[1]) { result.mesh = item.meshes[1]; result.activeQuality = ProbeSdfQuality::Medium; }
    else for (std::size_t available : {std::size_t(0), std::size_t(2)}) if (item.meshes[available]) { result.mesh = item.meshes[available]; result.activeQuality = ProbeSdfQuality(available); break; }
    result.changed = result.mesh != previous;
    return result;
}
ProbeSdfLodStatistics ProbeSdfLodCache::Statistics() const { std::lock_guard lock(impl_->mutex); return impl_->statistics; }
std::string ProbeSdfLodCache::Error(std::uint32_t asset, ProbeSdfQuality quality) const {
    const auto tier = Tier(quality); std::lock_guard lock(impl_->mutex); impl_->ValidateAsset(asset); return impl_->assets[asset].errors[tier];
}
bool ProbeSdfLodCache::WaitIdle(std::chrono::milliseconds timeout) const {
    if (timeout.count() < 0) throw std::invalid_argument("Negative Mesh SDF cache wait timeout");
    std::unique_lock lock(impl_->mutex); return impl_->condition.wait_for(lock, timeout, [&] { return impl_->statistics.queued == 0 && impl_->statistics.working == 0; });
}
} // namespace Render
