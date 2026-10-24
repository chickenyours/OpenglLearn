#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <glad/glad.h>
#include "ApplicationWindow/public/window.h"
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Public/Material/pbr_material.h"
#include "Render/Public/Pipeline/realtime_gi.h"

namespace {
using namespace Render;
using namespace std::chrono_literals;
constexpr std::uint32_t Size = 8;
constexpr double Pi = 3.14159265358979323846;
template<class T> struct Completion { T value{}; bool done = false; };
void Check(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

// A constant analytic surface isolates the production BRDF from camera,
// tessellation, normal variance, shadows, GI and display transformations.
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
    gl_Position=vec4(position,0,1);
    vWorldPosition=vec3(0); vWorldNormal=vec3(0,0,1);
    vTexCoord=uv; vTexCoord1=vTexCoord; vWorldTangent=vec4(1,0,0,1);
}
)GLSL";
}
struct alignas(16) ViewConstants {
    glm::mat4 viewProjection{1};
    glm::vec4 cameraPosition{0,0,10,0};
};
struct MaterialCase {
    bool skin = false, native = false, normalMapped = false;
    glm::vec3 albedo{.4f,.2f,.1f};
    float metallic = 0, roughness = .35f;
    float scatter = .6f, wrap = .5f, specularMix = .4f, roughness2 = .6f, transmission = 0;
};

// Independent double-precision reference for the existing opaque BRDF. It
// deliberately does not call a production CPU helper or duplicate skin code.
glm::dvec3 LegacyReference(const MaterialCase& material, glm::dvec3 lightDirection,double observedNoH=-1) {
    const glm::dvec3 N(0,0,1), V(0,0,1), L=glm::normalize(lightDirection);
    const double NoL=std::max(L.z,0.0);
    if (NoL<=0) return glm::dvec3(0);
    const glm::dvec3 H=glm::normalize(V+L), albedo(material.albedo);
    const auto F0=glm::mix(glm::dvec3(.05),albedo,double(material.metallic));
    const auto F=F0+(glm::dvec3(1)-F0)*std::pow(1-std::max(glm::dot(H,V),0.0),5.0);
    const double r=std::clamp(double(material.roughness),.045,1.0), a2=std::pow(r,4);
    const double NoH=observedNoH<0?H.z:observedNoH;
    const double denominator=NoH*NoH*(a2-1)+1;
    const double D=a2/(Pi*std::max(denominator*denominator,1e-12));
    const double k=(r+1)*(r+1)/8;
    const double G=NoL/std::max(NoL*(1-k)+k,1e-6); // NoV=1.
    const auto specular=D*G*F/std::max(4*NoL,1e-4);
    const auto diffuse=(glm::dvec3(1)-F)*(1-double(material.metallic))*albedo/Pi;
    const double t=std::clamp(L.z/.05,0.0,1.0), horizon=t*t*(3-2*t);
    return (diffuse+specular)*NoL*horizon;
}
double Maximum(const glm::dvec3& value) { return std::max({value.x,value.y,value.z}); }
double RelativeError(glm::dvec3 value,glm::dvec3 reference) {
    return Maximum(glm::abs(value-reference))/std::max(Maximum(glm::abs(reference)),1e-8);
}

