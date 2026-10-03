#include "ForestFire/Public/forest_module.h"
#include "ForestFire/Systems/forest_systems.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace ForestFire;

void Check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template<class Exception, class Function>
void ExpectThrows(Function&& function, const char* message) {
    try {
        function();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error(message);
}

SimulationConfig Config(std::uint32_t width = 4, std::uint32_t height = 1) {
    SimulationConfig config;
    config.width = width;
    config.height = height;
    config.initialTreeDensity = 0;
    config.growthProbability = 0;
    config.lightningProbability = 0;
    config.spreadProbability = 1;
    config.burnTicks = 1;
    config.neighborhood = Neighborhood::Four;
    config.boundary = BoundaryMode::Finite;
    config.stepSeconds = .125;
    return config;
}

void Start(ForestModule& forest) {
    const bool started = forest.Startup();
    Check(started, "Startup: " + forest.Error());
}

CellSnapshot At(const ForestModule& forest, std::uint32_t x, std::uint32_t y) {
    for (const auto& cell : forest.Snapshot()) {
        if (cell.x == x && cell.y == y) return cell;
    }
    throw std::runtime_error("snapshot is missing a grid coordinate");
}

bool Same(const std::vector<CellSnapshot>& left, const std::vector<CellSnapshot>& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (left[i].x != right[i].x || left[i].y != right[i].y ||
            left[i].state != right[i].state ||
            left[i].burnTicksRemaining != right[i].burnTicksRemaining) return false;
    }
    return true;
}

void CheckStats(const ForestModule& forest, std::size_t empty, std::size_t trees,
                std::size_t burning, std::uint64_t tick) {
    const auto& stats = forest.Stats();
    Check(stats.empty == empty && stats.trees == trees && stats.burning == burning,
          "population statistics must match the committed generation");
    Check(stats.tick == tick, "generation counter must advance once per fixed tick");
    Check(forest.Snapshot().size() == empty + trees + burning,
          "population counts must account for every grid cell");
}

void TestSynchronousPropagation() {
    auto config = Config();
    config.initialTreeDensity = 1;
    ForestModule forest(config);
    Start(forest);
    Check(forest.Ignite(0, 0), "a tree can be ignited");
    Check(!forest.Ignite(0, 0), "an already burning cell cannot be ignited again");
    forest.FixedTick();
    Check(At(forest, 0, 0).state == CellState::Empty, "old fire burns out");
    Check(At(forest, 1, 0).state == CellState::Burning, "old fire ignites adjacent tree");
    Check(At(forest, 2, 0).state == CellState::Tree &&
          At(forest, 3, 0).state == CellState::Tree,
          "new fire must not propagate twice within the same generation");
    CheckStats(forest, 1, 2, 1, 1);
    forest.FixedTick();
    Check(At(forest, 1, 0).state == CellState::Empty &&
          At(forest, 2, 0).state == CellState::Burning &&
          At(forest, 3, 0).state == CellState::Tree,
          "fire front advances exactly one cell per generation");
    CheckStats(forest, 2, 1, 1, 2);
    forest.FixedTick();
    forest.FixedTick();
    CheckStats(forest, 4, 0, 0, 4);
}

void TestBurnLifetimeAndEditing() {
    auto config = Config(2, 1);
    config.burnTicks = 3;
    ForestModule forest(config);
    Start(forest);
    Check(!forest.Ignite(0, 0), "empty ground cannot be ignited");
    Check(forest.SetCell(0, 0, CellState::Burning), "editor can create fire");
    Check(At(forest, 0, 0).burnTicksRemaining == 3, "new fire has full lifetime");
    for (std::uint32_t remaining = 2; remaining > 0; --remaining) {
        forest.FixedTick();
        const auto cell = At(forest, 0, 0);
        Check(cell.state == CellState::Burning && cell.burnTicksRemaining == remaining,
              "fire burns for its configured number of generations");
    }
    forest.FixedTick();
    Check(At(forest, 0, 0).state == CellState::Empty &&
          At(forest, 0, 0).burnTicksRemaining == 0,
          "exhausted fire becomes empty with no residual burn timer");
    Check(forest.SetCell(0, 0, CellState::Tree) && forest.Ignite(0, 0),
          "replanted tree can ignite after previous fire");
    Check(At(forest, 0, 0).burnTicksRemaining == 3, "reignition resets lifetime");
    Check(forest.SetCell(0, 0, CellState::Tree), "burning cell can be replanted");
    Check(At(forest, 0, 0).burnTicksRemaining == 0, "tree edit clears burn timer");
    CheckStats(forest, 1, 1, 0, 3);
}

