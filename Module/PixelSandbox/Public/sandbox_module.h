#pragma once
#include "module_base.h"
#include "materials.h"
#include "engine/ECS/Scene/scene.h"
#include "engine/ECS/System/system_pipeline.h"
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace PixelSandbox {
struct SimulationConfig {
    int width = 320, height = 200;
    std::uint32_t seed = 1337;
    double stepSeconds = 1.0 / 60.0;
    unsigned maxCatchUpSteps = 4;
    bool sleeping = true;
};
struct Statistics {
    std::uint64_t tick = 0;
    std::size_t particles = 0, activeChunks = 0, totalChunks = 0, visitedCells = 0;
    std::array<std::size_t, MaterialCount> counts{};
    double fieldMs = 0, heatMs = 0, materialMs = 0, totalMs = 0;
};
enum class BrushTool { Paint, Erase, Heat, Cool, Pressure };
enum class Preset { Empty, Falling, Volcano, Circuit, Garden, Elements };
class FieldSystem;
class HeatSystem;
class MaterialSystem;
class SandboxModule final : public IModule {
public:
    explicit SandboxModule(SimulationConfig config = {});
    ~SandboxModule() override;
    const char* GetName() const noexcept override { return "PixelSandbox"; }
    bool Startup() override;
    void Shutdown() override;
    bool IsStarted() const noexcept override;
    const SimulationConfig& Config() const noexcept;
    const Statistics& Stats() const noexcept;
    const std::string& Error() const noexcept;
    void FixedTick();
    void Advance(double seconds);
    bool IsPaused() const noexcept;
    void SetPaused(bool paused);
    void Clear();
    void LoadPreset(Preset preset);
    bool SetCell(int x, int y, Material material,
                 float temperature = std::numeric_limits<float>::quiet_NaN());
    bool SetTemperature(int x, int y, float temperature);
    const Cell& At(int x, int y) const; // out-of-bounds is an immutable wall.
    Cell& Edit(int x, int y); // checked; wakes the region. Do not change material here.
    bool InBounds(int x, int y) const noexcept;
    void Paint(int x, int y, int radius, Material material,
               BrushTool tool = BrushTool::Paint, bool replace = false);
    // Interpolated brush stroke; coordinates may be outside the grid.
    void Stroke(int x0, int y0, int x1, int y1, int radius, Material material,
                BrushTool tool = BrushTool::Paint, bool replace = false);
    std::vector<std::uint8_t> Pixels(bool heatMap = false) const;
    std::uint64_t Revision() const noexcept;
    std::uint64_t StateHash() const noexcept;
    bool Save(const std::filesystem::path& path);
    bool Load(const std::filesystem::path& path);

    // Local rule API: deterministic, cell/tick keyed random, canonical mutation,
    // and independent low resolution pressure/velocity. These run on host thread.
    std::uint32_t Random(int x, int y, std::uint32_t salt = 0) const noexcept;
    bool Chance(int x, int y, std::uint32_t salt, float probability) const noexcept;
    void Wake(int x, int y);
    float PressureAt(int x, int y) const;
    void AddPressure(int x, int y, float amount);
    void Explode(int x, int y, int radius, float power);
    bool TryMove(int x, int y, int nx, int ny);
    ECS::Core::Scene* Scene() noexcept;
    ECS::EntityID WorldEntity() const noexcept;
    using Rule = std::function<bool(SandboxModule&, int, int)>;
    void SetRule(Material material, Rule rule); // configure only while stopped.
    template<class T, class... A> T& AddSystem(A&&... arguments) {
        if (IsStarted()) throw std::logic_error("Configure sandbox systems while stopped");
        return Pipeline().Add<T>(std::forward<A>(arguments)...);
    }
private:
    friend class FieldSystem;
    friend class HeatSystem;
    friend class MaterialSystem;
    ECS::System::Pipeline& Pipeline();
    void StepFields();
    void StepHeat();
    void StepMaterials();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace PixelSandbox
