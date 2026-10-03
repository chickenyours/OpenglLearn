#include "PixelSandbox/Public/sandbox_module.h"
#include "PixelSandbox/Public/sandbox_components.h"
#include "PixelSandbox/Systems/sandbox_systems.h"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <fstream>
#include <mutex>
#include <stdexcept>

namespace PixelSandbox {
namespace {
using Clock = std::chrono::steady_clock;
double Milliseconds(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
std::uint32_t Mix(std::uint32_t value) {
    value ^= value >> 16; value *= 0x7feb352du;
    value ^= value >> 15; value *= 0x846ca68bu; return value ^ (value >> 16);
}
bool ValidTemperature(float value) { return std::isfinite(value) && value >= -273.0f && value <= 4000.0f; }
void RegisterTypes() {
    static std::once_flag once;
    std::call_once(once, [] {
        REGISTER_COMPONENT("pixel_sandbox_grid", GridStorage);
        REGISTER_COMPONENT("pixel_sandbox_chunk", ChunkRegion);
    });
}
const Cell BoundaryCell{Material::Wall, 0, 0, 20, 0, 0, 0};
} // namespace

struct SandboxModule::Impl {
    explicit Impl(SimulationConfig value) : config(value) {}
    SimulationConfig config;
    Statistics stats;
    std::string error;
    std::unique_ptr<ECS::Core::Scene> scene;
    GridStorage* grid = nullptr;
    ECS::EntityID world = 0;
    std::vector<ChunkRegion*> chunks;
    std::vector<std::uint8_t> active;
    int chunksX = 0, chunksY = 0, airWidth = 0, airHeight = 0;
    std::array<std::array<bool, MaterialCount>, MaterialCount> canMove{};
    const MaterialInfo* materials = Materials().data();
    std::array<Rule, MaterialCount> rules;
    ECS::System::Pipeline pipeline;
    ECS::System::Context context;
    double accumulator = 0;
    std::uint64_t revision = 1;
    bool started = false, paused = false, stepping = false, fieldActive = false;
    std::size_t Index(int x, int y) const { return std::size_t(y) * config.width + x; }
    std::size_t AirIndex(int x, int y) const { return std::size_t(y / FieldScale) * airWidth + x / FieldScale; }
    bool Active(int x, int y) const { return active[std::size_t(y / ChunkSize) * chunksX + x / ChunkSize] != 0; }
    std::uint32_t Stamp() const { return stepping ? static_cast<std::uint32_t>(stats.tick) : 0; }
};

SandboxModule::SandboxModule(SimulationConfig config) : impl_(std::make_unique<Impl>(config)) {
    impl_->pipeline.Add<FieldSystem>();
    impl_->pipeline.Add<HeatSystem>();
    impl_->pipeline.Add<MaterialSystem>();
    impl_->pipeline.RunBefore<HeatSystem, MaterialSystem>();
}
SandboxModule::~SandboxModule() { Shutdown(); }
bool SandboxModule::IsStarted() const noexcept { return impl_->started; }
const SimulationConfig& SandboxModule::Config() const noexcept { return impl_->config; }
const Statistics& SandboxModule::Stats() const noexcept { return impl_->stats; }
const std::string& SandboxModule::Error() const noexcept { return impl_->error; }
bool SandboxModule::IsPaused() const noexcept { return impl_->paused; }
std::uint64_t SandboxModule::Revision() const noexcept { return impl_->revision; }
ECS::Core::Scene* SandboxModule::Scene() noexcept { return impl_->scene.get(); }
ECS::EntityID SandboxModule::WorldEntity() const noexcept { return impl_->world; }
ECS::System::Pipeline& SandboxModule::Pipeline() { return impl_->pipeline; }

bool SandboxModule::Startup() {
    auto& s = *impl_;
    if (s.started) return true;
    try {
        const auto& c = s.config;
        if (c.width < 1 || c.height < 1 || c.width > 1024 || c.height > 1024 ||
            !std::isfinite(c.stepSeconds) || c.stepSeconds < 1e-5 || c.stepSeconds > 1 ||
            c.maxCatchUpSteps < 1 || c.maxCatchUpSteps > 64)
            throw std::invalid_argument("Grid must be 1..1024, step 0.00001..1, catch-up 1..64");
        RegisterTypes();
        s.scene = std::make_unique<ECS::Core::Scene>();
        auto worldDescription = s.scene->CreateArchTypeDescription();
        worldDescription->AddComponentArray<GridStorage>();
        auto worldType = s.scene->CreateArchType(worldDescription, 1);
        s.world = s.scene->CreateEntity(worldType).GetID();
        s.grid = s.scene->GetActiveComponent<GridStorage>(s.world).Get();
        s.grid->cells.resize(std::size_t(c.width) * c.height);
        s.chunksX = (c.width + ChunkSize - 1) / ChunkSize;
        s.chunksY = (c.height + ChunkSize - 1) / ChunkSize;
        s.airWidth = (c.width + FieldScale - 1) / FieldScale;
        s.airHeight = (c.height + FieldScale - 1) / FieldScale;
        s.grid->air.resize(std::size_t(s.airWidth) * s.airHeight);
        s.grid->nextAir.resize(s.grid->air.size());
        auto chunkDescription = s.scene->CreateArchTypeDescription();
        chunkDescription->AddComponentArray<ChunkRegion>();
        auto chunkType = s.scene->CreateArchType(chunkDescription, 64);
        auto entities = s.scene->CreateEntities(chunkType, std::size_t(s.chunksX) * s.chunksY);
        for (std::size_t i = 0; i < entities.size(); ++i) {
            auto* region = s.scene->GetActiveComponent<ChunkRegion>(entities[i].GetID()).Get();
            region->x = int(i % s.chunksX) * ChunkSize;
            region->y = int(i / s.chunksX) * ChunkSize;
            s.chunks.push_back(region);
        }
        s.active.resize(s.chunks.size(), 1);
        for (std::size_t from = 0; from < MaterialCount; ++from) {
            for (std::size_t to = 0; to < MaterialCount; ++to) {
                const auto& a = Info(static_cast<Material>(from));
                const auto& b = Info(static_cast<Material>(to));
                s.canMove[from][to] = from != to && a.phase != Matter::Solid && a.phase != Matter::Empty &&
                    (b.phase == Matter::Empty ||
                     ((a.phase == Matter::Powder || a.phase == Matter::Liquid) &&
                      (b.phase == Matter::Gas || (b.phase == Matter::Liquid && a.density > b.density))) ||
                     (a.phase == Matter::Gas && b.phase == Matter::Gas && a.density < b.density));
            }
        }
        s.started = true;
        Clear();
        s.context.scene = s.scene.get();
        s.context.SetService(this);
        if (!s.pipeline.Start(s.context)) throw std::runtime_error("Sandbox ECS pipeline failed to start");
        s.error.clear();
        return true;
    } catch (const std::exception& error) {
        s.error = error.what(); Shutdown(); return false;
    }
}

void SandboxModule::Shutdown() {
    auto& s = *impl_;
    if (s.stepping) return;
    s.pipeline.Stop(s.context);
    s.chunks.clear(); s.active.clear(); s.grid = nullptr; s.world = 0;
    s.scene.reset(); s.context.scene = nullptr; s.context.frameIndex = 0;
    s.stats = {}; s.accumulator = 0; s.started = false; s.paused = false; s.fieldActive = false;
}

bool SandboxModule::InBounds(int x, int y) const noexcept {
    return impl_->started && x >= 0 && y >= 0 && x < Config().width && y < Config().height;
}
const Cell& SandboxModule::At(int x, int y) const {
    return InBounds(x, y) ? impl_->grid->cells[impl_->Index(x, y)] : BoundaryCell;
}
Cell& SandboxModule::Edit(int x, int y) {
    if (!InBounds(x, y)) throw std::out_of_range("Pixel outside sandbox");
    Wake(x, y); ++impl_->revision;
    return impl_->grid->cells[impl_->Index(x, y)];
}

void SandboxModule::Wake(int x, int y) {
    if (!InBounds(x, y)) return;
    auto& s = *impl_;
    const auto own = std::size_t(y / ChunkSize) * s.chunksX + x / ChunkSize;
    if ((x & (ChunkSize - 1)) != 0 && (x & (ChunkSize - 1)) != ChunkSize - 1 &&
        (y & (ChunkSize - 1)) != 0 && (y & (ChunkSize - 1)) != ChunkSize - 1 &&
        s.chunks[own]->awake == 4 && (!s.stepping || s.active[own])) return;
    // One-cell halo: a modification at a tile seam also wakes its neighbor.
    for (int cy = std::max(0, y - 1) / ChunkSize; cy <= std::min(Config().height - 1, y + 1) / ChunkSize; ++cy)
        for (int cx = std::max(0, x - 1) / ChunkSize; cx <= std::min(Config().width - 1, x + 1) / ChunkSize; ++cx) {
            const auto index = std::size_t(cy) * s.chunksX + cx;
            s.chunks[index]->awake = 4;
            // Field/heat events must be visible to later passes THIS tick.
            // A material created there still has the current stamp and is skipped.
            if (s.stepping && !s.active[index]) { s.active[index] = 1; ++s.stats.activeChunks; }
        }
}

bool SandboxModule::SetCell(int x, int y, Material material, float temperature) {
    if (!InBounds(x, y) || !IsMaterial(material)) return false;
    if (std::isnan(temperature)) temperature = Info(material).temperature;
    if (!ValidTemperature(temperature)) return false;
    auto& s = *impl_;
    auto& cell = s.grid->cells[s.Index(x, y)];
    --s.stats.counts[std::size_t(cell.material)];
    ++s.stats.counts[std::size_t(material)];
    s.stats.particles = s.grid->cells.size() - s.stats.counts[std::size_t(Material::Empty)];
    cell = Cell{material, static_cast<std::uint8_t>(Random(x, y, 123)), Info(material).lifetime,
                temperature, s.Stamp(), 0, 0};
    Wake(x, y); ++s.revision;
    return true;
}
bool SandboxModule::SetTemperature(int x, int y, float value) {
    if (!InBounds(x, y) || !ValidTemperature(value)) return false;
    Edit(x, y).temperature = value;
    return true;
}
void SandboxModule::Clear() {
    auto& s = *impl_;
    if (!s.started || s.stepping) return;
    std::fill(s.grid->cells.begin(), s.grid->cells.end(), Cell{});
    std::fill(s.grid->air.begin(), s.grid->air.end(), AirCell{});
    std::fill(s.grid->nextAir.begin(), s.grid->nextAir.end(), AirCell{});
    for (auto* chunk : s.chunks) chunk->awake = 4;
    s.stats = {}; s.stats.totalChunks = s.chunks.size(); s.stats.activeChunks = s.chunks.size();
    s.stats.counts[0] = s.grid->cells.size(); s.context.frameIndex = 0;
    s.fieldActive = false; s.accumulator = 0; ++s.revision;
}

std::uint32_t SandboxModule::Random(int x, int y, std::uint32_t salt) const noexcept {
    return Mix(Config().seed ^ Mix(std::uint32_t(x) + std::uint32_t(y) * 65537u) ^
               Mix(std::uint32_t(Stats().tick)) ^ Mix(std::uint32_t(Stats().tick >> 32)) ^ Mix(salt));
}
bool SandboxModule::Chance(int x, int y, std::uint32_t salt, float probability) const noexcept {
    return probability > 0 && (probability >= 1 || double(Random(x, y, salt)) / 4294967296.0 < probability);
}
void SandboxModule::SetRule(Material material, Rule rule) {
    if (IsStarted() || !IsMaterial(material)) throw std::logic_error("Register valid material rules while stopped");
    impl_->rules[std::size_t(material)] = std::move(rule);
}

bool SandboxModule::TryMove(int x, int y, int nx, int ny) {
    if (!InBounds(x, y) || !InBounds(nx, ny) || (x == nx && y == ny)) return false;
    auto& s = *impl_;
    auto& from = s.grid->cells[s.Index(x, y)];
    auto& to = s.grid->cells[s.Index(nx, ny)];
    if (!s.canMove[std::size_t(from.material)][std::size_t(to.material)]) return false;
    const auto targetPhase = s.materials[std::size_t(to.material)].phase;
    // Density exchanges occur vertically; sideways liquid swaps create spurious mixing.
    if (targetPhase == Matter::Liquid && ny <= y) return false;
    if (s.materials[std::size_t(from.material)].phase == Matter::Gas && targetPhase == Matter::Gas && ny >= y) return false;
    std::swap(from, to);
    from.stamp = to.stamp = s.Stamp();
    Wake(x, y); Wake(nx, ny); ++s.revision;
    return true;
}

float SandboxModule::PressureAt(int x, int y) const {
    return InBounds(x, y) ? impl_->grid->air[impl_->AirIndex(x, y)].pressure : 0;
}
void SandboxModule::AddPressure(int x, int y, float amount) {
    if (!InBounds(x, y) || !std::isfinite(amount)) return;
    auto& air = impl_->grid->air[impl_->AirIndex(x, y)];
    air.pressure = std::clamp(air.pressure + amount, -20.0f, 20.0f);
    impl_->fieldActive = true; Wake(x, y); ++impl_->revision;
}
void SandboxModule::Explode(int x, int y, int radius, float power) {
    if (!InBounds(x, y) || !std::isfinite(power)) return;
    radius = std::clamp(radius, 1, 16); power = std::clamp(power, 0.0f, 8.0f);
    for (int dy = -radius; dy <= radius; ++dy) for (int dx = -radius; dx <= radius; ++dx) {
        if (dx * dx + dy * dy > radius * radius || !InBounds(x + dx, y + dy)) continue;
        const auto material = At(x + dx, y + dy).material;
        if (material == Material::Wall) continue;
        const float strength = 1.0f - std::sqrt(float(dx * dx + dy * dy)) / (radius + 1);
        AddPressure(x + dx, y + dy, power * strength * .35f);
        if (material == Material::Empty) {
            if (Chance(x + dx, y + dy, 822, .28f * strength)) SetCell(x + dx, y + dy, Material::Fire, 850);
        } else if (material == Material::Glass && strength * power > 1.0f) {
            SetCell(x + dx, y + dy, Material::Sand, 200);
        } else {
            SetTemperature(x + dx, y + dy, std::min(4000.0f, At(x + dx, y + dy).temperature + 600 * strength));
            // Defer chain reactions to the next tick; explosions never recurse.
            Edit(x + dx, y + dy).stamp = impl_->Stamp();
        }
    }
}

void SandboxModule::StepFields() {
    auto& s = *impl_; const auto begin = Clock::now();
    if (!s.fieldActive) { s.stats.fieldMs = Milliseconds(begin); return; }
    auto& input = s.grid->air; auto& output = s.grid->nextAir;
    bool remainsActive = false;
    const auto sample = [&](int x, int y) -> AirCell {
        if (x < 0 || y < 0 || x >= s.airWidth || y >= s.airHeight) return {};
        return input[std::size_t(y) * s.airWidth + x];
    };
    for (int y = 0; y < s.airHeight; ++y) for (int x = 0; x < s.airWidth; ++x) {
        const auto own = sample(x, y), left = sample(x - 1, y), right = sample(x + 1, y);
        const auto up = sample(x, y - 1), down = sample(x, y + 1);
        auto& next = output[std::size_t(y) * s.airWidth + x];
        // Stable, damped coarse pressure/velocity field (not a full CFD solver).
        next.pressure = std::clamp((own.pressure * .60f + .10f *
            (left.pressure + right.pressure + up.pressure + down.pressure) -
            .10f * (right.vx - left.vx + down.vy - up.vy)) * .97f, -20.0f, 20.0f);
        next.vx = std::clamp((own.vx + .12f * (left.pressure - right.pressure)) * .90f, -4.0f, 4.0f);
        next.vy = std::clamp((own.vy + .12f * (up.pressure - down.pressure)) * .90f, -4.0f, 4.0f);
        const int px = std::min(Config().width - 1, x * FieldScale + 2);
        const int py = std::min(Config().height - 1, y * FieldScale + 2);
        // A coarse node centered inside a solid blocks the field.
        if (s.materials[std::size_t(At(px, py).material)].phase == Matter::Solid) next = {};
        if (std::abs(next.pressure) + std::abs(next.vx) + std::abs(next.vy) > .015f) {
            remainsActive = true; Wake(px, py);
        } else next = {};
    }
    input.swap(output); s.fieldActive = remainsActive;
    s.stats.fieldMs = Milliseconds(begin);
}

void SandboxModule::StepHeat() {
    auto& s = *impl_; const auto begin = Clock::now();
    auto& cells = s.grid->cells;
    for (int y = 0; y < Config().height; ++y) {
        for (int cx = 0; cx < s.chunksX; ++cx) {
            if (!s.active[std::size_t(y / ChunkSize) * s.chunksX + cx]) continue;
            const int end = std::min(Config().width, (cx + 1) * ChunkSize);
            for (int x = cx * ChunkSize; x < end; ++x) {
                auto& a = cells[s.Index(x, y)];
                if (a.material == Material::Empty) continue;
                const float conductivity = s.materials[std::size_t(a.material)].conductivity;
                for (auto [nx, ny] : {std::pair{x + 1, y}, std::pair{x, y + 1}}) {
                    if (!InBounds(nx, ny)) continue;
                    auto& b = cells[s.Index(nx, ny)];
                    if (b.material == Material::Empty) continue;
                    const float exchange = (b.temperature - a.temperature) *
                        std::min(conductivity, s.materials[std::size_t(b.material)].conductivity) * .18f;
                    if (std::abs(exchange) < .005f) continue;
                    a.temperature += exchange; b.temperature -= exchange;
                    Wake(x, y); Wake(nx, ny); ++s.revision;
                }
                // Ambient cooling excludes insulating walls. Reactive particles
                // maintain their own heat sources in the following material pass.
                if (a.material != Material::Wall && std::abs(a.temperature - 20) > .1f) {
                    a.temperature += (20 - a.temperature) * .0008f;
                    Wake(x, y); ++s.revision;
                }
            }
        }
    }
    s.stats.heatMs = Milliseconds(begin);
}

void SandboxModule::StepMaterials() {
    auto& s = *impl_; const auto begin = Clock::now();
    const bool reverse = (s.stats.tick & 1) != 0;
    const auto stamp = s.Stamp();
    s.stats.visitedCells = 0;
    for (int y = Config().height - 1; y >= 0; --y) {
        for (int ci = 0; ci < s.chunksX; ++ci) {
            const int cx = reverse ? s.chunksX - 1 - ci : ci;
            if (!s.active[std::size_t(y / ChunkSize) * s.chunksX + cx]) continue;
            const int start = cx * ChunkSize, end = std::min(Config().width, start + ChunkSize);
            for (int xi = 0; xi < end - start; ++xi) {
                const int x = reverse ? end - 1 - xi : start + xi;
                ++s.stats.visitedCells;
                auto& cell = s.grid->cells[s.Index(x, y)];
                if (cell.material == Material::Empty || cell.stamp == stamp) continue;
                cell.stamp = stamp;
                const auto& custom = s.rules[std::size_t(cell.material)];
                if (custom ? custom(*this, x, y) : ApplyMaterialRule(*this, x, y)) continue;
                const auto& info = s.materials[std::size_t(cell.material)];
                if (info.phase == Matter::Solid || info.phase == Matter::Empty) continue;
                const int direction = (Random(x, y, 701) & 1) ? 1 : -1;
                const auto& air = s.grid->air[s.AirIndex(x, y)];
                const float threshold = info.phase == Matter::Gas ? .12f : info.phase == Matter::Liquid ? 1.0f : 1.5f;
                const int windX = air.vx > threshold ? 1 : air.vx < -threshold ? -1 : 0;
                const int windY = air.vy > threshold ? 1 : air.vy < -threshold ? -1 : 0;
                if ((windX || windY) && TryMove(x, y, x + windX, y + windY)) continue;
                if (info.phase == Matter::Gas) {
                    const int wind = air.vx > .15f ? 1 : air.vx < -.15f ? -1 : direction;
                    if (TryMove(x, y, x, y - 1) || TryMove(x, y, x + wind, y - 1) ||
                        TryMove(x, y, x - wind, y - 1)) continue;
                    // Failed random diffusion is not equilibrium: retry on later
                    // ticks while a lateral opening exists, even in a cold chunk.
                    if (At(x - 1, y).material == Material::Empty || At(x + 1, y).material == Material::Empty) Wake(x, y);
                    if (Chance(x, y, 702, .55f)) TryMove(x, y, x + wind, y);
                } else {
                    if (TryMove(x, y, x, y + 1) || TryMove(x, y, x + direction, y + 1) ||
                        TryMove(x, y, x - direction, y + 1)) continue;
                    if (info.phase != Matter::Liquid) continue;
                    // Check the complete lateral path; a liquid cannot teleport through walls.
                    for (int sign : {direction, -direction}) {
                        int target = x;
                        for (int distance = 1; distance <= info.dispersion; ++distance) {
                            const int nx = x + sign * distance;
                            if (!InBounds(nx, y) || At(nx, y).material != Material::Empty) break;
                            target = nx;
                            if (At(nx, y + 1).material == Material::Empty) break;
                        }
                        if (target != x && TryMove(x, y, target, y)) break;
                    }
                }
            }
        }
    }
    s.stats.materialMs = Milliseconds(begin);
}

void SandboxModule::FixedTick() {
    auto& s = *impl_;
    if (!s.started || s.stepping) return;
    const auto begin = Clock::now();
    struct Guard { bool& value; ~Guard() { value = false; } } guard{s.stepping};
    s.stepping = true; ++s.stats.tick;
    if (static_cast<std::uint32_t>(s.stats.tick) == 0)
        for (auto& cell : s.grid->cells) cell.stamp = 0xffffffffu;
    s.stats.activeChunks = 0;
    for (std::size_t i = 0; i < s.chunks.size(); ++i) {
        s.active[i] = !Config().sleeping || s.chunks[i]->awake > 0;
        s.stats.activeChunks += s.active[i] != 0;
        if (s.chunks[i]->awake) --s.chunks[i]->awake;
    }
    s.context.frameIndex = s.stats.tick; s.context.deltaSeconds = Config().stepSeconds;
    s.pipeline.Tick(s.context);
    s.stats.totalMs = Milliseconds(begin);
}
void SandboxModule::SetPaused(bool paused) {
    if (impl_->paused != paused) { impl_->paused = paused; impl_->accumulator = 0; }
}
void SandboxModule::Advance(double seconds) {
    auto& s = *impl_;
    if (!s.started || s.paused || s.stepping || !std::isfinite(seconds) || seconds < 0) return;
    const double step = Config().stepSeconds, budget = step * Config().maxCatchUpSteps;
    s.accumulator += seconds > budget ? budget + std::fmod(seconds, step) : seconds;
    unsigned count = 0;
    while (count < Config().maxCatchUpSteps && s.accumulator + step * 1e-9 >= step) {
        FixedTick(); s.accumulator = std::max(0.0, s.accumulator - step); ++count;
    }
    if (s.accumulator + step * 1e-9 >= step) s.accumulator = std::fmod(s.accumulator, step);
}

void SandboxModule::Paint(int x, int y, int radius, Material material, BrushTool tool, bool replace) {
    if (!IsStarted() || !IsMaterial(material)) return;
    radius = std::clamp(radius, 0, 64);
    if (x < -radius || y < -radius || x >= Config().width + radius || y >= Config().height + radius) return;
    for (int ny = std::max(0, y - radius); ny <= std::min(Config().height - 1, y + radius); ++ny)
        for (int nx = std::max(0, x - radius); nx <= std::min(Config().width - 1, x + radius); ++nx) {
            if ((nx - x) * (nx - x) + (ny - y) * (ny - y) > radius * radius) continue;
            if (tool == BrushTool::Erase) SetCell(nx, ny, Material::Empty);
            else if (tool == BrushTool::Heat || tool == BrushTool::Cool)
                SetTemperature(nx, ny, std::clamp(At(nx, ny).temperature + (tool == BrushTool::Heat ? 80 : -80), -273.0f, 4000.0f));
            else if (tool == BrushTool::Pressure) AddPressure(nx, ny, .8f);
            else if (replace || At(nx, ny).material == Material::Empty) SetCell(nx, ny, material);
        }
}
void SandboxModule::Stroke(int x0, int y0, int x1, int y1, int radius, Material material, BrushTool tool, bool replace) {
    if (!IsStarted()) return;
    // Bound hostile/off-window coordinates before arithmetic and Bresenham work.
    const int margin = 64;
    x0 = std::clamp(x0, -margin, Config().width + margin); x1 = std::clamp(x1, -margin, Config().width + margin);
    y0 = std::clamp(y0, -margin, Config().height + margin); y1 = std::clamp(y1, -margin, Config().height + margin);
    const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        Paint(x0, y0, radius, material, tool, replace);
        if (x0 == x1 && y0 == y1) break;
        const int twice = 2 * error;
        if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; }
    }
}

