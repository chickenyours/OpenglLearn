#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <glad/glad.h>
#include "ApplicationWindow/public/window.h"
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Private/Pipeline/post_process_shaders.h"
#include "Render/Private/Pipeline/temporal_effects_shaders.h"

namespace {
using namespace Render;
using namespace std::chrono_literals;
constexpr std::uint32_t Width = 64, Height = 64;
using Pixels = std::vector<float>;
using Texture = RenderResourceHandle<RHITextureSpec>;
using Pipeline = RenderResourceHandle<PipelineSpec>;

void Check(bool result, const std::string& message) { if (!result) throw std::runtime_error(message); }
template <class T> struct Completion { T value{}; bool done = false; };

float MeanDifference(const Pixels& a, const Pixels& b) {
    Check(a.size() == b.size(), "Readback dimensions differ");
    double sum = 0;
    for (std::size_t i = 0; i < a.size(); ++i) if (i % 4 != 3) sum += std::abs(a[i] - b[i]);
    return static_cast<float>(sum / (a.size() / 4 * 3));
}
float MaximumDifference(const Pixels& a, const Pixels& b) {
    Check(a.size() == b.size(), "Readback dimensions differ");
    float result = 0;
    for (std::size_t i = 0; i < a.size(); ++i) result = std::max(result, std::abs(a[i] - b[i]));
    return result;
}
float InteriorDifference(const Pixels& a, const Pixels& b) {
    double sum = 0;
    for (std::uint32_t y = 3; y + 3 < Height; ++y) for (std::uint32_t x = 3; x + 3 < Width; ++x)
        for (std::uint32_t channel = 0; channel < 3; ++channel) {
            const auto index = (y * Width + x) * 4 + channel;
            sum += std::abs(a[index] - b[index]);
        }
    return static_cast<float>(sum / ((Width - 6) * (Height - 6) * 3));
}
Pixels ColorImage(const std::function<glm::vec3(std::uint32_t, std::uint32_t)>& pixel) {
    Pixels result(Width * Height * 4);
    for (std::uint32_t y = 0; y < Height; ++y) for (std::uint32_t x = 0; x < Width; ++x) {
        const auto color = pixel(x, y);
        const auto offset = (y * Width + x) * 4;
        result[offset] = color.r; result[offset + 1] = color.g; result[offset + 2] = color.b; result[offset + 3] = 1;
    }
    return result;
}

class TemporalGpuTest {
public:
    explicit TemporalGpuTest(OpenglBackendContext context) { device_.Run(BackendType::Opengl, std::move(context)); }
    ~TemporalGpuTest() { Shutdown(); }
    void Shutdown() {
        if (stopped_) return;
        owner_.reset();
        device_.StopAndRelease();
        device_.returnSystem.DrainCallbacks();
        stopped_ = true;
    }
    void Run() {
        Setup();
        InspectUniform(fxaa_, "screen", 336);
        InspectUniform(taa_, "previousInverseViewProjection", 128);
        InspectUniform(blur_, "previousUnjitteredViewProjection", 256);
        CheckFxaa();
        CheckTaa();
        CheckReactive();
        CheckStableJitterResolve();
        CheckMotion();
        CheckDepthCopy();
        std::cout << "Temporal effects GPU test passed: real GLSL/400-byte UBO, FXAA, TAA rejection, camera blur and Depth32F copy\n";
    }
private:
    template <class Predicate> void Wait(Predicate done, const char* label) {
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!done()) {
            device_.returnSystem.DrainCallbacks();
            if (done()) break;
            Check(std::chrono::steady_clock::now() < deadline, std::string("Timeout: ") + label);
            glfwPollEvents();
            device_.returnSystem.WaitForCallbacks(5ms);
        }
    }
    Texture Input(PipelineDetail::ResourceBuilder& builder, const Pixels& pixels, bool depth = false) {
        CreateRHITextureSpec desc;
        desc.width = Width; desc.height = Height;
        desc.textureDataStoreType = desc.textureUseType = depth ? RHITextureFormat::Depth32F : RHITextureFormat::RGBA16F;
        desc.filterMode = depth ? RHIFilterMode::Nearest : RHIFilterMode::Linear;
        desc.addressMode = RHIAddressMode::ClampToEdge;
        desc.mipmaps = false; desc.data = pixels.data();
        return builder.Create<RHITextureSpec>([&](auto callback) { device_.async_CreateTexture(desc, callback); });
    }
    void Setup() {
        PipelineDetail::ResourceBuilder builder(device_);
        fxaa_ = builder.FullscreenPipeline(FullscreenVertexShader(), FxaaShader());
        taa_ = builder.FullscreenPipeline(FullscreenVertexShader(), TaaShader());
        blur_ = builder.FullscreenPipeline(FullscreenVertexShader(), CameraMotionBlurShader());
        depthCopy_ = builder.FullscreenPipeline(FullscreenVertexShader(), DepthHistoryCopyShader(), true);
        mesh_ = builder.FullscreenMesh();
        uniform_ = builder.Uniform(sizeof(TemporalConstants));
        output_ = builder.Texture(Width, Height, RHITextureFormat::RGBA16F);
        outputDepth_ = builder.Texture(Width, Height, RHITextureFormat::Depth32F);
        alternateOutput_ = builder.Texture(Width, Height, RHITextureFormat::RGBA16F);
        target_ = builder.Target(output_, {});
        alternateTarget_ = builder.Target(alternateOutput_, {});
        depthTarget_ = builder.Target({}, outputDepth_);
        uniformPixels_ = ColorImage([](auto, auto) { return glm::vec3(0.25f, 0.5f, 0.75f); });
        edgePixels_ = ColorImage([](auto x, auto y) { return glm::vec3(x > 14 + y * 0.55f ? 1.0f : 0.0f); });
        checkerPixels_ = ColorImage([](auto x, auto y) { return glm::vec3(((x / 2 + y / 2) % 2) ? 0.8f : 0.2f); });
        stripePixels_ = ColorImage([](auto x, auto) { return glm::vec3((x / 4) % 2 ? 1.0f : 0.0f); });
        uniformColor_ = Input(builder, uniformPixels_);
        edge_ = Input(builder, edgePixels_);
        checker_ = Input(builder, checkerPixels_);
        stripes_ = Input(builder, stripePixels_);
        history_ = Input(builder, ColorImage([](auto, auto) { return glm::vec3(0.6f); }));
        depth_ = Input(builder, Pixels(Width * Height, 0.5f), true);
        rejectedDepth_ = Input(builder, Pixels(Width * Height, 0.8f), true);
        backgroundDepth_ = Input(builder, Pixels(Width * Height, 1.0f), true);
        depthPixels_.resize(Width * Height);
        for (std::size_t i = 0; i < depthPixels_.size(); ++i)
            depthPixels_[i] = static_cast<float>(i) / static_cast<float>(depthPixels_.size() - 1);
        gradientDepth_ = Input(builder, depthPixels_, true);
        gradientPixels_ = ColorImage([](auto x, auto y) {
            return glm::vec3((x + 0.5f) / Width, (y + 0.5f) / Height, 0.5f);
        });
        for (std::uint32_t i = 0; i < TemporalJitterPeriod; ++i) {
            const auto jitter = TemporalJitter(i, Width, Height);
            jitteredGradients_[i] = Input(builder, ColorImage([jitter](auto x, auto y) {
                return glm::vec3((x + 0.5f) / Width - jitter.x, (y + 0.5f) / Height - jitter.y, 0.5f);
            }));
        }
        owner_ = builder.Finish();
    }
    TemporalConstants Constants() const {
        TemporalConstants constants;
        constants.screen.x = 1.0f / Width; constants.screen.y = 1.0f / Height;
        constants.cameraPosition = glm::vec4(0, 0, 2, 1);
        return constants;
    }
    void InspectUniform(Pipeline pipeline, const char* field, GLint expectedOffset) {
        struct Result { GLint bytes = -1, binding = -1, offset = -1; GLenum error = GL_NO_ERROR; };
        auto result = std::make_shared<Completion<Result>>();
        const std::string name(field);
        device_.async_ExecuteCode([this, pipeline, name, result] {
            const auto* pipe = device_.GetResourcePool().PipelineTable.Get(pipeline);
            const auto* program = pipe ? device_.GetResourcePool().shaderProgramTable.Get(pipe->shaderProgram) : nullptr;
            if (program) {
                const GLuint block = glGetUniformBlockIndex(program->rhi_id, "TemporalData");
                if (block != GL_INVALID_INDEX) {
                    glGetActiveUniformBlockiv(program->rhi_id, block, GL_UNIFORM_BLOCK_DATA_SIZE, &result->value.bytes);
                    glGetActiveUniformBlockiv(program->rhi_id, block, GL_UNIFORM_BLOCK_BINDING, &result->value.binding);
                    const char* pointer = name.c_str(); GLuint index = GL_INVALID_INDEX;
                    glGetUniformIndices(program->rhi_id, 1, &pointer, &index);
                    if (index != GL_INVALID_INDEX)
                        glGetActiveUniformsiv(program->rhi_id, 1, &index, GL_UNIFORM_OFFSET, &result->value.offset);
                }
            }
            result->value.error = glGetError();
        }, [result] { result->done = true; });
        Wait([result] { return result->done; }, "uniform reflection");
        Check(result->value.error == GL_NO_ERROR, "GL error reflecting TemporalData");
        Check(result->value.bytes == sizeof(TemporalConstants) && result->value.binding == 6, "TemporalData size/binding mismatch");
        Check(result->value.offset == expectedOffset, "TemporalData member offset mismatch: " + name);
    }
    Pixels Draw(Pipeline pipeline, Texture color, Texture history, Texture depth, Texture oldDepth,
                const TemporalConstants& constants, bool depthOnly = false, bool alternate = false, Texture opaque = {}) {
        RHICommand::BeginFrame begin;
        begin.frameIndex = ++frameIndex_; begin.framebufferWidth = Width; begin.framebufferHeight = Height;
        auto encoder = device_.BeginFrame(begin);
        const auto abort = [&] { encoder.Cancel(); throw std::runtime_error("Could not record temporal GPU test pass"); };
        if (!encoder.KeepAlive(owner_)) abort();
        RHICommand::SetRenderTarget target;
        target.target = depthOnly ? depthTarget_ : (alternate ? alternateTarget_ : target_);
        target.width = Width; target.height = Height;
        target.clearFlags = depthOnly ? RHICommand::ClearDepth : RHICommand::ClearColor;
        if (!encoder.SetRenderTarget(target)) abort();
        const auto* bytes = reinterpret_cast<const std::byte*>(&constants);
        if (!encoder.UpdateUniformBufferBytes(uniform_, bytes, 256, 0) ||
            !encoder.UpdateUniformBufferBytes(uniform_, bytes + 256, sizeof(constants) - 256, 256) ||
            !encoder.BindUniformBuffer(uniform_, TemporalEffectsBinding) ||
            !encoder.BindPipeline(pipeline) || !encoder.BindMesh(mesh_) ||
            !encoder.BindTexture(color, 0) || !encoder.BindTexture(history, 1) ||
            !encoder.BindTexture(depth, 2) || !encoder.BindTexture(oldDepth, 3) ||
            !encoder.BindTexture(opaque, 4) ||
            !encoder.DrawIndexed(RHICommand::DrawIndexed{3}) || !encoder.End(false)) abort();
        auto completed = std::make_shared<Completion<bool>>();
        if (!device_.async_SubmitFrameCommands(encoder.GetCommandBuffer(), [completed] { completed->done = true; })) abort();
        Wait([completed] { return completed->done; }, "effect frame");
        struct Readback { Pixels pixels; GLenum error = GL_NO_ERROR; };
        auto result = std::make_shared<Completion<Readback>>();
        result->value.pixels.resize(Width * Height * (depthOnly ? 1 : 4));
        const auto output = depthOnly ? outputDepth_ : (alternate ? alternateOutput_ : output_);
        device_.async_ExecuteCode([this, output, depthOnly, result] {
            const auto* texture = device_.GetResourcePool().TextureTable.Get(output);
            if (texture) glGetTextureImage(texture->rhi_id, 0, depthOnly ? GL_DEPTH_COMPONENT : GL_RGBA,
                GL_FLOAT, static_cast<GLsizei>(result->value.pixels.size() * sizeof(float)), result->value.pixels.data());
            result->value.error = glGetError();
        }, [result] { result->done = true; });
        Wait([result] { return result->done; }, "effect readback");
        Check(result->value.error == GL_NO_ERROR, "GL error during temporal draw/readback");
        for (float value : result->value.pixels) Check(std::isfinite(value), "Effect produced a nonfinite pixel");
        return result->value.pixels;
    }
    void CheckFxaa() {
        auto constants = Constants();
        const auto uniform = Draw(fxaa_, uniformColor_, {}, {}, {}, constants);
        Check(MaximumDifference(uniform, uniformPixels_) < 0.0005f, "FXAA changed a uniform color");
        const auto edge = Draw(fxaa_, edge_, {}, {}, {}, constants);
        std::size_t smoothed = 0;
        for (std::size_t i = 0; i < edge.size(); i += 4) if (edge[i] > 0.02f && edge[i] < 0.98f) ++smoothed;
        const auto delta = MeanDifference(edge, edgePixels_);
        Check(smoothed > 30 && delta > 0.001f, "FXAA did not smooth the staircase edge");
        std::cout << "FXAA smoothed pixels=" << smoothed << " mean delta=" << delta << '\n';
    }
    void CheckTaa() {
        auto constants = Constants();
        const auto passthrough = Draw(taa_, checker_, history_, depth_, depth_, constants);
        Check(MaximumDifference(passthrough, checkerPixels_) < 0.001f, "Invalid TAA history was not a direct copy");
        constants.temporal.w = 1;
        const auto accumulated = Draw(taa_, checker_, history_, depth_, depth_, constants);
        const auto delta = MeanDifference(accumulated, passthrough);
        Check(delta > 0.08f, "Valid TAA history was not accumulated");
        for (std::size_t i = 0; i < accumulated.size(); i += 4)
            Check(accumulated[i] >= 0.199f && accumulated[i] <= 0.801f, "TAA neighborhood clamp exceeded current bounds");
        const auto occluded = Draw(taa_, checker_, history_, depth_, rejectedDepth_, constants);
        Check(MaximumDifference(occluded, passthrough) < 0.001f, "TAA retained depth-disoccluded history");
        constants.previousViewProjection[3][0] = 4;
        constants.previousInverseViewProjection = glm::inverse(constants.previousViewProjection);
        const auto outside = Draw(taa_, checker_, history_, depth_, depth_, constants);
        Check(MaximumDifference(outside, passthrough) < 0.001f, "TAA sampled history outside the previous viewport");
        constants.previousViewProjection = glm::mat4(1); constants.previousViewProjection[3][3] = -1;
        const auto behind = Draw(taa_, checker_, history_, depth_, depth_, constants);
        Check(MaximumDifference(behind, passthrough) < 0.001f, "TAA retained history behind the camera");
        constants = Constants(); constants.temporal.w = 1;
        const auto clamped = Draw(taa_, uniformColor_, history_, depth_, depth_, constants);
        Check(MaximumDifference(clamped, uniformPixels_) < 0.0005f, "TAA failed to clamp stale color to a uniform neighborhood");
        std::cout << "TAA valid-history mean delta=" << delta << ", depth/UV/w rejection passed\n";
    }
    void CheckReactive() {
        auto constants = Constants(); constants.temporal.w=1; constants.reactive.x=1;
        // The old opaque test history has alpha=1: it models a fully reactive
        // transparent layer that has just moved away, while the current color
        // exactly matches the current opaque snapshot (current mask=0).
        const auto uncovered=Draw(taa_,checker_,history_,depth_,depth_,constants,false,false,checker_);
        Check(MeanDifference(uncovered,checkerPixels_)<.0005f,"Previous transparency left TAA ghosting");
        for (std::size_t i=3;i<uncovered.size();i+=4) Check(uncovered[i]<.001f,"Current opaque mask was not cleared");
        const auto transparent=Draw(taa_,checker_,history_,depth_,depth_,constants,false,false,uniformColor_);
        Check(MeanDifference(transparent,checkerPixels_)<.0005f,"Reactive transparency retained stale background");
        constants.motion.w=1; constants.previousUnjitteredViewProjection[3][0]=-.25f;
        const auto protectedBlur=Draw(blur_,checker_,{},depth_,{},constants,false,false,uniformColor_);
        Check(MeanDifference(protectedBlur,checkerPixels_)<.0005f,"Opaque depth blurred the transparent layer");
        std::cout << "Transparent current/previous reactive history and camera-blur exclusion passed\n";
    }
    void CheckMotion() {
        auto constants = Constants();
        constants.motion.w = 1;
        constants.cameraPosition = glm::vec4(0, 0, 100, 1);
        const auto stationary = Draw(blur_, stripes_, {}, depth_, {}, constants);
        Check(MaximumDifference(stationary, stripePixels_) < 0.001f, "Static camera motion blur changed its image");
        constants.currentInverseViewProjection = glm::inverse(JitterProjection(glm::mat4(1), {0.35f / Width, -0.2f / Height}));
        const auto jitterOnly = Draw(blur_, stripes_, {}, depth_, {}, constants);
        Check(MaximumDifference(jitterOnly, stationary) < 0.001f, "TAA jitter created false camera blur");
        constants.previousUnjitteredViewProjection[3][0] = -0.25f;
        const auto moving = Draw(blur_, stripes_, {}, depth_, {}, constants);
        const auto delta = MeanDifference(moving, stationary);
        Check(delta > 0.15f, "Camera translation did not produce motion blur");
        constants.motion.w = 0;
        const auto invalid = Draw(blur_, stripes_, {}, depth_, {}, constants);
        Check(MaximumDifference(invalid, stationary) < 0.001f, "Invalid camera history still blurred");
        constants.motion.w = 1;
        const auto background = Draw(blur_, stripes_, {}, backgroundDepth_, {}, constants);
        Check(MaximumDifference(background, stationary) < 0.001f, "Background was treated as a finite moving surface");
        std::cout << "Camera motion mean delta=" << delta << ", stationary/jitter/background copy passed\n";
    }
    void CheckStableJitterResolve() {
        // Analytic static world gradient, rasterized on eight distinct jitter
        // grids. The history COLOR must resolve onto one stable display grid;
        // tracking the changing jitter grid fails this test even if blending
        // itself works and the history depth comparison succeeds.
        Pixels previous;
        float maximumError = 0, maximumChange = 0;
        auto constants = Constants();
        glm::mat4 previousJittered(1);
        for (std::uint32_t frame = 0; frame < 24; ++frame) {
            const auto projection = JitterProjection(glm::mat4(1), TemporalJitter(frame, Width, Height));
            constants.currentInverseViewProjection = glm::inverse(projection);
            constants.previousViewProjection = previousJittered;
            constants.previousInverseViewProjection = glm::inverse(previousJittered);
            constants.temporal.w = frame ? 1.0f : 0.0f;
            const bool alternate = frame % 2 != 0;
            const auto history = frame ? (alternate ? output_ : alternateOutput_) : history_;
            auto current = Draw(taa_, jitteredGradients_[frame % TemporalJitterPeriod], history,
                                depth_, depth_, constants, false, alternate);
            maximumError = std::max(maximumError, InteriorDifference(current, gradientPixels_));
            if (!previous.empty()) maximumChange = std::max(maximumChange, InteriorDifference(current, previous));
            previous = std::move(current);
            previousJittered = projection;
        }
        std::cout << "TAA stable-grid gradient max error=" << maximumError << " max adjacent delta=" << maximumChange << '\n';
        Check(maximumError < 0.0006f && maximumChange < 0.0006f,
              "TAA history/output follows jitter instead of the stable display grid");
    }
    void CheckDepthCopy() {
        const auto copied = Draw(depthCopy_, {}, {}, gradientDepth_, {}, Constants(), true);
        Check(MaximumDifference(copied, depthPixels_) < 1e-6f, "History depth copy lost Depth32F precision");
    }

