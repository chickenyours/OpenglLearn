#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include <glad/glad.h>
#include "ApplicationWindow/public/window.h"
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Public/Material/pbr_material.h"

namespace {
using namespace Render;
using namespace std::chrono_literals;
constexpr std::uint32_t Width = 64, Height = 64;
using Pixels = std::vector<float>;
template <class T> struct Completion { T value{}; bool done = false; };
void Check(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

// This independent normal patch isolates BRDF sampling from shadow filtering,
// post processing, temporal history and triangle coverage. The production PBR
// fragment shader receives an analytic smooth normal field, translated over a
// pixel in 33 phases. Both point and directional lighting use the actual BRDF.
// There is deliberately no sphere tessellation whose silhouette can change.
std::string PatchVertexShader() {
    return R"GLSL(#version 450 core
layout(location=0) in vec2 position;
layout(location=1) in vec2 texCoord;
layout(std140,binding=7) uniform PatchData { vec4 patchOptions; };
out vec3 vWorldPosition;
out vec3 vWorldNormal;
out vec2 vTexCoord;
out vec4 vWorldTangent;
void main() {
    gl_Position=vec4(position,0,1);
    vWorldPosition=vec3(0);
    vWorldNormal=vec3((position+patchOptions.xy)*patchOptions.z,1);
    vWorldTangent=vec4(1,0,0,1);
    vTexCoord=texCoord;
}
)GLSL";
}

struct alignas(16) ViewConstants {
    glm::mat4 viewProjection{1};
    glm::vec4 cameraPosition{0, 0, 10, 0};
};
struct Stats { double energy = 0; float peak = 0; };
Stats Measure(const Pixels& image) {
    Stats result;
    for (std::size_t i = 0; i < image.size(); i += 4) {
        const auto value = image[i];
        Check(std::isfinite(value) && value >= 0 && value < 60000, "Invalid/clipped HDR BRDF sample");
        result.energy += value;
        result.peak = std::max(result.peak, value);
    }
    return result;
}
double RelativeVariation(const std::vector<double>& values) {
    const double mean = std::accumulate(values.begin(), values.end(), 0.0) / values.size();
    Check(mean > 0, "BRDF energy is zero");
    const auto extremes = std::minmax_element(values.begin(), values.end());
    return (*extremes.second - *extremes.first) / mean;
}
double RelativeDifference(const Pixels& a, const Pixels& b) {
    double difference = 0, total = 0;
    for (std::size_t i = 0; i < a.size(); i += 4) {
        difference += std::abs(a[i] - b[i]); total += std::abs(a[i]);
    }
    return difference / std::max(total, 1e-12);
}

class Fixture {
public:
    explicit Fixture(OpenglBackendContext context) { device_.Run(BackendType::Opengl, std::move(context)); }
    ~Fixture() { Shutdown(); }
    void Shutdown() {
        if (stopped_) return;
        owner_.reset();
        device_.StopAndRelease();
        device_.returnSystem.DrainCallbacks();
        stopped_ = true;
    }
    void Run() {
        Setup();
        InspectUniform();
        // An exactly flat normal field has no added variance, at any roughness.
        for (float roughness : {0.045f, 0.25f, 0.8f, 1.0f}) {
            auto off = Draw(false, roughness, 0, 0, false);
            auto on = Draw(true, roughness, 0, 0, false);
            Check(RelativeDifference(off, on) == 0, "Specular AA changed a flat normal field");
        }
        auto fullyRoughOff = Draw(false, 1, 0.31f, 4, false);
        auto fullyRoughOn = Draw(true, 1, 0.31f, 4, false);
        Check(RelativeDifference(fullyRoughOff, fullyRoughOn) == 0, "Specular AA changed fully rough shading");
        auto roughOff = Draw(false, 0.8f, 0.31f, 4, false);
        auto roughOn = Draw(true, 0.8f, 0.31f, 4, false);
        const auto roughDifference = RelativeDifference(roughOff, roughOn);
        Check(roughDifference < 0.03, "Specular AA substantially changed high-roughness shading");
        std::cout << "Flat and roughness=1 unchanged; roughness=.8 relative delta=" << roughDifference << '\n';
        for (bool pointLight : {false, true}) for (float roughness : {0.045f, 0.08f})
            CheckPhases(pointLight, roughness);
        std::cout << "PBR specular AA GPU test passed: real BRDF, 176-byte lighting UBO, flat/rough controls and phase stability\n";
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
    void Setup() {
        PipelineDetail::ResourceBuilder builder(device_);
        pipeline_ = builder.FullscreenPipeline(PatchVertexShader(), Material::PbrFragmentShader());
        // Also link the public mesh VS/FS pair, independent of the test vertex shader.
        CreateGraphicShaderDesc program;
        program.vertexShaderSource = builder.Source(ShaderSourceType::Vertex, Material::PbrVertexShader());
        program.fragmentShaderSource = builder.Source(ShaderSourceType::Fragment, Material::PbrFragmentShader());
        builder.Create<ShaderProgramSpec>([&](auto cb) { device_.async_CreateGraphicShader(program, cb); });
        mesh_ = builder.FullscreenMesh();
        view_ = builder.Uniform(sizeof(ViewConstants));
        material_ = builder.Uniform(Material::MakePbrTemplate()->ByteSize());
        lighting_ = builder.Uniform(sizeof(Material::PbrPassConstants));
        effects_ = builder.Uniform(sizeof(SceneEffectsConstants));
        patch_ = builder.Uniform(sizeof(glm::vec4));
        output_ = builder.Texture(Width, Height, RHITextureFormat::RGBA16F);
        target_ = builder.Target(output_, {});
        const std::array<float, 4> white{1,1,1,1}, flatNormal{.5f,.5f,1,1};
        const auto texture = [&](const auto& data) {
            CreateRHITextureSpec desc;
            desc.width = desc.height = 1;
            desc.textureDataStoreType = desc.textureUseType = RHITextureFormat::RGBA16F;
            desc.mipmaps = false; desc.filterMode = RHIFilterMode::Nearest;
            desc.addressMode = RHIAddressMode::ClampToEdge; desc.data = data.data();
            return builder.Create<RHITextureSpec>([&](auto cb) { device_.async_CreateTexture(desc, cb); });
        };
        white_ = texture(white); flatNormal_ = texture(flatNormal);
        owner_ = builder.Finish();
    }
    void InspectUniform() {
        struct Result { GLint size = -1, binding = -1, offset = -1; GLenum error = GL_NO_ERROR; };
        auto result = std::make_shared<Completion<Result>>();
        device_.async_ExecuteCode([this, result] {
            const auto* pipeline = device_.GetResourcePool().PipelineTable.Get(pipeline_);
            const auto* program = pipeline ? device_.GetResourcePool().shaderProgramTable.Get(pipeline->shaderProgram) : nullptr;
            if (program) {
                auto block = glGetUniformBlockIndex(program->rhi_id, "PbrPassData");
                if (block != GL_INVALID_INDEX) {
                    glGetActiveUniformBlockiv(program->rhi_id, block, GL_UNIFORM_BLOCK_DATA_SIZE, &result->value.size);
                    glGetActiveUniformBlockiv(program->rhi_id, block, GL_UNIFORM_BLOCK_BINDING, &result->value.binding);
                    const char* name = "specularAA"; GLuint index = GL_INVALID_INDEX;
                    glGetUniformIndices(program->rhi_id, 1, &name, &index);
                    if (index != GL_INVALID_INDEX)
                        glGetActiveUniformsiv(program->rhi_id, 1, &index, GL_UNIFORM_OFFSET, &result->value.offset);
                }
            }
            result->value.error = glGetError();
        }, [result] { result->done = true; });
        Wait([result] { return result->done; }, "PBR uniform reflection");
        Check(result->value.error == GL_NO_ERROR && result->value.size == 176 &&
              result->value.binding == 3 && result->value.offset == 160, "PBR specular AA uniform ABI mismatch");
    }
    Pixels Draw(bool enabled, float roughness, float phase, float normalSpan, bool pointLight) {
        Material::ParameterBlock material(Material::MakePbrTemplate());
        Check(material.Set("baseColor", glm::vec4(.8f,.8f,.8f,1)) &&
              material.Set("metallic", 1.0f) && material.Set("roughness", roughness), "Cannot set test material");
        Material::PbrPassConstants lighting;
        lighting.ambientAndExposure = glm::vec4(0);
        for (auto& color : lighting.lightColors) color = glm::vec4(0);
        lighting.specularAA.z = enabled ? 1.0f : 0.0f;
        SceneEffectsConstants effects;
        effects.sunDirectionIntensity = glm::vec4(0,0,-1,pointLight ? 0 : 1);
        effects.sunColor = glm::vec4(1);
        if (pointLight) {
            lighting.lightPositions[0] = glm::vec4(0,0,10,1);
            lighting.lightColors[0] = glm::vec4(100,100,100,0);
        }
        const glm::vec4 patch(phase * 2 / Width, phase * 2 / Height, normalSpan, 0);
        RHICommand::BeginFrame begin;
        begin.frameIndex = ++frame_; begin.framebufferWidth = Width; begin.framebufferHeight = Height;
        auto encoder = device_.BeginFrame(begin);
        try {
            Check(encoder.KeepAlive(owner_), "Cannot retain PBR test resources");
            RHICommand::SetRenderTarget target;
            target.target = target_; target.width = Width; target.height = Height;
            target.clearFlags = RHICommand::ClearColor;
            Check(encoder.SetRenderTarget(target), "Cannot select PBR test target");
            Check(encoder.UpdateUniformBuffer(view_, ViewConstants{}) && encoder.BindUniformBuffer(view_, 0) &&
                  encoder.UpdateUniformBufferBytes(material_, material.Bytes().data(), material.Bytes().size()) &&
                  encoder.BindUniformBuffer(material_, Material::MaterialParameterBinding) &&
                  encoder.UpdateUniformBuffer(lighting_, lighting) && encoder.BindUniformBuffer(lighting_, 3) &&
                  encoder.UpdateUniformBuffer(patch_, patch) && encoder.BindUniformBuffer(patch_, 7), "Cannot upload PBR test blocks");
            const auto* bytes = reinterpret_cast<const std::byte*>(&effects);
            for (std::uint32_t offset = 0; offset < sizeof(effects); offset += RHICommand::MaxInlineUniformBytes)
                Check(encoder.UpdateUniformBufferBytes(effects_, bytes + offset,
                    std::min<std::uint32_t>(RHICommand::MaxInlineUniformBytes, sizeof(effects)-offset), offset),
                    "Cannot upload scene block");
            Check(encoder.BindUniformBuffer(effects_, SceneEffectsBinding), "Cannot bind scene block");
            for (std::uint32_t slot : {0u,1u,2u,3u,4u,12u,13u,14u})
                Check(encoder.BindTexture(slot == 1 ? flatNormal_ : white_, slot), "Cannot bind PBR fallback texture");
            Check(encoder.BindPipeline(pipeline_) && encoder.BindMesh(mesh_) &&
                  encoder.DrawIndexed(RHICommand::DrawIndexed{3}) && encoder.End(false), "Cannot record PBR draw");
            auto complete = std::make_shared<Completion<bool>>();
            Check(device_.async_SubmitFrameCommands(encoder.GetCommandBuffer(), [complete] { complete->done = true; }),
                  "Cannot submit PBR draw");
            Wait([complete] { return complete->done; }, "PBR draw");
        } catch (...) { encoder.Cancel(); throw; }
        struct Readback { Pixels pixels = Pixels(Width * Height * 4); GLenum error = GL_NO_ERROR; bool valid = false; };
        auto result = std::make_shared<Completion<Readback>>();
        device_.async_ExecuteCode([this, result] {
            const auto* texture = device_.GetResourcePool().TextureTable.Get(output_);
            if (texture) {
                glGetTextureImage(texture->rhi_id, 0, GL_RGBA, GL_FLOAT,
                    static_cast<GLsizei>(result->value.pixels.size()*sizeof(float)), result->value.pixels.data());
                result->value.valid = true;
            }
            result->value.error = glGetError();
        }, [result] { result->done = true; });
        Wait([result] { return result->done; }, "PBR HDR readback");
        Check(result->value.valid && result->value.error == GL_NO_ERROR, "PBR GPU error/readback failure");
        for (float value : result->value.pixels) Check(std::isfinite(value), "PBR generated nonfinite value");
        return result->value.pixels;
    }
    void CheckPhases(bool pointLight, float roughness) {
        std::vector<double> offEnergy, onEnergy;
        float offPeak = 0, onPeak = 0;
        for (std::uint32_t i = 0; i < 33; ++i) {
            const float phase = static_cast<float>(i) / 32;
            auto off = Measure(Draw(false, roughness, phase, 4, pointLight));
            auto on = Measure(Draw(true, roughness, phase, 4, pointLight));
            offEnergy.push_back(off.energy); onEnergy.push_back(on.energy);
            offPeak = std::max(offPeak, off.peak); onPeak = std::max(onPeak, on.peak);
        }
        const auto offVariation = RelativeVariation(offEnergy), onVariation = RelativeVariation(onEnergy);
        std::cout << (pointLight ? "Point" : "Sun") << " roughness=" << roughness
                  << " energy relative range " << offVariation << " -> " << onVariation
                  << "; max HDR peak " << offPeak << " -> " << onPeak << '\n';
        Check(offVariation > .25, "Negative control did not exhibit subpixel specular aliasing");
        // This isotropic GGX approximation is a prefilter, not exact pixel
        // integration. Require a substantial measured improvement and bound
        // the residual, without claiming that it replaces temporal AA.
        Check(onVariation < offVariation * .25 && onVariation < .25,
              "Specular AA failed to stabilize subpixel highlight energy");
        Check(onPeak > 0 && onPeak < offPeak * .5, "Specular AA did not filter undersampled HDR peaks");
    }
    RHIDevice device_;
    std::shared_ptr<const void> owner_;
    RenderResourceHandle<PipelineSpec> pipeline_;
    RenderResourceHandle<VertexBufferSpec> mesh_;
    RenderResourceHandle<UniformBufferSpec> view_, material_, lighting_, effects_, patch_;
    RenderResourceHandle<RHITextureSpec> white_, flatNormal_, output_;
    RenderResourceHandle<RenderTargetSpec> target_;
    std::uint64_t frame_ = 0;
    bool stopped_ = false;
};
} // namespace

int main() {
    int result = 0;
    try {
        Check(glfwInit() == GLFW_TRUE, "glfwInit failed");
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        ApplicationWindow::Window window({Width, Height, "PBR specular AA GPU test", false});
        Check(window.Activate(), "Cannot activate OpenGL 4.5 context");
        auto context = window.GetRenderContextAsOpengl();
        Check(context.has_value(), "No OpenGL context");
        Fixture fixture(std::move(*context));
        fixture.Run();
        fixture.Shutdown();
    } catch (const std::exception& error) {
        std::cerr << "PBR specular AA GPU test failed: " << error.what() << '\n';
        result = 1;
    }
    glfwTerminate();
    return result;
}
