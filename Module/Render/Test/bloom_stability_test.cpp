#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <glad/glad.h>
#include "ApplicationWindow/public/window.h"
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Private/Pipeline/post_process_shaders.h"

namespace {
using namespace Render;
using namespace std::chrono_literals;

void Check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
template <typename T> struct Completion { T value{}; bool done = false; };

// Generate the source on the GPU. A translated tent splat distributes a fixed
// amount of radiance across neighboring pixels instead of changing its energy
// as a point-sampled Gaussian would. Only its subpixel phase changes.
std::string SourceShader() {
    return PostProcessDetail::FragmentPreamble() + R"GLSL(
void main() {
    vec2 distanceToCenter = abs(gl_FragCoord.xy - postMisc.xy);
    vec2 coverage = max(vec2(1.0) - distanceToCenter, vec2(0.0));
    float value = postMisc.z > 0.5 ? postMisc.w : postMisc.w * coverage.x * coverage.y;
    outColor = vec4(vec3(value), 1.0);
}
)GLSL";
}

// This is the previous production extraction algorithm, including its soft
// knee. The comparison changes spatial filtering, not the threshold function.
std::string PreviousExtractShader() {
    return PostProcessDetail::FragmentPreamble() + R"GLSL(
void main() {
    vec3 color = max(ReadImage(inputTexture, vUV).rgb, vec3(0.0));
    float brightness = dot(color, vec3(0.299, 0.587, 0.114));
    float threshold = max(postBloom.x, 0.0);
    float knee = max(threshold * 0.5, 1e-4);
    float soft = clamp(brightness - threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee);
    float contribution = max(soft, brightness - threshold) / max(brightness, 1e-5);
    outColor = vec4(clamp(color * contribution, vec3(0.0), vec3(60000.0)), 1.0);
}
)GLSL";
}

// Previous production reconstruction. Both paths use the current extraction
// and reduction, isolating the effect of fine/coarse mip weighting.
std::string PreviousUpsampleShader() {
    return PostProcessDetail::FragmentPreamble() + R"GLSL(
void main() {
    vec2 texel = 1.0 / vec2(textureSize(inputTexture, 0));
    vec3 small = vec3(0.0);
    for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) {
        float weight = (x == 0 ? 2.0 : 1.0) * (y == 0 ? 2.0 : 1.0);
        small += ReadImage(inputTexture, vUV + vec2(x, y) * texel).rgb * weight;
    }
    outColor = vec4(clamp(ReadImage(secondTexture, vUV).rgb + small / 32.0,
                          vec3(0.0), vec3(60000.0)), 1.0);
}
)GLSL";
}

struct Target {
    RenderResourceHandle<RHITextureSpec> color;
    RenderResourceHandle<RenderTargetSpec> framebuffer;
    std::uint32_t width = 0, height = 0;
};
struct Images {
    std::shared_ptr<const void> owner;
    Target source, filtered, previous, downsampled;
};
struct PyramidImages {
    std::shared_ptr<const void> owner;
    Target source;
    std::vector<Target> down, work, previousWork;
};
struct Readback {
    std::array<std::vector<float>, 4> rgba;
    GLenum error = GL_NO_ERROR;
};

class BloomFixture {
public:
    explicit BloomFixture(OpenglBackendContext context) {
        device_.Run(BackendType::Opengl, std::move(context));
    }
    ~BloomFixture() { Shutdown(); }

