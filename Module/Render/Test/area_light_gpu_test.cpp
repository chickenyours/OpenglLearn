#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include "ApplicationWindow/public/window.h"
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Public/Material/pbr_material.h"
#include "Render/Public/Pipeline/area_lights.h"
#include "Render/Public/Pipeline/material_render_pipeline.h"
#include "Render/Public/Pipeline/probe_sdf_scene.h"
#include "Render/Public/Pipeline/surface_cache.h"

namespace {
using namespace Render;
using namespace std::chrono_literals;
constexpr std::uint32_t Width = 16, Height = 16;
constexpr double Pi = 3.14159265358979323846;
template <class T> struct Completion { T value{}; bool done = false; };
void Check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct alignas(16) Surface {
    glm::vec4 position{0};
    glm::vec4 normalRoughness{0,1,0,.5f};
    glm::vec4 viewMetallic{0,1,0,0};
    glm::vec4 albedoMode{1,1,1,0}; // 0=diffuse, 1=specular, 2=both, 3=sample-count diagnostic
    glm::vec4 spanX{0}, spanY{0};
};
static_assert(sizeof(Surface) == 96);

std::string VertexShader() {
    return R"GLSL(#version 450 core
layout(location=0) in vec2 position;
layout(location=1) in vec2 uv;
out vec3 vWorldPosition;
out vec3 vWorldNormal;
out vec2 vTexCoord;
out vec2 vTexCoord1;
out vec4 vWorldTangent;
void main() {
    gl_Position = vec4(position,0,1);
    vWorldPosition = vec3(0); vWorldNormal = vec3(0,1,0);
    vTexCoord = uv; vTexCoord1=vTexCoord; vWorldTangent = vec4(1,0,0,1);
}
)GLSL";
}
std::string DiagnosticShader() {
    // Keep the real PBR BRDF and area integration implementation. Replace the
    // entry point to isolate linear diffuse/specular from AO, CSM, exposure,
    // temporal history and material textures. A counter inside the production
    // loop independently exposes the decoded 4x4/8x8 sample count.
    auto source = Material::PbrFragmentShader();
    const auto main = source.rfind("void main()");
    Check(main != std::string::npos, "Production PBR entry point missing");
    source.resize(main);
    const auto integrator = source.find("void EvaluateAreaLightingInternal(");
    Check(integrator != std::string::npos, "Production area integrator missing");
    source.insert(integrator,"int areaTestSampleCount;\n");
    const auto sample = source.find("            vec2 parameter = AreaLightSampleParameters(");
    Check(sample != std::string::npos, "Production area sampling loop missing");
    source.insert(sample,"            areaTestSampleCount += 1;\n");
    source += R"GLSL(
layout(std140,binding=10) uniform TestSurface {
    vec4 testPosition;
    vec4 testNormalRoughness;
    vec4 testViewMetallic;
    vec4 testAlbedoMode;
    vec4 testSpanX;
    vec4 testSpanY;
};
void main() {
    vec3 P = testPosition.xyz + (vTexCoord.x * 2.0 - 1.0) * testSpanX.xyz +
                               (vTexCoord.y * 2.0 - 1.0) * testSpanY.xyz;
    vec3 diffuse, specular;
    areaTestSampleCount = 0;
    EvaluateAreaLighting(P, testViewMetallic.xyz, testNormalRoughness.xyz,
                         testAlbedoMode.rgb, testViewMetallic.w, testNormalRoughness.w,
                         diffuse, specular);
    if (testAlbedoMode.w > 2.5) {
        outColor = vec4(vec3(float(areaTestSampleCount)),1);
        return;
    }
    vec3 color = testAlbedoMode.w < 0.5 ? diffuse :
                 (testAlbedoMode.w < 1.5 ? specular : diffuse + specular);
    outColor = vec4(color,1);
}
)GLSL";
    return source;
}