// Independently transcribed from recovered_skin_math.hlsl / native DXIL
// 1660..1800, not from a generated GLSL string. All input colors are encoded
// source bytes and are squared only in the recovered scattering operation.
glm::dvec3 NativeDiffuseReference(const MaterialCase& material,glm::dvec3 direction) {
    const glm::dvec3 geometry(0,0,1), view(0,0,1);
    const auto normal=material.normalMapped?glm::normalize(glm::dvec3(.5,0,1)):geometry;
    const auto light=glm::normalize(direction);
    const double coarseWrap=.15+.8*.2;
    const double macroWrap=std::clamp((glm::dot(geometry,light)+coarseWrap)/(1+coarseWrap),0.0,1.0);
    const double normalWeight=.3+.7*std::pow(macroWrap,.3);
    const auto lightNormal=glm::normalize(geometry*(1-normalWeight)+normal*normalWeight);
    const double wrap=(glm::dot(lightNormal,light)+coarseWrap)/(1+coarseWrap);
    const double colorFade=std::clamp(wrap*3,0.0,1.0);
    const std::array<double,3> bleed{206.0/255,109.0/255,173.0/255};
    const std::array<double,3> coarse{248.0/255,165.0/255,81.0/255};
    glm::dvec3 result(0);
    for (int c=0;c<3;++c) {
        const double keep=1-bleed[c]*bleed[c];
        const auto colorNormal=glm::normalize(geometry*(1-keep)+lightNormal*keep);
        const double channelWrap=(glm::dot(colorNormal,light)+coarseWrap)/(1+coarseWrap);
        const double response=std::pow(std::clamp(std::max(channelWrap,wrap),0.0,1.0),1.3);
        result[c]=response*(coarse[c]*coarse[c]*(1-colorFade)+colorFade)*.85;
    }
    // The fixture captures the production diffuse-only pass. Its host BRDF
    // applies the remaining nonmetallic Fresnel budget and Lambert units to
    // the native angular kernel; these are not part of the recovered kernel.
    double diffuseBudget=1;
    if (glm::dot(normal,light)>0) {
        const auto halfVector=glm::normalize(view+light);
        diffuseBudget-=std::pow(1-std::max(glm::dot(halfVector,view),0.0),5.0);
    }
    return result*glm::dvec3(material.albedo)*(diffuseBudget/Pi);
}