    void Initialize() {
        PipelineDetail::ResourceBuilder builder(device_);
        const auto vertex = FullscreenVertexShader();
        source_ = builder.FullscreenPipeline(vertex, SourceShader());
        extract_ = builder.FullscreenPipeline(vertex, BloomExtractShader());
        previous_ = builder.FullscreenPipeline(vertex, PreviousExtractShader());
        downsample_ = builder.FullscreenPipeline(vertex, BloomDownsampleShader());
        upsample_ = builder.FullscreenPipeline(vertex, BloomUpsampleShader());
        previousUpsample_ = builder.FullscreenPipeline(vertex, PreviousUpsampleShader());
        mesh_ = builder.FullscreenMesh();
        uniform_ = builder.Uniform(sizeof(PostProcessConstants));
        owner_ = builder.Finish();
    }
    void Shutdown() {
        if (!device_.IsRunning()) return;
        owner_.reset();
        device_.StopAndRelease();
    }
    Images Allocate(std::uint32_t width, std::uint32_t height) {
        PipelineDetail::ResourceBuilder builder(device_);
        const auto target = [&](std::uint32_t w, std::uint32_t h) {
            Target result;
            result.width = w; result.height = h;
            result.color = builder.Texture(w, h, RHITextureFormat::RGBA16F);
            result.framebuffer = builder.Target(result.color, {});
            return result;
        };
        const auto halfW = std::max(1u, (width + 1) / 2), halfH = std::max(1u, (height + 1) / 2);
        Images result;
        result.source = target(width, height);
        result.filtered = target(halfW, halfH);
        result.previous = target(halfW, halfH);
        result.downsampled = target(std::max(1u, (halfW + 1) / 2), std::max(1u, (halfH + 1) / 2));
        result.owner = builder.Finish();
        return result;
    }
    PyramidImages AllocatePyramid(std::uint32_t width, std::uint32_t height, std::uint32_t levels) {
        Check(levels > 0 && levels <= MaxBloomLevels, "Invalid fixture Bloom mip count");
        PipelineDetail::ResourceBuilder builder(device_);
        const auto target = [&](std::uint32_t w, std::uint32_t h) {
            Target result;
            result.width = w; result.height = h;
            result.color = builder.Texture(w, h, RHITextureFormat::RGBA16F);
            result.framebuffer = builder.Target(result.color, {});
            return result;
        };
        PyramidImages result;
        result.source = target(width, height);
        for (std::uint32_t level = 0; level < levels; ++level) {
            width = std::max(1u, (width + 1) / 2);
            height = std::max(1u, (height + 1) / 2);
            result.down.push_back(target(width, height));
            result.work.push_back(target(width, height));
            result.previousWork.push_back(target(width, height));
            if (width == 1 && height == 1) break;
        }
        result.owner = builder.Finish();
        return result;
    }
    Readback Render(const Images& images, const PostProcessConstants& constants) {
        RHICommand::BeginFrame begin;
        begin.frameIndex = ++frameIndex_;
        begin.framebufferWidth = begin.framebufferHeight = 64;
        auto encoder = device_.BeginFrame(begin);
        try {
            Check(encoder.KeepAlive(owner_) && encoder.KeepAlive(images.owner), "Cannot retain Bloom resources");
            Pass(encoder, images.source, source_, {}, constants);
            Pass(encoder, images.filtered, extract_, images.source.color, constants);
            Pass(encoder, images.previous, previous_, images.source.color, constants);
            Pass(encoder, images.downsampled, downsample_, images.filtered.color, constants);
            Check(encoder.End(false), "Cannot end Bloom frame");
            auto completed = std::make_shared<Completion<bool>>();
            Check(device_.async_SubmitFrameCommands(encoder.GetCommandBuffer(), [completed] { completed->done = true; }),
                  "Cannot submit Bloom frame");
            Wait(completed, "Bloom frame");
        } catch (...) { encoder.Cancel(); throw; }
        return ReadTargets({images.source, images.filtered, images.previous, images.downsampled});
    }
    // Readback order: original HDR, first extracted mip, old reconstruction,
    // production reconstruction. Their outputs share the first mip's size.
    Readback RenderPyramid(const PyramidImages& images, const PostProcessConstants& constants,
                           std::uint32_t levels = MaxBloomLevels) {
        levels = std::min<std::uint32_t>(levels, static_cast<std::uint32_t>(images.down.size()));
        Check(levels > 0, "Cannot render an empty Bloom pyramid");
        RHICommand::BeginFrame begin;
        begin.frameIndex = ++frameIndex_;
        begin.framebufferWidth = begin.framebufferHeight = 64;
        auto encoder = device_.BeginFrame(begin);
        Target result = images.down[levels - 1], previous = result;
        try {
            Check(encoder.KeepAlive(owner_) && encoder.KeepAlive(images.owner), "Cannot retain Bloom pyramid");
            Pass(encoder, images.source, source_, {}, constants);
            Pass(encoder, images.down[0], extract_, images.source.color, constants);
            for (std::uint32_t level = 1; level < levels; ++level)
                Pass(encoder, images.down[level], downsample_, images.down[level - 1].color, constants);
            for (int level = static_cast<int>(levels) - 2; level >= 0; --level) {
                Pass(encoder, images.work[level], upsample_, result.color, constants, images.down[level].color);
                Pass(encoder, images.previousWork[level], previousUpsample_, previous.color, constants, images.down[level].color);
                result = images.work[level];
                previous = images.previousWork[level];
            }
            Check(encoder.End(false), "Cannot end Bloom pyramid frame");
            auto completed = std::make_shared<Completion<bool>>();
            Check(device_.async_SubmitFrameCommands(encoder.GetCommandBuffer(), [completed] { completed->done = true; }),
                  "Cannot submit Bloom pyramid frame");
            Wait(completed, "Bloom pyramid frame");
        } catch (...) { encoder.Cancel(); throw; }
        return ReadTargets({images.source, images.down[0], previous, result});
    }

private:
    Readback ReadTargets(const std::array<Target, 4>& targets) {
        auto completion = std::make_shared<Completion<Readback>>();
        device_.async_ExecuteCode([this, completion, targets] {
            for (std::size_t i = 0; i < targets.size(); ++i) {
                const auto* texture = device_.GetResourcePool().TextureTable.Get(targets[i].color);
                if (!texture) { completion->value.error = GL_INVALID_VALUE; return; }
                auto& pixels = completion->value.rgba[i];
                pixels.resize(static_cast<std::size_t>(texture->width) * texture->height * 4);
                glGetTextureImage(texture->rhi_id, 0, GL_RGBA, GL_FLOAT,
                                  static_cast<GLsizei>(pixels.size() * sizeof(float)), pixels.data());
            }
            completion->value.error = glGetError();
        }, [completion] { completion->done = true; });
        Wait(completion, "Bloom float readback");
        Check(completion->value.error == GL_NO_ERROR, "OpenGL error during Bloom draw/readback: " +
              std::to_string(completion->value.error));
        for (const auto& image : completion->value.rgba) {
            Check(!image.empty(), "Empty Bloom readback");
            for (std::size_t i = 0; i < image.size(); ++i) {
                // Avoid allocating an error string for every HDR component in
                // the larger pyramid fixtures, including unoptimized builds.
                if (!std::isfinite(image[i]) || image[i] < 0 || image[i] > 60000)
                    throw std::runtime_error("Bloom generated invalid HDR output");
                if (i % 4 == 3 && image[i] != 1.0f)
                    throw std::runtime_error("Fullscreen pass did not cover the complete target");
            }
        }
        return std::move(completion->value);
    }