void TestProbabilityExtremes() {
    auto config = Config(2, 2);
    config.growthProbability = 1;
    config.lightningProbability = 1;
    config.spreadProbability = 0;
    ForestModule cycle(config);
    Start(cycle);
    cycle.FixedTick();
    CheckStats(cycle, 0, 4, 0, 1);
    cycle.FixedTick();
    CheckStats(cycle, 0, 0, 4, 2);
    cycle.FixedTick();
    CheckStats(cycle, 4, 0, 0, 3);
    cycle.FixedTick();
    CheckStats(cycle, 0, 4, 0, 4);

    config = Config(3, 1);
    config.spreadProbability = 0;
    ForestModule still(config);
    Start(still);
    Check(still.SetCell(0, 0, CellState::Burning) && still.SetCell(1, 0, CellState::Tree),
          "configure spread-probability test");
    for (int i = 0; i < 8; ++i) still.FixedTick();
    CheckStats(still, 2, 1, 0, 8);
    Check(At(still, 1, 0).state == CellState::Tree,
          "zero spread and lightning probabilities preserve adjacent tree");
}

void TestBoundaryAndNeighborhood() {
    for (const auto boundary : {BoundaryMode::Finite, BoundaryMode::Wrap}) {
        auto config = Config(3, 1);
        config.boundary = boundary;
        ForestModule forest(config);
        Start(forest);
        forest.SetCell(0, 0, CellState::Burning);
        forest.SetCell(2, 0, CellState::Tree);
        forest.FixedTick();
        Check(At(forest, 2, 0).state ==
                  (boundary == BoundaryMode::Wrap ? CellState::Burning : CellState::Tree),
              "only wrapped boundary connects opposite grid edges");
    }
    for (const auto neighborhood : {Neighborhood::Four, Neighborhood::Eight}) {
        auto config = Config(3, 3);
        config.neighborhood = neighborhood;
        ForestModule forest(config);
        Start(forest);
        forest.SetCell(1, 1, CellState::Burning);
        forest.SetCell(0, 0, CellState::Tree);
        forest.SetCell(1, 0, CellState::Tree);
        forest.FixedTick();
        Check(At(forest, 1, 0).state == CellState::Burning,
              "both neighborhoods include orthogonal cells");
        Check(At(forest, 0, 0).state ==
                  (neighborhood == Neighborhood::Eight ? CellState::Burning : CellState::Tree),
              "only eight-cell neighborhood includes diagonals");
    }
}

void TestUniqueNeighbors() {
    for (const auto boundary : {BoundaryMode::Finite, BoundaryMode::Wrap}) {
        for (const auto neighborhood : {Neighborhood::Four, Neighborhood::Eight}) {
            for (const auto dimensions : {std::pair{1u, 1u}, {1u, 2u}, {1u, 3u},
                                          {2u, 1u}, {3u, 1u}, {2u, 2u}, {3u, 3u}}) {
                auto config = Config(dimensions.first, dimensions.second);
                config.boundary = boundary;
                config.neighborhood = neighborhood;
                ForestModule forest(config);
                Start(forest);
                for (std::uint32_t y = 0; y < config.height; ++y) {
                    for (std::uint32_t x = 0; x < config.width; ++x) {
                        std::set<ECS::EntityID> expected;
                        for (int dy = -1; dy <= 1; ++dy) {
                            for (int dx = -1; dx <= 1; ++dx) {
                                if (dx == 0 && dy == 0) continue;
                                if (neighborhood == Neighborhood::Four && dx != 0 && dy != 0) continue;
                                int nx = static_cast<int>(x) + dx;
                                int ny = static_cast<int>(y) + dy;
                                if (boundary == BoundaryMode::Wrap) {
                                    nx = (nx + static_cast<int>(config.width)) % static_cast<int>(config.width);
                                    ny = (ny + static_cast<int>(config.height)) % static_cast<int>(config.height);
                                }
                                if (nx < 0 || ny < 0 || nx >= static_cast<int>(config.width) ||
                                    ny >= static_cast<int>(config.height) ||
                                    (nx == static_cast<int>(x) && ny == static_cast<int>(y))) continue;
                                expected.insert(forest.EntityAt(nx, ny));
                            }
                        }
                        const auto neighbors = forest.Neighbors(x, y);
                        const std::set<ECS::EntityID> actual(neighbors.begin(), neighbors.end());
                        Check(actual == expected && actual.size() == neighbors.size(),
                              "neighbors must be unique, exclude self and respect narrow-grid topology");
                    }
                }
            }
        }
    }
}