class Fixture {
public:
    explicit Fixture(OpenglBackendContext context) { device_.Run(BackendType::Opengl, std::move(context)); }
    ~Fixture() { Shutdown(); }
    void Initialize() {
        PipelineDetail::ResourceBuilder builder(device_);
        pipeline_ = builder.FullscreenPipeline(VertexShader(), DiagnosticShader());
        mesh_ = builder.FullscreenMesh();
        area_ = builder.Uniform(sizeof(AreaLightsConstants));
        surface_ = builder.Uniform(sizeof(Surface));
        color_ = builder.Texture(Width, Height, RHITextureFormat::RGBA16F);
        target_ = builder.Target(color_, {});
        owner_ = builder.Finish();
        InspectUniform();
    }
    void Shutdown() {
        if (!device_.IsRunning()) return;
        owner_.reset();
        device_.StopAndRelease();
        device_.returnSystem.DrainCallbacks();
    }
    glm::dvec3 Draw(const AreaLightsSettings& settings, const Surface& surface = {}) {
        const auto constants = MakeAreaLightsConstants(settings);
        RHICommand::BeginFrame begin;
        begin.frameIndex = ++frameIndex_;
        begin.framebufferWidth = begin.framebufferHeight = 64;
        auto encoder = device_.BeginFrame(begin);
        try {
            RHICommand::SetRenderTarget target;
            target.target = target_; target.width = Width; target.height = Height;
            Check(encoder.KeepAlive(owner_) && encoder.SetRenderTarget(target) &&
                  encoder.UpdateUniformBuffer(area_, constants) && encoder.BindUniformBuffer(area_, AreaLightsBinding) &&
                  encoder.UpdateUniformBuffer(surface_, surface) && encoder.BindUniformBuffer(surface_, 10) &&
                  encoder.BindPipeline(pipeline_) && encoder.BindMesh(mesh_) && encoder.DrawIndexed(RHICommand::DrawIndexed{3}),
                  "Cannot record area-light diagnostic");
            Check(encoder.End(false), "Cannot end area-light frame");
            auto result = std::make_shared<Completion<bool>>();
            Check(device_.async_SubmitFrameCommands(encoder.GetCommandBuffer(), [result] { result->done = true; }),
                  "Cannot submit area-light frame");
            Wait(result, "area-light frame");
        } catch (...) { encoder.Cancel(); throw; }
        struct Result { std::array<float, Width * Height * 4> rgba{}; GLenum error = GL_NO_ERROR; };
        auto result = std::make_shared<Completion<Result>>();
        device_.async_ExecuteCode([this, result] {
            const auto* texture = device_.GetResourcePool().TextureTable.Get(color_);
            if (!texture) { result->value.error = GL_INVALID_VALUE; return; }
            glGetTextureImage(texture->rhi_id, 0, GL_RGBA, GL_FLOAT, sizeof(result->value.rgba), result->value.rgba.data());
            result->value.error = glGetError();
        }, [result] { result->done = true; });
        Wait(result, "area-light readback");
        Check(result->value.error == GL_NO_ERROR, "OpenGL area-light error " + std::to_string(result->value.error));
        glm::dvec3 mean(0);
        for (std::size_t pixel = 0; pixel < Width * Height; ++pixel) {
            for (int channel = 0; channel < 3; ++channel) {
                const float value = result->value.rgba[pixel * 4 + channel];
                if (!std::isfinite(value) || value < 0 || value >= 60000)
                    throw std::runtime_error("Area lighting produced invalid/clipped HDR radiance");
                mean[channel] += value;
            }
            Check(result->value.rgba[pixel * 4 + 3] == 1, "Area-light draw missed a pixel");
        }
        return mean / double(Width * Height);
    }
private:
    template <class T> void Wait(const std::shared_ptr<Completion<T>>& result, const char* label) {
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!result->done) {
            device_.returnSystem.DrainCallbacks();
            if (result->done) break;
            Check(std::chrono::steady_clock::now() < deadline, std::string("Timeout: ") + label);
            glfwPollEvents();
            device_.returnSystem.WaitForCallbacks(2ms);
        }
    }
    void InspectUniform() {
        struct Reflection { GLint size = -1, binding = -1, lastOffset = -1; GLenum error = GL_NO_ERROR; };
        auto result = std::make_shared<Completion<Reflection>>();
        device_.async_ExecuteCode([this, result] {
            const auto* pipeline = device_.GetResourcePool().PipelineTable.Get(pipeline_);
            const auto* program = pipeline ? device_.GetResourcePool().shaderProgramTable.Get(pipeline->shaderProgram) : nullptr;
            if (program) {
                const auto block = glGetUniformBlockIndex(program->rhi_id, "AreaLightsData");
                if (block != GL_INVALID_INDEX) {
                    glGetActiveUniformBlockiv(program->rhi_id, block, GL_UNIFORM_BLOCK_DATA_SIZE, &result->value.size);
                    glGetActiveUniformBlockiv(program->rhi_id, block, GL_UNIFORM_BLOCK_BINDING, &result->value.binding);
                    const char* name = "rectangleLights[3].radianceTwoSided";
                    GLuint index = GL_INVALID_INDEX;
                    glGetUniformIndices(program->rhi_id, 1, &name, &index);
                    if (index != GL_INVALID_INDEX)
                        glGetActiveUniformsiv(program->rhi_id, 1, &index, GL_UNIFORM_OFFSET, &result->value.lastOffset);
                }
            }
            result->value.error = glGetError();
        }, [result] { result->done = true; });
        Wait(result, "area-light UBO reflection");
        Check(result->value.error == GL_NO_ERROR && result->value.size == 256 &&
              result->value.binding == 8 && result->value.lastOffset == 240,
              "Area light std140 layout does not match the 256-byte CPU block");
    }
    RHIDevice device_;
    std::shared_ptr<const void> owner_;
    RenderResourceHandle<PipelineSpec> pipeline_;
    RenderResourceHandle<VertexBufferSpec> mesh_;
    RenderResourceHandle<UniformBufferSpec> area_, surface_;
    RenderResourceHandle<RHITextureSpec> color_;
    RenderResourceHandle<RenderTargetSpec> target_;
    std::uint64_t frameIndex_ = 0;
};

