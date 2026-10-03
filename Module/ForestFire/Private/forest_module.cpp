#include "ForestFire/Public/forest_module.h"
#include "ForestFire/Systems/forest_systems.h"
#include <algorithm>
#include <cmath>

namespace ForestFire {
namespace {

bool IsState(CellState state) {
    return state == CellState::Empty || state == CellState::Tree || state == CellState::Burning;
}
bool IsStep(double seconds) {
    return std::isfinite(seconds) && seconds >= 1e-6 && seconds <= 60.0;
}
struct ExecutionGuard {
    bool& flag;
    explicit ExecutionGuard(bool& value) : flag(value) { flag = true; }
    ~ExecutionGuard() { flag = false; }
};

} // namespace

ForestModule::ForestModule(SimulationConfig config) : config_(config), random_(config.seed) {
    pipeline_.Add<EvaluateSystem>();
    pipeline_.Add<CommitSystem>();
    pipeline_.RunBefore<EvaluateSystem, CommitSystem>();
}

void ForestModule::RequireConfigurable() const {
    if (started_ || executing_) throw std::logic_error("ForestFire systems must be configured while stopped");
}

void ForestModule::ValidateConfig() const {
    if (config_.width == 0 || config_.height == 0 || config_.width > 2048 || config_.height > 2048 ||
        std::uint64_t(config_.width) * config_.height > 1048576)
        throw std::invalid_argument("ForestFire grid dimensions must be 1..2048 with at most 1048576 cells");
    for (double probability : {config_.initialTreeDensity, config_.growthProbability,
                               config_.lightningProbability, config_.spreadProbability}) {
        if (!std::isfinite(probability) || probability < 0 || probability > 1)
            throw std::invalid_argument("ForestFire probabilities must be finite and in [0,1]");
    }
    if (config_.burnTicks == 0 || config_.burnTicks > 1000000)
        throw std::invalid_argument("ForestFire burnTicks must be 1..1000000");
    if (!IsStep(config_.stepSeconds))
        throw std::invalid_argument("ForestFire stepSeconds must be finite and in [0.000001,60]");
    if (config_.maxCatchUpSteps == 0 || config_.maxCatchUpSteps > 1024)
        throw std::invalid_argument("ForestFire maxCatchUpSteps must be 1..1024");
    if (config_.boundary != BoundaryMode::Finite && config_.boundary != BoundaryMode::Wrap)
        throw std::invalid_argument("ForestFire boundary mode is invalid");
    if (config_.neighborhood != Neighborhood::Four && config_.neighborhood != Neighborhood::Eight)
        throw std::invalid_argument("ForestFire neighborhood is invalid");
}

bool ForestModule::Startup() {
    if (executing_) return false;
    if (started_) return true;
    try {
        ExecutionGuard guard(executing_);
        ValidateConfig();
        RegisterComponents();
        scene_ = std::make_unique<ECS::Core::Scene>();
        auto description = scene_->CreateArchTypeDescription();
        description->AddComponentArray<CellPosition>();
        description->AddComponentArray<Cell>();
        description->AddComponentArray<NextCell>();
        for (const auto& addComponent : cellComponentFactories_) addComponent(*description.Get());
        archetype_ = scene_->CreateArchType(description, 256);
        const std::size_t count = std::size_t(config_.width) * config_.height;
        entities_.reserve(count);
        for (auto entity : scene_->CreateEntities(archetype_, count)) entities_.push_back(entity.GetID());
        if (entities_.size() != count) throw std::runtime_error("ForestFire grid entity allocation failed");
        for (std::uint32_t y = 0; y < config_.height; ++y) {
            for (std::uint32_t x = 0; x < config_.width; ++x) {
                auto* position = scene_->GetActiveComponent<CellPosition>(entities_[std::size_t(y) * config_.width + x]).Get();
                if (!position) throw std::runtime_error("ForestFire grid position component is unavailable");
                position->x = x;
                position->y = y;
            }
        }
        BuildNeighbors();
        Populate();
        context_.scene = scene_.get();
        context_.SetService(this);
        if (!pipeline_.Start(context_)) throw std::runtime_error("ForestFire system pipeline failed to start");
        // Extension startup can initialize current cells or relocate components.
        // Publish statistics for the actual initialized grid before edits begin.
        RefreshViews();
        RefreshStatistics();
        started_ = true;
        error_.clear();
        return true;
    } catch (const std::exception& error) {
        error_ = error.what();
        Shutdown();
        return false;
    }
}

void ForestModule::Shutdown() {
    if (executing_) throw std::logic_error("ForestFire Shutdown cannot run inside a system callback");
    ExecutionGuard guard(executing_);
    pipeline_.Stop(context_);
    views_.clear();
    neighbors_.clear();
    entities_.clear();
    archetype_ = nullptr;
    scene_.reset();
    context_.scene = nullptr;
    context_.frameIndex = 0;
    statistics_ = {};
    accumulator_ = 0;
    started_ = false;
    paused_ = false;
}

void ForestModule::BuildNeighbors() {
    neighbors_.assign(entities_.size(), {});
    const int width = static_cast<int>(config_.width), height = static_cast<int>(config_.height);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const auto own = std::size_t(y) * config_.width + x;
        auto& result = neighbors_[own];
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            if (config_.neighborhood == Neighborhood::Four && dx != 0 && dy != 0) continue;
            int nx = x + dx, ny = y + dy;
            if (config_.boundary == BoundaryMode::Wrap) {
                nx = (nx + width) % width;
                ny = (ny + height) % height;
            } else if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
            const auto index = std::size_t(ny) * config_.width + nx;
            if (index == own || std::find(result.indices.begin(), result.indices.begin() + result.count, index)
                                    != result.indices.begin() + result.count) continue;
            result.indices[result.count++] = index;
        }
    }
}