std::vector<std::uint8_t> SandboxModule::Pixels(bool heatMap) const {
    std::vector<std::uint8_t> result;
    if (!IsStarted()) return result;
    result.resize(impl_->grid->cells.size() * 4);
    for (std::size_t i = 0; i < impl_->grid->cells.size(); ++i) {
        const auto& cell = impl_->grid->cells[i];
        const auto color = Info(cell.material).color;
        float r = float((color >> 16) & 255), g = float((color >> 8) & 255), b = float(color & 255);
        if (heatMap && cell.material != Material::Empty) {
            const float heat = std::clamp((cell.temperature + 50) / 850, 0.0f, 1.0f);
            r = 255 * std::clamp(heat * 2, 0.0f, 1.0f);
            g = 255 * std::clamp(1.4f - std::abs(heat - .55f) * 3, 0.0f, 1.0f);
            b = 255 * (1 - heat);
        } else if (cell.material != Material::Empty) {
            const float shade = .88f + float(cell.variant) / 255 * .20f;
            r *= shade; g *= shade; b *= shade;
            if (cell.charge > 16 || cell.material == Material::Spark) { r = 255; g = 240; b = 125; }
            if ((cell.flags & 1) && Info(cell.material).flammable) { r = 250; g = 90 + cell.variant * .25f; b = 35; }
        }
        result[i * 4] = static_cast<std::uint8_t>(std::clamp(r, 0.0f, 255.0f));
        result[i * 4 + 1] = static_cast<std::uint8_t>(std::clamp(g, 0.0f, 255.0f));
        result[i * 4 + 2] = static_cast<std::uint8_t>(std::clamp(b, 0.0f, 255.0f));
        result[i * 4 + 3] = 255;
    }
    return result;
}

