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
#include "Render/Private/Pipeline/realtime_gi_shaders.h"
#include "Render/Public/Material/pbr_material.h"
#include "Render/Public/Pipeline/realtime_gi.h"

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
layout(std140,binding=10) uniform PatchData { vec4 patchOptions; };
out vec3 vWorldPosition;
out vec3 vWorldNormal;
out vec2 vTexCoord;
out vec4 vWorldTangent;
void main() {
    gl_Position=vec4(position,0,1);
    vWorldPosition=patchOptions.w>0.5 ? vec3(position*2.0,-2.0) : vec3(0);
    vWorldNormal=vec3((position+patchOptions.xy)*patchOptions.z,1);
    if (patchOptions.w>1.5) vWorldNormal=-vWorldNormal;
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

// Independent double-precision reference deliberately evaluates all eight
// neighbors, including zero-weight ones. Nonuniform SH and directional depth
// moments exercise visibility, interpolation and local sky together.
ProbeLightingSample ReferenceRealtimeProbe(const RealtimeGiConstants& constants,
    const std::vector<glm::vec4>& cache, glm::vec3 position, glm::vec3 normal,
    glm::vec3 reflection, float roughness) {
    const glm::dvec3 P(position), N(normal), R(reflection), origin(constants.origin), spacing(constants.spacing);
    const glm::ivec3 counts(constants.counts);
    const auto fetch=[&](int index,int row) { return glm::dvec4(cache[std::size_t(row)*27+index]); };
    const auto basis=[](glm::dvec3 n) {
        return std::array<double,9>{.282095,.488603*n.y,.488603*n.z,.488603*n.x,
            1.092548*n.x*n.y,1.092548*n.y*n.z,.315392*(3*n.z*n.z-1),
            1.092548*n.x*n.z,.546274*(n.x*n.x-n.y*n.y)};
    };
    ProbeLightingSample result;
    const glm::dvec3 coord=(P-origin)/spacing;
    const glm::dvec3 outside=glm::max(glm::max(-.5-coord,coord-(glm::dvec3(counts)-.5)),glm::dvec3(0));
    result.coverage=float(1-std::clamp(std::max({outside.x,outside.y,outside.z})*4.0,0.0,1.0));
    if(result.coverage<=0) return result;
    const glm::dvec3 query=P+N*double(constants.runtime.y);
    const glm::dvec3 grid=glm::clamp((query-origin)/spacing,glm::dvec3(0),glm::dvec3(counts-1));
    const glm::ivec3 base=glm::min(glm::ivec3(glm::floor(grid)),counts-2);
    const glm::dvec3 phase=grid-glm::dvec3(base);
    const auto b=basis(N), r=basis(R);
    glm::dvec3 irradiance(0); double weightSum=0,sky=0;
    for(int z=0;z<2;++z) for(int y=0;y<2;++y) for(int x=0;x<2;++x) {
        const auto cell=base+glm::ivec3(x,y,z);
        const int index=cell.x+counts.x*(cell.y+counts.y*cell.z);
        if(fetch(index,25).x<.5) continue;
        const glm::dvec3 delta=query-(origin+spacing*glm::dvec3(cell));
        const double distance=glm::length(delta);
        const glm::dvec3 direction=distance>1e-6?delta/distance:N;
        const glm::dvec3 octDirection=direction/std::max(std::abs(direction.x)+std::abs(direction.y)+std::abs(direction.z),1e-8);
        glm::dvec2 oct(octDirection);
        if(octDirection.z<0) oct=glm::dvec2((1-std::abs(oct.y))*(oct.x>=0?1:-1),
                                         (1-std::abs(oct.x))*(oct.y>=0?1:-1));
        const glm::dvec2 pixel=(oct*.5+.5)*4.0-.5, pixelPhase=pixel-glm::floor(pixel);
        const glm::ivec2 lower(glm::floor(pixel));
        glm::dvec2 moments(0);
        for(int j=0;j<2;++j) for(int i=0;i<2;++i) {
            const auto tap=glm::clamp(lower+glm::ivec2(i,j),glm::ivec2(0),glm::ivec2(3));
            const double w=(i?pixelPhase.x:1-pixelPhase.x)*(j?pixelPhase.y:1-pixelPhase.y);
            moments+=glm::dvec2(fetch(index,9+tap.y*4+tap.x))*w;
        }
        double visibility=1;
        if(distance>moments.x+constants.runtime.z) {
            const double variance=std::max(moments.y-moments.x*moments.x,.00001);
            const double d=distance-moments.x-constants.runtime.z;
            const double probability=variance/(variance+d*d);
            visibility=probability*probability*probability;
        }
        const double normalWeight=std::pow(std::max(.05,.5+.5*glm::dot(N,-direction)),2);
        const double weight=(x?phase.x:1-phase.x)*(y?phase.y:1-phase.y)*(z?phase.z:1-phase.z)*visibility*normalWeight;
        glm::dvec3 E(0); double v=0;
        for(int i=0;i<9;++i) {
            const auto sh=fetch(index,i); E+=glm::dvec3(sh)*b[i];
            const double convolution=i==0?1:(i<4?2.0/3.0:.25);
            v+=sh.w*r[i]*(1+(convolution-1)*double(roughness)*roughness);
        }
        irradiance+=glm::max(E,glm::dvec3(0))*weight;
        sky+=std::clamp(v,0.0,1.0)*weight; weightSum+=weight;
    }
    if(weightSum>1e-8) {
        result.irradianceOverPi=glm::vec3(irradiance/weightSum);
        result.skyVisibility=float(sky/weightSum);
    } else result.skyVisibility=0;
    return result;
}

class Fixture {
public:
    explicit Fixture(OpenglBackendContext context) { device_.Run(BackendType::Opengl, std::move(context)); }
    ~Fixture() { Shutdown(); }
    void Shutdown() {
        if (stopped_) return;
        probeOwner_.reset();
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
        CheckFinitePointSources();
        CheckPointSourceBoundaries();
        CheckSpotLighting();
        CheckAreaSourceHorizon();
        CheckTransmission();
        CheckTransmissionComposite();
        CheckSkyLighting();
        CheckProbeSampling();
        CheckRealtimeProbeBoundaries();
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
        auto legacyAreaSource=Material::PbrFragmentShader();
        const std::string areaCall="EvaluateAreaLighting(vWorldPosition,V,N,geometricNormal,albedo,";
        const auto areaCallOffset=legacyAreaSource.find(areaCall);
        Check(areaCallOffset!=std::string::npos,"Production PBR area call missing");
        legacyAreaSource.replace(areaCallOffset,areaCall.size(),"EvaluateAreaLighting(vWorldPosition,V,N,albedo,");
        areaLegacy_=builder.FullscreenPipeline(PatchVertexShader(),legacyAreaSource);
        pointIrradiance_=builder.FullscreenPipeline(PatchVertexShader(),std::string(R"GLSL(#version 450 core
in vec3 vWorldPosition; in vec3 vWorldNormal;
layout(location=0) out vec4 outColor;
)GLSL")+RealtimeGiQueryGLSL()+PipelineDetail::RealtimeGiGeometryGLSL()+R"GLSL(
void main(){outColor=vec4(RtDirect(vWorldPosition,normalize(vWorldNormal)),1);}
)GLSL");
        probeSampler_=builder.FullscreenPipeline(PatchVertexShader(),std::string(R"GLSL(#version 450 core
in vec3 vWorldPosition; in vec3 vWorldNormal;
layout(location=0) out vec4 outColor;
)GLSL")+DiffuseProbeGLSL()+R"GLSL(
void main() {
    vec3 N=normalize(vWorldNormal), E; float sky;
    EvaluateDiffuseProbes(vWorldPosition,N,N,1.0,E,sky);
    outColor=vec4(E,sky);
}
)GLSL");
        backgroundCopy_ = builder.FullscreenPipeline(PatchVertexShader(), R"GLSL(#version 450 core
in vec2 vTexCoord;
layout(binding=0) uniform sampler2D image;
layout(location=0) out vec4 outColor;
void main() { outColor=texture(image,vTexCoord); }
)GLSL");
        CreateGraphicShaderDesc glassProgram;
        glassProgram.vertexShaderSource = builder.Source(ShaderSourceType::Vertex, PatchVertexShader());
        glassProgram.fragmentShaderSource = builder.Source(ShaderSourceType::Fragment, Material::PbrFragmentShader(Material::Domain::Translucent));
        auto glass = builder.Create<ShaderProgramSpec>([&](auto cb) { device_.async_CreateGraphicShader(glassProgram, cb); });
        auto glassSpec = Material::PbrPipelineSpec(glass,Material::Domain::Translucent);
        glassSpec.expectVertexLayout = {{VertexFieldType::Vec2,VertexFieldType::Vec2}};
        glassSpec.depthTest = false; // This fixture isolates shader/blend, not geometry visibility.
        translucent_ = builder.Create<PipelineSpec>([&](auto cb) { device_.async_CreatePipeline({glassSpec},cb); });
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
        surface_ = builder.Uniform(sizeof(SurfacePassConstants));
        area_ = builder.Uniform(sizeof(AreaLightsConstants));
        patch_ = builder.Uniform(sizeof(glm::vec4));
        sky_ = builder.Uniform(sizeof(SkyLightConstants));
        probes_=builder.Uniform(sizeof(DiffuseProbeConstants));
        probeVisibility_=builder.Uniform(sizeof(DiffuseProbeVisibilityConstants));
        probeSettings_=builder.Uniform(sizeof(DiffuseProbeRuntimeConstants));
        realtime_=builder.Uniform(sizeof(RealtimeGiConstants));
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
        // Positive tangent normal faces a light behind the macro surface.
        // It deliberately exposes normal-map horizon leakage in the real BRDF.
        const std::array<float,4> tiltedNormal{.975f,.5f,.656125f,1};
        tiltedNormal_=texture(tiltedNormal);
        CreateRHITextureSpec lut;
        lut.width=lut.height=SkyBrdfLutSize;
        lut.textureDataStoreType=lut.textureUseType=RHITextureFormat::RGBA16F;
        lut.mipmaps=false; lut.filterMode=RHIFilterMode::Linear; lut.addressMode=RHIAddressMode::ClampToEdge;
        lut.data=SkyBrdfIntegrationLut().data();
        skyBrdf_=builder.Create<RHITextureSpec>([&](auto cb) { device_.async_CreateTexture(lut,cb); });
        backgroundPixels_.resize(Width*Height*4);
        for (std::uint32_t y=0;y<Height;++y) for (std::uint32_t x=0;x<Width;++x) {
            const auto offset=(y*Width+x)*4;
            backgroundPixels_[offset]=(x+.5f)/Width;
            backgroundPixels_[offset+1]=(y+.5f)/Height;
            backgroundPixels_[offset+2]=.5f; backgroundPixels_[offset+3]=1;
        }
        const auto image = [&](const Pixels& pixels,bool depth) {
            CreateRHITextureSpec desc;
            desc.width=Width; desc.height=Height;
            desc.textureDataStoreType=desc.textureUseType=depth ? RHITextureFormat::Depth32F : RHITextureFormat::RGBA16F;
            desc.mipmaps=false; desc.filterMode=depth ? RHIFilterMode::Nearest : RHIFilterMode::Linear;
            desc.addressMode=RHIAddressMode::ClampToEdge; desc.data=pixels.data();
            return builder.Create<RHITextureSpec>([&](auto cb) { device_.async_CreateTexture(desc,cb); });
        };
        background_ = image(backgroundPixels_,false);
        backgroundDepth_ = image(Pixels(Width*Height,.98f),true);
        foregroundDepth_ = image(Pixels(Width*Height,.8f),true);
        Pixels contrast(Width*Height*4), edge=backgroundPixels_, edgeDepth(Width*Height,.98f);
        for (std::uint32_t y=0;y<Height;++y) for (std::uint32_t x=0;x<Width;++x) {
            const auto i=(y*Width+x)*4;
            const float value=x>=28 && x<36 ? 1.0f : 0.0f;
            contrast[i]=contrast[i+1]=contrast[i+2]=value; contrast[i+3]=1;
            if (x>=32) {
                edge[i]=edge[i+1]=edge[i+2]=1;
                edgeDepth[y*Width+x]=.8f;
            }
        }
        contrastBackground_=image(contrast,false);
        edgeBackground_=image(edge,false); edgeDepth_=image(edgeDepth,true);
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
    Pixels Draw(bool enabled, float roughness, float phase, float normalSpan, bool pointLight,
                int probe=0,float opticalStrength=1,bool materialAo=true,bool emission=false,bool backSurface=false,bool twoSided=false,
                float coverage=1,int backgroundPattern=0,float distortionPixels=4) {
        Material::ParameterBlock material(Material::MakePbrTemplate(probe ? Material::Domain::Translucent : Material::Domain::Surface));
        Check(material.Set("baseColor", glm::vec4(.8f,.8f,.8f,1)) &&
              material.Set("metallic", 1.0f) && material.Set("roughness", roughness) &&
              material.Set("useNormalMap",pointNormalMap_), "Cannot set test material");
        Material::PbrPassConstants lighting;
        lighting.ambientAndExposure = glm::vec4(0);
        for (auto& color : lighting.lightColors) color = glm::vec4(0);
        lighting.specularAA.z = enabled ? 1.0f : 0.0f;
        SceneEffectsConstants effects;
        effects.sunDirectionIntensity = glm::vec4(0,0,-1,pointLight ? 0 : 1);
        effects.sunColor = glm::vec4(1);
        SkyLightConstants sky;
        if (skyProbe_) {
            effects.sunDirectionIntensity.w=0;
            sky.options=glm::vec4(1);
            Check(material.Set("baseColor",glm::vec4(1))&&material.Set("ao",skyAo_),"Cannot set sky metal probe");
        }
        if (pointLight) {
            lighting.lightPositions[0] = glm::vec4(pointPosition_,1);
            lighting.lightColors[0] = glm::vec4(100,100,100,pointRadius_);
        }
        AreaLightsConstants areaData;
        if(areaHorizonProbe_) {
            effects.sunDirectionIntensity.w=0;
            AreaLightsSettings settings;settings.count=1;
            auto& light=settings.lights[0];light.center=pointPosition_;
            light.halfAxisU={.025f,0,0};light.halfAxisV={0,.025f,0};
            light.radiance=glm::vec3(100);light.twoSided=true;
            areaData=MakeAreaLightsConstants(settings);
        }
        SurfacePassConstants surface;
        RealtimeGiConstants realtime;realtime.geometry={0,1024,0,0};
        ViewConstants view;
        if (probe) {
            Check(material.Set("baseColor",glm::vec4(0,0,0,coverage)) && material.Set("metallic",0.0f) &&
                  material.Set("opacity",opticalStrength) && material.Set("ior",1.0f) && material.Set("thickness",0.0f) &&
                  material.Set("distortionStrength",probe==1 ? 0.0f : distortionPixels) && material.Set("useDistortionMap",probe>=2) &&
                  material.Set("ao",materialAo ? 1.0f : 0.0f) &&
                  material.Set("twoSided",twoSided) &&
                  material.Set("emissiveColor",emission ? glm::vec4(2,1,.5f,0) : glm::vec4(0)), "Cannot set transmission probe");
            effects.sunDirectionIntensity.w=0;
            surface.projection=glm::perspective(glm::radians(90.0f),1.0f,.1f,10.0f);
            surface.screenTime=glm::vec4(1.0f/Width,1.0f/Height,0,1);
            view.cameraPosition=glm::vec4(0);
        }
        const glm::vec4 patch(phase * 2 / Width, phase * 2 / Height, normalSpan, probe ? (backSurface ? 2.0f : 1.0f) : 0.0f);
        RHICommand::BeginFrame begin;
        begin.frameIndex = ++frame_; begin.framebufferWidth = Width; begin.framebufferHeight = Height;
        auto encoder = device_.BeginFrame(begin);
        try {
            Check(encoder.KeepAlive(owner_), "Cannot retain PBR test resources");
            if(probeOwner_) Check(encoder.KeepAlive(probeOwner_),"Cannot retain probe test resources");
            RHICommand::SetRenderTarget target;
            target.target = target_; target.width = Width; target.height = Height;
            target.clearFlags = RHICommand::ClearColor;
            if (probe) target.clearColor=glm::vec4(.125f,.25f,.5f,1);
            Check(encoder.SetRenderTarget(target), "Cannot select PBR test target");
            Check(encoder.UpdateUniformBuffer(view_, view) && encoder.BindUniformBuffer(view_, 0) &&
                  encoder.UpdateUniformBufferBytes(material_, material.Bytes().data(), material.Bytes().size()) &&
                  encoder.BindUniformBuffer(material_, Material::MaterialParameterBinding) &&
                  encoder.UpdateUniformBuffer(lighting_, lighting) && encoder.BindUniformBuffer(lighting_, 3) &&
                  encoder.UpdateUniformBuffer(surface_, surface) && encoder.BindUniformBuffer(surface_, SurfacePassBinding) &&
                  encoder.UpdateUniformBuffer(area_, areaData) && encoder.BindUniformBuffer(area_, AreaLightsBinding) &&
                  encoder.UpdateUniformBuffer(sky_, sky) && encoder.BindUniformBuffer(sky_, SkyLightBinding) &&
                  encoder.UpdateUniformBuffer(probeSettings_,DiffuseProbeRuntimeConstants{glm::vec4(probeSampling_?1:0,1,.12f,.15f)}) &&
                  encoder.BindUniformBuffer(probeSettings_,DiffuseProbeSettingsBinding) &&
                  encoder.BindUniformBuffer(probes_,DiffuseProbeBinding) &&
                  encoder.BindUniformBuffer(probeVisibility_,DiffuseProbeVisibilityBinding) &&
                  encoder.UpdateUniformBuffer(realtime_,realtime) && encoder.BindUniformBuffer(realtime_,RealtimeGiBinding) &&
                  encoder.UpdateUniformBuffer(patch_, patch) && encoder.BindUniformBuffer(patch_, 10), "Cannot upload PBR test blocks");
            const auto* bytes = reinterpret_cast<const std::byte*>(&effects);
            for (std::uint32_t offset = 0; offset < sizeof(effects); offset += RHICommand::MaxInlineUniformBytes)
                Check(encoder.UpdateUniformBufferBytes(effects_, bytes + offset,
                    std::min<std::uint32_t>(RHICommand::MaxInlineUniformBytes, sizeof(effects)-offset), offset),
                    "Cannot upload scene block");
            Check(encoder.BindUniformBuffer(effects_, SceneEffectsBinding), "Cannot bind scene block");
            const auto background=backgroundPattern==1 ? contrastBackground_ :
                backgroundPattern==2 ? edgeBackground_ : background_;
            if (backgroundPattern) {
                // Populate the real destination with the same immutable opaque
                // snapshot before the production straight-alpha glass draw.
                Check(encoder.BindTexture(background,0) && encoder.BindPipeline(backgroundCopy_) &&
                      encoder.BindMesh(mesh_) && encoder.DrawIndexed(RHICommand::DrawIndexed{3}),
                      "Cannot prefill opaque destination");
            }
            for (std::uint32_t slot : {0u,1u,2u,3u,4u,5u,6u,7u,12u,13u,14u,15u})
                Check(encoder.BindTexture(slot == 1 ? (pointNormalMap_?tiltedNormal_:flatNormal_) : white_, slot), "Cannot bind PBR fallback texture");
            Check(encoder.BindTexture(background,OpaqueColorTextureSlot) &&
                  encoder.BindTexture(probe==3 ? foregroundDepth_ : backgroundPattern==2 ? edgeDepth_ : backgroundDepth_,
                                      OpaqueDepthTextureSlot), "Cannot bind refraction snapshots");
            Check(encoder.BindTexture(white_,SkyReflectionTextureSlot)&&encoder.BindTexture(skyBrdf_,SkyBrdfTextureSlot),"Cannot bind sky probe textures");
            Check(encoder.BindPipeline(pointIrradianceProbe_ ? pointIrradiance_ : areaLegacyProbe_ ? areaLegacy_ : probeSampling_ ? probeSampler_ : probe ? translucent_ : pipeline_) && encoder.BindMesh(mesh_) &&
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
    void CheckTransmission() {
        const auto identity=Draw(true,.5f,0,0,false,1);
        const auto distorted=Draw(true,.5f,0,0,false,2);
        const auto foreground=Draw(true,.5f,0,0,false,3);
        Check(RelativeDifference(backgroundPixels_,identity)<.001,"IOR=1/zero offset did not preserve background");
        Check(RelativeDifference(identity,distorted)>.04,"Custom RG refraction had no effect");
        Check(RelativeDifference(identity,foreground)<.001,"Refraction pulled foreground depth across the receiver");
        const auto invisible=Draw(true,.5f,0,0,false,2,0);
        const auto back=Draw(true,.5f,0,0,false,1,1,true,false,true,false);
        const auto sheet=Draw(true,.5f,0,0,false,1,1,true,false,true,true);
        Check(RelativeDifference(identity,sheet)<.001,"Two-sided thin sheet backside was treated as an exit interface");
        for (std::size_t i=0;i<invisible.size();i+=4)
            Check(invisible[i]==.125f && invisible[i+1]==.25f && invisible[i+2]==.5f &&
                  back[i]==.125f && back[i+1]==.25f && back[i+2]==.5f,"Opacity zero/default back surface changed destination");
        const auto emissiveAo=Draw(true,.5f,0,0,false,1,1,true,true);
        const auto emissiveNoAo=Draw(true,.5f,0,0,false,1,1,false,true);
        Check(RelativeDifference(emissiveAo,emissiveNoAo)==0,"Material AO darkened emission/transmission");
        Check(emissiveAo[0]-identity[0]>1.99f,"Emission probe did not emit");
        // The right/top border must fall back, never stretch a clamped edge.
        for (std::uint32_t y=0;y<Height;++y) {
            const auto i=(y*Width+Width-1)*4;
            Check(std::abs(distorted[i]-identity[i])<.001f,"Out-of-view refraction failed to fall back");
        }
        std::cout<<"Transmission IOR=1 identity, custom distortion, foreground/border rejection, opacity zero, front/thin-sheet sides and emission/AO passed\n";
    }
    void CheckFinitePointSources() {
        // Disable screen AA here: the actual finite emitter, independently of
        // any temporal/pixel filter, must remove the ideal point's narrow spike.
        std::vector<double> idealEnergy,finiteEnergy;float idealPeak=0,finitePeak=0;
        for(unsigned i=0;i<33;++i) {
            const float phase=float(i)/32;
            pointRadius_=0;const auto ideal=Measure(Draw(false,.045f,phase,1,true));
            pointRadius_=1;const auto finite=Measure(Draw(false,.045f,phase,1,true));
            idealEnergy.push_back(ideal.energy);finiteEnergy.push_back(finite.energy);
            idealPeak=std::max(idealPeak,ideal.peak);finitePeak=std::max(finitePeak,finite.peak);
        }
        const auto before=RelativeVariation(idealEnergy),after=RelativeVariation(finiteEnergy);
        Check(before>.25&&after<before*.2&&after<.15,"Finite point source retained ideal-point highlight aliasing");
        Check(finitePeak>0&&finitePeak<idealPeak*.2,"Finite point footprint erased illumination or retained HDR spikes");
        const auto roughFinite=Draw(false,1,.31f,4,true);pointRadius_=0;
        Check(RelativeDifference(roughFinite,Draw(false,1,.31f,4,true))==0,"Finite source changed fully rough material response");
        std::cout<<"Finite point source phase range/mean="<<before<<" -> "<<after<<", peak="<<idealPeak<<" -> "<<finitePeak<<'\n';
    }
    void CheckPointSourceBoundaries() {
        pointNormalMap_=true;pointPosition_={1,0,-.1f};pointRadius_=0;
        const auto blocked=Measure(Draw(false,1,0,0,true));
        pointPosition_.z=.1f;const auto facing=Measure(Draw(false,1,0,0,true));
        Check(blocked.energy==0&&facing.energy>1,"Normal map admitted a point light behind the geometric surface");
        // Fully rough GGX is independent of the finite-source angular filter.
        // Near/far controls therefore isolate only the physical energy bound.
        pointNormalMap_=false;pointPosition_={0,0,.02f};pointRadius_=0;
        const auto idealNear=Measure(Draw(false,1,0,0,true));
        pointPosition_.z=.04f;const auto idealFar=Measure(Draw(false,1,0,0,true));
        Check(std::abs(idealNear.energy/idealFar.energy-4)<.002,"Radius-zero point light changed inverse-square attenuation");
        pointRadius_=.2f;pointPosition_.z=.02f;const auto finiteNear=Measure(Draw(false,1,0,0,true));
        pointPosition_.z=.04f;const auto finiteFar=Measure(Draw(false,1,0,0,true));
        Check(finiteNear.energy>0&&std::abs(finiteNear.energy/finiteFar.energy-1)<.002&&
              std::abs(finiteNear.energy/idealNear.energy-.01)<.0001,
              "Point light energy grew beyond I/radius^2 inside its finite source");
        pointIrradianceProbe_=true;pointPosition_={0,0,.25f};pointRadius_=0;
        const auto rawDirect=Measure(Draw(false,1,0,0,true));pointRadius_=.5f;
        const auto boundedDirect=Measure(Draw(false,1,0,0,true));
        Check(std::abs(rawDirect.peak-1600)<1&&std::abs(boundedDirect.peak-400)<1,
              "Realtime GI point irradiance disagrees with the finite-source energy bound");
        pointIrradianceProbe_=false;pointRadius_=0;pointPosition_={0,0,10};
        std::cout<<"Point geometric horizon, radius-zero inverse square and main/GI finite-source energy bounds passed\n";
    }
    void CheckAreaSourceHorizon() {
        areaHorizonProbe_=true;pointNormalMap_=true;pointPosition_={1,0,-.1f};
        areaLegacyProbe_=true;const auto legacyBehind=Measure(Draw(false,1,0,0,false));
        areaLegacyProbe_=false;const auto behind=Measure(Draw(false,1,0,0,false));
        pointPosition_.z=.1f;const auto facing=Measure(Draw(false,1,0,0,false));
        Check(legacyBehind.energy>1&&behind.energy==0&&facing.energy>1,
              "Normal map admitted a rectangle behind the geometric surface or removed front lighting");
        // Explicit Ng=N must agree with the unchanged public legacy integral
        // outside the narrow fade, at both normal and grazing incidence.
        pointNormalMap_=false;
        for(float z:{1.0f,.1f}) for(float roughness:{.045f,.5f,1.0f}) {
            pointPosition_={1,0,z};areaLegacyProbe_=false;
            const auto geometric=Draw(false,roughness,0,0,false);
            areaLegacyProbe_=true;const auto legacy=Draw(false,roughness,0,0,false);
            Check(Measure(legacy).energy>0&&RelativeDifference(legacy,geometric)==0,
                  "Area geometric horizon changed flat-normal lighting outside its fade");
        }
        // Within the explicit Ng fade, the legacy public overload still keeps
        // its original grazing integral rather than silently gaining the fade.
        pointPosition_={1,0,.02f};areaLegacyProbe_=true;
        const auto legacyGrazing=Measure(Draw(false,1,0,0,false));
        areaLegacyProbe_=false;const auto faded=Measure(Draw(false,1,0,0,false));
        Check(legacyGrazing.energy>0&&faded.energy>0&&faded.energy<legacyGrazing.energy*.5,
              "Explicit area Ng fade was absent or changed legacy grazing integration");
        areaHorizonProbe_=false;pointPosition_={0,0,10};
        std::cout<<"Area normal-map geometric horizon and unchanged legacy/flat-normal quadrature passed\n";
    }
    void CheckTransmissionComposite() {
        const auto draw=[&](float strength,float coverage=1,int pattern=1,float distortion=4) {
            return Draw(true,.5f,0,0,false,2,strength,true,false,false,false,coverage,pattern,distortion);
        };
        const auto bar=[](int x) { return x>=28 && x<36 ? 1.0f : 0.0f; };
        for (float strength : {1.0f,.9f,.5f,0.0f}) {
            const auto pixels=draw(strength);
            float maximumError=0;
            for (int x=8;x<56;++x) {
                const float displaced=x+4*strength;
                const int lower=static_cast<int>(std::floor(displaced));
                const float phase=displaced-lower;
                const float expected=bar(lower)*(1-phase)+bar(lower+1)*phase;
                maximumError=std::max(maximumError,std::abs(pixels[(32*Width+x)*4]-expected));
            }
            Check(maximumError<.001f,"Full-coverage refraction retained an unshifted second background edge");
            std::cout<<"Refraction optical strength="<<strength<<" full-coverage single-image max error="<<maximumError<<'\n';
        }
        const auto halfCoverage=draw(1,.5f);
        for (int x=8;x<56;++x) {
            const float expected=.5f*bar(x+4)+.5f*bar(x);
            Check(std::abs(halfCoverage[(32*Width+x)*4]-expected)<.001f,
                  "Partial coverage no longer performs a legitimate background mixture");
        }
        const auto zeroCoverage=draw(1,0);
        for (int x=8;x<56;++x)
            Check(zeroCoverage[(32*Width+x)*4]==bar(x),"Zero coverage changed the destination");

        // Move a refracted footprint across a high-contrast opaque foreground
        // edge in 1/16-pixel increments. Raw linear color sampling would pull
        // white foreground into the background before nearest-depth rejection.
        float previous=0, maximumStep=0, maximumValue=0;
        for (int phase=0;phase<=32;++phase) {
            const auto pixels=draw(1,1,2,1.0f+phase/16.0f);
            const float value=pixels[(32*Width+30)*4];
            maximumValue=std::max(maximumValue,value);
            if (phase) maximumStep=std::max(maximumStep,std::abs(value-previous));
            previous=value;
        }
        Check(maximumValue<.50f,"Refraction bilinear footprint leaked foreground color");
        Check(maximumStep<.01f,"Foreground rejection produced a discontinuous refraction jump");
        std::cout<<"Real alpha composite: soft/zero coverage passed; foreground phase max step="
                 <<maximumStep<<", max background red="<<maximumValue<<'\n';
    }
    void CheckSkyLighting() {
        const auto legacy=Draw(true,.5f,0,0,false);
        skyProbe_=true;
        for(float roughness:{.045f,.25f,.5f,.8f,1.0f}) {
            skyAo_=1;
            const auto lit=Draw(true,roughness,0,0,false);
            const auto energy=Measure(lit).energy/(Width*Height);
            Check(std::abs(energy-1)<.002,"Unit white sky lost energy on a rough white conductor");
            skyAo_=0;
            const auto occluded=Draw(true,roughness,0,0,false);
            Check(Measure(occluded).energy<.001,"Material AO did not occlude sky specular lighting");
            std::cout << "Sky white metal roughness=" << roughness << " energy=" << energy << '\n';
        }
        skyProbe_=false;
        const auto noSky=Draw(true,.5f,0,0,false);
        Check(RelativeDifference(noSky,legacy)==0,"Disabled sky retained prior environment state");
    }
    void CheckProbeSampling() {
        ProbeBakeSettings settings;
        settings.origin=glm::vec3(-1); settings.spacing=glm::vec3(2); settings.counts={2,2,2};
        ProbeBakeLighting light;
        light.sky.enabled=true;
        const auto volume=BakeDiffuseProbeVolume(settings,{},light);
        PipelineDetail::ResourceBuilder builder(device_);
        const auto uniform=[&](const auto& data) {
            CreateUniformBufferDesc desc; desc.byteSize=sizeof(data); desc.initialData=&data; desc.usage=BufferUsage::Static;
            return builder.Create<UniformBufferSpec>([&](auto cb){device_.async_CreateUniformBuffer(desc,cb);});
        };
        probes_=uniform(volume->Constants()); probeVisibility_=uniform(volume->Visibility());
        probeOwner_=builder.Finish(); probeSampling_=true;
        const auto expected=SampleDiffuseProbeVolume(*volume,{0,0,0},{0,0,1},{0,0,1});
        const auto pixels=Draw(true,1,0,0,false);
        for(std::size_t i=0;i<pixels.size();i+=4) {
            for(int c=0;c<3;++c) Check(std::abs(pixels[i+c]-expected.irradianceOverPi[c])<.001f,"CPU/GPU probe SH or interpolation mismatch");
            Check(std::abs(pixels[i+3]-expected.skyVisibility)<.001f,"CPU/GPU local sky visibility mismatch");
        }
        struct Blocks { bool valid=true; GLenum error=GL_NO_ERROR; };
        auto blocks=std::make_shared<Completion<Blocks>>();
        device_.async_ExecuteCode([this,blocks]{
            const auto* p=device_.GetResourcePool().PipelineTable.Get(probeSampler_);
            const auto* s=device_.GetResourcePool().shaderProgramTable.Get(p->shaderProgram);
            const std::array<const char*,3> names{"DiffuseProbeDataBlock","DiffuseProbeSettingsBlock","DiffuseProbeVisibilityBlock"};
            const std::array<GLint,3> sizes{14400,16,16384};
            for(int i=0;i<3;++i) {
                auto index=glGetUniformBlockIndex(s->rhi_id,names[i]); GLint size=0,binding=0;
                if(index==GL_INVALID_INDEX) {blocks->value.valid=false;continue;}
                glGetActiveUniformBlockiv(s->rhi_id,index,GL_UNIFORM_BLOCK_DATA_SIZE,&size);
                glGetActiveUniformBlockiv(s->rhi_id,index,GL_UNIFORM_BLOCK_BINDING,&binding);
                blocks->value.valid &= size==sizes[i]&&binding==12+i;
            }
            blocks->value.error=glGetError();
        },[blocks]{blocks->done=true;});
        Wait([blocks]{return blocks->done;},"Probe block reflection");
        Check(blocks->value.valid&&blocks->value.error==GL_NO_ERROR,"Probe uniform ABI mismatch");
        probeSampling_=false;
        std::cout<<"World probe GPU SH/visibility agrees with CPU; UBO 14400/16/16384 bytes verified\n";
    }
    void CheckRealtimeProbeBoundaries() {
        constexpr unsigned CacheWidth=27;
        std::vector<glm::vec4> cache(CacheWidth*RealtimeGiCacheRows,glm::vec4(0));
        for(unsigned index=0;index<CacheWidth;++index) {
            for(int row=0;row<9;++row) {
                const float phase=float(int((index+unsigned(row))%5)-2);
                cache[std::size_t(row)*CacheWidth+index]=row==0?
                    glm::vec4(.15f+.012f*index,.2f+.007f*index,.3f+.009f*index,.6f+.012f*index):
                    glm::vec4(.001f*phase,-.002f*phase,.003f*phase,.008f*phase);
            }
            for(int row=9;row<25;++row) {
                const float mean=.5f+.1f*(index%5)+.03f*(row-9);
                cache[std::size_t(row)*CacheWidth+index]=glm::vec4(mean,mean*mean+.15f+.01f*(row-9),0,0);
            }
            cache[25*CacheWidth+index].x=1;
        }
        RealtimeGiConstants constants;
        constants.origin=glm::vec4(0);constants.spacing={2,1.5f,3,0};constants.counts={3,3,3,0};
        constants.runtime.y=0;constants.runtime.z=.15f;
        struct alignas(16) Query { glm::vec4 position,normalRoughness,reflection; };
        Query query{glm::vec4(0),glm::vec4(glm::normalize(glm::vec3(1,2,3)),.37f),
                    glm::vec4(glm::normalize(glm::vec3(-2,1,3)),0)};
        PipelineDetail::ResourceBuilder builder(device_);
        // Count calls to the real texture accessor in this diagnostic only;
        // production GLSL stays uninstrumented. Color and sky still use the
        // same query, so a count-only shortcut cannot pass the reference check.
        auto source=RealtimeGiQueryGLSL();
        const auto fetch=source.find("return texelFetch(REALTIME_GI_CACHE,");
        Check(fetch!=std::string::npos,"Realtime cache accessor missing");
        source.insert(fetch,"diagnosticFetches+=1; ");
        source=std::string(R"GLSL(#version 450 core
layout(location=0) out vec4 outColor;
layout(std140,binding=10) uniform TestProbeQuery {
    vec4 queryPosition,queryNormalRoughness,queryReflection;
};
float diagnosticFetches;
)GLSL")+source+R"GLSL(
void main() {
    diagnosticFetches=0;
    vec3 E;float sky;
    float coverage=EvaluateRealtimeGi(queryPosition.xyz,queryNormalRoughness.xyz,
        queryReflection.xyz,queryNormalRoughness.w,E,sky);
    outColor=gl_FragCoord.x<1.0?vec4(E,sky):vec4(coverage,diagnosticFetches,0,1);
}
)GLSL";
        const auto pipeline=builder.FullscreenPipeline(R"GLSL(#version 450 core
layout(location=0) in vec2 position;
void main() { gl_Position=vec4(position,0,1); }
)GLSL",source);
        const auto rt=builder.Uniform(sizeof(constants)), queryBuffer=builder.Uniform(sizeof(query));
        CreateRHITextureSpec texture;
        texture.width=CacheWidth;texture.height=RealtimeGiCacheRows;
        texture.textureDataStoreType=texture.textureUseType=RHITextureFormat::RGBA32F;
        texture.mipmaps=false;texture.filterMode=RHIFilterMode::Nearest;
        texture.addressMode=RHIAddressMode::ClampToEdge;texture.data=cache.data();
        const auto cacheTexture=builder.Create<RHITextureSpec>([&](auto cb) {device_.async_CreateTexture(texture,cb);});
        const auto color=builder.Texture(2,1,RHITextureFormat::RGBA32F);
        const auto target=builder.Target(color,{});
        const auto resources=builder.Finish();
        struct Case { const char* name;glm::vec3 grid;unsigned accesses; };
        const std::array cases{
            Case{"minimum X face",{0,.375f,.625f},56},Case{"maximum X face",{2,.375f,.625f},56},
            Case{"minimum Y face",{.375f,0,.625f},56},Case{"maximum Y face",{.375f,2,.625f},56},
            Case{"minimum Z face",{.375f,.625f,0},56},Case{"maximum Z face",{.375f,.625f,2},56},
            Case{"minimum edge",{0,0,.625f},28},Case{"maximum edge",{2,2,.625f},28},
            Case{"minimum corner",{0,0,0},14},Case{"maximum corner",{2,2,2},14},
            Case{"interior probe center",{1,1,1},14},Case{"interior",{.375f,.625f,1.25f},112},
            Case{"tiny nonzero phase",{1e-10f,.375f,.625f},112},
            Case{"clamped face",{-.1f,.375f,.625f},56},
            Case{"partial outside coverage",{-.625f,.375f,.625f},56},
            Case{"outside coverage",{-1,.375f,.625f},0}};
        for(const auto& test:cases) {
            query.position=glm::vec4(test.grid*glm::vec3(constants.spacing),0);
            const auto reference=ReferenceRealtimeProbe(constants,cache,glm::vec3(query.position),
                glm::vec3(query.normalRoughness),glm::vec3(query.reflection),query.normalRoughness.w);
            RHICommand::BeginFrame begin;
            begin.frameIndex=++frame_;begin.framebufferWidth=Width;begin.framebufferHeight=Height;
            auto encoder=device_.BeginFrame(begin);
            try {
                RHICommand::SetRenderTarget output;output.target=target;output.width=2;output.height=1;
                Check(encoder.KeepAlive(owner_)&&encoder.KeepAlive(resources)&&encoder.SetRenderTarget(output)&&
                    encoder.UpdateUniformBuffer(rt,constants)&&encoder.BindUniformBuffer(rt,RealtimeGiBinding)&&
                    encoder.UpdateUniformBuffer(queryBuffer,query)&&encoder.BindUniformBuffer(queryBuffer,10)&&
                    encoder.BindTexture(cacheTexture,RealtimeGiCacheTextureSlot)&&encoder.BindPipeline(pipeline)&&
                    encoder.BindMesh(mesh_)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3})&&encoder.End(false),
                    "Cannot record realtime probe diagnostic");
                auto done=std::make_shared<Completion<bool>>();
                Check(device_.async_SubmitFrameCommands(encoder.GetCommandBuffer(),[done] {done->done=true;}),
                      "Cannot submit realtime probe diagnostic");
                Wait([done] {return done->done;},"Realtime probe diagnostic");
            } catch(...) {encoder.Cancel();throw;}
            struct Readback {std::array<float,8> rgba{};GLenum error=GL_NO_ERROR;};
            auto result=std::make_shared<Completion<Readback>>();
            device_.async_ExecuteCode([this,color,result] {
                const auto* image=device_.GetResourcePool().TextureTable.Get(color);
                if(!image) {result->value.error=GL_INVALID_VALUE;return;}
                glGetTextureImage(image->rhi_id,0,GL_RGBA,GL_FLOAT,sizeof(result->value.rgba),result->value.rgba.data());
                result->value.error=glGetError();
            },[result] {result->done=true;});
            Wait([result] {return result->done;},"Realtime probe readback");
            const auto& pixels=result->value.rgba;
            Check(result->value.error==GL_NO_ERROR,"Realtime probe GPU readback failed");
            for(int c=0;c<3;++c) Check(std::abs(pixels[c]-reference.irradianceOverPi[c])<2e-5f,
                std::string(test.name)+": realtime irradiance disagrees with unculled CPU reference");
            Check(std::abs(pixels[3]-reference.skyVisibility)<2e-5f&&std::abs(pixels[4]-reference.coverage)<2e-6f,
                  std::string(test.name)+": realtime local sky/coverage changed");
            Check(pixels[5]==float(test.accesses),std::string(test.name)+": zero-weight probe access budget mismatch");
        }
        std::cout<<"Realtime probes agree with unculled CPU reference: face/edge/corner accesses 56/28/14, interior 112; tiny nonzero phases retained\n";
    }
    void CheckSpotLighting() {
        // Exercise the actual world transport shader, independently of PBR,
        // the surface atlas, history, exposure and screen-space reprojection.
        SpotLightSettings light;light.enabled=true;light.direction={0,0,-7};
        light.intensity={24,12,6};light.sourceRadius=.125f;
        RealtimeGiConstants constants;constants.trace.y=.001f;
        struct alignas(16) Query {glm::vec4 position,normal;};
        Query query{};
        std::array<ProbeTriangle,2> blocker{};
        blocker[0].a={-5,-5,-1};blocker[0].b={5,-5,-1};blocker[0].c={5,5,-1};
        blocker[1].a={-5,-5,-1};blocker[1].b={5,5,-1};blocker[1].c={-5,5,-1};
        const auto scene=BuildRealtimeGiScene(blocker);
        PipelineDetail::ResourceBuilder builder(device_);
        const auto pipeline=builder.FullscreenPipeline(R"GLSL(#version 450 core
layout(location=0) in vec2 position;
void main(){gl_Position=vec4(position,0,1);}
)GLSL",std::string(R"GLSL(#version 450 core
layout(location=0) out vec4 outColor;
layout(std140,binding=10) uniform SpotTestQuery {vec4 queryPosition,queryNormal;};
)GLSL")+RealtimeGiQueryGLSL()+PipelineDetail::RealtimeGiGeometryGLSL()+R"GLSL(
void main(){outColor=vec4(RtDirect(queryPosition.xyz,queryNormal.xyz),1);}
)GLSL");
        const auto rt=builder.Uniform(sizeof(constants)),queryBuffer=builder.Uniform(sizeof(query));
        CreateRHITextureSpec texture;texture.width=scene->Width();texture.height=scene->Height();
        texture.textureDataStoreType=texture.textureUseType=RHITextureFormat::RGBA32F;
        texture.mipmaps=false;texture.filterMode=RHIFilterMode::Nearest;texture.addressMode=RHIAddressMode::ClampToEdge;
        texture.data=scene->Pixels().data();
        const auto geometry=builder.Create<RHITextureSpec>([&](auto cb){device_.async_CreateTexture(texture,cb);});
        const auto color=builder.Texture(1,1,RHITextureFormat::RGBA32F);
        const auto target=builder.Target(color,{});
        const auto resources=builder.Finish();
        struct Case {const char* name;double angle,distance;float radius;bool reverseNormal,blocked,enabled;};
        const std::array cases{
            Case{"axis",0,2,.125f,false,false,true},
            Case{"inner boundary",double(light.innerAngle),2,.125f,false,false,true},
            Case{"penumbra 1",.21,2,.125f,false,false,true},
            Case{"penumbra 2",.27,2,.125f,false,false,true},
            Case{"penumbra 3",.32,2,.125f,false,false,true},
            Case{"outer boundary",double(light.outerAngle),2,.125f,false,false,true},
            Case{"outside cone",.4,2,.125f,false,false,true},
            Case{"behind source",3.141592653589793,2,.125f,false,false,true},
            Case{"receiver backside",0,2,.125f,true,false,true},
            Case{"disabled",0,2,.125f,false,false,false},
            Case{"inverse square",0,4,.125f,false,false,true},
            Case{"finite radius",0,2,4,false,false,true},
            Case{"near source",0,.001,.125f,false,false,true},
            Case{"opaque occluder",0,2,.125f,false,true,true},
            Case{"occluder inside represented source",0,2,1.5f,false,true,true}};
        Material::PbrPassConstants noPoints;noPoints.lightColors={};AreaLightsConstants noAreas;
        for(const auto& test:cases) {
            light.enabled=test.enabled;light.sourceRadius=test.radius;constants.spotLight=MakeSpotLightConstants(light);
            constants.geometry={int(scene->NodeCount()*2),int(scene->Width()),test.blocked?int(scene->TriangleCount()):0,int(scene->NodeCount())};
            const glm::dvec3 position(std::sin(test.angle)*test.distance,0,-std::cos(test.angle)*test.distance);
            const glm::dvec3 normal=-position/test.distance*(test.reverseNormal?-1.0:1.0);
            query.position=glm::vec4(glm::vec3(position),0);query.normal=glm::vec4(glm::vec3(normal),0);
            const double phase=std::clamp((std::cos(test.angle)-std::cos(double(light.outerAngle)))/
                (std::cos(double(light.innerAngle))-std::cos(double(light.outerAngle))),0.0,1.0);
            const double beam=phase*phase*(3-2*phase);
            const bool occluded=test.blocked&&test.radius<1;
            const double factor=test.enabled&&!test.reverseNormal&&!occluded?
                beam/std::max({test.distance*test.distance,double(test.radius)*test.radius,.0001}):0;
            const auto reference=glm::dvec3(light.intensity)*factor;
            RHICommand::BeginFrame begin;begin.frameIndex=++frame_;begin.framebufferWidth=Width;begin.framebufferHeight=Height;
            auto encoder=device_.BeginFrame(begin);
            try {
                RHICommand::SetRenderTarget output;output.target=target;output.width=1;output.height=1;
                Check(encoder.KeepAlive(owner_)&&encoder.KeepAlive(resources)&&encoder.SetRenderTarget(output)&&
                    encoder.UpdateUniformBuffer(rt,constants)&&encoder.BindUniformBuffer(rt,RealtimeGiBinding)&&
                    encoder.UpdateUniformBuffer(queryBuffer,query)&&encoder.BindUniformBuffer(queryBuffer,10)&&
                    encoder.UpdateUniformBuffer(lighting_,noPoints)&&encoder.BindUniformBuffer(lighting_,3)&&
                    encoder.UpdateUniformBuffer(area_,noAreas)&&encoder.BindUniformBuffer(area_,8)&&
                    encoder.BindTexture(geometry,0)&&encoder.BindPipeline(pipeline)&&encoder.BindMesh(mesh_)&&
                    encoder.DrawIndexed(RHICommand::DrawIndexed{3})&&encoder.End(false),"Cannot record world spot test");
                auto done=std::make_shared<Completion<bool>>();
                Check(device_.async_SubmitFrameCommands(encoder.GetCommandBuffer(),[done]{done->done=true;}),"Cannot submit world spot test");
                Wait([done]{return done->done;},"World spot test");
            }catch(...){encoder.Cancel();throw;}
            struct Readback {std::array<float,4> rgba{};GLenum error=GL_NO_ERROR;};
            auto result=std::make_shared<Completion<Readback>>();
            device_.async_ExecuteCode([this,color,result]{
                const auto* image=device_.GetResourcePool().TextureTable.Get(color);
                if(!image){result->value.error=GL_INVALID_VALUE;return;}
                glGetTextureImage(image->rhi_id,0,GL_RGBA,GL_FLOAT,sizeof(result->value.rgba),result->value.rgba.data());
                result->value.error=glGetError();
            },[result]{result->done=true;});
            Wait([result]{return result->done;},"World spot readback");
            Check(result->value.error==GL_NO_ERROR,"World spot GPU readback failed");
            for(int c=0;c<3;++c)Check(std::isfinite(result->value.rgba[c])&&
                std::abs(double(result->value.rgba[c])-reference[c])<std::max(2e-5,std::abs(reference[c])*1e-5),
                std::string(test.name)+": world spot cone, inverse-square energy or visibility disagrees with reference");
            Check(result->value.rgba[3]==1,"World spot target was not written");
        }
        std::cout<<"World spot transport: RGB beam/penumbra, zero backward and receiver-backside light, inverse square, finite source and opaque visibility passed\n";
    }
    bool probeSampling_=false, skyProbe_=false,pointNormalMap_=false,pointIrradianceProbe_=false;
    bool areaHorizonProbe_=false,areaLegacyProbe_=false;
    float skyAo_=1,pointRadius_=0;
    glm::vec3 pointPosition_{0,0,10};
    RHIDevice device_;
    std::shared_ptr<const void> owner_;
    std::shared_ptr<const void> probeOwner_;
    RenderResourceHandle<PipelineSpec> pipeline_, translucent_, backgroundCopy_, probeSampler_,pointIrradiance_,areaLegacy_;
    RenderResourceHandle<VertexBufferSpec> mesh_;
    RenderResourceHandle<UniformBufferSpec> view_, material_, lighting_, effects_, surface_, area_, patch_, sky_;
    RenderResourceHandle<UniformBufferSpec> probes_,probeVisibility_,probeSettings_,realtime_;
    RenderResourceHandle<RHITextureSpec> skyBrdf_;
    RenderResourceHandle<RHITextureSpec> white_, flatNormal_,tiltedNormal_, output_, background_, backgroundDepth_, foregroundDepth_;
    RenderResourceHandle<RHITextureSpec> contrastBackground_, edgeBackground_, edgeDepth_;
    Pixels backgroundPixels_;
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
