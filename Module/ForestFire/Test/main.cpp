// Interactive host only: simulation belongs to ForestModule; all scene drawing
// belongs to ForestRenderer and the existing Render module's RHI command queue.
#include "ForestFire/Public/forest_module.h"
#include "ForestFire/Public/forest_renderer.h"
#include "Render/module.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;

std::uint32_t Number(std::string_view value, const char* option) {
    std::uint32_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        throw std::invalid_argument(std::string("Invalid value for ") + option);
    return result;
}

void PrintUsage() {
    std::cout << "forest_fire_demo [--width cells] [--height cells] [--seed number]\n"
                 "                 [--wrap] [--four-neighbors] [--paused]\n"
                 "                 [--smoke-test] [--capture file.ppm]\n"
                 "Space: pause/resume; N: single step; R: reset seed; +/-: speed\n"
                 "Left mouse: ignite trees; right: plant; middle: erase\n"
                 "Shift: larger brush; Escape: exit\n";
}

void IgniteCenter(ForestFire::ForestModule& forest) {
    const auto& config = forest.Config();
    const int x = static_cast<int>(config.width / 2), y = static_cast<int>(config.height / 2);
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            if (x + dx >= 0 && y + dy >= 0 &&
                x + dx < static_cast<int>(config.width) && y + dy < static_cast<int>(config.height))
                forest.Ignite(static_cast<std::uint32_t>(x + dx), static_cast<std::uint32_t>(y + dy));
        }
    }
}

void SavePpm(const std::string& path, const std::vector<unsigned char>& pixels, int width, int height) {
    std::ofstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open capture file: " + path);
    file << "P6\n" << width << ' ' << height << "\n255\n";
    for (int y = height - 1; y >= 0; --y)
        file.write(reinterpret_cast<const char*>(pixels.data() + std::size_t(y) * width * 3), std::streamsize(width) * 3);
    if (!file) throw std::runtime_error("Cannot write capture file: " + path);
}
} // namespace

