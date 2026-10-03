#include "PixelSandbox/Public/sandbox_module.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace PixelSandbox;
using Clock = std::chrono::steady_clock;

struct Result {
    std::string scenario;
    int width = 0, height = 0, steps = 0;
    bool sleeping = true;
    double p50 = 0, p95 = 0, mean = 0, meanVisited = 0, meanActiveRatio = 0;
    double meanField = 0, meanHeat = 0, meanMaterial = 0;
    std::size_t particles = 0;
    std::uint64_t checksum = 0;
};

void Populate(SandboxModule& world, const std::string& scenario) {
    world.Clear();
    if (scenario == "elements") { world.LoadPreset(Preset::Elements); return; }
    const int width = world.Config().width, height = world.Config().height;
    for (int x = 0; x < width; ++x) world.SetCell(x, height - 1, Material::Wall);
    if (scenario == "static") {
        for (int y = height * 3 / 4; y < height - 1; ++y)
            for (int x = 0; x < width; ++x) world.SetCell(x, y, Material::Sand);
    } else if (scenario == "falling") {
        for (int y = 1; y < height / 2; ++y)
            for (int x = 1; x < width - 1; ++x)
                if (world.Random(x, y, 51) % 7 == 0)
                    world.SetCell(x, y, Material::Sand);
    } else if (scenario == "dense") {
        for (int y = height / 8; y < height * 7 / 8; ++y)
            for (int x = 1; x < width - 1; ++x) {
                const auto sample = world.Random(x, y, 17) % 10;
                if (sample < 4) world.SetCell(x, y, Material::Sand);
                else if (sample < 7) world.SetCell(x, y, Material::Water);
                else if (sample < 9) world.SetCell(x, y, Material::Oil);
            }
    } else if (scenario == "reactive") {
        // Fine-grained seeded contact deliberately exercises simultaneous heat,
        // quenching, corrosion, ignition, explosions and conductor rules.
        for (int y = height / 8; y < height * 7 / 8; ++y)
            for (int x = 1; x < width - 1; ++x) {
                const auto sample = world.Random(x, y, 923) % 100;
                Material material = Material::Empty;
                if (sample < 20) material = Material::Sand;
                else if (sample < 35) material = Material::Water;
                else if (sample < 47) material = Material::Wood;
                else if (sample < 57) material = Material::Oil;
                else if (sample < 65) material = Material::Acid;
                else if (sample < 73) material = Material::Metal;
                else if (sample < 79) material = Material::Lava;
                else if (sample < 84) material = Material::Gas;
                else if (sample < 90) material = Material::Gunpowder;
                if (material != Material::Empty) world.SetCell(x, y, material);
            }
    } else throw std::invalid_argument("Unknown benchmark scenario");
}

double Percentile(const std::vector<double>& values, double fraction) {
    const auto index = static_cast<std::size_t>(std::ceil(fraction * values.size()));
    return values[std::min(values.size() - 1, index == 0 ? 0 : index - 1)];
}

Result Run(const std::string& scenario, int width, int height, int steps, bool sleeping) {
    SimulationConfig config;
    config.width = width;
    config.height = height;
    config.seed = 827391;
    config.sleeping = sleeping;
    SandboxModule world(config);
    if (!world.Startup()) throw std::runtime_error("Startup: " + world.Error());
    Populate(world, scenario);
    // Warm-up is identical in reference runs, and excluded from measurements.
    for (int i = 0; i < 30; ++i) world.FixedTick();
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(steps));
    Result result{scenario, width, height, steps, sleeping};
    for (int i = 0; i < steps; ++i) {
        const auto start = Clock::now();
        world.FixedTick();
        const double milliseconds = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        samples.push_back(milliseconds);
        result.mean += milliseconds;
        result.meanVisited += static_cast<double>(world.Stats().visitedCells);
        result.meanField += world.Stats().fieldMs;
        result.meanHeat += world.Stats().heatMs;
        result.meanMaterial += world.Stats().materialMs;
        if (world.Stats().totalChunks != 0)
            result.meanActiveRatio += static_cast<double>(world.Stats().activeChunks) / world.Stats().totalChunks;
    }
    result.mean /= steps;
    result.meanVisited /= steps;
    result.meanActiveRatio /= steps;
    result.meanField /= steps;
    result.meanHeat /= steps;
    result.meanMaterial /= steps;
    std::sort(samples.begin(), samples.end());
    result.p50 = Percentile(samples, 0.50);
    result.p95 = Percentile(samples, 0.95);
    result.particles = world.Stats().particles;
    result.checksum = world.StateHash();
    return result;
}