void SandboxModule::LoadPreset(Preset preset) {
    if (!IsStarted()) return;
    Clear();
    const int w = Config().width, h = Config().height;
    auto box = [&](float x0, float y0, float x1, float y1, Material material) {
        for (int y = int(y0 * h); y < int(y1 * h); ++y)
            for (int x = int(x0 * w); x < int(x1 * w); ++x) SetCell(x, y, material);
    };
    if (preset == Preset::Empty) return;
    box(0, .96f, 1, 1, Material::Wall);
    if (preset == Preset::Falling) {
        box(.06f, .08f, .32f, .26f, Material::Sand);
        box(.36f, .10f, .57f, .28f, Material::Water);
        box(.63f, .08f, .81f, .23f, Material::Oil);
        box(.13f, .62f, .45f, .64f, Material::Stone);
        box(.58f, .49f, .90f, .51f, Material::Stone);
        box(.85f, .10f, .93f, .21f, Material::Salt);
    } else if (preset == Preset::Volcano) {
        box(.05f, .70f, .95f, .94f, Material::Water);
        for (int y = h / 3; y < h * 96 / 100; ++y) {
            const int half = 2 + (y - h / 3) / 2;
            for (int x = w / 2 - half; x <= w / 2 + half; ++x)
                if (std::abs(x - w / 2) > std::max(2, w / 40)) SetCell(x, y, Material::Stone);
        }
        box(.47f, .28f, .53f, .64f, Material::Lava);
        box(.18f, .58f, .26f, .68f, Material::Wood);
        box(.76f, .56f, .86f, .68f, Material::Ice);
    } else if (preset == Preset::Circuit) {
        const int y = h / 2;
        for (int x = w / 8; x < w * 3 / 4; ++x) SetCell(x, y, Material::Metal);
        SetCell(w / 8 - 1, y, Material::Spark);
        box(.74f, .43f, .81f, .50f, Material::Gunpowder);
        box(.82f, .38f, .92f, .57f, Material::Gas);
        box(.10f, .76f, .65f, .78f, Material::Glass);
        box(.18f, .66f, .55f, .76f, Material::Brine);
    } else if (preset == Preset::Garden) {
        box(.04f, .75f, .94f, .96f, Material::Soil);
        box(.18f, .68f, .46f, .75f, Material::Water);
        box(.63f, .68f, .80f, .75f, Material::Water);
        for (int x = w / 10; x < w * 9 / 10; x += std::max(3, w / 18)) SetCell(x, h * 68 / 100 - 1, Material::Seed);
        box(.07f, .32f, .12f, .56f, Material::Wood);
        box(.86f, .39f, .94f, .50f, Material::Acid);
    } else if (preset == Preset::Elements) {
        // Four isolated workbenches: melting, freezing, irrigation and ignition.
        box(.49f, 0, .51f, .96f, Material::Wall);
        box(0, .47f, 1, .49f, Material::Wall);
        box(.08f, .34f, .40f, .36f, Material::Heater);
        box(.07f, .34f, .08f, .36f, Material::Battery);
        box(.10f, .24f, .37f, .34f, Material::Wax);
        box(.60f, .34f, .92f, .36f, Material::Cooler);
        box(.59f, .34f, .60f, .36f, Material::Battery);
        box(.62f, .25f, .90f, .34f, Material::Water);
        box(.60f, .21f, .62f, .34f, Material::Glass);
        box(.90f, .21f, .92f, .34f, Material::Glass);
        box(.62f, .10f, .70f, .16f, Material::Snow);
        box(.82f, .10f, .89f, .16f, Material::DryIce);
        box(.04f, .85f, .46f, .96f, Material::Soil);
        box(.08f, .81f, .44f, .85f, Material::Seed);
        SetCell(w / 4, h * 58 / 100, Material::Clone);
        SetCell(w / 4 + 1, h * 58 / 100, Material::Water);
        for (int x = w * 58 / 100; x < w * 90 / 100; ++x) SetCell(x, h * 86 / 100, Material::Fuse);
        SetCell(w * 58 / 100 - 1, h * 86 / 100, Material::Spark);
        box(.87f, .82f, .93f, .86f, Material::Gunpowder);
        box(.57f, .58f, .94f, .76f, Material::Glass);
        for (int y = h * 60 / 100; y < h * 74 / 100; ++y)
            for (int x = w * 59 / 100; x < w * 92 / 100; ++x)
                SetCell(x, y, (x & 1) ? Material::Hydrogen : Material::Oxygen);
    }
}