void TestValidation() {
    AreaLightsSettings settings;
    Check(ValidateAreaLightsSettings(settings), "Default area-light settings invalid");
    Check(!settings.stratifiedSamples, "Ordinary area lights no longer default to cell midpoints");
    Check(MakeAreaLightsConstants(settings).lights[0].centerEnabled.w == 0, "Default area light is active");
    settings.count = 5;
    Check(!ValidateAreaLightsSettings(settings), "Too many area lights accepted");
    settings.count = 1;
    for (const auto samples : {0u, 3u, 16u}) {
        settings.samplesPerAxis = samples;
        Check(!ValidateAreaLightsSettings(settings), "Invalid area quadrature accepted");
    }
    settings.samplesPerAxis = 4;
    settings.lights[0].halfAxisU = glm::vec3(0);
    Check(!ValidateAreaLightsSettings(settings), "Zero-area emitter accepted");
    settings.lights[0].halfAxisU = {.5f,0,.1f};
    Check(!ValidateAreaLightsSettings(settings), "Nonrectangular light axes accepted");
    settings.lights[0].halfAxisU = {.5f,0,0};
    settings.lights[0].radiance.x = -1;
    Check(!ValidateAreaLightsSettings(settings), "Negative radiance accepted");
    settings.lights[0].radiance.x = std::numeric_limits<float>::quiet_NaN();
    Check(!ValidateAreaLightsSettings(settings), "NaN area radiance accepted");
    settings.lights[0].radiance = {1,2,3};
    settings.samplesPerAxis = 8;
    settings.specularFilter = .5f;
    const auto packed = MakeAreaLightsConstants(settings);
    Check(packed.lights[0].centerEnabled.w == 1 && packed.lights[0].halfAxisU.w == 8 &&
          packed.lights[0].halfAxisV.w == .5f && packed.lights[0].radianceTwoSided.z == 3 &&
          packed.lights[1].centerEnabled.w == 0, "Area-light packing/count mismatch");
    for (const auto grid : {4u,8u}) for (const bool stratified : {false,true}) {
        settings.samplesPerAxis = grid;
        settings.stratifiedSamples = stratified;
        const auto constants = MakeAreaLightsConstants(settings);
        Check(constants.lights[0].halfAxisU.w == (stratified ? -float(grid) : float(grid)) &&
              constants.lights[0].halfAxisV.w == .5f && constants.lights[0].centerEnabled.w == 1 &&
              constants.lights[1].centerEnabled.w == 0,
              "Signed area sampling mode changed the grid, filter strength or active-light slots");
    }
}