    template <typename T> void Wait(const std::shared_ptr<Completion<T>>& completion, const char* label) {
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!completion->done) {
            device_.returnSystem.DrainCallbacks();
            if (completion->done) break;
            Check(std::chrono::steady_clock::now() < deadline, std::string("Timed out waiting for ") + label);
            glfwPollEvents();
            device_.returnSystem.WaitForCallbacks(2ms);
        }
    }
    void Pass(RHIFrameEncoder& encoder, const Target& destination, RenderResourceHandle<PipelineSpec> pipeline,
              RenderResourceHandle<RHITextureSpec> input, const PostProcessConstants& constants,
              RenderResourceHandle<RHITextureSpec> second = {}) {
        RHICommand::SetRenderTarget target;
        target.target = destination.framebuffer; target.width = destination.width; target.height = destination.height;
        Check(encoder.SetRenderTarget(target) && encoder.BindPipeline(pipeline) && encoder.BindMesh(mesh_) &&
              encoder.UpdateUniformBuffer(uniform_, constants) && encoder.BindUniformBuffer(uniform_, PostProcessBinding) &&
              encoder.BindTexture(input, 0) && encoder.BindTexture(second, 1) && encoder.BindTexture({}, 2) &&
              encoder.DrawIndexed(RHICommand::DrawIndexed{3}), "Cannot record Bloom pass");
    }
    RHIDevice device_;
    std::shared_ptr<const void> owner_;
    RenderResourceHandle<PipelineSpec> source_, extract_, previous_, downsample_;
    RenderResourceHandle<PipelineSpec> upsample_, previousUpsample_;
    RenderResourceHandle<VertexBufferSpec> mesh_;
    RenderResourceHandle<UniformBufferSpec> uniform_;
    std::uint64_t frameIndex_ = 0;
};

