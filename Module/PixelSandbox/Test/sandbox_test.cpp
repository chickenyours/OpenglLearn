#include "PixelSandbox/Public/sandbox_module.h"
#include "PixelSandbox/Public/sandbox_palette.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {
using namespace PixelSandbox;

void Check(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

SimulationConfig Config(int width = 16, int height = 16, bool sleeping = true) {
    SimulationConfig config;
    config.width = width;
    config.height = height;
    config.seed = 827391;
    config.stepSeconds = 0.125;
    config.sleeping = sleeping;
    return config;
}

void Start(SandboxModule& world) {
    Check(world.Startup(), "Startup: " + world.Error());
    world.Clear();
}

void Step(SandboxModule& world, int count) {
    for (int i = 0; i < count; ++i) world.FixedTick();
}

std::array<std::size_t, MaterialCount> Counts(const SandboxModule& world) {
    std::array<std::size_t, MaterialCount> result{};
    for (int y = 0; y < world.Config().height; ++y)
        for (int x = 0; x < world.Config().width; ++x)
            ++result[static_cast<std::size_t>(world.At(x, y).material)];
    return result;
}

std::size_t Count(const SandboxModule& world, Material material) {
    return Counts(world)[static_cast<std::size_t>(material)];
}

void Fill(SandboxModule& world, Material material) {
    for (int y = 0; y < world.Config().height; ++y)
        for (int x = 0; x < world.Config().width; ++x)
            world.SetCell(x, y, material);
}

void TestSingleUpdateAndBounds() {
    SandboxModule world(Config(9, 12));
    Start(world);
    Check(world.SetCell(4, 1, Material::Sand), "valid edit accepted");
    world.FixedTick();
    Check(world.At(4, 2).material == Material::Sand, "sand falls one row per tick");
    Check(Count(world, Material::Sand) == 1, "movement preserves particle count");
    world.FixedTick();
    Check(world.At(4, 3).material == Material::Sand, "moved particle updates again next tick");
    Check(!world.SetCell(-1, 0, Material::Sand) && !world.SetCell(9, 0, Material::Sand),
          "out-of-bounds writes are rejected");
    Check(world.At(-1, 0).material == Material::Wall && world.At(9, 12).material == Material::Wall,
          "out-of-bounds reads are solid walls");
}

void TestDensityAndWalls() {
    SandboxModule world(Config(1, 10));
    Start(world);
    world.SetCell(0, 8, Material::Water);
    world.SetCell(0, 7, Material::Sand);
    Step(world, 12);
    Check(world.At(0, 9).material == Material::Sand, "sand sinks through water");
    Check(world.At(0, 8).material == Material::Water, "water displaced above sand");
    world.Clear();
    world.SetCell(0, 9, Material::Oil);
    world.SetCell(0, 8, Material::Water);
    Step(world, 12);
    Check(world.At(0, 9).material == Material::Water && world.At(0, 8).material == Material::Oil,
          "water sinks below oil");
    world.Clear();
    world.SetCell(0, 5, Material::Wall);
    world.SetCell(0, 1, Material::Sand);
    Step(world, 24);
    Check(world.At(0, 4).material == Material::Sand && world.At(0, 5).material == Material::Wall,
          "particles stop at immutable wall");
}

void TestConservationAndStatistics() {
    SandboxModule world(Config(32, 32));
    Start(world);
    for (int y = 1; y < 12; ++y)
        for (int x = 1; x < 31; ++x)
            if ((x + y) % 3 == 0)
                world.SetCell(x, y, std::array{Material::Sand, Material::Water, Material::Oil}[(x + 2 * y) % 3]);
    const auto before = Counts(world);
    for (int step = 0; step < 100; ++step) {
        world.FixedTick();
        Check(Counts(world) == before, "inert movement must conserve each material at tick " + std::to_string(step));
        Check(world.Stats().counts == before, "statistics agree with actual grid contents");
        Check(world.Stats().particles == 32u * 32u - before[0], "particle count excludes empty space");
    }
}

void TestExtensionAndNoCascade() {
    SandboxModule world(Config(14, 5));
    unsigned calls = 0;
    world.SetRule(Material::Sand, [&calls](SandboxModule& sim, int x, int y) {
        ++calls;
        sim.SetCell(x, y, Material::Empty);
        sim.SetCell(x + 1, y, Material::Sand);
        return true;
    });
    Start(world);
    world.SetCell(1, 2, Material::Sand);
    for (int tick = 1; tick <= 6; ++tick) {
        world.FixedTick();
        Check(calls == static_cast<unsigned>(tick), "newly written material must not cascade within a tick");
        Check(world.At(1 + tick, 2).material == Material::Sand, "registered rule controls one local move per tick");
    }
    Check(world.Scene() != nullptr, "running simulation owns an ECS scene");
    bool rejected = false;
    try { world.SetRule(Material::Sand, {}); } catch (const std::logic_error&) { rejected = true; }
    Check(rejected, "rules cannot be reconfigured while simulation runs");
}

void TestThermalPhases() {
    const std::array cases{
        std::tuple{Material::Water, 180.0f, Material::Steam},
        std::tuple{Material::Water, -40.0f, Material::Ice},
        std::tuple{Material::Ice, 40.0f, Material::Water},
        std::tuple{Material::Steam, 20.0f, Material::Water},
        std::tuple{Material::Sand, 1600.0f, Material::Glass},
        std::tuple{Material::Lava, 100.0f, Material::Stone}};
    for (const auto& [input, temperature, expected] : cases) {
        SandboxModule world(Config(3, 3));
        Start(world);
        Fill(world, Material::Wall);
        world.SetCell(1, 1, input, temperature);
        world.FixedTick();
        Check(world.At(1, 1).material == expected, std::string(Info(input).name) + " thermal transition");
    }
}

void TestLavaWaterAndSalt() {
    SandboxModule world(Config(5, 5));
    Start(world);
    Fill(world, Material::Wall);
    world.SetCell(2, 2, Material::Lava);
    world.SetCell(3, 2, Material::Water);
    world.FixedTick();
    Check(Count(world, Material::Lava) == 0 && Count(world, Material::Stone) == 1,
          "water quenches lava into stone");
    Check(Count(world, Material::Steam) == 1, "quenching creates steam");
    Fill(world, Material::Wall);
    world.SetCell(2, 2, Material::Salt);
    world.SetCell(3, 2, Material::Water);
    Step(world, 24);
    Check(Count(world, Material::Brine) == 1 && Count(world, Material::Salt) == 0,
          "salt dissolves into water to form brine");
}

void TestAcidCapacity() {
    SandboxModule world(Config(5, 5));
    Start(world);
    Fill(world, Material::Wall);
    world.SetCell(2, 2, Material::Acid);
    world.Edit(2, 2).life = 1;
    world.SetCell(2, 3, Material::Stone);
    Step(world, 200);
    Check(Count(world, Material::Stone) == 0, "acid dissolves adjacent susceptible solid");
    Check(Count(world, Material::Acid) == 0, "acid is exhausted after finite corrosion capacity");
    Check(Count(world, Material::Wall) == 23, "corrosion preserves walls");
    world.Clear();
    Fill(world, Material::Glass);
    world.SetCell(2, 2, Material::Acid);
    const auto acidLife = world.At(2, 2).life;
    Step(world, 24);
    Check(Count(world, Material::Glass) == 24 && world.At(2, 2).life == acidLife,
          "glass resists corrosion without consuming acid capacity");
}

void TestIgnitionAndExplosion() {
    SandboxModule world(Config(16, 16));
    Start(world);
    Fill(world, Material::Wall);
    world.SetCell(7, 7, Material::Wood, 550.0f);
    world.FixedTick();
    Check(world.At(7, 7).flags != 0 || world.At(7, 7).material != Material::Wood,
          "hot fuel ignites");
    world.Clear();
    world.SetCell(8, 8, Material::Gunpowder, 800.0f);
    world.FixedTick();
    Check(Count(world, Material::Gunpowder) == 0, "hot gunpowder detonates");
    Check(Count(world, Material::Fire) > 0 || world.PressureAt(8, 8) > 0,
          "explosion injects flame or pressure");
    world.Clear();
    world.SetCell(5, 5, Material::Wall);
    world.Explode(5, 5, 3, 15.0f);
    Check(world.At(5, 5).material == Material::Wall, "explosion preserves indestructible wall");
}

void TestConductorPropagation() {
    SandboxModule world(Config(14, 5));
    Start(world);
    Fill(world, Material::Wall);
    for (int x = 2; x <= 10; ++x) world.SetCell(x, 2, Material::Metal);
    world.SetCell(1, 2, Material::Spark);
    world.FixedTick();
    Check(world.At(10, 2).charge == 0, "electrical pulse does not traverse entire conductor in one tick");
    bool reachedEnd = false;
    for (int tick = 0; tick < 90; ++tick) {
        world.FixedTick();
        reachedEnd |= world.At(10, 2).charge > 0;
    }
    Check(reachedEnd, "electrical pulse eventually traverses conductor");
    Check(Count(world, Material::Metal) == 9, "conductors survive a pulse");
}

void TestSmallEdgesAndBrush() {
    for (const auto [width, height] : std::array{std::pair{1, 1}, std::pair{1, 9}, std::pair{9, 1}, std::pair{17, 19}}) {
        SandboxModule world(Config(width, height));
        Start(world);
        world.Paint(-1, -1, 2, Material::Sand);
        world.Stroke(-5, -5, width + 5, height + 5, 1, Material::Water);
        Step(world, 20);
        const auto pixels = world.Pixels();
        Check(pixels.size() == static_cast<std::size_t>(width * height * 4), "RGBA buffer exactly matches small world");
        Check(Counts(world) == world.Stats().counts, "edge edits preserve consistent material counts");
    }
}

void TestFixedTimeAndPause() {
    SandboxModule world(Config());
    Start(world);
    world.SetCell(4, 1, Material::Sand);
    world.Advance(0.0625);
    Check(world.Stats().tick == 0, "partial timestep is accumulated");
    world.Advance(0.0625);
    Check(world.Stats().tick == 1, "two partial updates yield one fixed tick");
    world.SetPaused(true);
    const auto pausedHash = world.StateHash();
    world.Advance(0.5);
    Check(world.StateHash() == pausedHash && world.Stats().tick == 1, "pause freezes automatic simulation");
    world.FixedTick();
    Check(world.Stats().tick == 2, "explicit single-step works while paused");
    world.SetPaused(false);
    world.Advance(0.125);
    Check(world.Stats().tick == 3, "resume does not replay paused wall time");
    world.Advance(100.0);
    Check(world.Stats().tick <= 3 + world.Config().maxCatchUpSteps, "catch-up work is bounded");
}

void TestDeterminism() {
    SandboxModule a(Config(48, 32)), b(Config(48, 32));
    Start(a);
    Start(b);
    a.LoadPreset(Preset::Falling);
    b.LoadPreset(Preset::Falling);
    for (int tick = 0; tick < 100; ++tick) {
        a.FixedTick();
        b.FixedTick();
        Check(a.StateHash() == b.StateHash(), "same seed and operations produce identical state");
    }
}

void TestSleepingAndWake() {
    SandboxModule world(Config(96, 48));
    Start(world);
    for (int x = 0; x < 96; ++x) world.SetCell(x, 47, Material::Wall);
    Step(world, 100);
    Check(world.Stats().visitedCells == 0, "quiet world stops visiting particle cells");
    world.SetCell(31, 4, Material::Sand);
    world.FixedTick();
    Check(world.At(31, 5).material == Material::Sand, "editing wakes a sleeping region");
    Check(world.TryMove(31, 5, 32, 5), "canonical move across chunk boundary succeeds");
    world.FixedTick();
    Check(world.At(32, 6).material == Material::Sand, "neighbor chunk wakes after cross-boundary move");
    Step(world, 100);
    Check(world.At(32, 46).material == Material::Sand, "woken particle settles above floor");
}

void TestSleepReference() {
    SandboxModule sleeping(Config(64, 48, true)), full(Config(64, 48, false));
    Start(sleeping);
    Start(full);
    for (int y = 40; y < 48; ++y)
        for (int x = 0; x < 64; ++x) {
            sleeping.SetCell(x, y, Material::Sand);
            full.SetCell(x, y, Material::Sand);
        }
    Step(sleeping, 80);
    Step(full, 80);
    Check(Counts(sleeping) == Counts(full), "sleeping/reference material populations agree");
    for (int y = 0; y < 48; ++y)
        for (int x = 0; x < 64; ++x) {
            const Cell& a = sleeping.At(x, y);
            const Cell& b = full.At(x, y);
            Check(a.material == b.material && a.temperature == b.temperature && a.life == b.life && a.charge == b.charge,
                  "sleeping/reference settled physical state agrees");
        }
    Check(sleeping.Stats().visitedCells < full.Stats().visitedCells, "sleeping reduces settled-world visits");
}

void CheckPhysicalState(const SandboxModule& a, const SandboxModule& b, const std::string& scenario) {
    Check(a.Stats().tick == b.Stats().tick, scenario + ": ticks agree");
    for (int y = 0; y < a.Config().height; ++y) {
        for (int x = 0; x < a.Config().width; ++x) {
            const auto& left = a.At(x, y);
            const auto& right = b.At(x, y);
            Check(left.material == right.material && left.variant == right.variant &&
                  left.temperature == right.temperature && left.life == right.life &&
                  left.charge == right.charge && left.flags == right.flags,
                  scenario + ": sleeping/full physical mismatch at tick " + std::to_string(a.Stats().tick) +
                  " cell " + std::to_string(x) + "," + std::to_string(y) +
                  " temperatures " + std::to_string(left.temperature) + "/" + std::to_string(right.temperature));
        }
    }
}

void TestConfigurationAndInputValidation() {
    std::vector<SimulationConfig> invalid;
    for (int dimension : {-1, 0, 1025, std::numeric_limits<int>::max()}) {
        auto width = Config(); width.width = dimension; invalid.push_back(width);
        auto height = Config(); height.height = dimension; invalid.push_back(height);
    }
    for (double step : {-1.0, 0.0, 0.000001, 1.01, std::numeric_limits<double>::infinity(),
                        std::numeric_limits<double>::quiet_NaN()}) {
        auto config = Config(); config.stepSeconds = step; invalid.push_back(config);
    }
    for (unsigned catchUp : {0u, 65u, std::numeric_limits<unsigned>::max()}) {
        auto config = Config(); config.maxCatchUpSteps = catchUp; invalid.push_back(config);
    }
    for (const auto& config : invalid) {
        SandboxModule world(config);
        Check(!world.Startup() && !world.IsStarted(), "invalid config fails before creating a running world");
        Check(!world.Error().empty() && world.Scene() == nullptr, "invalid config reports error and releases ECS scene");
        world.Shutdown();
    }
    auto valid = Config(1024, 1);
    valid.stepSeconds = 0.00001;
    valid.maxCatchUpSteps = 64;
    SandboxModule boundary(valid);
    Start(boundary);
    boundary.FixedTick();
    Check(boundary.Stats().tick == 1, "valid configuration boundary starts and steps");

    SandboxModule world(Config());
    Start(world);
    world.SetCell(4, 4, Material::Metal);
    const auto before = world.StateHash();
    for (float temperature : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                              -std::numeric_limits<float>::infinity(), -274.0f, 4001.0f})
        Check(!world.SetTemperature(4, 4, temperature), "nonfinite or out-of-range temperature rejected");
    Check(!world.SetCell(4, 4, Material::Fire, std::numeric_limits<float>::infinity()), "infinite material temperature rejected");
    Check(!world.SetCell(4, 4, Material::Count), "invalid material enum rejected");
    world.AddPressure(4, 4, std::numeric_limits<float>::quiet_NaN());
    world.AddPressure(4, 4, std::numeric_limits<float>::infinity());
    world.Explode(4, 4, 4, std::numeric_limits<float>::quiet_NaN());
    Check(world.StateHash() == before, "invalid edits do not modify simulation state");
    world.Advance(0.0625);
    for (double elapsed : {-1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        world.Advance(elapsed);
    Check(world.Stats().tick == 0, "invalid elapsed time is ignored");
    world.Advance(0.0625);
    Check(world.Stats().tick == 1, "invalid elapsed time preserves valid accumulated partial tick");
    Check(world.SetCell(1, 1, Material::Water, std::numeric_limits<float>::quiet_NaN()) &&
          world.At(1, 1).temperature == Info(Material::Water).temperature,
          "SetCell NaN sentinel intentionally chooses catalog temperature");
}

void TestLifecycleAndPauseIdempotence() {
    SandboxModule world(Config());
    Check(!world.IsStarted() && world.Scene() == nullptr, "new module is stopped");
    world.FixedTick();
    world.Advance(0.5);
    Check(!world.SetCell(0, 0, Material::Sand) && world.Pixels().empty(), "stopped editing and rendering are safe");
    bool rejected = false;
    try { world.Edit(0, 0); } catch (const std::out_of_range&) { rejected = true; }
    Check(rejected, "checked edit rejects stopped world");
    Start(world);
    world.SetCell(4, 1, Material::Sand);
    world.FixedTick();
    const auto state = world.StateHash();
    auto* scene = world.Scene();
    Check(world.Startup() && world.Scene() == scene && world.StateHash() == state,
          "repeated Startup preserves world and ECS scene");
    world.Advance(0.0625);
    world.SetPaused(false);
    world.Advance(0.0625);
    Check(world.Stats().tick == 2, "setting unchanged pause state preserves partial time");
    world.Advance(0.0625);
    world.SetPaused(true);
    world.SetPaused(true);
    world.Advance(10.0);
    world.SetPaused(false);
    world.Advance(0.0625);
    Check(world.Stats().tick == 2, "changing pause state discards old partial time and paused wall time");
    world.Advance(0.0625);
    Check(world.Stats().tick == 3, "resume accumulates a fresh timestep");
    world.Shutdown();
    world.Shutdown();
    Check(!world.IsStarted() && world.Scene() == nullptr && world.Stats().tick == 0,
          "repeated Shutdown releases state safely");
    Check(world.Startup() && world.Stats().tick == 0 && world.Stats().particles == 0,
          "restart creates fresh empty state");
    world.SetCell(4, 1, Material::Sand);
    world.FixedTick();
    Check(world.At(4, 2).material == Material::Sand, "ECS pipeline functions after restart");
}

void TestThermalSleepReference() {
    SandboxModule sleeping(Config(64, 16, true)), full(Config(64, 16, false));
    Start(sleeping); Start(full);
    for (auto* world : {&sleeping, &full}) {
        Fill(*world, Material::Wall);
        for (int x = 1; x < 63; ++x) world->SetCell(x, 8, Material::Metal);
        Step(*world, 12);
        world->SetTemperature(7, 8, 900.0f);
    }
    for (int tick = 0; tick < 220; ++tick) {
        sleeping.FixedTick(); full.FixedTick();
        CheckPhysicalState(sleeping, full, "local thermal propagation");
    }
    Check(sleeping.At(17, 8).temperature > 20.0f, "localized heat reaches neighboring chunk");
    sleeping.Clear(); full.Clear();
    Step(sleeping, 12); Step(full, 12);
    sleeping.AddPressure(31, 8, 12.0f);
    full.AddPressure(31, 8, 12.0f);
    for (int tick = 0; tick < 80; ++tick) {
        sleeping.FixedTick(); full.FixedTick();
        Check(sleeping.StateHash() == full.StateHash(),
              "localized pressure field agrees across sleeping chunk boundaries at tick " + std::to_string(tick));
    }
}

void TestProbabilisticSleepReference() {
    for (const std::string scenario : {"plant", "acid", "gas"}) {
        auto sleepingConfig = Config(48, 16, true);
        auto fullConfig = Config(48, 16, false);
        // This seed has four initial failed gas motion rolls. A live random
        // opportunity must keep the region awake beyond the four-tick timer.
        if (scenario == "gas") sleepingConfig.seed = fullConfig.seed = 1;
        SandboxModule sleeping(sleepingConfig), full(fullConfig);
        Start(sleeping); Start(full);
        for (auto* world : {&sleeping, &full}) {
            Fill(*world, Material::Wall);
            if (scenario == "plant") {
                world->SetCell(15, 8, Material::Plant);
                world->SetCell(15, 9, Material::Water);
                world->SetCell(16, 8, Material::Empty);
            } else if (scenario == "acid") {
                world->SetCell(15, 8, Material::Acid);
                world->SetCell(16, 8, Material::Stone);
            } else {
                world->SetCell(8, 8, Material::Gas);
                world->SetCell(9, 8, Material::Empty);
            }
        }
        for (int tick = 0; tick < 240; ++tick) {
            sleeping.FixedTick(); full.FixedTick();
            CheckPhysicalState(sleeping, full, "probabilistic " + scenario);
        }
        if (scenario == "plant") Check(Count(sleeping, Material::Plant) > 1, "eligible dormant plant eventually grows");
        if (scenario == "acid") Check(Count(sleeping, Material::Stone) == 0, "eligible dormant acid eventually corrodes");
    }
}

void TestExtendedPhasesAndSoil() {
    SandboxModule world(Config(7, 7)); Start(world); Fill(world, Material::Wall);
    world.SetCell(3, 3, Material::Snow, 10);
    world.FixedTick();
    Check(world.At(3, 3).material == Material::Water, "warm powder snow melts");
    world.SetCell(3, 3, Material::Wax, 100);
    world.Edit(3, 3).flags = 1; world.Edit(3, 3).life = 40;
    world.FixedTick();
    Check(world.At(3, 3).material == Material::MoltenWax && world.At(3, 3).life == 40 &&
          world.At(3, 3).flags == 1, "wax phase change preserves active fuel state");
    world.SetCell(3, 3, Material::MoltenWax, 30);
    world.FixedTick();
    Check(world.At(3, 3).material == Material::Wax, "cold liquid wax hardens");
    world.SetCell(3, 3, Material::DryIce, -70);
    world.FixedTick();
    Check(world.At(3, 3).material == Material::CarbonDioxide && world.PressureAt(3, 3) > 0,
          "dry ice sublimates and injects pressure");
    world.Clear(); Fill(world, Material::Wall);
    world.SetCell(3, 3, Material::Soil); world.SetCell(4, 3, Material::Water);
    world.FixedTick();
    Check(Count(world, Material::Mud) == 1 && Count(world, Material::Water) == 0,
          "soil stores one neighboring water pixel as mud");
    world.SetTemperature(3, 3, 150);
    world.FixedTick();
    Check(Count(world, Material::Steam) == 1 && Count(world, Material::Soil) == 1,
          "drying mud releases steam into the emptied pore");
    world.Clear(); Fill(world, Material::Wall);
    world.SetCell(3, 3, Material::Seed); world.SetCell(3, 4, Material::Mud);
    Step(world, 100);
    Check(Count(world, Material::Plant) == 1 && Count(world, Material::Mud) == 0 &&
          Count(world, Material::Soil) == 1, "seed consumes mud moisture to germinate");
}

void TestReactiveGases() {
    SandboxModule world(Config(7, 7)); Start(world); Fill(world, Material::Wall);
    world.SetCell(3, 3, Material::Hydrogen, 500); world.SetCell(4, 3, Material::Oxygen);
    world.FixedTick();
    Check(Count(world, Material::Hydrogen) == 0 && Count(world, Material::Oxygen) == 0 &&
          Count(world, Material::Steam) == 2, "ignited hydrogen and oxygen form steam");
    Check(world.PressureAt(3, 3) > 0 && Count(world, Material::Wall) == 47,
          "gas reaction produces pressure without destroying walls");
    world.Clear(); Fill(world, Material::Wall);
    world.SetCell(3, 3, Material::Oxygen); world.SetCell(2, 3, Material::Fire);
    world.FixedTick();
    Check(Count(world, Material::Oxygen) == 0 && Count(world, Material::Fire) == 2,
          "oxygen is consumed by an adjacent flame");
    world.Clear(); Fill(world, Material::Wall);
    world.SetCell(3, 2, Material::Fire); world.SetCell(3, 3, Material::CarbonDioxide);
    world.SetCell(4, 3, Material::Wood, 400); world.Edit(4, 3).flags = 1; world.Edit(4, 3).life = 50;
    world.FixedTick();
    Check(Count(world, Material::Fire) == 0 && world.At(4, 3).flags == 0,
          "CO2 suppresses flame and burning solid fuel");
    world.Clear(); Fill(world, Material::Wall);
    world.SetCell(3, 3, Material::Water); world.Edit(3, 3).charge = 22;
    world.SetCell(3, 2, Material::Empty);
    world.FixedTick();
    Check(Count(world, Material::Hydrogen) == 1 && Count(world, Material::Oxygen) == 1,
          "electrical water pulse emits both gases when space exists");
}

void TestPoweredDevicesAndFuse() {
    SandboxModule world(Config(12, 7)); Start(world); Fill(world, Material::Wall);
    world.SetCell(2, 3, Material::Heater); world.SetCell(3, 3, Material::Wax);
    Step(world, 16);
    Check(world.At(3, 3).material == Material::Wax && world.At(3, 3).temperature == 20,
          "unpowered heater is inert");
    world.SetCell(1, 3, Material::Battery);
    bool melted = false;
    for (int i = 0; i < 48; ++i) { world.FixedTick(); melted |= Count(world, Material::MoltenWax) > 0; }
    Check(melted, "battery repeatedly drives a heater to melt wax");
    world.Clear(); Fill(world, Material::Wall);
    world.SetCell(1, 3, Material::Battery); world.SetCell(2, 3, Material::Cooler);
    world.SetCell(3, 3, Material::Water);
    Step(world, 32);
    Check(world.At(3, 3).material == Material::Ice, "powered cooler freezes adjacent water");
    world.SetCell(3, 3, Material::Wax);
    Step(world, 60);
    Check(world.At(3, 3).material == Material::Wax && world.At(3, 3).flags == 0 &&
          world.At(3, 3).temperature < 20, "cooler current cools fuel without igniting it");
    world.Clear(); Fill(world, Material::Wall);
    for (int x = 2; x < 9; ++x) world.SetCell(x, 3, Material::Fuse);
    world.SetCell(1, 3, Material::Spark);
    Step(world, 10);
    Check(world.At(8, 3).material == Material::Fuse && world.At(8, 3).flags == 0,
          "fuse delay prevents instantaneous chain propagation");
    Step(world, 160);
    Check(Count(world, Material::Fuse) < 7, "fuse line burns progressively");
    world.Clear(); Fill(world, Material::Wall);
    world.SetCell(3, 3, Material::Fuse, 220); world.Edit(3, 3).flags = 1; world.Edit(3, 3).life = 3;
    world.SetCell(3, 4, Material::Water);
    Step(world, 5);
    Check(world.At(3, 3).material == Material::Fuse && world.At(3, 3).flags == 0,
          "water interrupts a burning fuse without consuming the segment");
}

void TestCloneAndPalette() {
    SandboxModule world(Config(32, 16)); Start(world); Fill(world, Material::Wall);
    world.SetCell(15, 8, Material::Clone); world.SetCell(15, 7, Material::Sand);
    world.SetCell(16, 8, Material::Empty);
    Step(world, 7);
    Check(Count(world, Material::Sand) == 1, "clone respects emission cadence");
    Check((world.At(15, 8).flags >> 8) == unsigned(Material::Sand), "clone remembers sampled material");
    world.FixedTick();
    Check(world.At(16, 8).material == Material::Sand && Count(world, Material::Clone) == 1,
          "clone emits across chunk boundary without cloning itself");
    Step(world, 20);
    world.SetCell(16, 8, Material::Empty);
    Step(world, 8);
    Check(world.At(16, 8).material == Material::Sand, "opening a blocked clone wakes production");
    std::array<int, MaterialCount> occurrences{};
    for (int group = 1; group < int(PaletteGroup::Count); ++group) {
        for (int page = 0; page < PalettePages(PaletteGroup(group)); ++page)
            for (auto material : PaletteMaterials(PaletteGroup(group), page)) ++occurrences[std::size_t(material)];
    }
    for (std::size_t i = 1; i < MaterialCount; ++i) Check(occurrences[i] == 1, "every drawable material has exactly one category");
    Check(PaletteMaterials(PaletteGroup::All).size() == MaterialCount - 1, "all palette includes every material");
    Check(PaletteMaterials(PaletteGroup::All, 100) == PaletteMaterials(PaletteGroup::All, PalettePages(PaletteGroup::All) - 1),
          "palette page clamps at the final page");
}

void TestExtendedSleepReference() {
    for (auto preset : {Preset::Elements, Preset::Garden}) {
        SandboxModule sleeping(Config(64, 48, true)), full(Config(64, 48, false));
        Start(sleeping); Start(full); sleeping.LoadPreset(preset); full.LoadPreset(preset);
        for (int tick = 0; tick < 180; ++tick) {
            sleeping.FixedTick(); full.FixedTick();
            CheckPhysicalState(sleeping, full, "extended preset " + std::to_string(int(preset)));
            Check(sleeping.StateHash() == full.StateHash(), "extended preset air and particles agree");
        }
    }
}

struct TemporarySave {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("pixel_sandbox_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".pxsb");
    ~TemporarySave() { std::error_code ignored; std::filesystem::remove(path, ignored); }
};

void TestExtendedPersistence() {
    SandboxModule source(Config(64, 32)), restored(Config(64, 32));
    Start(source); Start(restored); Fill(source, Material::Wall);
    for (unsigned i = unsigned(Material::Soil); i < unsigned(Material::Count); ++i)
        source.SetCell(2 + 3 * int(i - unsigned(Material::Soil)), 8, Material(i));
    source.Edit(2 + 3 * (int(Material::Clone) - int(Material::Soil)), 8).flags = unsigned(Material::Snow) << 8;
    TemporarySave file;
    Check(source.Save(file.path) && restored.Load(file.path), "extended catalog save/load succeeds");
    Check(source.StateHash() == restored.StateHash(), "all new IDs and programmed clone persist");
    for (int i = 0; i < 80; ++i) {
        source.FixedTick(); restored.FixedTick();
        Check(source.StateHash() == restored.StateHash(), "new material save resumes deterministically");
    }
    source.Clear(); source.SetCell(8, 8, Material::Sand);
    Check(source.Save(file.path), "write legacy-compatible particle payload");
    { std::fstream fileBytes(file.path, std::ios::in | std::ios::out | std::ios::binary);
      fileBytes.seekp(8); const char version[4] = {1, 0, 0, 0}; fileBytes.write(version, 4); }
    Check(restored.Load(file.path) && restored.StateHash() == source.StateHash(), "version 1 save remains readable");
    source.SetCell(8, 8, Material::Clone); source.Edit(8, 8).flags = 255u << 8;
    Check(source.Save(file.path), "write malformed clone target fixture");
    const auto before = restored.StateHash();
    Check(!restored.Load(file.path) && restored.StateHash() == before, "invalid clone target is rejected transactionally");
}

void TestPersistence() {
    SandboxModule source(Config(32, 24)), restored(Config(32, 24));
    Start(source);
    Start(restored);
    source.LoadPreset(Preset::Falling);
    Step(source, 12);
    source.AddPressure(16, 12, 4.0f);
    TemporarySave file;
    Check(source.Save(file.path), "save succeeds: " + source.Error());
    Check(restored.Load(file.path), "load succeeds: " + restored.Error());
    Check(restored.StateHash() == source.StateHash(), "save/load preserves exact state");
    Check(restored.Stats().tick == source.Stats().tick, "save/load preserves simulation time");
    SandboxModule mismatched(Config(24, 32));
    Start(mismatched);
    mismatched.SetCell(1, 1, Material::Sand);
    const auto mismatchedBefore = mismatched.StateHash();
    Check(!mismatched.Load(file.path), "save with different dimensions is rejected");
    Check(mismatched.StateHash() == mismatchedBefore, "dimension mismatch preserves current world");
    for (int i = 0; i < 40; ++i) {
        source.FixedTick();
        restored.FixedTick();
        Check(restored.StateHash() == source.StateHash(), "loaded state reproduces future simulation");
    }
    Check(source.Save(file.path), "resave succeeds");
    std::ifstream input(file.path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(input)), {});
    input.close();
    Check(bytes.size() > 16, "save contains payload");
    const auto before = restored.StateHash();
    { std::ofstream output(file.path, std::ios::binary | std::ios::trunc); output.write(bytes.data(), static_cast<std::streamsize>(bytes.size() - 7)); }
    Check(!restored.Load(file.path), "truncated save is rejected");
    Check(restored.StateHash() == before, "failed truncated load preserves current state");
    { std::ofstream output(file.path, std::ios::binary | std::ios::trunc); output << "not a sandbox file"; }
    Check(!restored.Load(file.path), "unrecognized file is rejected");
    Check(restored.StateHash() == before, "failed malformed load preserves current state");
}
} // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests{
        {"single update and bounds", TestSingleUpdateAndBounds},
        {"density and walls", TestDensityAndWalls},
        {"conservation and statistics", TestConservationAndStatistics},
        {"extension and no cascading updates", TestExtensionAndNoCascade},
        {"thermal phase transitions", TestThermalPhases},
        {"lava water and salt", TestLavaWaterAndSalt},
        {"finite acid capacity", TestAcidCapacity},
        {"ignition and explosion", TestIgnitionAndExplosion},
        {"conductor pulse propagation", TestConductorPropagation},
        {"small grids edges and brushes", TestSmallEdgesAndBrush},
        {"fixed timestep and pause", TestFixedTimeAndPause},
        {"deterministic replay", TestDeterminism},
        {"sleeping and cross-chunk wake", TestSleepingAndWake},
        {"sleeping reference equivalence", TestSleepReference},
        {"configuration and input validation", TestConfigurationAndInputValidation},
        {"lifecycle and pause idempotence", TestLifecycleAndPauseIdempotence},
        {"thermal sleeping reference equivalence", TestThermalSleepReference},
        {"probabilistic sleeping reference equivalence", TestProbabilisticSleepReference},
        {"transactional persistence and replay", TestPersistence},
        {"extended phases and soil cycle", TestExtendedPhasesAndSoil},
        {"reactive gases and electrolysis", TestReactiveGases},
        {"powered devices and delayed fuse", TestPoweredDevicesAndFuse},
        {"clone cadence and palette coverage", TestCloneAndPalette},
        {"extended sleeping reference equivalence", TestExtendedSleepReference},
        {"extended saves and legacy compatibility", TestExtendedPersistence}};
    int failures = 0;
    for (const auto& [name, test] : tests) {
        try { test(); std::cout << "[PASS] " << name << '\n'; }
        catch (const std::exception& error) { ++failures; std::cerr << "[FAIL] " << name << ": " << error.what() << '\n'; }
    }
    std::cout << tests.size() - failures << '/' << tests.size() << " test groups passed\n";
    return failures == 0 ? 0 : 1;
}