void TestSamplingStructure() {
    for (const auto grid : {4u,8u}) {
        std::vector<unsigned> projectedU(grid * grid), projectedV(grid * grid);
        glm::dvec2 centroid(0);
        for (std::uint32_t y = 0; y < grid; ++y) for (std::uint32_t x = 0; x < grid; ++x) {
            const auto midpoint = AreaLightSampleParameters(x,y,grid,false);
            const glm::vec2 expectedMidpoint((float(x) + .5f) * (2.f / float(grid)) - 1.f,
                                             (float(y) + .5f) * (2.f / float(grid)) - 1.f);
            Check(midpoint == expectedMidpoint, "Ordinary area-light cell midpoint changed");
            const auto point = AreaLightSampleParameters(x,y,grid,true);
            Check(std::isfinite(point.x) && std::isfinite(point.y) &&
                  point.x > -1 && point.x < 1 && point.y > -1 && point.y < 1,
                  "Stratified area sample escaped the emitter interior");
            const auto unit = (glm::dvec2(point) + 1.0) * .5;
            Check(std::uint32_t(std::floor(unit.x * grid)) == x &&
                  std::uint32_t(std::floor(unit.y * grid)) == y,
                  "Stratified area sampling left an original 2D quadrature cell empty");
            const auto uLayer = std::uint32_t(std::floor(unit.x * grid * grid));
            const auto vLayer = std::uint32_t(std::floor(unit.y * grid * grid));
            Check(uLayer < projectedU.size() && vLayer < projectedV.size(),
                  "Stratified sample projected outside the emitter sublayers");
            ++projectedU[uLayer]; ++projectedV[vLayer];
            centroid += glm::dvec2(point);
            const auto opposite = AreaLightSampleParameters(grid - 1 - x,grid - 1 - y,grid,true);
            Check(glm::length(point + opposite) < 1e-7f,
                  "Stratified area pattern lost central symmetry");
        }
        Check(std::all_of(projectedU.begin(),projectedU.end(),[](unsigned count) { return count == 1; }) &&
              std::all_of(projectedV.begin(),projectedV.end(),[](unsigned count) { return count == 1; }),
              "Stratified area pattern does not occupy every projected sublayer exactly once");
        Check(glm::length(centroid / double(grid * grid)) < 1e-9,
              "Stratified area pattern shifted the emitter centroid");
    }
}

AreaLightsSettings Light(float height = 2, float halfU = .5f, float halfV = .5f) {
    AreaLightsSettings settings;
    settings.count = 1;
    settings.lights[0].center = {0,height,0};
    settings.lights[0].halfAxisU = {halfU,0,0};
    settings.lights[0].halfAxisV = {0,0,halfV};
    return settings;
}