double Energy(const std::vector<float>& rgba) {
    double energy = 0;
    for (std::size_t i = 0; i < rgba.size(); i += 4) energy += rgba[i];
    return energy;
}
struct Variation { double mean = 0, relativeRange = 0, coefficientOfVariation = 0; };
Variation Summarize(const std::vector<double>& energies) {
    Variation result;
    result.mean = std::accumulate(energies.begin(), energies.end(), 0.0) / energies.size();
    const auto [minimum, maximum] = std::minmax_element(energies.begin(), energies.end());
    double variance = 0;
    for (const double energy : energies) variance += (energy - result.mean) * (energy - result.mean);
    result.relativeRange = (*maximum - *minimum) / std::max(result.mean, 1e-12);
    result.coefficientOfVariation = std::sqrt(variance / energies.size()) / std::max(result.mean, 1e-12);
    return result;
}

void TestConstantAndDimensions(BloomFixture& fixture) {
    for (const auto size : {glm::uvec2(64, 64), glm::uvec2(63, 37), glm::uvec2(1, 31),
                            glm::uvec2(31, 1), glm::uvec2(1, 1)}) {
        auto images = fixture.Allocate(size.x, size.y);
        auto constants = MakePostProcessConstants({});
        constants.misc = {0, 0, 1, 100};
        const auto pixels = fixture.Render(images, constants);
        double maximumError = 0;
        for (const std::size_t image : {1u, 3u}) {
            for (std::size_t i = 0; i < pixels.rgba[image].size(); ++i) {
                if (i % 4 == 3) continue;
                maximumError = std::max(maximumError, std::abs(double(pixels.rgba[image][i]) - 99.2));
            }
        }
        Check(maximumError < 0.13, "Constant HDR was dimmed by Bloom stabilization or downsampling");
        std::cout << "Constant HDR 100, " << size.x << 'x' << size.y << ": max error=" << maximumError << '\n';
        constants.bloomStability.y = 20;
        const auto withSuppression = fixture.Render(images, constants);
        for (const std::size_t image : {1u, 3u})
            for (std::size_t i = 0; i < withSuppression.rgba[image].size(); ++i)
                if (i % 4 != 3) Check(std::abs(withSuppression.rgba[image][i] - 99.2f) < 0.13f,
                                     "Optional firefly suppression attenuated a constant HDR field");
        // Also exercise an asymmetric bright sample touching the clamp edge.
        constants.misc = {0.75f, 0.75f, 0, 1500};
        fixture.Render(images, constants);
    }
}