    RHIDevice device_;
    std::shared_ptr<const void> owner_;
    Pipeline fxaa_, taa_, blur_, depthCopy_;
    RenderResourceHandle<VertexBufferSpec> mesh_;
    RenderResourceHandle<UniformBufferSpec> uniform_;
    RenderResourceHandle<RenderTargetSpec> target_, depthTarget_, alternateTarget_;
    Texture output_, alternateOutput_, outputDepth_, uniformColor_, edge_, checker_, stripes_, history_;
    Texture depth_, rejectedDepth_, backgroundDepth_, gradientDepth_;
    std::array<Texture, TemporalJitterPeriod> jitteredGradients_;
    Pixels uniformPixels_, edgePixels_, checkerPixels_, stripePixels_, depthPixels_, gradientPixels_;
    std::uint64_t frameIndex_ = 0;
    bool stopped_ = false;
};
} // namespace

int main() {
    int result = 0;
    try {
        Check(glfwInit() == GLFW_TRUE, "glfwInit failed");
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        ApplicationWindow::Window window({Width, Height, "Temporal effects GPU test", false});
        Check(window.Activate(), "Could not activate hidden OpenGL 4.5 window");
        auto context = window.GetRenderContextAsOpengl();
        Check(context.has_value(), "No OpenGL context");
        TemporalGpuTest test(std::move(*context));
        test.Run();
        test.Shutdown();
    } catch (const std::exception& error) {
        std::cerr << "Temporal effects GPU test failed: " << error.what() << '\n';
        result = 1;
    }
    glfwTerminate();
    return result;
}
