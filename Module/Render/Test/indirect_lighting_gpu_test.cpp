#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include "ApplicationWindow/public/window.h"
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Private/Pipeline/post_process_shaders.h"
#include "Render/Private/Pipeline/indirect_lighting_shaders.h"

namespace {
using namespace Render;
using namespace std::chrono_literals;
using Texture = RenderResourceHandle<RHITextureSpec>;
using Pipeline = RenderResourceHandle<PipelineSpec>;
using Target = RenderResourceHandle<RenderTargetSpec>;
using Pixels = std::vector<float>;
void Check(bool condition, const std::string& message) { if (!condition) throw std::runtime_error(message); }
template <class T> struct Completion { T value{}; bool done = false; };

struct alignas(16) FixtureConstants {
    glm::mat4 inverseVP{1}, viewProjection{1};
    glm::vec4 camera{0}, reflectedRadiance{0}, emittedRadiance{0};
    glm::ivec4 options{0, 1, 0, 0}; // normal output, wall, reversed wall normal, luminous floor
    glm::vec4 pattern{0}; // wall checker frequency, phase, enabled, reserved
};
static_assert(sizeof(FixtureConstants) == 208);

// GPU-generated analytic perpendicular surfaces provide exact input geometry
// independent of PBR capture implementation. The wall's outgoing Lambertian
// diffuse radiance and emission can be varied independently, with no ambient.
std::string FixtureShader() {
    return R"GLSL(#version 450 core
in vec2 vUV;
layout(location=0) out vec4 outColor;
layout(std140,binding=8) uniform FixtureData {
    mat4 inverseVP;
    mat4 viewProjection;
    vec4 camera;
    vec4 reflectedRadiance;
    vec4 emittedRadiance;
    ivec4 options;
    vec4 pattern;
};
void main() {
    vec4 farPoint = inverseVP * vec4(vUV * 2.0 - 1.0, 1.0, 1.0);
    vec3 direction = normalize(farPoint.xyz / farPoint.w - camera.xyz);
    float closest = 1e20;
    vec3 position = vec3(0), normal = vec3(0), radiance = vec3(0);
    if (direction.y < -1e-6) {
        float t = -camera.y / direction.y;
        vec3 p = camera.xyz + direction * t;
        if (t > 0.0 && abs(p.x) < 3.0 && p.z > -2.5 && p.z < 3.0) {
            closest = t; position = p; normal = vec3(0,1,0);
            radiance = options.w != 0 ? vec3(4.0) : vec3(0.0);
        }
    }
    if (options.y != 0 && direction.z < -1e-6) {
        float t = (-1.0 - camera.z) / direction.z;
        vec3 p = camera.xyz + direction * t;
        if (t > 0.0 && t < closest && abs(p.x) < 3.0 && p.y >= 0.0 && p.y < 3.0) {
            closest = t; position = p;
            normal = options.z != 0 ? vec3(0,0,-1) : vec3(0,0,1);
            float checker = mod(floor((p.x+3.0)*pattern.x+pattern.y) + floor(p.y*pattern.x), 2.0);
            radiance = reflectedRadiance.rgb + emittedRadiance.rgb * (pattern.z > 0.5 ? mix(0.2,1.8,checker) : 1.0);
        }
    }
    if (closest == 1e20) { outColor = vec4(0); gl_FragDepth = 1.0; return; }
    vec4 clip = viewProjection * vec4(position,1);
    gl_FragDepth = clip.z / clip.w * 0.5 + 0.5;
    outColor = vec4(options.x != 0 ? normal : radiance, 1.0);
}
)GLSL";
}

void CheckSettings() {
    IndirectLightingSettings settings;
    std::string error = "old error";
    Check(!settings.enabled && ValidateIndirectLightingSettings(settings, &error) && error.empty(), "Invalid GI defaults");
    for (float IndirectLightingSettings::* field : {&IndirectLightingSettings::intensity,
            &IndirectLightingSettings::radius, &IndirectLightingSettings::thickness, &IndirectLightingSettings::bias}) {
        settings = {}; settings.*field = std::numeric_limits<float>::quiet_NaN();
        Check(!ValidateIndirectLightingSettings(settings), "GI accepted NaN");
        settings = {}; settings.*field = -1;
        Check(!ValidateIndirectLightingSettings(settings), "GI accepted a negative parameter");
    }
    settings = {}; settings.sampleCount = 0;
    Check(!ValidateIndirectLightingSettings(settings), "GI accepted no samples");
    settings = {}; settings.stepCount = MaxIndirectLightingSteps + 1;
    Check(!ValidateIndirectLightingSettings(settings), "GI accepted too many trace steps");
}