void TestMovingHighlight(BloomFixture& fixture) {
    auto images = fixture.Allocate(64, 64);
    for (const float amplitude : {32.0f, 128.0f, 1500.0f}) {
        auto constants = MakePostProcessConstants({});
        std::vector<double> sourceEnergy, filteredEnergy, previousEnergy, reducedEnergy;
        for (int phase = 0; phase < 33; ++phase) {
            const float offset = 2.0f * phase / 32.0f;
            constants.misc = {31.0f + offset, 31.375f + offset * 0.37f, 0, amplitude};
            const auto pixels = fixture.Render(images, constants);
            sourceEnergy.push_back(Energy(pixels.rgba[0]));
            filteredEnergy.push_back(Energy(pixels.rgba[1]));
            previousEnergy.push_back(Energy(pixels.rgba[2]));
            // A pixel in the next mip covers four times the area.
            reducedEnergy.push_back(4.0 * Energy(pixels.rgba[3]));
            Check(std::abs(reducedEnergy.back() - filteredEnergy.back()) < filteredEnergy.back() * 0.003,
                  "Bloom downsample changed the integrated highlight energy");
        }
        const auto source = Summarize(sourceEnergy), filtered = Summarize(filteredEnergy);
        const auto previous = Summarize(previousEnergy), reduced = Summarize(reducedEnergy);
        std::cout << "Moving highlight amplitude=" << amplitude << ": source range/mean=" << source.relativeRange << '\n';
        std::cout << "Previous extraction: mean=" << previous.mean << " range/mean=" << previous.relativeRange
                  << " stddev/mean=" << previous.coefficientOfVariation << '\n';
        std::cout << "Stable extraction: mean=" << filtered.mean << " range/mean=" << filtered.relativeRange
                  << " stddev/mean=" << filtered.coefficientOfVariation
                  << " next-mip stddev/mean=" << reduced.coefficientOfVariation << '\n';
        Check(source.relativeRange < 0.001, "Moving source changed energy, invalidating the filter comparison");
        Check(previous.mean > 0.1 && filtered.mean > previous.mean * 0.9, "Bloom comparison lost the highlight");
        Check(previous.coefficientOfVariation > 0.001, "Reference fixture did not exercise extraction flicker");
        Check(filtered.coefficientOfVariation < previous.coefficientOfVariation * 0.5 &&
              filtered.relativeRange < previous.relativeRange * 0.5,
              "Bloom prefilter did not halve subpixel-phase energy variation");
        Check(reduced.coefficientOfVariation < previous.coefficientOfVariation * 0.5,
              "Later Bloom mip reintroduced subpixel-phase flicker");
    }
}

struct HaloProfile {
    double energy = 0, rmsRadius = 0, coreFraction = 0, peakFraction = 0;
};
HaloProfile Profile(const std::vector<float>& rgba, std::uint32_t width, std::uint32_t height) {
    HaloProfile result;
    result.energy = Energy(rgba);
    Check(result.energy > 0, "Bloom impulse vanished");
    double centerX = 0, centerY = 0;
    for (std::uint32_t y = 0; y < height; ++y) for (std::uint32_t x = 0; x < width; ++x) {
        const double value = rgba[(y * width + x) * 4];
        centerX += value * (x + .5); centerY += value * (y + .5);
    }
    centerX /= result.energy; centerY /= result.energy;
    for (std::uint32_t y = 0; y < height; ++y) for (std::uint32_t x = 0; x < width; ++x) {
        const double value = rgba[(y * width + x) * 4];
        const double dx = x + .5 - centerX, dy = y + .5 - centerY;
        const double radiusSquared = dx * dx + dy * dy;
        result.rmsRadius += value * radiusSquared;
        if (radiusSquared <= 16) result.coreFraction += value;
        result.peakFraction = std::max(result.peakFraction, value);
    }
    result.rmsRadius = std::sqrt(result.rmsRadius / result.energy);
    result.coreFraction /= result.energy;
    result.peakFraction /= result.energy;
    return result;
}

// Compare images after removing their known translation and total intensity.
// This measures a changing halo shape, not the intended movement/threshold
// response. Readback analysis does not reimplement any Bloom filter.
double AlignedShapeDifference(const std::vector<float>& previous, const std::vector<float>& current,
                              std::uint32_t width, std::uint32_t height, double shiftX, double shiftY) {
    const double previousEnergy = Energy(previous), currentEnergy = Energy(current);
    const auto read = [&](int x, int y) {
        x = std::clamp(x, 0, static_cast<int>(width) - 1);
        y = std::clamp(y, 0, static_cast<int>(height) - 1);
        return double(current[(y * width + x) * 4]);
    };
    double difference = 0;
    for (std::uint32_t y = 0; y < height; ++y) for (std::uint32_t x = 0; x < width; ++x) {
        const double px = x + shiftX, py = y + shiftY;
        const int ix = static_cast<int>(std::floor(px)), iy = static_cast<int>(std::floor(py));
        const double fx = px - ix, fy = py - iy;
        const double row0 = read(ix, iy) * (1 - fx) + read(ix + 1, iy) * fx;
        const double row1 = read(ix, iy + 1) * (1 - fx) + read(ix + 1, iy + 1) * fx;
        const double aligned = row0 * (1 - fy) + row1 * fy;
        difference += std::abs(aligned / currentEnergy - previous[(y * width + x) * 4] / previousEnergy);
    }
    return difference;
}