void Print(const Result& result, bool csv) {
    std::cout << std::fixed << std::setprecision(6);
    if (csv) {
        std::cout << result.scenario << ',' << result.width << ',' << result.height << ','
                  << result.steps << ',' << (result.sleeping ? 1 : 0) << ',' << result.p50 << ','
                  << result.p95 << ',' << result.mean << ',' << result.meanVisited << ','
                  << result.meanActiveRatio << ',' << result.meanField << ',' << result.meanHeat << ','
                  << result.meanMaterial << ',' << result.particles << ',' << result.checksum << '\n';
    } else {
        std::cout << "{\"scenario\":\"" << result.scenario << "\",\"width\":" << result.width
                  << ",\"height\":" << result.height << ",\"steps\":" << result.steps
                  << ",\"sleeping\":" << (result.sleeping ? "true" : "false")
                  << ",\"seed\":827391,\"warmup_steps\":30,\"tick_ms_p50\":" << result.p50
                  << ",\"tick_ms_p95\":" << result.p95 << ",\"tick_ms_mean\":" << result.mean
                  << ",\"visited_cells_mean\":" << result.meanVisited
                  << ",\"active_chunk_ratio_mean\":" << result.meanActiveRatio
                  << ",\"field_ms_mean\":" << result.meanField << ",\"heat_ms_mean\":" << result.meanHeat
                  << ",\"material_ms_mean\":" << result.meanMaterial
                  << ",\"particles\":" << result.particles << ",\"checksum\":\"" << result.checksum << "\"}\n";
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        int steps = 120;
        bool csv = false;
        for (int i = 1; i < argc; ++i) {
            const std::string argument(argv[i]);
            if (argument == "--csv") csv = true;
            else if (argument == "--steps" && i + 1 < argc) {
                steps = std::stoi(argv[++i]);
                if (steps < 1 || steps > 100000) throw std::invalid_argument("--steps must be in [1,100000]");
            } else if (argument == "--help") {
                std::cout << "pixel_sandbox_benchmark [--steps N] [--csv]\n"
                             "Deterministic 320x200 / 640x400 falling, dense, reactive, static and elements scenes.\n"
                             "JSON Lines by default. Static scenes also run without sleeping; checksums must agree.\n";
                return 0;
            } else throw std::invalid_argument("Unknown or incomplete argument: " + argument);
        }
        if (csv)
            std::cout << "scenario,width,height,steps,sleeping,tick_ms_p50,tick_ms_p95,tick_ms_mean,visited_cells_mean,active_chunk_ratio_mean,field_ms_mean,heat_ms_mean,material_ms_mean,particles,checksum\n";
        for (const auto dimensions : {std::pair{320, 200}, std::pair{640, 400}}) {
            for (const std::string scenario : {"falling", "dense", "reactive", "static", "elements"}) {
                const auto result = Run(scenario, dimensions.first, dimensions.second, steps, true);
                Print(result, csv);
                if (scenario == "static") {
                    const auto reference = Run(scenario, dimensions.first, dimensions.second, steps, false);
                    Print(reference, csv);
                    if (result.checksum != reference.checksum) {
                        std::cerr << "Sleeping/reference state mismatch for " << dimensions.first << 'x' << dimensions.second << '\n';
                        return 1;
                    }
                }
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
