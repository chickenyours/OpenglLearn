#include "Render/Private/Pipeline/eye_shell_render_pass.h"
#include "Render/Private/Pipeline/eye_shell_shaders.h"
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Private/Pipeline/post_process_shaders.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <stdexcept>
#include <glm/gtc/matrix_inverse.hpp>

namespace Render::PipelineDetail {
namespace {
void Need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class T>T Parameter(const Material::MaterialSnapshot& material,const char* name,T fallback){
    const auto& p=material.Parameters();const auto* layout=p.GetTemplate()->FindLayout(name);
    if(layout&&layout->size>=sizeof(T)&&layout->offset+sizeof(T)<=p.Bytes().size())std::memcpy(&fallback,p.Bytes().data()+layout->offset,sizeof(T));
    return fallback;
}
void Upload(RHIFrameEncoder& e,RenderResourceHandle<UniformBufferSpec> b,const void* data,std::uint32_t size,std::uint32_t binding){
    for(std::uint32_t offset=0;offset<size;offset+=RHICommand::MaxInlineUniformBytes)
        Need(e.UpdateUniformBufferBytes(b,static_cast<const std::byte*>(data)+offset,std::min<std::uint32_t>(RHICommand::MaxInlineUniformBytes,size-offset),offset),"Cannot upload eye-shell constants");
    Need(e.BindUniformBuffer(b,binding),"Cannot bind eye-shell constants");
}
struct alignas(16) ShellView {glm::mat4 viewProjection{1};glm::vec4 camera{0};};
constexpr std::array<const char*,13> Groups{"eyeShellSurface","eyeShellOutlineArea","eyeShellOutlineSmooth","eyeShellFillArea","eyeShellFillSmooth","eyeShellBlurOutlineArea","eyeShellBlurOutlineSmooth","eyeShellBlurFillArea","eyeShellBlurFillSmooth","eyeShellSpecular","eyeShellFake","eyeShellCavity","eyeShellDebug"};
struct alignas(16) ShellMaterial {std::array<glm::vec4,13> groups;};
struct alignas(16) ShellPass {
    glm::mat4 inverseProjection{1},view{1},projection{1};glm::vec4 viewport{0},sunDirectionIntensity{0},sunColor{0};
    std::array<glm::vec4,4> lightPositions{},lightColors{};
};
static_assert(sizeof(ShellMaterial)==208&&sizeof(ShellPass)==368);
}
bool IsEyeShellMaterial(const Material::MaterialSnapshot& material){return Parameter<std::uint32_t>(material,"eyeShellEnabled",0)!=0;}
struct EyeShellRenderPass::Impl {
    RHIDevice& device;std::shared_ptr<const void> programsOwner,targetsOwner;
    RenderResourceHandle<PipelineSpec> copy;std::array<RenderResourceHandle<PipelineSpec>,12> shell;
    RenderResourceHandle<VertexBufferSpec> fullscreen;
    RenderResourceHandle<UniformBufferSpec> viewBuffer,objectBuffer,materialBuffer,passBuffer;
    RenderResourceHandle<RHITextureSpec> snapshot,flatNormal,black;
    RenderResourceHandle<RenderTargetSpec> snapshotTarget,outputTarget;
    RenderResourceHandle<RHITextureSpec> outputColor;
    std::uint32_t width=0,height=0;
    std::map<std::pair<std::uint32_t,std::uint32_t>,std::uint32_t> variants;
    explicit Impl(RHIDevice& d):device(d){}
    void Initialize(){
        if(programsOwner)return;ResourceBuilder b(device);
        copy=b.FullscreenPipeline(FullscreenVertexShader(),EyeShellCopyShader());fullscreen=b.FullscreenMesh();
        CreateGraphicShaderDesc shader;shader.vertexShaderSource=b.Source(ShaderSourceType::Vertex,EyeShellVertexShader());
        shader.fragmentShaderSource=b.Source(ShaderSourceType::Fragment,EyeShellFragmentShader());
        auto program=b.Create<ShaderProgramSpec>([&](auto cb){device.async_CreateGraphicShader(shader,cb);});
        for(unsigned second=0;second<2;++second)for(unsigned cull=0;cull<3;++cull)for(unsigned front=0;front<2;++front){
            PipelineSpec spec;spec.shaderProgram=program;spec.expectVertexLayout=Material::PbrVertexLayout(second!=0);
            spec.depthTest=spec.depthWrite=spec.blendEnable=false;spec.cullMode=static_cast<CullMode>(cull);spec.frontFaceCounterClockwise=front!=0;
            shell[second*6+cull*2+front]=b.Create<PipelineSpec>([&](auto cb){device.async_CreatePipeline({spec},cb);});
        }
        viewBuffer=b.Uniform(sizeof(ShellView));objectBuffer=b.Uniform(sizeof(glm::mat4));materialBuffer=b.Uniform(sizeof(ShellMaterial));passBuffer=b.Uniform(sizeof(ShellPass));
        const auto solid=[&](std::array<float,4> pixels){CreateRHITextureSpec s;s.width=s.height=1;s.mipmaps=false;s.data=pixels.data();
            s.textureDataStoreType=s.textureUseType=RHITextureFormat::RGBA32F;s.addressMode=RHIAddressMode::ClampToEdge;
            return b.Create<RHITextureSpec>([&](auto cb){device.async_CreateTexture(s,cb);});};
        flatNormal=solid({.5f,.5f,1,1});black=solid({0,0,0,1});programsOwner=b.Finish();
    }
    std::uint32_t RasterVariant(RenderResourceHandle<PipelineSpec> pipeline){
        auto key=std::make_pair(pipeline.id,pipeline.version);auto found=variants.find(key);if(found!=variants.end())return found->second;
        struct Result {VertexLayout layout;CullMode cull=CullMode::Back;bool frontCCW=true,done=false,valid=false;};auto result=std::make_shared<Result>();auto* d=&device;
        device.async_ExecuteCode([d,pipeline,result]{if(const auto* spec=d->GetResourcePool().PipelineTable.Get(pipeline)){result->layout=spec->expectVertexLayout;result->cull=spec->cullMode;result->frontCCW=spec->frontFaceCounterClockwise;result->valid=true;}},[result]{result->done=true;});
        using namespace std::chrono_literals;const auto end=std::chrono::steady_clock::now()+10s;
        while(!result->done){device.returnSystem.DrainCallbacks();if(result->done)break;Need(std::chrono::steady_clock::now()<end,"Eye-shell layout query timed out");device.returnSystem.WaitForCallbacks(2ms);}
        Need(result->valid,"Eye-shell source pipeline missing");bool second=result->layout.typeSlots==Material::PbrVertexLayout(true).typeSlots;
        Need(second||result->layout.typeSlots==Material::PbrVertexLayout(false).typeSlots,"Eye-shell requires the PBR48/56 vertex layout");
        const auto cull=static_cast<std::uint32_t>(result->cull);Need(cull<3,"Invalid eye-shell source cull mode");
        const std::uint32_t variant=(second?6u:0u)+cull*2+(result->frontCCW?1u:0u);
        variants.emplace(key,variant);return variant;
    }
    void Targets(const EyeShellPassTarget& target){
        if(targetsOwner&&width==target.width&&height==target.height&&outputColor==target.color)return;
        ResourceBuilder b(device);snapshot=b.Texture(target.width,target.height,RHITextureFormat::RGBA16F);
        snapshotTarget=b.Target(snapshot,{});outputTarget=b.Target(target.color,{});targetsOwner=b.Finish();
        width=target.width;height=target.height;outputColor=target.color;
    }
};
EyeShellRenderPass::EyeShellRenderPass(RHIDevice& device):impl_(std::make_unique<Impl>(device)){}
EyeShellRenderPass::~EyeShellRenderPass(){Shutdown();}
EyeShellPassResult EyeShellRenderPass::Record(RHIFrameEncoder& encoder,std::span<const RenderItem> items,
    const EyeShellPassTarget& target,const EyeShellPassInputs& inputs,const EyeShellSettings& settings){
    EyeShellPassResult result;if(!settings.enabled)return result;
    if(std::none_of(items.begin(),items.end(),[](const auto& item){return item.material&&IsEyeShellMaterial(*item.material);}))return result;
    Need(target.width&&target.height&&target.color.IsValid()&&inputs.color.IsValid()&&inputs.depth.IsValid(),"Invalid eye-shell targets");
    Need(std::isfinite(inputs.nearPlane)&&inputs.nearPlane>0&&std::abs(glm::determinant(inputs.projection))>1e-10f,"Invalid eye-shell camera");
    auto& p=*impl_;p.Initialize();p.Targets(target);
    Need(encoder.KeepAlive(p.programsOwner)&&encoder.KeepAlive(p.targetsOwner),"Cannot retain eye-shell resources");
    RHICommand::SetRenderTarget rt;rt.target=p.snapshotTarget;rt.width=target.width;rt.height=target.height;
    Need(encoder.SetRenderTarget(rt)&&encoder.BindPipeline(p.copy)&&encoder.BindMesh(p.fullscreen)&&encoder.BindTexture(inputs.color,0)&&encoder.DrawIndexed({3}),"Cannot snapshot eye-shell HDR");
    rt.target=p.outputTarget;Need(encoder.SetRenderTarget(rt),"Cannot select eye-shell destination");
    ShellView view{inputs.projection*inputs.view,glm::vec4(inputs.cameraPosition,1)};Upload(encoder,p.viewBuffer,&view,sizeof(view),0);
    ShellPass pass;pass.inverseProjection=glm::inverse(inputs.projection);pass.view=inputs.view;pass.projection=inputs.projection;
    pass.viewport={1.f/target.width,1.f/target.height,inputs.nearPlane,1};
    pass.sunDirectionIntensity=glm::vec4(inputs.sunDirection,inputs.sunIntensity);pass.sunColor=glm::vec4(inputs.sunColor,0);
    pass.lightPositions=inputs.lighting.lightPositions;pass.lightColors=inputs.lighting.lightColors;
    for(const auto& item:items){
        if(!item.material||!IsEyeShellMaterial(*item.material))continue;
        const auto& resources=item.material->Resources();const auto variant=p.RasterVariant(resources.pipeline);
        ShellMaterial material;
        for(std::size_t i=0;i<Groups.size();++i){material.groups[i]=Parameter<glm::vec4>(*item.material,Groups[i],glm::vec4(0));
            for(int c=0;c<4;++c)Need(std::isfinite(material.groups[i][c]),"Nonfinite eye-shell material parameter");}
        float determinant=glm::determinant(glm::mat3(item.model));Need(std::isfinite(determinant),"Invalid eye-shell transform");
        pass.viewport.w=std::max(std::cbrt(std::abs(determinant)),1e-5f);
        Upload(encoder,p.passBuffer,&pass,sizeof(pass),3);Upload(encoder,p.materialBuffer,&material,sizeof(material),2);Upload(encoder,p.objectBuffer,&item.model,sizeof(item.model),1);
        auto normal=p.flatNormal,fake=p.black;
        for(const auto& texture:resources.textures){if(!texture.texture.IsValid())continue;if(texture.slot==1)normal=texture.texture;if(texture.slot==7)fake=texture.texture;}
        Need(encoder.KeepAlive(item.material)&&encoder.BindPipeline(p.shell[variant])&&encoder.BindMesh(item.mesh)&&
            encoder.BindTexture(p.snapshot,0)&&encoder.BindTexture(inputs.depth,1)&&encoder.BindTexture(normal,2)&&encoder.BindTexture(fake,3)&&encoder.DrawIndexed(item.draw),"Cannot draw eye-shell postprocess");
        ++result.draws;
    }
    result.passes=result.draws?2:0;return result;
}
void EyeShellRenderPass::Shutdown(){if(!impl_)return;impl_->targetsOwner.reset();impl_->programsOwner.reset();impl_->variants.clear();impl_->width=impl_->height=0;impl_->outputColor={};impl_->snapshot={};}
} // namespace Render::PipelineDetail