class Fixture {
public:
    explicit Fixture(OpenglBackendContext context) { device_.Run(BackendType::Opengl,std::move(context)); }
    ~Fixture() { Shutdown(); }
    void Shutdown() {
        if (!device_.IsRunning()) return;
        owner_.reset(); device_.StopAndRelease(); device_.returnSystem.DrainCallbacks();
    }
    void Run() {
        Setup();
        CheckNormalizationPrecision();
        CheckLegacy();
        CheckFiniteSkin();
        CheckNativeDiffuse();
        CheckLobeMixture();
        CheckTransmission();
        std::cout << "Skin material GPU test passed: independent legacy BRDF, finite angular response, convex specular lobes and transmission switch\n";
    }
private:
    template<class Predicate> void Wait(Predicate done,const char* label) {
        const auto deadline=std::chrono::steady_clock::now()+10s;
        while (!done()) {
            device_.returnSystem.DrainCallbacks();
            if (done()) break;
            Check(std::chrono::steady_clock::now()<deadline,std::string("Timeout: ")+label);
            glfwPollEvents(); device_.returnSystem.WaitForCallbacks(5ms);
        }
    }
    void Setup() {
        PipelineDetail::ResourceBuilder builder(device_);
        pipeline_=builder.FullscreenPipeline(VertexShader(),Material::PbrFragmentShader());
        diffusePipeline_=builder.FullscreenPipeline(VertexShader(),Material::PbrFragmentShader(Material::Domain::Surface,Material::PbrOutput::DiffuseRadiance));
        auto diagnostic=Material::PbrFragmentShader();
        const auto main=diagnostic.rfind("void main()");
        Check(main!=std::string::npos,"Production PBR main missing");
        diagnostic.resize(main);
        diagnostic+=R"GLSL(
void main() {
    vec3 N=SurfaceNormal(PbrFlowUVs(vTexCoord));
    vec3 V=SafeNormalize(uCameraPosition.xyz-vWorldPosition,N);
    vec3 L=sunDirectionIntensity.w>0.0?-SafeNormalize(sunDirectionIntensity.xyz,vec3(0,-1,0)):
        SafeNormalize(lightPositions[0].xyz-vWorldPosition,N);
    vec3 H=SafeNormalize(V+L,N);
    outColor=vec4(dot(N,H),dot(N,V),dot(N,L),1);
}
)GLSL";
        normalizationPipeline_=builder.FullscreenPipeline(VertexShader(),diagnostic);
        mesh_=builder.FullscreenMesh();
        view_=builder.Uniform(sizeof(ViewConstants));
        material_=builder.Uniform(Material::MakePbrTemplate()->ByteSize());
        lighting_=builder.Uniform(sizeof(Material::PbrPassConstants));
        effects_=builder.Uniform(sizeof(SceneEffectsConstants));
        surface_=builder.Uniform(sizeof(SurfacePassConstants));
        area_=builder.Uniform(sizeof(AreaLightsConstants));
        sky_=builder.Uniform(sizeof(SkyLightConstants));
        probes_=builder.Uniform(sizeof(DiffuseProbeConstants));
        probeVisibility_=builder.Uniform(sizeof(DiffuseProbeVisibilityConstants));
        probeSettings_=builder.Uniform(sizeof(DiffuseProbeRuntimeConstants));
        realtime_=builder.Uniform(sizeof(RealtimeGiConstants));
        color_=builder.Texture(Size,Size,RHITextureFormat::RGBA32F);
        target_=builder.Target(color_,{});
        const auto texture=[&](std::array<float,4> data) {
            CreateRHITextureSpec desc; desc.width=desc.height=1;
            desc.textureDataStoreType=desc.textureUseType=RHITextureFormat::RGBA16F;
            desc.mipmaps=false; desc.filterMode=RHIFilterMode::Nearest;
            desc.addressMode=RHIAddressMode::ClampToEdge; desc.data=data.data();
            return builder.Create<RHITextureSpec>([&](auto cb) { device_.async_CreateTexture(desc,cb); });
        };
        white_=texture({1,1,1,1}); flatNormal_=texture({.5f,.5f,1,1});
        tiltedNormal_=texture({.75f,.5f,1,1}); // Exact binary fractions in RGBA16F.
        owner_=builder.Finish();
    }
    glm::dvec3 Draw(const MaterialCase& settings,glm::vec3 direction,bool point=false,bool normalization=false) {
        Material::ParameterBlock material(Material::MakePbrTemplate());
        const auto set=[&](const char* name,const auto& value) {
            std::string error;
            Check(material.Set(name,value,&error),std::string("Cannot set ")+name+": "+error);
        };
        set("baseColor",glm::vec4(settings.albedo,1)); set("baseColorIsSRGB",false);
        set("metallic",settings.metallic); set("roughness",settings.roughness);
        set("useNormalMap",settings.normalMapped);
        set("skinEnabled",settings.skin); set("skinScatterColor",glm::vec4(1,.45f,.3f,1));
        set("skinScatterStrength",settings.scatter); set("skinWrap",settings.wrap);
        set("skinSpecularMix",settings.specularMix); set("skinRoughness2",settings.roughness2);
        set("skinTransmission",settings.transmission); set("skinDetailStrength",0.0f);
        set("skinDetailTiling",glm::vec4(1,1,0,0)); set("useSkinDetailMap",false);
        set("skinDiffuseNormalBlend",.5f);
        set("useSkinControlMap",settings.native);
        if (settings.native) {
            set("skinSpecularBase1",0.0f); set("skinSpecularBase2",0.0f);
            set("skinSpecularScale1",0.0f); set("skinSpecularScale2",0.0f);
            set("skinCoarseWrapScale",.2f); set("skinCoarseWrapBase",.15f);
            set("skinDiffuseStraight",1.0f); set("skinDiffuseSteep",.3f);
            set("skinDiffuseAngleExponent",.3f); set("skinDiffuseDistribution",1.3f);
            set("skinDiffuseScale",.85f); set("skinCoarseColorInvRange",3.0f);
            set("skinCoarseColor",glm::vec4(248.f/255,165.f/255,81.f/255,1));
            set("skinDetailColorBleed",glm::vec4(206.f/255,109.f/255,173.f/255,1));
        }
        Material::PbrPassConstants lighting;
        lighting.ambientAndExposure=glm::vec4(0); lighting.lightColors={};
        lighting.specularAA.z=0;
        direction=glm::normalize(direction);
        SceneEffectsConstants effects;
        effects.sunColor=glm::vec4(1); effects.sunDirectionIntensity=glm::vec4(-direction,point?0:1);
        if (point) {
            lighting.lightPositions[0]=glm::vec4(direction*10.f,1);
            lighting.lightColors[0]=glm::vec4(100,100,100,0); // Unit incident radiance.
        }
        RHICommand::BeginFrame begin; begin.frameIndex=++frame_;
        begin.framebufferWidth=begin.framebufferHeight=Size;
        auto encoder=device_.BeginFrame(begin);
        const auto block=[&](auto handle,const auto& value,std::uint32_t binding) {
            const auto* bytes=reinterpret_cast<const std::byte*>(&value);
            for (std::uint32_t offset=0;offset<sizeof(value);offset+=RHICommand::MaxInlineUniformBytes)
                Check(encoder.UpdateUniformBufferBytes(handle,bytes+offset,
                    std::min<std::uint32_t>(RHICommand::MaxInlineUniformBytes,sizeof(value)-offset),offset),"Cannot upload skin test UBO");
            Check(encoder.BindUniformBuffer(handle,binding),"Cannot bind skin test UBO");
        };
        try {
            RHICommand::SetRenderTarget target; target.target=target_; target.width=target.height=Size;
            target.clearFlags=RHICommand::ClearColor; target.clearColor=glm::vec4(-1);
            Check(encoder.KeepAlive(owner_)&&encoder.SetRenderTarget(target),"Cannot select skin test target");
            block(view_,ViewConstants{},0); block(lighting_,lighting,3);
            block(effects_,effects,SceneEffectsBinding); block(surface_,SurfacePassConstants{},SurfacePassBinding);
            block(area_,AreaLightsConstants{},AreaLightsBinding); block(sky_,SkyLightConstants{},SkyLightBinding);
            block(probes_,DiffuseProbeConstants{},DiffuseProbeBinding);
            block(probeVisibility_,DiffuseProbeVisibilityConstants{},DiffuseProbeVisibilityBinding);
            block(probeSettings_,DiffuseProbeRuntimeConstants{},DiffuseProbeSettingsBinding);
            block(realtime_,RealtimeGiConstants{},RealtimeGiBinding);
            for (std::uint32_t offset=0;offset<material.Bytes().size();offset+=RHICommand::MaxInlineUniformBytes)
                Check(encoder.UpdateUniformBufferBytes(material_,material.Bytes().data()+offset,
                    std::min<std::uint32_t>(RHICommand::MaxInlineUniformBytes,material.Bytes().size()-offset),offset),
                    "Cannot upload skin material");
            Check(encoder.BindUniformBuffer(material_,Material::MaterialParameterBinding),"Cannot bind skin material");
            for (std::uint32_t slot=0;slot<16;++slot)
                Check(encoder.BindTexture(slot==1?(settings.normalMapped?tiltedNormal_:flatNormal_):white_,slot),"Cannot bind skin test texture");
            Check(encoder.BindPipeline(normalization?normalizationPipeline_:settings.native?diffusePipeline_:pipeline_)&&encoder.BindMesh(mesh_)&&
                  encoder.DrawIndexed(RHICommand::DrawIndexed{3})&&encoder.End(false),"Cannot record skin test draw");
            auto complete=std::make_shared<Completion<bool>>();
            Check(device_.async_SubmitFrameCommands(encoder.GetCommandBuffer(),[complete]{complete->done=true;}),"Cannot submit skin test draw");
            Wait([complete]{return complete->done;},"skin draw");
        } catch (...) { encoder.Cancel(); throw; }
        struct Readback { std::array<float,Size*Size*4> pixels{}; GLenum error=GL_NO_ERROR; bool valid=false; };
        auto result=std::make_shared<Completion<Readback>>();
        device_.async_ExecuteCode([this,result] {
            const auto* texture=device_.GetResourcePool().TextureTable.Get(color_);
            if (texture) {
                glGetTextureImage(texture->rhi_id,0,GL_RGBA,GL_FLOAT,sizeof(result->value.pixels),result->value.pixels.data());
                result->value.valid=true;
            }
            result->value.error=glGetError();
        },[result]{result->done=true;});
        Wait([result]{return result->done;},"skin readback");
        Check(result->value.valid&&result->value.error==GL_NO_ERROR,"Skin readback failed");
        glm::dvec3 mean(0);
        for (std::size_t i=0;i<Size*Size;++i) {
            for (int channel=0;channel<3;++channel) {
                const auto value=result->value.pixels[i*4+channel];
                Check(std::isfinite(value)&&value>=0&&value<60000,"Skin shader produced invalid/clipped HDR radiance");
                mean[channel]+=value;
            }
            Check(result->value.pixels[i*4+3]==1,"Skin patch did not cover its target");
        }
        return mean/double(Size*Size);
    }
    void CheckLegacy() {
        double worst=0;
        MaterialCase material; material.skin=false;
        // Aggressive inactive settings must never change ordinary materials.
        material.scatter=1; material.wrap=.8f; material.specularMix=1;
        material.roughness2=.9f; material.transmission=1;
        for (bool point:{false,true}) for (float roughness:{.08f,.35f,.9f})
            for (float metallic:{0.f,1.f}) for (glm::vec3 direction:{glm::vec3(.15f,0,1),glm::vec3(1,0,.25f),glm::vec3(0,0,-1)}) {
                material.roughness=roughness; material.metallic=metallic;
                const auto expected=LegacyReference(material,glm::dvec3(direction));
                const auto actual=Draw(material,direction,point);
                const double error=RelativeError(actual,expected); worst=std::max(worst,error);
                Check(error<.003,"Skin-disabled rendering differs from independent legacy BRDF: "+std::to_string(error)+
                    "; roughness="+std::to_string(roughness)+"; metallic="+std::to_string(metallic)+"; point="+std::to_string(point));
            }
        std::cout<<"Skin disabled: 36 directional/point BRDF controls; worst relative error="<<worst<<'\n';
    }
    void CheckNormalizationPrecision() {
        MaterialCase material; material.roughness=.08f;
        // Very narrow on-axis GGX is ill-conditioned w.r.t. sub-ULP GPU
        // reciprocal-square-root normalization. Expose it independently; use
        // a slightly off-axis front light for the strict ideal BRDF controls.
        for (bool point:{false,true}) {
            const auto dots=Draw(material,{0,0,1},point,true);
            const auto actual=Draw(material,{0,0,1},point);
            const auto ideal=LegacyReference(material,{0,0,1});
            const auto corrected=LegacyReference(material,{0,0,1},dots.x);
            std::cout.precision(10);
            std::cout<<"Narrow-axis normalization: point="<<point<<", NoH="<<dots.x<<", NoV="<<dots.y<<", NoL="<<dots.z
                <<", ideal-reference error="<<RelativeError(actual,ideal)
                <<", observed-NoH reference error="<<RelativeError(actual,corrected)<<'\n';
            Check(RelativeError(actual,corrected)<.003,"Narrow-axis BRDF mismatch is not explained by measured GPU normalization");
        }
    }
    void CheckFiniteSkin() {
        MaterialCase material; material.skin=true; material.transmission=.3f;
        for (bool point:{false,true}) for (float roughness:{.045f,.35f,1.f})
            for (glm::vec3 direction:{glm::vec3(0,0,1),glm::vec3(1,0,.01f),glm::vec3(1,0,-.1f),glm::vec3(0,0,-1)}) {
                material.roughness=roughness; Draw(material,direction,point);
            }
        std::cout<<"Skin enabled: front/side/back, point/directional, minimum/medium/maximum roughness are finite and nonnegative\n";
    }
    void CheckLobeMixture() {
        MaterialCase material; material.skin=true; material.albedo=glm::vec3(0);
        material.scatter=0; material.wrap=0; material.transmission=0;
        material.roughness=material.roughness2=.35f; material.specularMix=0;
        const auto first=Draw(material,{0,0,1});
        Check(Maximum(first)>1e-4,"Specular energy control is black");
        for (float mix:{.5f,1.f}) {
            material.specularMix=mix;
            Check(RelativeError(Draw(material,{0,0,1}),first)<.003,"Equal skin lobes gained energy when mixed");
        }
        material.roughness2=.65f; material.specularMix=0;
        const auto narrow=Draw(material,{0,0,1}); material.specularMix=1;
        const auto broad=Draw(material,{0,0,1}); material.specularMix=.5f;
        const auto blended=Draw(material,{0,0,1});
        Check(RelativeError(narrow,broad)>.1,"Secondary roughness does not change the skin highlight");
        Check(RelativeError(blended,.5*(narrow+broad))<.003,"Skin specular lobes do not preserve convex energy weights");
        std::cout<<"Dual lobe: equal lobes preserve energy; different lobes use a bounded convex combination\n";
    }
    void CheckNativeDiffuse() {
        MaterialCase material; material.skin=material.native=true; material.scatter=1;
        double worst=0;
        for (bool point:{false,true}) for (bool mapped:{false,true})
            for (glm::vec3 direction:{glm::vec3(0,0,1),glm::vec3(1,0,0),glm::vec3(std::sqrt(1-.15f*.15f),0,-.15f)}) {
                material.normalMapped=mapped;
                const auto expected=NativeDiffuseReference(material,glm::dvec3(direction));
                const auto actual=Draw(material,direction,point);
                const double error=RelativeError(actual,expected); worst=std::max(worst,error);
                Check(error<.003,"Native RGB diffuse differs from independently recovered kernel: "+std::to_string(error));
                Check(Maximum(actual)>1e-5,"Native RGB wrap lost its front/terminator/back response");
            }
        std::cout<<"Native RGB diffuse: 12 point/directional controls, flat/tilted normal, front/90deg/dot=-.15; worst relative error="<<worst<<'\n';
    }
    void CheckTransmission() {
        MaterialCase material; material.skin=true; material.transmission=0;
        for (bool point:{false,true}) {
            const auto blocked=Draw(material,{0,0,-1},point);
            Check(Maximum(blocked)<1e-7,"Opaque skin with disabled transmission leaks full backlight");
            material.transmission=.4f;
            const auto transmitted=Draw(material,{0,0,-1},point);
            Check(Maximum(transmitted)>1e-5,"Enabled skin transmission produces no backlighting");
            material.transmission=0;
        }
        std::cout<<"Transmission: zero full-backlight response when off, finite positive response when enabled\n";
    }
    RHIDevice device_;
    std::shared_ptr<const void> owner_;
    RenderResourceHandle<PipelineSpec> pipeline_,diffusePipeline_,normalizationPipeline_;
    RenderResourceHandle<VertexBufferSpec> mesh_;
    RenderResourceHandle<UniformBufferSpec> view_,material_,lighting_,effects_,surface_,area_,sky_;
    RenderResourceHandle<UniformBufferSpec> probes_,probeVisibility_,probeSettings_,realtime_;
    RenderResourceHandle<RHITextureSpec> color_,white_,flatNormal_,tiltedNormal_;
    RenderResourceHandle<RenderTargetSpec> target_;
    std::uint64_t frame_=0;
};
} // namespace

int main(int argc,char** argv) {
    if (argc==2&&std::string(argv[1])=="--schema-size") {
        const auto schema=Material::MakePbrTemplate();
        if (!schema) return 1;
        std::cout<<schema->ByteSize()<<'\n'; return 0;
    }
    int result=0;
    try {
        Check(glfwInit()==GLFW_TRUE,"glfwInit failed");
        glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
        ApplicationWindow::Window window({Size,Size,"Skin material GPU test",false});
        Check(window.Activate(),"Cannot activate OpenGL 4.5 context");
        auto context=window.GetRenderContextAsOpengl(); Check(context.has_value(),"No OpenGL context");
        Fixture fixture(std::move(*context)); fixture.Run(); fixture.Shutdown();
    } catch (const std::exception& error) {
        std::cerr<<"Skin material GPU test failed: "<<error.what()<<'\n'; result=1;
    }
    glfwTerminate(); return result;
}