void TestSeedAndReset() {
    auto config = Config(12, 9);
    config.seed = 1729;
    config.initialTreeDensity = .53;
    config.growthProbability = .17;
    config.lightningProbability = .11;
    config.spreadProbability = .37;
    config.burnTicks = 3;
    ForestModule first(config), second(config);
    Start(first);
    Start(second);
    const auto initial = first.Snapshot();
    const auto entity = first.EntityAt(3, 2);
    Check(Same(initial, second.Snapshot()), "same seed produces identical starting forest");
    for (int i = 0; i < 20; ++i) {
        first.FixedTick();
        second.FixedTick();
        Check(Same(first.Snapshot(), second.Snapshot()),
              "same seed and actions produce identical stochastic evolution");
    }
    const auto evolved = first.Snapshot();
    first.SetPaused(true);
    Check(first.Reset(), "reset started forest");
    Check(first.IsPaused() && first.Stats().tick == 0, "reset preserves pause and clears generation");
    Check(first.EntityAt(3, 2) == entity, "reset keeps cell entities stable for extensions");
    Check(Same(initial, first.Snapshot()), "reset restores original generated forest");
    for (int i = 0; i < 20; ++i) first.FixedTick();
    Check(Same(evolved, first.Snapshot()), "reset restores random sequence as well as cell state");
    Check(first.Reset(2718), "reset with a new seed");
    Check(!Same(initial, first.Snapshot()), "different seed changes generated forest");
    const auto reseeded = first.Snapshot();
    first.FixedTick();
    Check(first.Reset() && Same(reseeded, first.Snapshot()), "new seed becomes reset baseline");
}

void TestFixedTimestep() {
    auto config = Config(1, 1);
    config.maxCatchUpSteps = 3;
    ForestModule forest(config);
    Start(forest);
    forest.Advance(.0625);
    Check(forest.Stats().tick == 0, "substep time is accumulated");
    forest.SetPaused(false);
    forest.Advance(.0625);
    Check(forest.Stats().tick == 1, "unchanged pause setting preserves accumulated partial time");
    forest.SetPaused(true);
    forest.Advance(100);
    Check(forest.Stats().tick == 1, "paused wall time produces no generations");
    forest.FixedTick();
    Check(forest.Stats().tick == 2 && forest.IsPaused(), "manual step works while paused");
    forest.SetPaused(false);
    forest.Advance(0);
    Check(forest.Stats().tick == 2, "paused time must not be replayed when resumed");
    forest.Advance(1);
    Check(forest.Stats().tick == 5, "catch-up work is limited per call");
    forest.Advance(0);
    Check(forest.Stats().tick == 5, "discarded catch-up backlog is not replayed");
    forest.Advance(.0625);
    Check(forest.SetStepSeconds(.25), "valid simulation speed change");
    forest.Advance(.1875);
    Check(forest.Stats().tick == 5, "speed change clears old partial step");
    forest.Advance(.0625);
    Check(forest.Stats().tick == 6, "new fixed step duration is used");
    forest.Advance(.125);
    Check(forest.Reset(), "reset for accumulator test");
    forest.Advance(.125);
    Check(forest.Stats().tick == 0, "reset clears accumulated partial time");
    forest.Advance(.125);
    Check(forest.Stats().tick == 1, "simulation continues after accumulator reset");
}