void TestPyramidConstants(BloomFixture& fixture) {
    auto constants = MakePostProcessConstants({});
    constants.misc = {0, 0, 1, 100};
    auto images = fixture.AllocatePyramid(128, 128, MaxBloomLevels);
    for (std::uint32_t levels = 1; levels <= MaxBloomLevels; ++levels) {
        const auto pixels = fixture.RenderPyramid(images, constants, levels);
        double maximumError = 0;
        for (std::size_t i = 0; i < pixels.rgba[3].size(); i += 4)
            maximumError = std::max(maximumError, std::abs(double(pixels.rgba[3][i]) - 99.2));
        std::cout << "Pyramid constant, levels=" << levels << ": new=" << pixels.rgba[3][0]
                  << ", previous=" << pixels.rgba[2][0] << '\n';
        Check(maximumError < .13, "Adding Bloom levels changed constant HDR intensity");
    }
    for (const auto size : {glm::uvec2(127,71), glm::uvec2(1,31), glm::uvec2(31,1), glm::uvec2(1,1)}) {
        auto odd = fixture.AllocatePyramid(size.x, size.y, MaxBloomLevels);
        for (const float scatter : {0.0f, 0.8f, 1.0f}) for (const float radius : {.5f, 2.0f}) {
            constants.bloomStability.z = scatter;
            constants.bloomStability.w = radius;
            constants.misc = {0, 0, 1, 100};
            const auto pixels = fixture.RenderPyramid(odd, constants);
            for (std::size_t i = 0; i < pixels.rgba[3].size(); i += 4)
                Check(std::abs(pixels.rgba[3][i] - 99.2f) < .13f, "Odd/1xN pyramid or scatter/radius changed constant HDR");
            // Also exercise a nonuniform edge impulse at each extreme.
            constants.misc = {.75f, .75f, 0, 1500};
            fixture.RenderPyramid(odd, constants);
        }
    }
}

void TestPyramidImpulse(BloomFixture& fixture) {
    // Sufficient padding for all six levels: the complete filter support stays
    // inside the image, so border clamping cannot hide an energy gain/loss.
    auto images = fixture.AllocatePyramid(1024, 1024, MaxBloomLevels);
    auto constants = MakePostProcessConstants({});
    constants.misc = {512.25f, 512.375f, 0, 1500};
    for (std::uint32_t levels = 1; levels <= MaxBloomLevels; ++levels) {
        const auto pixels = fixture.RenderPyramid(images, constants, levels);
        const double gain = Energy(pixels.rgba[3]) / Energy(pixels.rgba[1]);
        std::cout << "Pyramid impulse, levels=" << levels << ": energy gain=" << gain << '\n';
        Check(std::abs(gain - 1) < .008, "Bloom mip reconstruction changed integrated impulse energy");
        if (levels == 5) {
            const auto old = Profile(pixels.rgba[2], images.down[0].width, images.down[0].height);
            const auto current = Profile(pixels.rgba[3], images.down[0].width, images.down[0].height);
            std::cout << "Five-level halo previous/new: RMS radius=" << old.rmsRadius << '/' << current.rmsRadius
                      << ", core fraction=" << old.coreFraction << '/' << current.coreFraction
                      << ", peak fraction=" << old.peakFraction << '/' << current.peakFraction << '\n';
            Check(current.rmsRadius > old.rmsRadius * 1.5 && current.coreFraction < old.coreFraction * .7 &&
                  current.peakFraction < old.peakFraction * .65,
                  "Normalized Bloom did not redistribute the hard core into a softer halo");
        }
    }
    std::array<HaloProfile, 2> profiles;
    for (std::size_t i = 0; i < profiles.size(); ++i) {
        constants.bloomStability.w = i == 0 ? .5f : 2.0f;
        profiles[i] = Profile(fixture.RenderPyramid(images, constants, 5).rgba[3], images.down[0].width, images.down[0].height);
    }
    std::cout << "Halo radius .5/2: RMS=" << profiles[0].rmsRadius << '/' << profiles[1].rmsRadius << '\n';
    Check(profiles[1].rmsRadius > profiles[0].rmsRadius * 1.2 &&
          std::abs(profiles[1].energy / profiles[0].energy - 1) < .008,
          "Bloom radius failed to widen the halo without changing intensity");
}