// Independent closed-form cosine-weighted solid angle of a centered parallel
// rectangle. Unlike the shader, this reference does not sample the rectangle.
double RectangleIrradiance(double a, double b, double distance) {
    const double x = std::sqrt(distance * distance + a * a);
    const double y = std::sqrt(distance * distance + b * b);
    return 2 * (a / x * std::atan(b / x) + b / y * std::atan(a / y));
}
// Unculled double-precision midpoint quadrature, independent of the production
// sample helper and mode. The optional dense grid approximates the continuous
// emitter; its default retains the exact ordinary-midpoint rejection oracle.
glm::dvec3 DiffuseQuadrature(const AreaLightsSettings& settings,const Surface& surface,
                           std::uint32_t referenceGrid = 0) {
    const auto N=glm::normalize(glm::dvec3(surface.normalRoughness));
    const auto V=glm::normalize(glm::dvec3(surface.viewMetallic));
    glm::dvec3 result(0);
    const auto grid = referenceGrid ? referenceGrid : settings.samplesPerAxis;
    const double diffuseFraction=1-std::clamp(double(surface.viewMetallic.w),0.0,1.0);
    for(unsigned index=0;index<settings.count;++index) {
        const auto& light=settings.lights[index];if(!light.enabled)continue;
        const auto U=glm::dvec3(light.halfAxisU),W=glm::dvec3(light.halfAxisV);
        const auto emitterN=glm::normalize(glm::cross(U,W));
        const double cellArea=4*glm::length(glm::cross(U,W))/double(grid*grid);
        for(unsigned y=0;y<grid;++y)for(unsigned x=0;x<grid;++x) {
            const auto delta=glm::dvec3(light.center)-glm::dvec3(surface.position)+
                U*((double(x)+.5)*2/grid-1)+W*((double(y)+.5)*2/grid-1);
            const double distanceSquared=glm::dot(delta,delta);const auto L=delta/std::sqrt(distanceSquared);
            const double nl=std::max(0.0,glm::dot(N,L)),face=glm::dot(emitterN,-L);
            const double emitter=light.twoSided?std::abs(face):std::max(0.0,face);
            if(nl<=0||emitter<=0)continue;
            const auto H=glm::normalize(V+L);
            const double fresnel=.05+.95*std::pow(1-std::max(0.0,glm::dot(H,V)),5);
            result+=glm::dvec3(light.radiance)*glm::dvec3(surface.albedoMode)*
                ((1-fresnel)*diffuseFraction/Pi*cellArea*nl*emitter/distanceSquared);
        }
    }
    return result;
}