void TestInvalidConfiguration() {
    const auto invalid = [](const std::function<void(SimulationConfig&)>& mutate) {
        auto config = Config();
        mutate(config);
        ForestModule forest(config);
        Check(!forest.Startup(), "invalid configuration must reject Startup");
        Check(!forest.IsStarted() && !forest.Error().empty(), "failed Startup reports an error");
        forest.Shutdown();
    };
    invalid([](auto& c) { c.width = 0; });
    invalid([](auto& c) { c.height = 0; });
    invalid([](auto& c) { c.width = 2049; });
    invalid([](auto& c) { c.width = 1025; c.height = 1024; });
    invalid([](auto& c) { c.burnTicks = 0; });
    invalid([](auto& c) { c.burnTicks = 1000001; });
    invalid([](auto& c) { c.maxCatchUpSteps = 0; });
    invalid([](auto& c) { c.maxCatchUpSteps = 1025; });
    invalid([](auto& c) { c.boundary = static_cast<BoundaryMode>(99); });
    invalid([](auto& c) { c.neighborhood = static_cast<Neighborhood>(99); });
    for (auto field : {&SimulationConfig::initialTreeDensity, &SimulationConfig::growthProbability,
                       &SimulationConfig::lightningProbability, &SimulationConfig::spreadProbability}) {
        for (double value : {-0.01, 1.01, std::numeric_limits<double>::quiet_NaN(),
                              std::numeric_limits<double>::infinity()}) {
            invalid([=](auto& c) { c.*field = value; });
        }
    }
    for (double value : {0., -1., 1e-7, 61., std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::infinity()}) {
        invalid([=](auto& c) { c.stepSeconds = value; });
    }
}

void TestInvalidOperationsAndLifecycle() {
    ForestModule forest(Config(2, 2));
    Check(!forest.IsStarted() && !forest.Reset() && !forest.Reset(99),
          "stopped module cannot reset");
    Check(!forest.SetCell(0, 0, CellState::Tree) && !forest.Ignite(0, 0),
          "stopped module rejects cell edits");
    forest.Advance(1);
    forest.FixedTick();
    ExpectThrows<std::out_of_range>([&] { forest.EntityAt(0, 0); },
                                   "stopped module cannot expose entities");
    Start(forest);
    Check(forest.IsStarted() && forest.Scene() != nullptr, "startup creates ECS scene");
    const auto entity = forest.EntityAt(1, 1);
    Start(forest);
    Check(forest.EntityAt(1, 1) == entity, "startup is idempotent");
    const auto before = forest.Snapshot();
    Check(!forest.SetCell(2, 0, CellState::Tree) && !forest.SetCell(0, 2, CellState::Tree) &&
          !forest.SetCell(0, 0, static_cast<CellState>(99)) && !forest.Ignite(99, 99),
          "invalid cell edits are rejected");
    ExpectThrows<std::out_of_range>([&] { forest.EntityAt(2, 0); }, "invalid entity lookup throws");
    ExpectThrows<std::out_of_range>([&] { forest.Neighbors(0, 2); }, "invalid neighbor lookup throws");
    for (double seconds : {-1., std::numeric_limits<double>::quiet_NaN(),
                            std::numeric_limits<double>::infinity()}) forest.Advance(seconds);
    for (double seconds : {0., -1., 1e-7, 61., std::numeric_limits<double>::quiet_NaN(),
                            std::numeric_limits<double>::infinity()}) {
        Check(!forest.SetStepSeconds(seconds), "invalid fixed step duration is rejected");
    }
    Check(Same(before, forest.Snapshot()) && forest.Stats().tick == 0,
          "rejected input cannot change the forest or advance time");
    forest.Advance(.125);
    Check(forest.Stats().tick == 1, "invalid speed change preserves valid timestep");
    forest.Shutdown();
    forest.Shutdown();
    Check(!forest.IsStarted() && forest.Scene() == nullptr, "shutdown is idempotent and releases scene");
    Start(forest);
    CheckStats(forest, 4, 0, 0, 0);
}

struct ExtensionProbe {
    int starts = 0;
    int ticks = 0;
    int ends = 0;
    std::uint64_t lastFrame = 0;
};

struct Fuel : ForestComponent<Fuel> {
    int remaining = 4;
};

