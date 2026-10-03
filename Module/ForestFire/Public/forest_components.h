#pragma once

#include "engine/ECS/Component/component_loader_registry.h"
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace ForestFire {

enum class CellState : std::uint8_t { Empty, Tree, Burning };
enum class BoundaryMode : std::uint8_t { Finite, Wrap };
enum class Neighborhood : std::uint8_t { Four, Eight };

struct SimulationConfig {
    std::uint32_t width = 160;
    std::uint32_t height = 100;
    std::uint32_t seed = 1337;
    double initialTreeDensity = .65;
    // Probabilities apply per simulation tick, not per second.
    double growthProbability = .001;
    double lightningProbability = .00002;
    // Independent ignition chance for each distinct burning neighbor.
    double spreadProbability = 1.0;
    std::uint32_t burnTicks = 3;
    BoundaryMode boundary = BoundaryMode::Finite;
    Neighborhood neighborhood = Neighborhood::Eight;
    double stepSeconds = .05;
    std::uint32_t maxCatchUpSteps = 8;
};

template<class T>
struct ForestComponent : ECS::Component::Component<T> {
    // Procedurally initialized by ForestModule; JSON scene loading is not used.
    bool LoadFromMetaDataImpl(const Json::Value&, Log::StackLogErrorHandle) { return false; }
};

struct CellPosition : ForestComponent<CellPosition> {
    std::uint32_t x = 0, y = 0;
};

struct Cell : ForestComponent<Cell> {
    CellState state = CellState::Empty;
    std::uint32_t burnTicksRemaining = 0;
};

// Separate destination component prevents propagation across several cells in
// one tick. Extension systems can alter this component between evaluate/commit.
struct NextCell : ForestComponent<NextCell> {
    CellState state = CellState::Empty;
    std::uint32_t burnTicksRemaining = 0;
};

struct CellSnapshot {
    std::uint32_t x = 0, y = 0;
    CellState state = CellState::Empty;
    std::uint32_t burnTicksRemaining = 0;
    bool operator==(const CellSnapshot&) const = default;
};

struct Statistics {
    std::uint64_t tick = 0;
    std::size_t empty = 0, trees = 0, burning = 0;
    bool operator==(const Statistics&) const = default;
};

inline void RegisterComponents() {
    static std::once_flag registered;
    std::call_once(registered, [] {
        REGISTER_COMPONENT("forest_fire_position", CellPosition);
        REGISTER_COMPONENT("forest_fire_cell", Cell);
        REGISTER_COMPONENT("forest_fire_next_cell", NextCell);
    });
}

} // namespace ForestFire