void ForestModule::RefreshViews() {
    views_.clear();
    views_.reserve(entities_.size());
    for (auto entity : entities_) {
        auto* current = scene_->GetActiveComponent<Cell>(entity).Get();
        auto* next = scene_->GetActiveComponent<NextCell>(entity).Get();
        if (!current || !next) throw std::logic_error("ForestFire grid entities and required components must remain alive");
        views_.push_back({current, next});
    }
}

bool ForestModule::Chance(double probability) {
    if (probability <= 0) return false;
    if (probability >= 1) return true;
    // Explicit conversion avoids implementation-specific distribution mappings.
    return static_cast<double>(random_()) / 4294967296.0 < probability;
}

void ForestModule::Populate() {
    random_.seed(config_.seed);
    accumulator_ = 0;
    context_.frameIndex = 0;
    statistics_ = {};
    RefreshViews();
    for (auto view : views_) {
        view.current->state = Chance(config_.initialTreeDensity) ? CellState::Tree : CellState::Empty;
        view.current->burnTicksRemaining = 0;
        view.next->state = view.current->state;
        view.next->burnTicksRemaining = 0;
    }
    RefreshStatistics();
}

bool ForestModule::Reset() { return Reset(config_.seed); }

bool ForestModule::Reset(std::uint32_t seed) {
    if (!started_ || executing_) return false;
    config_.seed = seed;
    Populate();
    return true;
}

void ForestModule::Evaluate() {
    RefreshViews();
    for (std::size_t index = 0; index < views_.size(); ++index) {
        const auto& current = *views_[index].current;
        auto& next = *views_[index].next;
        next.state = current.state;
        next.burnTicksRemaining = 0;
        switch (current.state) {
        case CellState::Empty:
            if (Chance(config_.growthProbability)) next.state = CellState::Tree;
            break;
        case CellState::Tree: {
            bool ignite = false;
            for (std::size_t i = 0; i < neighbors_[index].count; ++i) {
                if (views_[neighbors_[index].indices[i]].current->state == CellState::Burning &&
                    Chance(config_.spreadProbability)) { ignite = true; break; }
            }
            if (ignite || Chance(config_.lightningProbability)) {
                next.state = CellState::Burning;
                next.burnTicksRemaining = config_.burnTicks;
            }
            break;
        }
        case CellState::Burning:
            if (current.burnTicksRemaining > 1) next.burnTicksRemaining = current.burnTicksRemaining - 1;
            else next.state = CellState::Empty;
            break;
        default:
            throw std::logic_error("ForestFire Cell has an invalid state");
        }
    }
}

void ForestModule::Commit() {
    RefreshViews();
    // Validate the complete destination before writing any current state.
    for (auto view : views_) {
        if (!IsState(view.next->state) ||
            (view.next->state == CellState::Burning && view.next->burnTicksRemaining == 0))
            throw std::logic_error("ForestFire NextCell must have a valid state and positive fire lifetime");
    }
    for (auto view : views_) {
        view.current->state = view.next->state;
        view.current->burnTicksRemaining = view.next->state == CellState::Burning ? view.next->burnTicksRemaining : 0;
    }
}

void ForestModule::RefreshStatistics() {
    statistics_.empty = statistics_.trees = statistics_.burning = 0;
    for (auto view : views_) {
        switch (view.current->state) {
        case CellState::Empty: ++statistics_.empty; break;
        case CellState::Tree: ++statistics_.trees; break;
        case CellState::Burning: ++statistics_.burning; break;
        default: throw std::logic_error("ForestFire Cell has an invalid state");
        }
    }
}