class ForceIgnitionSystem final : public ECS::System::System {
public:
    ForceIgnitionSystem() : System("TestForceIgnition", ECS::System::Phase::Update) {
        Writes<NextCell>();
        Writes<Fuel>();
    }
    void OnTick() override {
        auto* context = GetContext();
        auto* forest = context->GetService<ForestModule>();
        Check(forest && context->scene == forest->Scene(), "extension receives forest and scene services");
        auto* next = context->scene->GetActiveComponent<NextCell>(forest->EntityAt(0, 0)).Get();
        auto* fuel = context->scene->GetActiveComponent<Fuel>(forest->EntityAt(0, 0)).Get();
        Check(next != nullptr && next->state == CellState::Tree,
              "custom rule executes after built-in evaluation");
        Check(fuel && fuel->remaining > 0, "custom rule can access its additional cell component");
        --fuel->remaining;
        next->state = CellState::Burning;
        next->burnTicksRemaining = 2;
    }
};

class ObserveCommittedSystem final : public ECS::System::System {
public:
    explicit ObserveCommittedSystem(ExtensionProbe& probe)
        : System("TestObserveCommitted", ECS::System::Phase::PostUpdate), probe_(probe) {
        Reads<Cell>();
        Reads<CellPosition>();
    }
    bool OnStart() override {
        auto* context = GetContext();
        Check(context && context->scene && context->GetService<ForestModule>(),
              "extension startup receives valid ECS context");
        auto* forest = context->GetService<ForestModule>();
        auto* fuel = context->scene->GetActiveComponent<Fuel>(forest->EntityAt(0, 0)).Get();
        Check(fuel && fuel->remaining == 4, "custom component exists and is initialized before system startup");
        ++probe_.starts;
        return true;
    }
    void OnTick() override {
        auto* context = GetContext();
        auto* forest = context->GetService<ForestModule>();
        const auto entity = forest->EntityAt(0, 0);
        const auto* cell = context->scene->GetActiveComponent<Cell>(entity).Get();
        const auto* position = context->scene->GetActiveComponent<CellPosition>(entity).Get();
        Check(cell && cell->state == CellState::Burning && cell->burnTicksRemaining == 2,
              "post-update extension sees custom rule committed to ECS cell");
        Check(position && position->x == 0 && position->y == 0,
              "each grid cell has an ECS coordinate component");
        Check(context->deltaSeconds == .125, "extensions receive fixed timestep");
        ++probe_.ticks;
        probe_.lastFrame = context->frameIndex;
    }
    void OnEnd() override { ++probe_.ends; }
private:
    ExtensionProbe& probe_;
};

void TestEcsExtension() {
    auto config = Config(1, 1);
    config.growthProbability = 1;
    config.burnTicks = 2;
    ExtensionProbe probe;
    ForestModule forest(config);
    ExpectThrows<std::invalid_argument>([&] { forest.AddCellComponent<Fuel>(""); },
                                       "additional component registration requires a key");
    forest.AddCellComponent<Fuel>("forest_fire_test_fuel");
    forest.AddSystem<ForceIgnitionSystem>();
    forest.AddSystem<ObserveCommittedSystem>(probe);
    Check(forest.RunBefore<EvaluateSystem, ForceIgnitionSystem>() &&
          forest.RunBefore<ForceIgnitionSystem, CommitSystem>(),
          "custom transition rule can be ordered between evaluation and commit");
    Start(forest);
    Check(probe.starts == 1, "extension starts with module");
    ExpectThrows<std::logic_error>([&] { forest.AddSystem<ForceIgnitionSystem>(); },
                                  "running pipeline rejects structural system changes");
    ExpectThrows<std::logic_error>([&] { forest.RunBefore<EvaluateSystem, CommitSystem>(); },
                                  "running pipeline rejects dependency changes");
    ExpectThrows<std::logic_error>([&] { forest.AddCellComponent<Fuel>("forest_fire_test_fuel"); },
                                  "running module rejects cell archetype changes");
    forest.FixedTick();
    Check(probe.ticks == 1 && probe.lastFrame == 1, "extension receives first simulation generation");
    CheckStats(forest, 0, 0, 1, 1);
    Check(forest.Reset(), "reset with registered extensions");
    Check(forest.Scene()->GetActiveComponent<Fuel>(forest.EntityAt(0, 0)).Get()->remaining == 3,
          "reset preserves additional component data");
    forest.FixedTick();
    Check(probe.starts == 1 && probe.ticks == 2 && probe.lastFrame == 1,
          "reset preserves registered systems and restarts generation counter");
    forest.Shutdown();
    Check(probe.ends == 1, "shutdown ends extension once");
    Start(forest);
    forest.FixedTick();
    forest.Shutdown();
    Check(probe.starts == 2 && probe.ticks == 3 && probe.ends == 2,
          "module and extension lifecycle can be restarted");
}