void TestMovingPyramid(BloomFixture& fixture) {
    auto images = fixture.AllocatePyramid(512, 512, 5);
    for (const float amplitude : {32.0f, 128.0f, 1500.0f}) {
        auto constants = MakePostProcessConstants({});
        std::vector<double> extractedEnergy, currentEnergy, oldEnergy;
        std::vector<float> previousCurrent, previousOld;
        double currentShape = 0, oldShape = 0, worstGainError = 0;
        for (int phase = 0; phase < 33; ++phase) {
            const float offset = 2.0f * phase / 32.0f;
            constants.misc = {255.0f + offset, 255.375f + offset * .37f, 0, amplitude};
            auto pixels = fixture.RenderPyramid(images, constants);
            extractedEnergy.push_back(Energy(pixels.rgba[1]));
            oldEnergy.push_back(Energy(pixels.rgba[2]));
            currentEnergy.push_back(Energy(pixels.rgba[3]));
            worstGainError = std::max(worstGainError, std::abs(currentEnergy.back() / extractedEnergy.back() - 1));
            if (phase > 0) {
                currentShape += AlignedShapeDifference(previousCurrent, pixels.rgba[3], images.down[0].width,
                                                       images.down[0].height, 1.0/32, .37/32);
                oldShape += AlignedShapeDifference(previousOld, pixels.rgba[2], images.down[0].width,
                                                   images.down[0].height, 1.0/32, .37/32);
            }
            previousCurrent = std::move(pixels.rgba[3]);
            previousOld = std::move(pixels.rgba[2]);
        }
        const auto extracted = Summarize(extractedEnergy), current = Summarize(currentEnergy), old = Summarize(oldEnergy);
        std::cout << "Moving five-level halo amplitude=" << amplitude << ": extracted/old/new energy CV="
                  << extracted.coefficientOfVariation << '/' << old.coefficientOfVariation << '/' << current.coefficientOfVariation
                  << ", old/new aligned shape delta=" << oldShape / 32 << '/' << currentShape / 32
                  << ", worst gain error=" << worstGainError << '\n';
        Check(worstGainError < .008 && current.coefficientOfVariation < extracted.coefficientOfVariation + .001,
              "Complete Bloom pyramid added moving-highlight energy flicker");
        Check(currentShape < oldShape * .7, "Softer Bloom did not reduce phase-dependent halo shape changes");
    }
}
}

int main() {
    int result = 0;
    try {
        Check(glfwInit() == GLFW_TRUE, "glfwInit failed");
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        ApplicationWindow::Window window({64, 64, "Bloom stability GPU test", false});
        Check(window.Activate(), "Hidden OpenGL 4.5 window creation failed");
        auto context = window.GetRenderContextAsOpengl();
        Check(context.has_value(), "Missing OpenGL context");
        BloomFixture fixture(std::move(*context));
        fixture.Initialize();
        std::cout << std::unitbuf << std::fixed << std::setprecision(6);
        TestConstantAndDimensions(fixture);
        TestMovingHighlight(fixture);
        TestPyramidConstants(fixture);
        TestPyramidImpulse(fixture);
        TestMovingPyramid(fixture);
        fixture.Shutdown();
        std::cout << "Bloom GPU stability passed.\n";
    } catch (const std::exception& error) {
        std::cerr << "Bloom GPU stability failed: " << error.what() << '\n';
        result = 1;
    }
    glfwTerminate();
    return result;
}