void TestSamplingModeEnergy(Fixture& fixture) {
    for (const auto grid : {4u,8u}) for (const bool stratified : {false,true}) {
        auto settings = Light();
        settings.samplesPerAxis = grid;
        settings.stratifiedSamples = stratified;
        Surface countSurface;
        countSurface.albedoMode.w = 3;
        Check(fixture.Draw(settings,countSurface) == glm::dvec3(grid * grid),
              "Production area integrator decoded the signed 4x4/8x8 grid incorrectly");
    }
    const auto checkDense = [&](AreaLightsSettings settings, const Surface& surface) {
        // This oracle never uses AreaLightSampleParameters or the signed grid.
        // Verify reference convergence before judging the 16/64-sample shader.
        const auto dense = DiffuseQuadrature(settings,surface,512);
        const auto coarser = DiffuseQuadrature(settings,surface,256);
        Check(glm::length(dense) > 0 && glm::length(coarser / dense - glm::dvec3(1)) < 1e-5,
              "Dense area-light reference has not converged");
        const auto observed = fixture.Draw(settings,surface);
        const auto relative = glm::abs(observed / dense - glm::dvec3(1));
        const double worst = std::max({relative.x,relative.y,relative.z});
        Check(worst < .008, "Area sampling mode disagrees with independent dense unoccluded integration");
        return worst;
    };
    double worstRelativeError = 0;
    for (const auto shape : {glm::vec3(.5f,.5f,2),glm::vec3(.4f,.8f,3),glm::vec3(1,.75f,4)}) {
        for (const auto grid : {4u,8u}) for (const bool stratified : {false,true}) {
            auto settings = Light(shape.z,shape.x,shape.y);
            settings.samplesPerAxis = grid;
            settings.stratifiedSamples = stratified;
            worstRelativeError = std::max(worstRelativeError,checkDense(settings,{}));
        }
    }
    Surface oblique;
    oblique.position = {.2f,0,.15f,0};
    oblique.normalRoughness = glm::vec4(glm::normalize(glm::vec3(.1f,1,.05f)),.5f);
    oblique.viewMetallic = glm::vec4(glm::normalize(glm::vec3(.2f,1,.1f)),0);
    for (const auto grid : {4u,8u}) for (const bool stratified : {false,true}) {
        auto settings = Light(3,.4f,.8f);
        settings.samplesPerAxis = grid;
        settings.stratifiedSamples = stratified;
        worstRelativeError = std::max(worstRelativeError,checkDense(settings,oblique));
    }
    // These wide-emitter cases make the signed mode/grid observable through
    // the actual production integral. A negative 4 misread as midpoint has
    // >1.3% error; a negative 8 misread as stratified 4 has >1.2% error.
    // Both must still meet the existing 0.8% energy tolerance, without a
    // low-sample CPU mirror of the new production pattern.
    auto signedFour = Light(1.4f,1.1f,.6f);
    signedFour.stratifiedSamples = true;
    worstRelativeError = std::max(worstRelativeError,checkDense(signedFour,{}));
    auto signedEight = Light(2,2,2);
    signedEight.samplesPerAxis = 8;
    signedEight.stratifiedSamples = true;
    worstRelativeError = std::max(worstRelativeError,checkDense(signedEight,{}));
    auto ordinary = Light(3,.4f,.8f);
    const auto defaultOutput = fixture.Draw(ordinary);
    ordinary.stratifiedSamples = false;
    Check(fixture.Draw(ordinary) == defaultOutput, "Explicit midpoint mode changed ordinary default lighting");
    std::cout << "Area midpoint/stratified 4x4/8x8 independent dense max relative error="
              << worstRelativeError << "; signed mode/grid and ordinary default verified\n";
}
void TestWholeLightRejection(Fixture& fixture) {
    for(const auto grid:{4u,8u}) {
        auto light=Light(2,1,.5f);light.samplesPerAxis=grid;
        Surface surface;
        light.lights[0].radiance=glm::vec3(0);
        Check(glm::length(fixture.Draw(light,surface))==0,"Enabled zero-radiance area light contributed energy");
        light.lights[0].radiance={1,2,3};
        light.lights[0].halfAxisV*=-1;
        Check(glm::length(fixture.Draw(light,surface))==0,"Back-facing one-sided rectangle contributed energy");
        light.lights[0].twoSided=true;
        const auto twoSided=DiffuseQuadrature(light,surface);
        Check(glm::length(twoSided)>0&&glm::length(fixture.Draw(light,surface)/twoSided-glm::dvec3(1))<.004,
              "Whole-emitter rejection discarded a two-sided back face");
        const auto belowHorizon=glm::vec3(0,-1,0);
        surface.normalRoughness=glm::vec4(belowHorizon,.5f);
        surface.viewMetallic=glm::vec4(belowHorizon,0);
        Check(glm::length(fixture.Draw(light,surface))==0,"Rectangle wholly behind receiver horizon contributed energy");
        // Its center is below this receiver's horizon, but two half-axis
        // corners and midpoint columns are above it and must remain lit.
        const auto grazing=glm::normalize(glm::vec3(1,-.1f,0));
        surface.normalRoughness=glm::vec4(grazing,.5f);surface.viewMetallic=glm::vec4(grazing,0);
        for(const bool twoFaces:{false,true}) {
            light.lights[0].twoSided=twoFaces;
            light.lights[0].halfAxisV={0,0,.5f};
            const auto expected=DiffuseQuadrature(light,surface),observed=fixture.Draw(light,surface);
            Check(glm::length(expected)>.001&&glm::length(observed/expected-glm::dvec3(1))<.004,
                  "Whole-light horizon rejection discarded a straddling emitter or changed quadrature energy");
        }
    }
    std::cout<<"Area whole-light rejection preserves 4x4/8x8 energy, two-sided emission and horizon straddling\n";
}
void TestGeometryAndEnergy(Fixture& fixture) {
    Check(glm::length(fixture.Draw({})) == 0, "Zero lights produced illumination");
    double worstRelativeError = 0;
    for (const auto shape : {glm::vec3(.5f,.5f,2), glm::vec3(.4f,.8f,3), glm::vec3(1,.75f,4)}) {
        auto settings = Light(shape.z, shape.x, shape.y);
        settings.samplesPerAxis = 8;
        const double observed = fixture.Draw(settings).x;
        // At these angles Schlick's grazing term contributes below 1e-5;
        // the diffuse dielectric reflectance is effectively 1-F0=.95.
        const double expected = .95 * RectangleIrradiance(shape.x, shape.y, shape.z) / Pi;
        const double error = std::abs(observed / expected - 1);
        worstRelativeError = std::max(worstRelativeError, error);
        Check(error < .008, "Rectangle light disagrees with the analytic area/solid-angle integral");
    }
    auto settings = Light(3,.4f,.8f);
    const auto original = fixture.Draw(settings);
    settings.lights[0].radiance = {2,3,4};
    const auto colored = fixture.Draw(settings);
    Check(glm::length(colored / original - glm::dvec3(2,3,4)) < .008, "Area radiance/color did not scale linearly");
    settings.lights[0].radiance = {1,1,1};
    settings.lights[0].halfAxisV *= -1;
    Check(glm::length(fixture.Draw(settings)) == 0, "One-sided area light emitted backwards");
    settings.lights[0].twoSided = true;
    Check(glm::length(fixture.Draw(settings) / original - glm::dvec3(1)) < .003, "Two-sided emission did not restore the back face");
    settings.lights[0].enabled = false;
    Check(glm::length(fixture.Draw(settings)) == 0, "Disabled area light still emitted");

    settings = Light(3,.4f,.8f);
    const auto rotation = glm::rotate(glm::mat4(1), 1.57079632679f, glm::vec3(0,1,0));
    settings.lights[0].halfAxisU = glm::vec3(rotation * glm::vec4(settings.lights[0].halfAxisU,0));
    settings.lights[0].halfAxisV = glm::vec3(rotation * glm::vec4(settings.lights[0].halfAxisV,0));
    Check(glm::length(fixture.Draw(settings) / original - glm::dvec3(1)) < .003, "Rotating the rectangle around its normal changed centered diffuse light");
    settings = Light(20);
    const auto frontal = fixture.Draw(settings).x;
    const auto tilt = glm::rotate(glm::mat4(1), .6f, glm::vec3(0,0,1));
    settings.lights[0].halfAxisU = glm::vec3(tilt * glm::vec4(settings.lights[0].halfAxisU,0));
    settings.lights[0].halfAxisV = glm::vec3(tilt * glm::vec4(settings.lights[0].halfAxisV,0));
    Check(std::abs(fixture.Draw(settings).x / frontal - std::cos(.6)) < .006, "Far-field emitter cosine/orientation is wrong");
    const double nearValue = fixture.Draw(Light(8)).x, farValue = fixture.Draw(Light(16)).x;
    Check(std::abs(nearValue / farValue - 4) < .05, "Far-field area light did not approach inverse-square falloff");
    auto small = Light(16,.25f,.25f), large = Light(16,.5f,.5f);
    Check(std::abs(fixture.Draw(large).x / fixture.Draw(small).x - 4) < .05, "Fixed radiance failed to scale with emitter area");
    settings = Light(2,2,2);
    const double expected = .95 * RectangleIrradiance(2,2,2) / Pi;
    const double error4 = std::abs(fixture.Draw(settings).x - expected);
    settings.samplesPerAxis = 8;
    const double error8 = std::abs(fixture.Draw(settings).x - expected);
    Check(error8 < error4, "Higher area quadrature did not converge toward the analytic diffuse integral");
    settings = Light(3);
    const auto one = fixture.Draw(settings);
    settings.count = 4;
    for (std::uint32_t i = 1; i < settings.count; ++i) settings.lights[i] = settings.lights[0];
    Check(glm::length(fixture.Draw(settings) / one - glm::dvec3(4)) < .008, "Four area light slots did not add independently");
    Surface metallic;
    metallic.viewMetallic.w = 1;
    Check(glm::length(fixture.Draw(settings, metallic)) == 0, "Metallic material gained diffuse area illumination");
    auto below = Light(-2);
    below.lights[0].twoSided = true;
    Check(glm::length(fixture.Draw(below)) == 0, "Area light below the receiver illuminated its front hemisphere");
    std::cout << "Area diffuse analytic max relative error=" << worstRelativeError
              << "; wide rectangle abs error 4x4/8x8=" << error4 << '/' << error8 << '\n';
}