int main(int argc, char** argv) {
    ForestFire::SimulationConfig config;
    config.width = 160;
    config.height = 100;
    bool smoke = false, paused = false;
    std::string capturePath;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string option = argv[i];
            if (option == "--help" || option == "-h") { PrintUsage(); return 0; }
            if (option == "--smoke-test") smoke = true;
            else if (option == "--capture") {
                if (++i == argc) throw std::invalid_argument("--capture requires a PPM output path");
                capturePath = argv[i];
                smoke = true;
            } else if (option == "--paused") paused = true;
            else if (option == "--wrap") config.boundary = ForestFire::BoundaryMode::Wrap;
            else if (option == "--four-neighbors") config.neighborhood = ForestFire::Neighborhood::Four;
            else if (option == "--width" || option == "--height" || option == "--seed") {
                if (++i == argc) throw std::invalid_argument(option + " requires a number");
                const auto value = Number(argv[i], option.c_str());
                if (option == "--width") config.width = value;
                else if (option == "--height") config.height = value;
                else config.seed = value;
            } else throw std::invalid_argument("Unknown option: " + option);
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        PrintUsage();
        return 2;
    }

    // Limit only this CPU mesh demo. The headless simulation has its own limits.
    if (config.width == 0 || config.height == 0 || config.width > 512 || config.height > 512) {
        std::cerr << "Demo dimensions must each be between 1 and 512 cells\n";
        return 2;
    }

    ForestFire::ForestModule forest(config);
    ApplicationWindow::ApplicationWindowModule windowModule;
    Render::RenderModule renderModule;
    std::unique_ptr<ForestFire::ForestRenderer> renderer;
    int result = 0;
    try {
        if (!forest.Startup()) throw std::runtime_error(forest.Error());
        forest.SetPaused(paused);
        IgniteCenter(forest);

        if (smoke) {
            // GLFW hints require initialization and must precede Window::Activate.
            if (!glfwInit()) throw std::runtime_error("GLFW initialization failed");
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        }
        if (!windowModule.Startup()) throw std::runtime_error("ApplicationWindow startup failed");
        auto window = windowModule.GetCurrentWindow();
        auto* native = window->GetNativeWindow();
        glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
        glfwSetWindowTitle(native, "Forest / Fire - ECS Cellular Automaton");
        glfwSetWindowSize(native, 1280, 800);
        glfwSetWindowSizeLimits(native, 640, 400, GLFW_DONT_CARE, GLFW_DONT_CARE);
        if (!renderModule.Startup()) throw std::runtime_error("Render startup failed");
        auto device = renderModule.GetRHIDevice();
        renderer = std::make_unique<ForestFire::ForestRenderer>(device);
        renderer->Initialize();
        PrintUsage();

        std::array<bool, GLFW_KEY_LAST + 1> previousKeys{};
        auto pressed = [&](int key) {
            const bool down = glfwGetWindowAttrib(native, GLFW_FOCUSED) != 0 && glfwGetKey(native, key) == GLFW_PRESS;
            const bool edge = down && !previousKeys[key];
            previousKeys[key] = down;
            return edge;
        };
        const auto started = Clock::now();
        auto last = started, lastProgress = started;
        constexpr std::uint64_t SmokeFrames = 32;
        std::uint64_t observedFrames = 0;
        int width = 0, height = 0;

        while (!window->ShouldClose()) {
            window->PollEvents();
            device->returnSystem.DrainCallbacks();
            if (!renderer->Error().empty()) throw std::runtime_error(renderer->Error());
            const auto now = Clock::now();
            const double elapsed = std::chrono::duration<double>(now - last).count();
            last = now;
            if (renderer->CompletedFrames() != observedFrames) {
                observedFrames = renderer->CompletedFrames();
                lastProgress = now;
            }
            if (now - lastProgress > std::chrono::seconds(15))
                throw std::runtime_error("Render resource or frame completion timeout");
            if (smoke && now - started > std::chrono::seconds(45))
                throw std::runtime_error("Forest render smoke test timeout");
            if (smoke && renderer->CompletedFrames() >= SmokeFrames) break;
            if (pressed(GLFW_KEY_ESCAPE)) break;
            if (pressed(GLFW_KEY_SPACE)) forest.SetPaused(!forest.IsPaused());
            const bool singleStep = pressed(GLFW_KEY_N);
            if (singleStep) { forest.SetPaused(true); forest.FixedTick(); }
            if (pressed(GLFW_KEY_R)) {
                if (!forest.Reset()) throw std::runtime_error(forest.Error());
                IgniteCenter(forest);
            }
            const bool faster = pressed(GLFW_KEY_EQUAL) | pressed(GLFW_KEY_KP_ADD);
            const bool slower = pressed(GLFW_KEY_MINUS) | pressed(GLFW_KEY_KP_SUBTRACT);
            if (faster) forest.SetStepSeconds(std::max(1.0 / 240.0, forest.Config().stepSeconds / 1.25));
            if (slower) forest.SetStepSeconds(std::min(2.0, forest.Config().stepSeconds * 1.25));

            glfwGetFramebufferSize(native, &width, &height);
            if (width < 1 || height < 1) {
                // A minimized interactive window need not produce GPU frames.
                lastProgress = now;
                glfwWaitEventsTimeout(0.05);
                continue;
            }
            if (!smoke && glfwGetWindowAttrib(native, GLFW_FOCUSED) != 0) {
                int windowWidth, windowHeight;
                glfwGetWindowSize(native, &windowWidth, &windowHeight);
                double mouseX, mouseY;
                glfwGetCursorPos(native, &mouseX, &mouseY);
                const auto hit = ForestFire::ForestRenderer::PickCell(
                    mouseX * width / std::max(1, windowWidth), mouseY * height / std::max(1, windowHeight),
                    width, height, config.width, config.height);
                if (hit) {
                    const bool ignite = glfwGetMouseButton(native, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
                    const bool plant = glfwGetMouseButton(native, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
                    const bool erase = glfwGetMouseButton(native, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
                    const bool wide = glfwGetKey(native, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                                      glfwGetKey(native, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
                    const int radius = wide ? 4 : 1;
                    if (ignite || plant || erase) {
                        for (int dy = -radius; dy <= radius; ++dy) {
                            for (int dx = -radius; dx <= radius; ++dx) {
                                const int x = static_cast<int>(hit->first) + dx, y = static_cast<int>(hit->second) + dy;
                                if (dx * dx + dy * dy > radius * radius || x < 0 || y < 0 ||
                                    x >= static_cast<int>(config.width) || y >= static_cast<int>(config.height)) continue;
                                if (erase) forest.SetCell(x, y, ForestFire::CellState::Empty);
                                else if (plant) forest.SetCell(x, y, ForestFire::CellState::Tree);
                                else forest.Ignite(x, y);
                            }
                        }
                    }
                }
                if (!singleStep) forest.Advance(elapsed);
            }
            if (renderer->Ready() && !renderer->Busy()) {
                if (smoke) forest.FixedTick();
                const bool finalCapture = !capturePath.empty() && renderer->CompletedFrames() + 1 == SmokeFrames;
                renderer->Draw(forest, width, height, !finalCapture);
            }
            device->returnSystem.WaitForCallbacks(std::chrono::milliseconds(4));
        }

        const auto retireDeadline = Clock::now() + std::chrono::seconds(10);
        while (renderer->Busy() && Clock::now() < retireDeadline) {
            device->returnSystem.DrainCallbacks();
            device->returnSystem.WaitForCallbacks(std::chrono::milliseconds(4));
        }
        if (renderer->Busy()) throw std::runtime_error("Forest frame retirement timeout");
        if (!renderer->Error().empty()) throw std::runtime_error(renderer->Error());

        if (smoke) {
            if (renderer->CompletedFrames() != SmokeFrames) throw std::runtime_error("Smoke test closed before completing frames");
            // Readback and error inspection are QA only. All scene rendering above
            // is encoded through Render's public frame/resource commands.
            struct Readback { bool done = false; unsigned error = 0; std::vector<unsigned char> pixels; };
            auto readback = std::make_shared<Readback>();
            const bool capture = !capturePath.empty();
            device->async_ExecuteCode([readback, capture, width, height] {
                glFinish();
                if (capture) {
                    readback->pixels.resize(std::size_t(width) * height * 3);
                    glReadBuffer(GL_BACK);
                    glPixelStorei(GL_PACK_ALIGNMENT, 1);
                    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, readback->pixels.data());
                }
                readback->error = glGetError();
            }, [readback] { readback->done = true; });
            const auto deadline = Clock::now() + std::chrono::seconds(10);
            while (!readback->done && Clock::now() < deadline) {
                device->returnSystem.DrainCallbacks();
                device->returnSystem.WaitForCallbacks(std::chrono::milliseconds(4));
            }
            if (!readback->done || readback->error != GL_NO_ERROR) throw std::runtime_error("GPU smoke/readback failed");
            if (capture) SavePpm(capturePath, readback->pixels, width, height);
            std::cout << "ForestFire smoke PASS: " << renderer->CompletedFrames() << " RHI frames, "
                      << forest.Stats().tick << " ECS ticks, GL_NO_ERROR\n";
        }
        renderer->Shutdown();
    } catch (const std::exception& error) {
        std::cerr << "ForestFire: " << error.what() << '\n';
        result = 1;
    }

    // Join the render thread before releasing callback state and the GL window.
    renderModule.Shutdown();
    renderer.reset();
    windowModule.Shutdown();
    forest.Shutdown();
    return result;
}
