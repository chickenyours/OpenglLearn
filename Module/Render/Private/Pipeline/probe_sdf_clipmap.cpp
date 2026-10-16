#include "Render/Public/Pipeline/probe_sdf_clipmap.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <glm/gtc/matrix_inverse.hpp>

namespace Render {
namespace {
using Clock = std::chrono::steady_clock;
using Cell = std::array<std::int64_t, 3>;
std::size_t Count(glm::uvec3 n) { return std::size_t(n.x) * n.y * n.z; }
std::size_t Index(glm::uvec3 p, glm::uvec3 n) { return (std::size_t(p.z) * n.y + p.y) * n.x + p.x; }
std::size_t Padded(std::size_t n) { return (n + ProbeSdfAtlasWidth - 1) / ProbeSdfAtlasWidth * ProbeSdfAtlasWidth; }
bool Finite(glm::vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
std::uint32_t Mod(std::int64_t value, std::uint32_t count) {
    const auto result = value % std::int64_t(count);
    return std::uint32_t(result < 0 ? result + count : result);
}
glm::uvec3 Physical(glm::uvec3 logical, const ProbeSdfClipmapLevel& level) { return (logical + level.scroll) % level.counts; }
float BoxDistance(glm::vec3 p, glm::vec3 lo, glm::vec3 hi) { return glm::length(glm::max(glm::max(lo - p, p - hi), glm::vec3(0))); }

struct Object {
    std::shared_ptr<const ProbeMeshSdf> mesh;
    glm::mat4 transform{1}, inverse{1};
    glm::vec3 lo{0}, hi{0};
    float minimumScale = 1, maximumScale = 1;
    bool movable = false;
    std::uint32_t userId = 0;
};
void ValidateMesh(const std::shared_ptr<const ProbeMeshSdf>& mesh) {
    if (!mesh || !mesh->Geometry() || !mesh->Geometry()->TriangleCount()) throw std::invalid_argument("Clipmap needs a nonempty baked Mesh SDF");
}
Object Prepare(const ProbeSdfInstance& instance, std::shared_ptr<const ProbeMeshSdf> mesh) {
    ValidateMesh(mesh); Object result; result.mesh = std::move(mesh); result.transform = instance.localToWorld;
    result.movable = instance.movable; result.userId = instance.userId;
    for (int i = 0; i != 4; ++i) for (int j = 0; j != 4; ++j)
        if (!std::isfinite(result.transform[i][j])) throw std::invalid_argument("Nonfinite clipmap object transform");
    if (std::abs(result.transform[0].w) > 1e-6f || std::abs(result.transform[1].w) > 1e-6f || std::abs(result.transform[2].w) > 1e-6f || std::abs(result.transform[3].w - 1) > 1e-6f)
        throw std::invalid_argument("Clipmap object transform must be affine");
    const glm::vec3 scales{glm::length(glm::vec3(result.transform[0])), glm::length(glm::vec3(result.transform[1])), glm::length(glm::vec3(result.transform[2]))};
    if (!Finite(scales) || glm::any(glm::lessThan(scales, glm::vec3(1e-4f)))) throw std::invalid_argument("Singular clipmap object transform");
    for (int i = 0; i != 3; ++i) for (int j = i + 1; j != 3; ++j)
        if (std::abs(glm::dot(glm::vec3(result.transform[i]), glm::vec3(result.transform[j]))) > scales[i] * scales[j] * 1e-4f)
            throw std::invalid_argument("Sheared clipmap object transforms are unsupported");
    result.minimumScale = std::min({scales.x, scales.y, scales.z}); result.maximumScale = std::max({scales.x, scales.y, scales.z});
    result.inverse = glm::inverse(result.transform);
    for (int i = 0; i != 4; ++i) for (int j = 0; j != 4; ++j)
        if (!std::isfinite(result.inverse[i][j])) throw std::invalid_argument("Clipmap inverse transform overflow");
    result.lo = glm::vec3(std::numeric_limits<float>::max()); result.hi = -result.lo;
    for (int z = 0; z != 2; ++z) for (int y = 0; y != 2; ++y) for (int x = 0; x != 2; ++x) {
        const auto lo = result.mesh->BoundsMin(), hi = result.mesh->BoundsMax();
        const auto p = glm::vec3(result.transform * glm::vec4(x ? hi.x : lo.x, y ? hi.y : lo.y, z ? hi.z : lo.z, 1));
        if (!Finite(p)) throw std::invalid_argument("Clipmap object bounds overflow");
        result.lo = glm::min(result.lo, p); result.hi = glm::max(result.hi, p);
    }
    return result;
}
bool Same(const Object& a, const Object& b) {
    if (a.mesh != b.mesh || a.movable != b.movable || a.userId != b.userId) return false;
    for (int i = 0; i != 4; ++i) if (a.transform[i] != b.transform[i]) return false;
    return true;
}
float Distance(const Object& object, glm::vec3 p) {
    const auto local = glm::vec3(object.inverse * glm::vec4(p, 1)); const auto& mesh = *object.mesh;
    const auto end = mesh.Origin() + glm::vec3(mesh.Counts() - glm::uvec3(1)) * mesh.Spacing();
    if (local == glm::clamp(local, mesh.Origin(), end)) return (mesh.Sample(local) - mesh.SurfaceBias()) * object.minimumScale;
    return std::max(std::max(0.f, mesh.ConservativeDistance(local) - mesh.SurfaceBias()) * object.minimumScale,
                    std::max(0.f, BoxDistance(p, object.lo, object.hi) - mesh.SurfaceBias() * object.minimumScale));
}
float Influence(const Object& object, const ProbeSdfClipmapLevel& level) {
    const auto spacing = object.mesh->Spacing();
    const float padding = 4 * std::max({spacing.x, spacing.y, spacing.z}) * object.maximumScale;
    return std::max(level.narrowBand + object.mesh->SurfaceBias() * object.minimumScale, padding)
        + object.mesh->InterpolationError() * object.minimumScale + glm::length(level.spacing);
}
// Enumerate only overlapping logical bricks. Floating coordinates are clamped
// before integer conversion, including far-away objects and camera teleports.
template<class Function> void ObjectBricks(const Object& object, const ProbeSdfClipmapLevel& level, std::uint32_t brick, Function function) {
    const float influence = Influence(object, level);
    const auto lo = (object.lo - glm::vec3(influence) - level.origin) / level.spacing;
    const auto hi = (object.hi + glm::vec3(influence) - level.origin) / level.spacing;
    const auto maximum = glm::vec3(level.counts - glm::uvec3(1));
    if (glm::any(glm::lessThan(hi, glm::vec3(0))) || glm::any(glm::greaterThan(lo, maximum))) return;
    const auto first = glm::uvec3(glm::floor(glm::clamp(lo, glm::vec3(0), maximum))) / brick;
    const auto last = glm::uvec3(glm::ceil(glm::clamp(hi, glm::vec3(0), maximum))) / brick;
    for (std::uint32_t z = first.z; z <= last.z; ++z) for (std::uint32_t y = first.y; y <= last.y; ++y) for (std::uint32_t x = first.x; x <= last.x; ++x) function(glm::uvec3(x, y, z));
}
void MergeRanges(std::vector<ProbeSdfAtlasRange>& ranges) {
    std::sort(ranges.begin(), ranges.end(), [](auto a, auto b) { return a.offset < b.offset; });
    std::size_t count = 0;
    for (const auto range : ranges) {
        if (count && std::uint64_t(ranges[count - 1].offset) + ranges[count - 1].count >= range.offset) {
            auto& previous = ranges[count - 1]; previous.count = std::max(previous.offset + previous.count, range.offset + range.count) - previous.offset;
        } else ranges[count++] = range;
    }
    ranges.resize(count);
}
// Each job owns one physical brick. Reuse a small, lazily created team while
// moving the camera instead of creating operating-system threads every frame.
class BrickWorkers {
    std::mutex mutex_;
    std::condition_variable ready_, finished_;
    std::function<void(std::size_t)> function_;
    std::vector<std::jthread> workers_;
    std::atomic_size_t next_{0};
    std::size_t count_ = 0;
    std::uint64_t epoch_ = 0;
    unsigned pending_ = 0;
    bool stopping_ = false;
    std::exception_ptr failure_;
    void Drain() {
        try {
            for (;;) {
                const auto first = next_.fetch_add(4, std::memory_order_relaxed);
                if (first >= count_) break;
                for (auto i = first; i < std::min(first + 4, count_); ++i) function_(i);
            }
        } catch (...) {
            std::lock_guard lock(mutex_); if (!failure_) failure_ = std::current_exception();
        }
    }
  public:
    explicit BrickWorkers(unsigned totalWorkers) {
        try {
            for (unsigned i = 1; i < totalWorkers; ++i) workers_.emplace_back([this] {
                std::uint64_t seen = 0;
                std::unique_lock lock(mutex_);
                for (;;) {
                    ready_.wait(lock, [&] { return stopping_ || epoch_ != seen; });
                    if (stopping_) return;
                    seen = epoch_; lock.unlock(); Drain(); lock.lock();
                    if (--pending_ == 0) finished_.notify_one();
                }
            });
        } catch (...) {
            { std::lock_guard lock(mutex_); stopping_ = true; }
            ready_.notify_all(); workers_.clear(); throw;
        }
    }
    ~BrickWorkers() {
        { std::lock_guard lock(mutex_); stopping_ = true; }
        ready_.notify_all(); workers_.clear();
    }
    void Run(std::size_t count, std::function<void(std::size_t)> function) {
        {
            std::lock_guard lock(mutex_); function_ = std::move(function); count_ = count;
            next_.store(0, std::memory_order_relaxed); failure_ = nullptr;
            pending_ = unsigned(workers_.size()); ++epoch_;
        }
        ready_.notify_all(); Drain();
        std::unique_lock lock(mutex_); finished_.wait(lock, [&] { return pending_ == 0; });
        function_ = {}; if (failure_) std::rethrow_exception(failure_);
    }
};
} // namespace

struct ProbeSdfClipmapVolume::Impl {
    struct Cache { std::vector<glm::vec2> fixed, moving; std::vector<Object> objects; Cell originCell{}; bool initialized = false; };
    ProbeSdfClipmapSettings settings;
    std::vector<ProbeSdfClipmapLevel> levels;
    std::vector<Cache> caches;
    std::vector<glm::vec4> pixels;
    std::vector<ProbeSdfAtlasRange> ranges;
    std::vector<glm::vec4> rangePixels;
    std::vector<ProbeSdfAtlasRange> rangeRanges;
    ProbeSdfClipmapStatistics statistics;
    std::uint64_t generation = 0;
    std::unique_ptr<BrickWorkers> workers;
};

ProbeSdfClipmapVolume::ProbeSdfClipmapVolume(const ProbeSdfClipmapSettings& settings) : impl_(std::make_unique<Impl>()) {
    if (!settings.levelCount || settings.levelCount > ProbeSdfClipmapMaximumLevels || !settings.brickSize || settings.brickSize > 32
        || glm::any(glm::lessThan(settings.counts, glm::uvec3(2))) || glm::any(glm::greaterThan(settings.counts, glm::uvec3(256)))
        || glm::any(glm::notEqual(settings.counts % settings.brickSize, glm::uvec3(0)))
        || !std::isfinite(settings.baseSpacing) || settings.baseSpacing <= 0 || !std::isfinite(settings.spacingScale) || settings.spacingScale <= 1
        || !std::isfinite(settings.baseMaxDistance) || settings.baseMaxDistance <= 0 || !std::isfinite(settings.narrowBandVoxels) || settings.narrowBandVoxels < 1
        || Count(settings.counts) * settings.levelCount > 16777216)
        throw std::invalid_argument("Invalid dense SDF clipmap settings");
    impl_->settings = settings; impl_->levels.resize(settings.levelCount); impl_->caches.resize(settings.levelCount);
    std::size_t offset = 0, rangeOffset = 0;
    for (std::uint32_t i = 0; i < settings.levelCount; ++i) {
        auto& level = impl_->levels[i]; level.counts = settings.counts;
        level.rangeCounts = settings.counts / settings.brickSize;
        level.rangeAtlasOffset = std::uint32_t(rangeOffset); rangeOffset += Padded(Count(level.rangeCounts));
        const double spacing = double(settings.baseSpacing) * std::pow(double(settings.spacingScale), i);
        if (!std::isfinite(spacing) || spacing < 1e-6 || spacing > 1e6) throw std::invalid_argument("Invalid SDF clipmap level spacing");
        level.spacing = glm::vec3(float(spacing)); level.atlasOffset = std::uint32_t(offset);
        level.narrowBand = std::max(settings.baseMaxDistance, settings.narrowBandVoxels * float(spacing));
        if (!std::isfinite(level.narrowBand)) throw std::invalid_argument("Invalid SDF clipmap narrow band");
        const auto total = Count(level.counts); offset += Padded(total);
        auto& cache = impl_->caches[i]; cache.fixed.resize(total, {level.narrowBand, 0}); cache.moving.resize(total, {level.narrowBand, 0});
    }
    impl_->pixels.resize(offset, glm::vec4(0));
    impl_->rangePixels.resize(rangeOffset, glm::vec4(0));
}
ProbeSdfClipmapVolume::~ProbeSdfClipmapVolume() = default;
ProbeSdfClipmapVolume::ProbeSdfClipmapVolume(ProbeSdfClipmapVolume&&) noexcept = default;
ProbeSdfClipmapVolume& ProbeSdfClipmapVolume::operator=(ProbeSdfClipmapVolume&&) noexcept = default;
const ProbeSdfClipmapSettings& ProbeSdfClipmapVolume::Settings() const { return impl_->settings; }
const std::vector<ProbeSdfClipmapLevel>& ProbeSdfClipmapVolume::Levels() const { return impl_->levels; }
const std::vector<glm::vec4>& ProbeSdfClipmapVolume::Pixels() const { return impl_->pixels; }
const std::vector<ProbeSdfAtlasRange>& ProbeSdfClipmapVolume::DirtyRanges() const { return impl_->ranges; }
const std::vector<glm::vec4>& ProbeSdfClipmapVolume::RangePixels() const { return impl_->rangePixels; }
const std::vector<ProbeSdfAtlasRange>& ProbeSdfClipmapVolume::RangeDirtyRanges() const { return impl_->rangeRanges; }
const ProbeSdfClipmapStatistics& ProbeSdfClipmapVolume::Statistics() const { return impl_->statistics; }
std::uint64_t ProbeSdfClipmapVolume::Generation() const { return impl_->generation; }

bool ProbeSdfClipmapVolume::Update(std::span<const ProbeSdfInstance> instances, glm::vec3 camera, std::span<const ProbeSdfClipmapMeshes> fields) {
    const auto begin = Clock::now(); auto& state = *impl_; const auto& settings = state.settings;
    if (!Finite(camera) || (!fields.empty() && fields.size() != instances.size()) || instances.size() > 1048576)
        throw std::invalid_argument("Invalid clipmap camera or per-instance level sources");
    // Preflight every transform/source and snapped origin before touching live
    // data, so invalid input cannot leave a half-scrolled cache behind.
    std::vector<std::vector<Object>> prepared(settings.levelCount); std::vector<Cell> origins(settings.levelCount);
    for (const auto& instance : instances) ValidateMesh(instance.mesh);
    for (std::uint32_t i = 0; i < settings.levelCount; ++i) {
        const auto& level = state.levels[i];
        for (int axis = 0; axis != 3; ++axis) {
            const double cell = std::floor(double(camera[axis]) / (double(settings.brickSize) * level.spacing[axis])) * settings.brickSize;
            // Beyond this, float world positions no longer resolve the grid.
            if (!std::isfinite(cell) || std::abs(cell) > 4194304) throw std::invalid_argument("Clipmap camera exceeds float grid precision");
            origins[i][axis] = std::int64_t(cell) - settings.counts[axis] / 2;
        }
        prepared[i].reserve(instances.size());
        for (std::size_t object = 0; object < instances.size(); ++object) {
            auto mesh = !fields.empty() ? fields[object][i] : nullptr;
            if (!mesh) mesh = instances[object].globalMesh ? instances[object].globalMesh : instances[object].mesh;
            prepared[i].push_back(Prepare(instances[object], std::move(mesh)));
        }
    }
    state.ranges.clear(); state.rangeRanges.clear(); state.statistics = {}; bool changed = false;
    const auto brickCounts = settings.counts / settings.brickSize; const auto brickTotal = Count(brickCounts);
    const auto brickVoxels = std::uint64_t(settings.brickSize) * settings.brickSize * settings.brickSize;
    for (std::uint32_t i = 0; i < settings.levelCount; ++i) {
        auto& level = state.levels[i]; auto& cache = state.caches[i]; auto& stats = level.statistics; stats = {};
        stats.totalVoxels = Count(level.counts); stats.totalBricks = brickTotal;
        Cell delta{}; bool scrolled = false, teleport = false;
        for (int axis = 0; axis != 3; ++axis) {
            delta[axis] = origins[i][axis] - cache.originCell[axis];
            scrolled |= delta[axis] != 0;
            teleport |= std::abs(delta[axis]) >= level.counts[axis];
        }
        const bool full = !cache.initialized || teleport;
        if (full) stats.fullReason = cache.initialized ? ProbeSdfClipmapFullReason::Teleport : ProbeSdfClipmapFullReason::Initial;
        const auto previousError = level.sourceInterpolationError; level.sourceInterpolationError = 0;
        for (const auto& object : prepared[i]) level.sourceInterpolationError = std::max(level.sourceInterpolationError, object.mesh->InterpolationError() * object.minimumScale);
        for (int axis = 0; axis != 3; ++axis) {
            level.origin[axis] = float(double(origins[i][axis]) * level.spacing[axis]);
            level.scroll[axis] = full ? 0 : Mod(std::int64_t(level.scroll[axis]) + delta[axis], level.counts[axis]);
        }
        std::vector<std::uint8_t> dirty(brickTotal, full ? 3 : 0);
        if (full) stats.enteringVoxels = stats.totalVoxels;
        else if (scrolled) for (std::uint32_t z = 0; z < brickCounts.z; ++z) for (std::uint32_t y = 0; y < brickCounts.y; ++y) for (std::uint32_t x = 0; x < brickCounts.x; ++x) {
            const glm::uvec3 first = glm::uvec3(x, y, z) * settings.brickSize; bool enters = false;
            for (int axis = 0; axis != 3; ++axis) {
                const auto oldFirst = std::int64_t(first[axis]) + delta[axis];
                enters |= oldFirst < 0 || oldFirst + settings.brickSize > level.counts[axis];
            }
            if (enters) { dirty[Index({x, y, z}, brickCounts)] = 3; stats.enteringVoxels += brickVoxels; }
        }
        if (!full) {
            const auto mark = [&](const Object& object) { ObjectBricks(object, level, settings.brickSize, [&](glm::uvec3 b) { dirty[Index(b, brickCounts)] |= object.movable ? 2 : 1; }); };
            // Array reordering can also change owner indices. Dirty both old
            // and new bounds at every changed index, even for removed objects.
            for (std::size_t object = 0, end = std::max(cache.objects.size(), prepared[i].size()); object < end; ++object) {
                const bool old = object < cache.objects.size(), current = object < prepared[i].size();
                if (old && current && Same(cache.objects[object], prepared[i][object])) continue;
                if (old) mark(cache.objects[object]);
                if (current) mark(prepared[i][object]);
            }
        }
        const bool hasDirty = std::any_of(dirty.begin(), dirty.end(), [](auto bits) { return bits != 0; });
        if (hasDirty) {
            std::vector<std::uint8_t> rangeDirty(brickTotal, 0);
            // Logical brick bins avoid querying every object for every texel.
            // Ring addressing only affects storage, never world-space culling.
            std::vector<std::vector<std::uint32_t>> fixedBins(brickTotal), movingBins(brickTotal);
            for (std::uint32_t object = 0; object < prepared[i].size(); ++object) {
                const auto& value = prepared[i][object]; auto& bins = value.movable ? movingBins : fixedBins;
                ObjectBricks(value, level, settings.brickSize, [&](glm::uvec3 b) { const auto at = Index(b, brickCounts); if (dirty[at] & (value.movable ? 2 : 1)) bins[at].push_back(object); });
            }
            struct BrickJob { glm::uvec3 first, physical; std::size_t bin; std::uint8_t layers; };
            std::vector<BrickJob> jobs; jobs.reserve(std::count_if(dirty.begin(), dirty.end(), [](auto bits) { return bits != 0; }));
            for (std::uint32_t z = 0; z < brickCounts.z; ++z) for (std::uint32_t y = 0; y < brickCounts.y; ++y) for (std::uint32_t x = 0; x < brickCounts.x; ++x) {
                const glm::uvec3 first = glm::uvec3(x, y, z) * settings.brickSize; const auto brickAt = Index({x, y, z}, brickCounts); const auto bits = dirty[brickAt];
                if (!bits) continue;
                const auto physicalFirst = Physical(first, level), physicalBrick = physicalFirst / settings.brickSize;
                jobs.push_back({first, physicalFirst, brickAt, bits});
                stats.updatedVoxels += brickVoxels; ++stats.updatedBricks;
                if (bits & 1) { stats.staticVoxelUpdates += brickVoxels; stats.objectDistanceQueries += fixedBins[brickAt].size() * brickVoxels; }
                if (bits & 2) { stats.dynamicVoxelUpdates += brickVoxels; stats.objectDistanceQueries += movingBins[brickAt].size() * brickVoxels; }
                for (std::uint32_t nz = 0; nz != 2; ++nz) for (std::uint32_t ny = 0; ny != 2; ++ny) for (std::uint32_t nx = 0; nx != 2; ++nx) {
                    const auto neighbor = (physicalBrick + brickCounts - glm::uvec3(nx, ny, nz)) % brickCounts;
                    rangeDirty[Index(neighbor, brickCounts)] = 1;
                }
                for (std::uint32_t vz = 0; vz < settings.brickSize; ++vz) for (std::uint32_t vy = 0; vy < settings.brickSize; ++vy) {
                    // scroll is brick-aligned, hence each brick x-row is
                    // contiguous in physical storage and cannot wrap midway.
                    state.ranges.push_back({level.atlasOffset + std::uint32_t(Index(physicalFirst + glm::uvec3(0, vy, vz), level.counts)), settings.brickSize});
                }
            }
            const auto writeBrick = [&](std::size_t jobIndex) {
                const auto& job = jobs[jobIndex];
                for (std::uint32_t vz = 0; vz < settings.brickSize; ++vz) for (std::uint32_t vy = 0; vy < settings.brickSize; ++vy) {
                    for (std::uint32_t vx = 0; vx < settings.brickSize; ++vx) {
                        const auto logical = job.first + glm::uvec3(vx, vy, vz); const auto at = Index(job.physical + glm::uvec3(vx, vy, vz), level.counts);
                        const auto world = level.origin + glm::vec3(logical) * level.spacing;
                        const auto merge = [&](const std::vector<std::uint32_t>& objects) {
                            glm::vec2 result(level.narrowBand, 0);
                            // Debug STL vector iterators register with a shared
                            // proxy lock. Avoid constructing begin/end for every
                            // voxel, especially across the parallel workers.
                            const auto* indices = objects.data();
                            for (std::size_t candidate = 0; candidate < objects.size(); ++candidate) {
                                const auto object = indices[candidate]; const float value = Distance(prepared[i][object], world);
                                if (value < result.x) result = {value, float(object + 1)};
                            }
                            return result;
                        };
                        if (job.layers & 1) cache.fixed[at] = merge(fixedBins[job.bin]);
                        if (job.layers & 2) cache.moving[at] = merge(movingBins[job.bin]);
                        const auto a = cache.fixed[at], b = cache.moving[at], nearest = a.x <= b.x ? a : b;
                        state.pixels[level.atlasOffset + at] = {nearest.x, nearest.y, a.x, b.x};
                    }
                }
            };
            static const unsigned workerCount = std::clamp(std::thread::hardware_concurrency(), 1u, 8u);
            if (jobs.size() * brickVoxels >= 8192 && workerCount > 1) {
                if (!state.workers) state.workers = std::make_unique<BrickWorkers>(workerCount);
                state.workers->Run(jobs.size(), writeBrick);
            } else for (std::size_t job = 0; job < jobs.size(); ++job) writeBrick(job);
            // A trilinear cell remains between its eight node values. These
            // exact bounds let GPU rays skip whole bricks without discarding
            // zero crossings; the extra shared node belongs to the next brick.
            for (std::uint32_t z = 0; z < brickCounts.z; ++z) for (std::uint32_t y = 0; y < brickCounts.y; ++y) for (std::uint32_t x = 0; x < brickCounts.x; ++x) {
                const glm::uvec3 brick{x, y, z}; const auto at = Index(brick, brickCounts);
                if (!rangeDirty[at]) continue;
                float minimum = std::numeric_limits<float>::max(), maximum = -minimum;
                for (std::uint32_t vz = 0; vz <= settings.brickSize; ++vz) for (std::uint32_t vy = 0; vy <= settings.brickSize; ++vy) for (std::uint32_t vx = 0; vx <= settings.brickSize; ++vx) {
                    const auto node = (brick * settings.brickSize + glm::uvec3(vx, vy, vz)) % level.counts;
                    const float value = state.pixels[level.atlasOffset + Index(node, level.counts)].x;
                    minimum = std::min(minimum, value); maximum = std::max(maximum, value);
                }
                const auto rangeAt = level.rangeAtlasOffset + std::uint32_t(at);
                state.rangePixels[rangeAt] = {minimum, maximum, 0, 0};
                state.rangeRanges.push_back({rangeAt, 1});
            }
        }
        const bool levelChanged = hasDirty || scrolled || previousError != level.sourceInterpolationError;
        if (levelChanged) { ++level.generation; changed = true; }
        if (full) ++state.statistics.fullLevels;
        if (cache.initialized && scrolled) ++state.statistics.scrolledLevels;
        cache.originCell = origins[i]; cache.objects = std::move(prepared[i]); cache.initialized = true;
        state.statistics.updatedVoxels += stats.updatedVoxels; state.statistics.enteringVoxels += stats.enteringVoxels; state.statistics.updatedBricks += stats.updatedBricks;
        state.statistics.staticVoxelUpdates += stats.staticVoxelUpdates; state.statistics.dynamicVoxelUpdates += stats.dynamicVoxelUpdates; state.statistics.objectDistanceQueries += stats.objectDistanceQueries;
        state.statistics.totalVoxels += stats.totalVoxels; state.statistics.totalBricks += stats.totalBricks;
    }
    MergeRanges(state.ranges);
    MergeRanges(state.rangeRanges);
    if (changed) ++state.generation;
    state.statistics.generation = state.generation;
    state.statistics.updateMilliseconds = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
    return changed;
}

glm::vec4 ProbeSdfClipmapVolume::SampleLogical(glm::uvec3 logical, std::uint32_t index) const {
    if (index >= impl_->levels.size() || !impl_->caches[index].initialized) throw std::out_of_range("Uninitialized SDF clipmap level");
    const auto& level = impl_->levels[index];
    if (glm::any(glm::greaterThanEqual(logical, level.counts))) throw std::out_of_range("SDF clipmap logical voxel is outside level");
    return impl_->pixels[level.atlasOffset + Index(Physical(logical, level), level.counts)];
}
glm::vec4 ProbeSdfClipmapVolume::SampleWorld(glm::vec3 p, std::uint32_t index) const {
    if (index >= impl_->levels.size() || !impl_->caches[index].initialized || !Finite(p)) throw std::out_of_range("Invalid SDF clipmap world sample");
    const auto& level = impl_->levels[index]; const auto maximum = glm::vec3(level.counts - glm::uvec3(1));
    if (glm::any(glm::lessThan(p, level.origin)) || glm::any(glm::greaterThan(p, level.origin + maximum * level.spacing)))
        throw std::out_of_range("SDF clipmap world sample is outside level");
    const auto coordinate = glm::clamp((p - level.origin) / level.spacing, glm::vec3(0), maximum);
    const auto first = glm::uvec3(glm::floor(coordinate)), last = glm::min(first + glm::uvec3(1), level.counts - glm::uvec3(1));
    const auto fraction = coordinate - glm::vec3(first); glm::vec4 result(0);
    for (std::uint32_t z = 0; z != 2; ++z) for (std::uint32_t y = 0; y != 2; ++y) for (std::uint32_t x = 0; x != 2; ++x) {
        const float weight = (x ? fraction.x : 1 - fraction.x) * (y ? fraction.y : 1 - fraction.y) * (z ? fraction.z : 1 - fraction.z);
        result += SampleLogical({x ? last.x : first.x, y ? last.y : first.y, z ? last.z : first.z}, index) * weight;
    }
    result.y = SampleLogical(glm::min(glm::uvec3(glm::floor(coordinate + glm::vec3(.5f))), level.counts - glm::uvec3(1)), index).y;
    return result;
}
float ProbeSdfClipmapVolume::ObjectDistance(std::uint32_t object, glm::vec3 p, std::uint32_t index) const {
    if (index >= impl_->levels.size() || !impl_->caches[index].initialized || !Finite(p)) throw std::out_of_range("Invalid clipmap object sample");
    const auto& cache = impl_->caches[index];
    if (object >= cache.objects.size()) throw std::out_of_range("Invalid clipmap object index");
    return Distance(cache.objects[object], p);
}
} // namespace Render