namespace {
struct Bytes {
    std::vector<std::uint8_t> data;
    std::size_t offset = 0;
    void Put(std::uint64_t value, unsigned count) { for (unsigned i = 0; i < count; ++i) data.push_back(std::uint8_t(value >> (i * 8))); }
    std::uint64_t Get(unsigned count) {
        if (offset + count > data.size()) throw std::runtime_error("Truncated sandbox save");
        std::uint64_t value = 0; for (unsigned i = 0; i < count; ++i) value |= std::uint64_t(data[offset++]) << (i * 8); return value;
    }
    void Float(float value) { Put(std::bit_cast<std::uint32_t>(value), 4); }
    float Float() { return std::bit_cast<float>(std::uint32_t(Get(4))); }
    void Double(double value) { Put(std::bit_cast<std::uint64_t>(value), 8); }
    double Double() { return std::bit_cast<double>(Get(8)); }
};
constexpr std::uint64_t SaveMagic = 0x315842534c455850ULL; // PXELSBX1, little endian.
}

std::uint64_t SandboxModule::StateHash() const noexcept {
    if (!IsStarted()) return 0;
    std::uint64_t hash = 14695981039346656037ull;
    auto add = [&](std::uint64_t value) { for (int i = 0; i < 8; ++i) { hash ^= (value >> (i * 8)) & 255; hash *= 1099511628211ull; } };
    add(Config().width); add(Config().height); add(Config().seed); add(Stats().tick);
    for (const auto& cell : impl_->grid->cells) {
        add(std::uint8_t(cell.material)); add(cell.variant); add(cell.life);
        add(std::bit_cast<std::uint32_t>(cell.temperature)); add(cell.charge); add(cell.flags);
    }
    for (const auto& air : impl_->grid->air) {
        add(std::bit_cast<std::uint32_t>(air.pressure)); add(std::bit_cast<std::uint32_t>(air.vx)); add(std::bit_cast<std::uint32_t>(air.vy));
    }
    return hash;
}