class IndirectLightingGpuTest {
public:
    explicit IndirectLightingGpuTest(OpenglBackendContext context) { device_.Run(BackendType::Opengl, std::move(context)); }
    ~IndirectLightingGpuTest() { Shutdown(); }
    void Shutdown() {
        if (stopped_) return;
        owner_.reset(); device_.StopAndRelease(); device_.returnSystem.DrainCallbacks(); stopped_ = true;
    }
    void Run() {
        CheckSettings();
        PipelineDetail::ResourceBuilder builder(device_);
        fixture_ = builder.FullscreenPipeline(FullscreenVertexShader(), FixtureShader(), true);
        gather_ = builder.FullscreenPipeline(FullscreenVertexShader(), IndirectLightingGatherShader());
        geometry_ = builder.FullscreenPipeline(FullscreenVertexShader(), IndirectLightingGeometryShader());
        cachedGather_ = builder.FullscreenPipeline(FullscreenVertexShader(), IndirectLightingGatherShader(true));
        cachedDenoise_ = builder.FullscreenPipeline(FullscreenVertexShader(), IndirectLightingDenoiseShader(true));
        cachedUpsample_ = builder.FullscreenPipeline(FullscreenVertexShader(), IndirectLightingUpsampleShader(true));
        denoise_ = builder.FullscreenPipeline(FullscreenVertexShader(), IndirectLightingDenoiseShader());
        upsample_ = builder.FullscreenPipeline(FullscreenVertexShader(), IndirectLightingUpsampleShader());
        fixtureBuffer_ = builder.Uniform(sizeof(FixtureConstants));
        giBuffer_ = builder.Uniform(sizeof(IndirectLightingConstants));
        mesh_ = builder.FullscreenMesh();
        owner_ = builder.Finish();
        InspectUniform();
        CheckCorner(96, 80);
        CheckCorner(97, 81);
        CheckCorner(192, 160);
        CheckCorner(1281,129); // Odd viewport crossing the trace-grid cap.
        CheckDenoising();
        std::cout << "Indirect lighting GPU test passed: 224-byte UBO, single diffuse bounce/emission, "
                     "fixed miss denominator, plane self-hit/backface rejection and odd-size upsampling\n";
    }
private:
    template <class Predicate> void Wait(Predicate done, const char* label) {
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!done()) {
            device_.returnSystem.DrainCallbacks();
            if (done()) break;
            Check(std::chrono::steady_clock::now() < deadline, std::string("Timeout: ") + label);
            glfwPollEvents(); device_.returnSystem.WaitForCallbacks(5ms);
        }
    }
    void InspectUniform() {
        struct Result { GLint bytes = -1, binding = -1, offset = -1; GLenum error = GL_NO_ERROR; };
        auto result = std::make_shared<Completion<Result>>();
        device_.async_ExecuteCode([this, result] {
            const auto* pipeline = device_.GetResourcePool().PipelineTable.Get(gather_);
            const auto* program = pipeline ? device_.GetResourcePool().shaderProgramTable.Get(pipeline->shaderProgram) : nullptr;
            if (program) {
                const auto block = glGetUniformBlockIndex(program->rhi_id, "IndirectLightingData");
                if (block != GL_INVALID_INDEX) {
                    glGetActiveUniformBlockiv(program->rhi_id, block, GL_UNIFORM_BLOCK_DATA_SIZE, &result->value.bytes);
                    glGetActiveUniformBlockiv(program->rhi_id, block, GL_UNIFORM_BLOCK_BINDING, &result->value.binding);
                    const char* name = "giOptions"; GLuint index = GL_INVALID_INDEX;
                    glGetUniformIndices(program->rhi_id, 1, &name, &index);
                    if (index != GL_INVALID_INDEX) glGetActiveUniformsiv(program->rhi_id, 1, &index, GL_UNIFORM_OFFSET, &result->value.offset);
                }
            }
            result->value.error = glGetError();
        }, [result] { result->done = true; });
        Wait([result] { return result->done; }, "GI uniform reflection");
        Check(result->value.error == GL_NO_ERROR && result->value.bytes == 224 && result->value.binding == 9 &&
              result->value.offset == 208, "Indirect lighting uniform layout mismatch");
    }
    void Draw(Pipeline pipeline, Target target, std::uint32_t width, std::uint32_t height,
              Texture input, Texture normal, Texture depth, const FixtureConstants& fixture,
              const IndirectLightingConstants& gi, const std::shared_ptr<const void>& images, bool capture,
              Texture geometry = {}) {
        RHICommand::BeginFrame begin;
        begin.frameIndex = ++frameIndex_; begin.framebufferWidth = width; begin.framebufferHeight = height;
        auto encoder = device_.BeginFrame(begin);
        try {
            Check(encoder.KeepAlive(owner_) && encoder.KeepAlive(images), "Cannot retain GI resources");
            RHICommand::SetRenderTarget bind;
            bind.target = target; bind.width = width; bind.height = height;
            bind.clearFlags = capture ? RHICommand::ClearColor | RHICommand::ClearDepth : RHICommand::ClearColor;
            Check(encoder.SetRenderTarget(bind), "Cannot select GI test target");
            Check(encoder.UpdateUniformBuffer(fixtureBuffer_, fixture) && encoder.BindUniformBuffer(fixtureBuffer_, 8) &&
                  encoder.UpdateUniformBuffer(giBuffer_, gi) && encoder.BindUniformBuffer(giBuffer_, IndirectLightingBinding),
                  "Cannot upload GI test constants");
            Check(encoder.BindTexture(input, 0) && encoder.BindTexture(normal, 1) && encoder.BindTexture(depth, 2) &&
                  encoder.BindTexture(geometry,3) &&
                  encoder.BindPipeline(pipeline) && encoder.BindMesh(mesh_) && encoder.DrawIndexed(RHICommand::DrawIndexed{3}) &&
                  encoder.End(false), "Cannot record GI test frame");
            auto result = std::make_shared<Completion<bool>>();
            Check(device_.async_SubmitFrameCommands(encoder.GetCommandBuffer(), [result] { result->done = true; }), "Cannot submit GI test frame");
            Wait([result] { return result->done; }, "GI test frame");
        } catch (...) { encoder.Cancel(); throw; }
    }
    Pixels Read(Texture texture, std::uint32_t width, std::uint32_t height) {
        struct Readback { Pixels pixels; bool found = false; GLenum error = GL_NO_ERROR; };
        auto result = std::make_shared<Completion<Readback>>();
        result->value.pixels.resize(std::size_t(width) * height * 4);
        device_.async_ExecuteCode([this, texture, result] {
            const auto* resource = device_.GetResourcePool().TextureTable.Get(texture);
            if (resource) {
                result->value.found = true;
                glGetTextureImage(resource->rhi_id, 0, GL_RGBA, GL_FLOAT,
                    static_cast<GLsizei>(result->value.pixels.size() * sizeof(float)), result->value.pixels.data());
            }
            result->value.error = glGetError();
        }, [result] { result->done = true; });
        Wait([result] { return result->done; }, "GI image readback");
        Check(result->value.found && result->value.error == GL_NO_ERROR, "GL error reading GI image");
        for (float value : result->value.pixels) Check(std::isfinite(value), "Nonfinite GI image");
        return result->value.pixels;
    }
    void CheckCorner(std::uint32_t width, std::uint32_t height) {
        auto halfWidth = (width + 1) / 2, halfHeight = (height + 1) / 2;
        const auto longest=std::max(halfWidth,halfHeight);
        if(longest>MaxIndirectLightingDimension) {
            halfWidth=std::max(1u,halfWidth*MaxIndirectLightingDimension/longest);
            halfHeight=std::max(1u,halfHeight*MaxIndirectLightingDimension/longest);
        }
        const glm::vec3 eye(2.4f, 2.3f, 4.2f);
        const auto view = glm::lookAt(eye, glm::vec3(0, 0.6f, -0.2f), glm::vec3(0,1,0));
        const auto projection = glm::perspective(glm::radians(55.0f), float(width) / height, 0.1f, 20.0f);
        FixtureConstants fixture;
        fixture.viewProjection = projection * view;
        fixture.inverseVP = glm::inverse(fixture.viewProjection);
        fixture.camera = glm::vec4(eye, 1);
        // Red wall lit by white direct irradiance: source outgoing diffuse
        // radiance rho * E / pi. The unlit floor receives only the GI result.
        fixture.reflectedRadiance = glm::vec4(glm::vec3(0.8f, 0.04f, 0.02f) * (10.0f / 3.141592653589793f), 0);
        IndirectLightingSettings settings;
        settings.enabled = true; settings.sampleCount = 32; settings.stepCount = 48;
        auto gi = MakeIndirectLightingConstants(settings, projection, view);
        PipelineDetail::ResourceBuilder builder(device_);
        const auto radiance = builder.Texture(width, height, RHITextureFormat::RGBA16F);
        const auto normal = builder.Texture(width, height, RHITextureFormat::RGBA16F);
        const auto depth = builder.Texture(width, height, RHITextureFormat::Depth32F);
        const auto raw = builder.Texture(halfWidth, halfHeight, RHITextureFormat::RGBA16F);
        const auto ping = builder.Texture(halfWidth, halfHeight, RHITextureFormat::RGBA16F);
        const auto pong = builder.Texture(halfWidth, halfHeight, RHITextureFormat::RGBA16F);
        const auto full = builder.Texture(width, height, RHITextureFormat::RGBA16F);
        const auto positions = builder.Texture(width,height,RHITextureFormat::RGBA32F);
        const auto positionsTarget = builder.Target(positions,{});
        const auto radianceTarget = builder.Target(radiance, depth), normalTarget = builder.Target(normal, depth);
        const auto rawTarget = builder.Target(raw, {}), fullTarget = builder.Target(full, {});
        const auto pingTarget = builder.Target(ping, {}), pongTarget = builder.Target(pong, {});
        const auto images = builder.Finish();
        const auto capture = [&] {
            fixture.options.x = 0;
            Draw(fixture_, radianceTarget, width, height, {}, {}, {}, fixture, gi, images, true);
            fixture.options.x = 1;
            Draw(fixture_, normalTarget, width, height, {}, {}, {}, fixture, gi, images, true);
            Draw(geometry_, positionsTarget,width,height,{}, {},depth,fixture,gi,images,false);
        };
        const auto illuminate = [&](bool filtered = true, bool cached = true) {
            const auto cache = cached ? positions : Texture{};
            Draw(cached ? cachedGather_ : gather_, rawTarget, halfWidth, halfHeight, radiance, normal, depth, fixture, gi, images, false,cache);
            Texture input = raw;
            auto filterConstants = gi;
            if (filtered) for (std::uint32_t pass = 0; pass < IndirectLightingDenoisePasses; ++pass) {
                filterConstants.options.w = 1 << pass;
                const bool usePing = pass % 2 == 0;
                Draw(cached ? cachedDenoise_ : denoise_, usePing ? pingTarget : pongTarget, halfWidth, halfHeight, input,
                     normal, depth, fixture, filterConstants, images, false,cache);
                input = usePing ? ping : pong;
            }
            Draw(cached ? cachedUpsample_ : upsample_, fullTarget, width, height, input, normal, depth, fixture, gi, images, false,cache);
            return Read(full, width, height);
        };
        capture();
        const auto normals = Read(normal, width, height);
        std::vector<bool> receiver(std::size_t(width) * height, false);
        for (std::uint32_t y = 0; y < height; ++y) for (std::uint32_t x = 0; x < width; ++x) {
            const auto index = std::size_t(y) * width + x;
            if (normals[index * 4 + 1] < 0.99f || normals[index * 4 + 3] < 0.5f) continue;
            const auto h = fixture.inverseVP * glm::vec4((x + 0.5f) / width * 2 - 1,
                                                        (y + 0.5f) / height * 2 - 1, 1, 1);
            const auto direction = glm::normalize(glm::vec3(h) / h.w - eye);
            const auto point = eye + direction * (-eye.y / direction.y);
            // Restrict assertions to the wall's front side. Some floor behind
            // the finite wall is visible around its edge and should legitimately
            // receive emission when the source normal is reversed.
            receiver[index] = std::abs(point.x) < 1.5f && point.z > -0.75f && point.z < 1.5f;
        }
        const auto floorMean = [&](const Pixels& values) {
            glm::dvec3 sum(0); std::size_t count = 0;
            for (std::size_t i = 0; i < values.size(); i += 4) {
                if (!receiver[i / 4]) continue;
                sum += glm::dvec3(values[i], values[i+1], values[i+2]); ++count;
            }
            Check(count > 100, "GI corner fixture has no floor receivers");
            return glm::vec3(sum / double(count));
        };
        const auto red = illuminate();
        const auto redMean = floorMean(red);
        const auto reference = illuminate(true,false);
        double cacheDifference=0;
        for(std::size_t i=0;i<red.size();++i) cacheDifference+=std::abs(red[i]-reference[i]);
        cacheDifference/=red.size();
        std::cout << "GI geometry cache/reference mean error=" << cacheDifference << '\n';
        Check(cacheDifference<0.001,"Geometry cache changed indirect illumination");
        const auto roughness = [&](const Pixels& values) {
            double laplacian = 0; std::size_t count = 0;
            for (std::uint32_t y = 1; y + 1 < height; ++y) for (std::uint32_t x = 1; x + 1 < width; ++x) {
                const auto p = std::size_t(y) * width + x;
                if (!receiver[p] || !receiver[p-1] || !receiver[p+1] || !receiver[p-width] || !receiver[p+width]) continue;
                laplacian += std::abs(4 * values[p*4] - values[(p-1)*4] - values[(p+1)*4] -
                                      values[(p-width)*4] - values[(p+width)*4]); ++count;
            }
            return static_cast<float>(laplacian / std::max<std::size_t>(count, 1));
        };
        if (width >= 160) {
            const auto unfiltered = illuminate(false);
            const auto before = roughness(unfiltered), after = roughness(red);
            const float energyRatio = redMean.r / floorMean(unfiltered).r;
            std::cout << "GI flat receiver roughness raw/filtered=" << before << '/' << after
                      << " energy ratio=" << energyRatio << '\n';
            Check(after < before * 0.55f, "GI flat receiver retains sample-count noise and staircase bands");
            Check(energyRatio > 0.8f && energyRatio < 1.2f, "GI smoothing removes or invents substantial illumination");
        }
        float maximumFloorRed = 0;
        for (std::size_t i = 0; i < red.size(); i += 4)
            if (receiver[i / 4]) maximumFloorRed = std::max(maximumFloorRed, red[i]);
        Check(redMean.r > 0.05f && redMean.r > redMean.g * 10 && redMean.r > redMean.b * 20,
              "Red wall failed to illuminate the unlit floor with its diffuse color");
        // Even a visible emitter covers only part of the receiver hemisphere.
        // An implementation normalizing only hits would approach pi*source.
        // A perpendicular wall occupies at most half the floor hemisphere.
        // The finite sample set allows a little slack above pi/2, but cannot
        // approach pi*L, which would mean misses were removed from the divisor.
        Check(maximumFloorRed < fixture.reflectedRadiance.r * 2.0f, "GI discarded the zero contribution of missed rays");
        fixture.reflectedRadiance *= 0.5f; capture();
        const auto halfMean = floorMean(illuminate());
        Check(std::abs(halfMean.r / redMean.r - 0.5f) < 0.015f, "One-bounce radiance is not linear in source illumination");
        fixture.reflectedRadiance = glm::vec4(0);
        fixture.emittedRadiance = glm::vec4(0, 3, 0, 0); capture();
        const auto greenMean = floorMean(illuminate());
        Check(greenMean.g > 0.05f && greenMean.r < 0.0001f && greenMean.b < 0.0001f,
              "A purely emissive surface failed to illuminate its neighbour");
        if (width >= 160) {
            fixture.pattern = {24, 0, 1, 0}; capture();
            const auto checkerA = illuminate();
            fixture.pattern.y = 0.5f; capture();
            const auto checkerB = illuminate();
            const float a = floorMean(checkerA).g, b = floorMean(checkerB).g;
            std::cout << "GI fine-emitter phases E=" << a << '/' << b << " uniform=" << greenMean.g << '\n';
            Check(std::abs(a-b) < greenMean.g * 0.12f, "Subpixel checker phase changes diffuse indirect illumination");
            Check(a > greenMean.g * 0.8f && a < greenMean.g * 1.2f,
                  "Source footprint filtering loses or amplifies average emitter energy");
            fixture.pattern = glm::vec4(0); capture();
        }
        fixture.options.z = 1; capture();
        const auto backface = floorMean(illuminate());
        std::cout << "GI " << width << 'x' << height << " front/backface green E=" << greenMean.g << '/' << backface.g << '\n';
        Check(glm::length(backface) < 0.0001f, "GI transports light from the source's backface");
        fixture.options.z = 0; fixture.emittedRadiance = glm::vec4(0); capture();
        Check(glm::length(floorMean(illuminate())) < 0.0001f, "GI invented ambient light without any radiance source");
        fixture.options.y = 0; fixture.options.w = 1; capture();
        const auto self = floorMean(illuminate());
        Check(glm::length(self) < 0.0001f, "An isolated luminous plane illuminates itself through depth quantization");
        fixture.options.y = 1; fixture.options.w = 0; fixture.emittedRadiance = glm::vec4(0,3,0,0); capture();
        gi.options.z = 0;
        Check(glm::length(floorMean(illuminate())) < 0.0001f, "Disabled GI retained previous irradiance");
        std::cout << "GI " << width << 'x' << height << " floor red E=" << redMean.r << ',' << redMean.g << ',' << redMean.b
                  << " maximum=" << maximumFloorRed << " emissive green E=" << greenMean.g
                  << " backface/self=" << glm::length(backface) << '/' << glm::length(self) << '\n';
    }
    Texture Input(PipelineDetail::ResourceBuilder& builder, std::uint32_t width, std::uint32_t height,
                  const Pixels& values, bool depth = false) {
        CreateRHITextureSpec desc;
        desc.width = width; desc.height = height;
        desc.textureDataStoreType = desc.textureUseType = depth ? RHITextureFormat::Depth32F : RHITextureFormat::RGBA16F;
        desc.mipmaps = false; desc.addressMode = RHIAddressMode::ClampToEdge;
        desc.filterMode = depth ? RHIFilterMode::Nearest : RHIFilterMode::Linear;
        desc.data = values.data();
        return builder.Create<RHITextureSpec>([&](auto callback) { device_.async_CreateTexture(desc, callback); });
    }
    void CheckDenoising() {
        // A known smooth irradiance ramp with both coherent bands and independent
        // noise. A diagonal unlit neighbour tests depth-only and normal-only
        // discontinuities separately, so a plain image blur cannot pass.
        constexpr std::uint32_t width = 192, height = 128, halfWidth = width/2, halfHeight = height/2;
        IndirectLightingSettings settings; settings.enabled = true;
        const auto projection = glm::ortho(-3.0f,3.0f,-2.0f,2.0f,0.1f,10.0f);
        auto constants = MakeIndirectLightingConstants(settings, projection, glm::mat4(1));
        const auto boundary = [](std::uint32_t y) { return width/2 + y/5; };
        const auto expected = [](float x) { return 2.0f + 3.0f * (x+0.5f)/width; };
        for (bool depthBoundary : {true, false}) {
            Pixels depthPixels(width*height), normalPixels(width*height*4,0), noisy(halfWidth*halfHeight*4,0);
            for (std::uint32_t y=0;y<height;++y) for (std::uint32_t x=0;x<width;++x) {
                const auto p = std::size_t(y)*width+x;
                const bool bright = x < boundary(y);
                const float z = !bright && depthBoundary ? -4.0f : -2.0f;
                const auto clip = projection * glm::vec4(0,0,z,1);
                depthPixels[p] = clip.z*0.5f+0.5f;
                normalPixels[p*4 + (!bright && !depthBoundary ? 1 : 2)] = 1;
                normalPixels[p*4+3] = 1;
            }
            for (std::uint32_t y=0;y<halfHeight;++y) for (std::uint32_t x=0;x<halfWidth;++x) {
                const auto p = (std::size_t(y)*halfWidth+x)*4;
                const auto fullX=x*2+1, fullY=y*2+1;
                const std::uint32_t hash=(x*73856093u)^(y*19349663u);
                const float random=float(hash&65535u)/32767.5f-1.0f;
                const float signal=fullX<boundary(fullY) ? expected(float(fullX)) + 0.8f*std::sin(float(x)*0.78f)+0.55f*random : 0;
                noisy[p]=noisy[p+1]=noisy[p+2]=signal; noisy[p+3]=1;
            }
            PipelineDetail::ResourceBuilder builder(device_);
            const auto depth=Input(builder,width,height,depthPixels,true);
            const auto normal=Input(builder,width,height,normalPixels);
            const auto source=Input(builder,halfWidth,halfHeight,noisy);
            const auto ping=builder.Texture(halfWidth,halfHeight,RHITextureFormat::RGBA16F);
            const auto pong=builder.Texture(halfWidth,halfHeight,RHITextureFormat::RGBA16F);
            const auto output=builder.Texture(width,height,RHITextureFormat::RGBA16F);
            const auto pingTarget=builder.Target(ping,{}), pongTarget=builder.Target(pong,{}), target=builder.Target(output,{});
            const auto images=builder.Finish();
            Draw(upsample_,target,width,height,source,normal,depth,{},constants,images,false);
            const auto before=Read(output,width,height);
            Texture input=source;
            for (std::uint32_t pass=0;pass<IndirectLightingDenoisePasses;++pass) {
                constants.options.w=1<<pass;
                Draw(denoise_,pass%2==0?pingTarget:pongTarget,halfWidth,halfHeight,input,normal,depth,{},constants,images,false);
                input=pass%2==0?ping:pong;
            }
            Draw(upsample_,target,width,height,input,normal,depth,{},constants,images,false);
            const auto after=Read(output,width,height);
            double beforeSquared=0,afterSquared=0,energy=0,reference=0;
            std::size_t count=0; float maximumLeak=0;
            for (std::uint32_t y=0;y<height;++y) for (std::uint32_t x=0;x<width;++x) {
                const auto p=(std::size_t(y)*width+x)*4;
                if (x>=boundary(y)) maximumLeak=std::max(maximumLeak,after[p]);
                if (x<32 || x+28>=boundary(y) || y<24 || y+24>=height) continue;
                const float value=expected(float(x));
                beforeSquared+=std::pow(before[p]-value,2); afterSquared+=std::pow(after[p]-value,2);
                energy+=after[p]; reference+=value; ++count;
            }
            Check(count>100,"Denoise fixture contains no smooth interior");
            const double rawError=std::sqrt(beforeSquared/count), filteredError=std::sqrt(afterSquared/count);
            std::cout << "GI " << (depthBoundary?"depth":"normal") << " edge ramp RMS raw/filtered="
                      << rawError << '/' << filteredError << " energy=" << energy/reference << " leak=" << maximumLeak << '\n';
            Check(filteredError<rawError*0.25 && filteredError<0.12,"GI denoising retains flat-surface bands or stair steps");
            Check(std::abs(energy/reference-1.0)<0.03,"GI denoising changed smooth-surface energy");
            Check(maximumLeak<0.0001f,"GI denoising leaked illumination across a geometric edge");
        }
    }
    RHIDevice device_;
    std::shared_ptr<const void> owner_;
    Pipeline fixture_, gather_, denoise_, upsample_, geometry_, cachedGather_, cachedDenoise_, cachedUpsample_;
    RenderResourceHandle<UniformBufferSpec> fixtureBuffer_, giBuffer_;
    RenderResourceHandle<VertexBufferSpec> mesh_;
    std::uint64_t frameIndex_ = 0;
    bool stopped_ = false;
};
} // namespace

int main() {
    int result = 0;
    try {
        Check(glfwInit() == GLFW_TRUE, "glfwInit failed");
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        ApplicationWindow::Window window({64,64,"Indirect lighting GPU test",false});
        Check(window.Activate(), "Cannot activate hidden OpenGL window");
        auto context = window.GetRenderContextAsOpengl();
        Check(context.has_value(), "Missing OpenGL context");
        IndirectLightingGpuTest test(std::move(*context)); test.Run(); test.Shutdown();
    } catch (const std::exception& error) {
        std::cerr << "Indirect lighting GPU test failed: " << error.what() << '\n'; result = 1;
    }
    glfwTerminate();
    return result;
}