class InitializeTreeSystem final : public ECS::System::System {
public:
    InitializeTreeSystem() : System("TestInitializeTree", ECS::System::Phase::PreUpdate) {
        Writes<Cell>();
    }
    bool OnStart() override {
        auto* context = GetContext();
        auto* forest = context->GetService<ForestModule>();
        auto* cell = context->scene->GetActiveComponent<Cell>(forest->EntityAt(0, 0)).Get();
        if (!cell) return false;
        cell->state = CellState::Tree;
        cell->burnTicksRemaining = 0;
        return true;
    }
};

void TestStartupCellInitialization() {
    ForestModule forest(Config(2, 1));
    forest.AddSystem<InitializeTreeSystem>();
    Start(forest);
    Check(At(forest, 0, 0).state == CellState::Tree,
          "extension can initialize a current cell during startup");
    CheckStats(forest, 1, 1, 0, 0);
    Check(forest.SetCell(0, 0, CellState::Burning), "edit tree initialized by extension");
    CheckStats(forest, 1, 0, 1, 0);
    Check(forest.SetCell(0, 0, CellState::Empty), "erase fire initialized from extension tree");
    CheckStats(forest, 2, 0, 0, 0);
}

template<bool RejectFirst>
class LifecycleSystem final : public ECS::System::System {
public:
    explicit LifecycleSystem(ExtensionProbe& probe)
        : System(RejectFirst ? "TestRejectFirstStart" : "TestStartBeforeFailure",
                 RejectFirst ? ECS::System::Phase::Update : ECS::System::Phase::PreUpdate),
          probe_(probe) {}
    bool OnStart() override {
        ++probe_.starts;
        return !RejectFirst || probe_.starts > 1;
    }
    void OnTick() override { ++probe_.ticks; }
    void OnEnd() override { ++probe_.ends; }
private:
    ExtensionProbe& probe_;
};

void TestFailedSystemStartup() {
    ExtensionProbe preceding, rejecting;
    ForestModule forest(Config(2, 2));
    forest.AddSystem<LifecycleSystem<false>>(preceding);
    forest.AddSystem<LifecycleSystem<true>>(rejecting);
    Check(!forest.Startup() && !forest.IsStarted() && !forest.Error().empty(),
          "extension startup failure is reported by module");
    Check(forest.Scene() == nullptr && forest.Snapshot().empty(),
          "failed system startup releases partially initialized scene");
    Check(preceding.starts == 1 && preceding.ends == 1 &&
          rejecting.starts == 1 && rejecting.ends == 0,
          "failed startup unwinds previously started systems exactly once");
    Start(forest);
    Check(forest.Error().empty(), "successful retry clears previous startup error");
    forest.FixedTick();
    Check(preceding.starts == 2 && preceding.ticks == 1 &&
          rejecting.starts == 2 && rejecting.ticks == 1,
          "same module can retry system startup and run its pipeline");
    forest.Shutdown();
    Check(preceding.ends == 2 && rejecting.ends == 1,
          "retried pipeline shuts down every successfully started system");
}
} // namespace

int main() {
    const std::pair<const char*, void (*)()> tests[] = {
        {"synchronous propagation", TestSynchronousPropagation},
        {"burn lifetime and editing", TestBurnLifetimeAndEditing},
        {"probability extremes", TestProbabilityExtremes},
        {"boundary and neighborhood", TestBoundaryAndNeighborhood},
        {"unique neighbors on small grids", TestUniqueNeighbors},
        {"seed and reset", TestSeedAndReset},
        {"fixed timestep", TestFixedTimestep},
        {"invalid configuration", TestInvalidConfiguration},
        {"invalid operations and lifecycle", TestInvalidOperationsAndLifecycle},
        {"ECS extension", TestEcsExtension},
        {"startup cell initialization", TestStartupCellInitialization},
        {"failed system startup rollback", TestFailedSystemStartup},
    };
    int failures = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        }
    }
    std::cout << (failures ? "ForestFire tests failed" : "All ForestFire tests passed") << '\n';
    return failures ? 1 : 0;
}