bool SandboxModule::Save(const std::filesystem::path& path) {
    auto& s = *impl_;
    try {
        if (!IsStarted() || s.stepping || path.empty()) throw std::runtime_error("Cannot save stopped or updating sandbox");
        Bytes out; out.data.reserve(128 + s.grid->cells.size() * 16 + s.grid->air.size() * 12);
        out.Put(SaveMagic, 8); out.Put(2, 4); out.Put(Config().width, 4); out.Put(Config().height, 4);
        out.Put(Config().seed, 4); out.Put(Stats().tick, 8); out.Double(Config().stepSeconds);
        out.Put(Config().maxCatchUpSteps, 4); out.Put(Config().sleeping, 1); out.Put(s.paused, 1);
        out.Double(s.accumulator); out.Put(s.fieldActive, 1);
        for (const auto& cell : s.grid->cells) {
            out.Put(std::uint8_t(cell.material), 1); out.Put(cell.variant, 1); out.Put(cell.life, 2);
            out.Float(cell.temperature); out.Put(cell.stamp, 4); out.Put(cell.charge, 2); out.Put(cell.flags, 2);
        }
        for (const auto& air : s.grid->air) { out.Float(air.pressure); out.Float(air.vx); out.Float(air.vy); }
        for (const auto* chunk : s.chunks) out.Put(chunk->awake, 1);
        const auto temporary = std::filesystem::path(path.native() + std::filesystem::path(".pixel-sandbox.tmp").native());
        { std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
          if (!file) throw std::runtime_error("Cannot open save destination");
          file.write(reinterpret_cast<const char*>(out.data.data()), std::streamsize(out.data.size()));
          file.flush(); if (!file) throw std::runtime_error("Failed writing sandbox save"); }
        std::filesystem::rename(temporary, path);
        s.error.clear(); return true;
    } catch (const std::exception& error) { s.error = error.what(); return false; }
}