void ForestModule::FixedTick() {
    if (!started_ || executing_) return;
    ExecutionGuard guard(executing_);
    context_.deltaSeconds = config_.stepSeconds;
    ++context_.frameIndex;
    pipeline_.Tick(context_);
    // PostUpdate extensions may have changed or relocated components.
    RefreshViews();
    ++statistics_.tick;
    RefreshStatistics();
}

void ForestModule::Advance(double seconds) {
    if (!started_ || paused_ || executing_ || !std::isfinite(seconds) || seconds < 0) return;
    const double step = config_.stepSeconds;
    const double budget = step * config_.maxCatchUpSteps;
    // Bound arithmetic even for DBL_MAX elapsed time, preserving fractional time.
    accumulator_ += seconds > budget ? budget + std::fmod(seconds, step) : seconds;
    std::uint32_t steps = 0;
    const double tolerance = step * 1e-10;
    while (steps < config_.maxCatchUpSteps && accumulator_ + tolerance >= step) {
        FixedTick();
        accumulator_ = std::max(0.0, accumulator_ - step);
        ++steps;
    }
    if (accumulator_ + tolerance >= step) {
        accumulator_ = std::fmod(accumulator_, step);
        if (accumulator_ + tolerance >= step) accumulator_ = 0;
    }
}

void ForestModule::SetPaused(bool paused) noexcept {
    if (executing_ || paused_ == paused) return;
    paused_ = paused;
    accumulator_ = 0;
}

bool ForestModule::SetStepSeconds(double seconds) {
    if (executing_ || !IsStep(seconds)) return false;
    config_.stepSeconds = seconds;
    accumulator_ = 0;
    return true;
}

std::size_t ForestModule::Index(std::uint32_t x, std::uint32_t y) const {
    if (!scene_ || entities_.size() != std::size_t(config_.width) * config_.height ||
        x >= config_.width || y >= config_.height)
        throw std::out_of_range("ForestFire cell coordinate is unavailable");
    return std::size_t(y) * config_.width + x;
}

ECS::EntityID ForestModule::EntityAt(std::uint32_t x, std::uint32_t y) const {
    return entities_[Index(x, y)];
}

std::vector<ECS::EntityID> ForestModule::Neighbors(std::uint32_t x, std::uint32_t y) const {
    const auto& neighbors = neighbors_[Index(x, y)];
    std::vector<ECS::EntityID> result;
    result.reserve(neighbors.count);
    for (std::size_t i = 0; i < neighbors.count; ++i) result.push_back(entities_[neighbors.indices[i]]);
    return result;
}

bool ForestModule::SetCell(std::uint32_t x, std::uint32_t y, CellState state) {
    if (!started_ || executing_ || x >= config_.width || y >= config_.height || !IsState(state)) return false;
    auto entity = entities_[std::size_t(y) * config_.width + x];
    auto* current = scene_->GetActiveComponent<Cell>(entity).Get();
    auto* next = scene_->GetActiveComponent<NextCell>(entity).Get();
    if (!current || !next) throw std::logic_error("ForestFire grid components are unavailable");
    auto count = [&](CellState value) -> std::size_t& {
        switch (value) {
        case CellState::Empty: return statistics_.empty;
        case CellState::Tree: return statistics_.trees;
        case CellState::Burning: return statistics_.burning;
        default: throw std::logic_error("ForestFire Cell has an invalid state");
        }
    };
    --count(current->state);
    ++count(state);
    current->state = next->state = state;
    current->burnTicksRemaining = next->burnTicksRemaining = state == CellState::Burning ? config_.burnTicks : 0;
    return true;
}

bool ForestModule::Ignite(std::uint32_t x, std::uint32_t y) {
    if (!started_ || executing_ || x >= config_.width || y >= config_.height) return false;
    auto* cell = scene_->GetActiveComponent<Cell>(entities_[std::size_t(y) * config_.width + x]).Get();
    return cell && cell->state == CellState::Tree && SetCell(x, y, CellState::Burning);
}

std::vector<CellSnapshot> ForestModule::Snapshot() const {
    std::vector<CellSnapshot> result;
    if (!scene_) return result;
    result.reserve(entities_.size());
    for (std::size_t i = 0; i < entities_.size(); ++i) {
        const auto* cell = scene_->GetActiveComponent<Cell>(entities_[i]).Get();
        if (!cell) throw std::logic_error("ForestFire grid components are unavailable");
        result.push_back({static_cast<std::uint32_t>(i % config_.width), static_cast<std::uint32_t>(i / config_.width),
                          cell->state, cell->burnTicksRemaining});
    }
    return result;
}

} // namespace ForestFire