double RelativeRange(const std::vector<double>& values) {
    const double mean = std::accumulate(values.begin(), values.end(), 0.0) / values.size();
    const auto [minimum, maximum] = std::minmax_element(values.begin(), values.end());
    Check(mean > 0, "Area specular response vanished");
    return (*maximum - *minimum) / mean;
}
void TestSpecularCells(Fixture& fixture) {
    for (const auto grid : {4u,8u}) for (const auto roughness : {.045f,.08f}) {
        auto settings = Light(2,.75f,.75f);
        settings.samplesPerAxis = grid;
        Surface surface;
        surface.viewMetallic.w = 1;
        surface.normalRoughness.w = roughness;
        surface.albedoMode = {.7f,.7f,.7f,1};
        const float cell = 1.5f / grid;
        surface.position.z = cell * .5f;
        std::vector<double> unfiltered, filtered;
        for (int phase = 0; phase <= 32; ++phase) {
            surface.position.x = cell * (phase / 32.0f - .5f);
            settings.specularFilter = 0;
            unfiltered.push_back(fixture.Draw(settings,surface).x);
            settings.specularFilter = 1;
            filtered.push_back(fixture.Draw(settings,surface).x);
        }
        const double before = RelativeRange(unfiltered), after = RelativeRange(filtered);
        const double mean = std::accumulate(filtered.begin(),filtered.end(),0.0) / filtered.size();
        std::cout << "Area GGX grid=" << grid << " roughness=" << roughness << " phase range/mean="
                  << before << " -> " << after << ", filtered radiance=" << mean << '\n';
        Check(before > .5, "Area specular negative control missed discrete sample highlights");
        Check(after < before * .2 && after < .15, "Area cell filtering left discrete moving specular spots");
        Check(mean > .2 && mean < .9, "Area filtering erased or amplified the finite-emitter highlight");
    }
}
#include "area_light_shadow_submission_test.inl"
} // namespace