bool SandboxModule::Load(const std::filesystem::path& path) {
    auto& s = *impl_;
    try {
        if (!IsStarted() || s.stepping) throw std::runtime_error("Cannot load stopped or updating sandbox");
        const auto size = std::filesystem::file_size(path);
        const std::size_t expected = 55 + s.grid->cells.size() * 16 + s.grid->air.size() * 12 + s.chunks.size();
        if (size != expected) throw std::runtime_error("Sandbox save size does not match this grid");
        Bytes in; in.data.resize(std::size_t(size));
        { std::ifstream file(path, std::ios::binary);
          if (!file.read(reinterpret_cast<char*>(in.data.data()), std::streamsize(size))) throw std::runtime_error("Cannot read sandbox save"); }
        if (in.Get(8) != SaveMagic) throw std::runtime_error("Unsupported sandbox save format");
        const auto version = in.Get(4);
        if (version != 1 && version != 2) throw std::runtime_error("Unsupported sandbox save version");
        const auto width = in.Get(4), height = in.Get(4);
        if (width != std::uint64_t(Config().width) || height != std::uint64_t(Config().height))
            throw std::runtime_error("Load requires matching grid dimensions");
        const auto seed = std::uint32_t(in.Get(4)); const auto tick = in.Get(8);
        const auto step = in.Double(); const auto catchup = unsigned(in.Get(4));
        const auto sleeping = in.Get(1), paused = in.Get(1); const auto accumulator = in.Double(); const auto fieldActive = in.Get(1);
        if (!std::isfinite(step) || step < 1e-5 || step > 1 || catchup < 1 || catchup > 64 ||
            sleeping > 1 || paused > 1 || fieldActive > 1 || !std::isfinite(accumulator) || accumulator < 0 || accumulator >= step)
            throw std::runtime_error("Invalid saved simulation settings");
        std::vector<Cell> cells(s.grid->cells.size());
        Statistics stats; stats.tick = tick; stats.totalChunks = s.chunks.size();
        for (auto& cell : cells) {
            cell.material = static_cast<Material>(in.Get(1)); cell.variant = std::uint8_t(in.Get(1)); cell.life = std::uint16_t(in.Get(2));
            cell.temperature = in.Float(); cell.stamp = std::uint32_t(in.Get(4)); cell.charge = std::uint16_t(in.Get(2)); cell.flags = std::uint16_t(in.Get(2));
            if (!IsMaterial(cell.material) || !ValidTemperature(cell.temperature) || cell.charge > 24)
                throw std::runtime_error("Invalid saved particle");
            if (version == 1 && cell.material >= Material::Soil) throw std::runtime_error("Invalid legacy material ID");
            if (cell.material == Material::Clone) {
                const auto target = static_cast<Material>(cell.flags >> 8);
                if (!IsMaterial(target) || target == Material::Wall || IsDevice(target))
                    throw std::runtime_error("Invalid saved clone target");
            }
            ++stats.counts[std::size_t(cell.material)];
        }
        std::vector<AirCell> air(s.grid->air.size());
        for (auto& node : air) {
            node.pressure = in.Float(); node.vx = in.Float(); node.vy = in.Float();
            if (!std::isfinite(node.pressure) || !std::isfinite(node.vx) || !std::isfinite(node.vy) ||
                std::abs(node.pressure) > 20 || std::abs(node.vx) > 4 || std::abs(node.vy) > 4)
                throw std::runtime_error("Invalid saved air field");
        }
        std::vector<std::uint8_t> awake(s.chunks.size());
        for (auto& value : awake) { value = std::uint8_t(in.Get(1)); if (value > 4) throw std::runtime_error("Invalid saved chunk"); stats.activeChunks += value > 0; }
        if (in.offset != in.data.size()) throw std::runtime_error("Trailing save data");
        // Commit only after every byte is validated. GPU grid dimensions stay stable.
        s.grid->cells.swap(cells); s.grid->air.swap(air);
        std::fill(s.grid->nextAir.begin(), s.grid->nextAir.end(), AirCell{});
        for (std::size_t i = 0; i < awake.size(); ++i) s.chunks[i]->awake = awake[i];
        stats.particles = s.grid->cells.size() - stats.counts[0]; s.stats = stats;
        s.config.seed = seed; s.config.stepSeconds = step; s.config.maxCatchUpSteps = catchup; s.config.sleeping = sleeping != 0;
        s.paused = paused != 0; s.accumulator = accumulator; s.fieldActive = fieldActive != 0; s.context.frameIndex = tick;
        ++s.revision; s.error.clear(); return true;
    } catch (const std::exception& error) { s.error = error.what(); return false; }
}
} // namespace PixelSandbox