int main(int argc,char** argv) {
    int result = 0;
    try {
        bool contracts=false,vulkan=false,validation=false;
        for(int i=1;i<argc;++i){const std::string argument=argv[i];contracts|=argument=="--shadow-submission-contracts";
            vulkan|=argument=="--vulkan";validation|=argument=="--validation";}
        Check(!vulkan||contracts,"Use --shadow-submission-contracts for the Vulkan lifetime test entry point");
        if(contracts){TestShadowSubmissionContracts(vulkan,validation);glfwTerminate();return 0;}
        TestValidation();
        TestSamplingStructure();
        Check(glfwInit() == GLFW_TRUE, "glfwInit failed");
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        ApplicationWindow::Window window({64,64,"Rectangle area light GPU test",false});
        Check(window.Activate(), "Cannot create hidden OpenGL 4.5 window");
        auto context = window.GetRenderContextAsOpengl();
        Check(context.has_value(), "Missing OpenGL context");
        Fixture fixture(std::move(*context));
        fixture.Initialize();
        std::cout << std::unitbuf << std::fixed << std::setprecision(6);
        TestGeometryAndEnergy(fixture);
        TestSamplingModeEnergy(fixture);
        TestWholeLightRejection(fixture);
        TestSpecularCells(fixture);
        fixture.Shutdown();
        std::cout << "Rectangle area light GPU test passed: area/radiance geometry, 256B UBO and filtered GGX; GL errors=0\n";
    } catch (const std::exception& error) {
        std::cerr << "Rectangle area light GPU test failed: " << error.what() << '\n';
        result = 1;
    }
    glfwTerminate();
    return result;
}
