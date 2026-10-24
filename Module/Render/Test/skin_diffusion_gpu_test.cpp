#include "ApplicationWindow/public/window.h"
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Private/Pipeline/skin_diffusion_shaders.h"
#include "Render/Private/Pipeline/post_process_shaders.h"
#include "Render/Public/Pipeline/material_render_pipeline.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <future>
#include <iostream>
#include <stdexcept>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>
#if defined(_WIN32)
extern "C" { __declspec(dllexport) unsigned long NvOptimusEnablement=1; }
#endif
using namespace Render;
namespace {
constexpr unsigned Size=96;
void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
template<class F> auto OnThread(RHIDevice& device,F operation) {
    using T=std::invoke_result_t<F>;auto done=std::make_shared<std::promise<T>>();auto future=done->get_future();
    device.async_ExecuteCode([done,operation]{try{if constexpr(std::is_void_v<T>){operation();done->set_value();}else done->set_value(operation());}catch(...){done->set_exception(std::current_exception());}});
    return future.get();
}
void Submit(RHIDevice& device,RHIFrameEncoder& encoder){
    Check(encoder.End(false),"End SSS frame");auto done=std::make_shared<bool>(false);
    Check(device.async_SubmitFrameCommands(encoder.GetCommandBuffer(),[done]{*done=true;}),"Submit SSS frame");
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while(!*done){device.returnSystem.DrainCallbacks();Check(std::chrono::steady_clock::now()<deadline,"SSS frame timeout");if(!*done)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));}
}
void Upload(RHIFrameEncoder& e,RenderResourceHandle<UniformBufferSpec> b,const void* data,unsigned size,unsigned binding){
    for(unsigned offset=0;offset<size;offset+=256)Check(e.UpdateUniformBufferBytes(b,static_cast<const std::byte*>(data)+offset,std::min(256u,size-offset),offset),"Upload SSS test data");
    Check(e.BindUniformBuffer(b,binding),"Bind SSS test data");
}
float Pixel(const std::vector<float>& a,unsigned x,unsigned y,unsigned c){return a[(y*Size+x)*4+c];}
struct KernelFixture {
    RHIDevice& device;PipelineDetail::ResourceBuilder builder;
    RenderResourceHandle<PipelineSpec> blur,burley,burleyMath;RenderResourceHandle<VertexBufferSpec> mesh;
    RenderResourceHandle<UniformBufferSpec> buffer;
    RenderResourceHandle<RHITextureSpec> work[2];RenderResourceHandle<RenderTargetSpec> target[2];
    std::shared_ptr<const void> owner;
    explicit KernelFixture(RHIDevice& d):device(d),builder(d){
        blur=builder.FullscreenPipeline(FullscreenVertexShader(),PipelineDetail::SkinDiffusionBlurShader());
        burley=builder.FullscreenPipeline(FullscreenVertexShader(),PipelineDetail::SkinDiffusionBurleyShader());
        auto diagnostic=PipelineDetail::SkinDiffusionBurleyShader();diagnostic.resize(diagnostic.find("void main() {"));
        diagnostic+="void main(){float u=clamp(vUV.x,.001,.999);outColor=vec4(sourceBurleySample(u,1.7),u,1.0);}";
        burleyMath=builder.FullscreenPipeline(FullscreenVertexShader(),diagnostic);
        mesh=builder.FullscreenMesh();buffer=builder.Uniform(sizeof(PipelineDetail::SkinDiffusionConstants));
        for(int i=0;i<2;++i){work[i]=builder.Texture(Size,Size,RHITextureFormat::RGBA32F);target[i]=builder.Target(work[i],{});}
        owner=builder.Finish();
    }
    std::vector<float> Run(std::vector<float> source,std::vector<float> profile,float distance=.5f,float radius=.02f,bool depthEdge=false,const std::vector<float>* albedo=nullptr,float preservation=.5f,bool sourceBurley=false){
        PipelineDetail::ResourceBuilder input(device);
        auto constants=PipelineDetail::SkinDiffusionConstants{};
        constants.projection=glm::perspectiveRH_NO(glm::radians(60.f),1.f,.05f,10.f);constants.inverseProjection=glm::inverse(constants.projection);
        constants.radius={radius,1,.65f,1.5f};constants.channels={1,.5f,.25f,64};constants.texturing.x=preservation;constants.radial={64,1,0,0};
        glm::vec4 projected=constants.projection*glm::vec4(0,0,-distance,1);
        std::vector<float> depths(Size*Size*4,projected.z/projected.w*.5f+.5f);
        if(depthEdge){glm::vec4 back=constants.projection*glm::vec4(0,0,-distance-.2f,1);
            for(unsigned y=0;y<Size;++y)for(unsigned x=Size/2;x<Size;++x)depths[(y*Size+x)*4]=back.z/back.w*.5f+.5f;}
        const auto texture=[&](const std::vector<float>& pixels){CreateRHITextureSpec desc;desc.width=desc.height=Size;
            desc.textureDataStoreType=desc.textureUseType=RHITextureFormat::RGBA32F;desc.data=pixels.data();desc.mipmaps=false;
            desc.filterMode=RHIFilterMode::Nearest;desc.addressMode=RHIAddressMode::ClampToEdge;
            return input.Create<RHITextureSpec>([&](auto cb){device.async_CreateTexture(desc,cb);});};
        std::vector<float> white(source.size(),1);
        auto src=texture(source),meta=texture(profile),depth=texture(depths),color=texture(albedo?*albedo:white);auto inputs=input.Finish();
        auto encoder=device.BeginFrame({0,Size,Size});Check(encoder.KeepAlive(owner)&&encoder.KeepAlive(inputs),"Pin kernel fixture");
        for(int pass=0;pass<2;++pass){Check(encoder.SetRenderTarget({target[pass],Size,Size}),"SSS kernel target");
            constants.viewport={Size,Size,pass?0.f:1.f,pass?1.f:0.f};constants.texturing.y=pass?0.f:1.f;Upload(encoder,buffer,&constants,sizeof(constants),5);
            Check(encoder.BindPipeline(sourceBurley?burley:blur)&&encoder.BindMesh(mesh)&&encoder.BindTexture(pass?work[0]:src,0)&&encoder.BindTexture(meta,1)&&encoder.BindTexture(depth,2)&&encoder.BindTexture(color,3)&&encoder.DrawIndexed({3}),"SSS production kernel");}
        Submit(device,encoder);return OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(work[1]);});
    }
    void Test(){
        // Independent double-precision CDF/PDF identity for the exact recovered
        // source cubic inverse. This catches a wrong exp base or radial Jacobian.
        auto diagnostic=device.BeginFrame({0,Size,Size});Check(diagnostic.KeepAlive(owner),"Pin source kernel math");
        Check(diagnostic.SetRenderTarget({target[0],Size,Size})&&diagnostic.BindPipeline(burleyMath)&&diagnostic.BindMesh(mesh)&&diagnostic.DrawIndexed({3}),"Source radial math pass");
        Submit(device,diagnostic);const auto samples=OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(work[0]);});
        for(unsigned x=0;x<Size;++x){double r=Pixel(samples,x,Size/2,0),inversePdf=Pixel(samples,x,Size/2,1),u=Pixel(samples,x,Size/2,2),d=1.7;
            double a=std::exp(-r/d),b=std::exp(-r/(3*d));double cdf=1-.25*a-.75*b;
            double reference=8*3.14159265358979323846*d/(a+b);
            Check(std::abs(cdf-u)<2e-5&&std::abs(inversePdf-reference)/reference<3e-5,"Recovered radial inverse-CDF/PDF does not match independent distribution");}
        std::vector<float> source(Size*Size*4,0),profile(source.size());
        for(unsigned y=0;y<Size;++y)for(unsigned x=0;x<Size;++x){auto p=(y*Size+x)*4;
            profile[p]=profile[p+1]=.5f;profile[p+2]=profile[p+3]=1;
            for(int c=0;c<3;++c)source[p+c]=.375f;source[p+3]=1;}
        const auto constant=Run(source,profile);
        for(unsigned i=0;i<source.size();++i)Check(std::isfinite(constant[i])&&std::abs(constant[i]-source[i])<1e-5f,"SSS does not preserve constant radiance");
        const auto radialConstant=Run(source,profile,.5f,.02f,false,nullptr,.5f,true);
        for(unsigned i=0;i<source.size();++i)Check(std::isfinite(radialConstant[i])&&std::abs(radialConstant[i]-source[i])<1e-5f,"Source radial kernel does not preserve constant radiance");
        for(unsigned y=0;y<Size;++y)for(unsigned x=0;x<Size;++x)for(int c=0;c<3;++c)source[(y*Size+x)*4+c]=x==Size/2?1.f:0.f;
        const auto nearResult=Run(source,profile),farResult=Run(source,profile,1.f);
        const auto radialNear=Run(source,profile,.5f,.02f,false,nullptr,.5f,true);
        const auto radialFar=Run(source,profile,1.f,.02f,false,nullptr,.5f,true);
        float radialDifference=0;
        for(unsigned i=0;i<source.size();++i)radialDifference=std::max(radialDifference,std::abs(radialNear[i]-radialFar[i]));
        Check(Pixel(radialNear,Size/2,Size/2,0)<.95f&&radialDifference>.005f,"Source radial gather has no spatial/perspective effect");
        auto variance=[&](const std::vector<float>& values,int channel){double sum=0,moment=0;
            for(unsigned x=0;x<Size;++x){double w=Pixel(values,x,Size/2,channel);sum+=w;moment+=w*std::pow(double(x)-Size/2,2);}
            Check(std::abs(sum-1)<.08,"SSS plane transport loses energy");return moment/sum;};
        double red=variance(nearResult,0),green=variance(nearResult,1),blue=variance(nearResult,2),distant=variance(farResult,0);
        Check(red>green*2&&green>blue*1.8,"SSS RGB transport radii do not separate");
        Check(red>distant*2.8&&red<distant*5.2,"SSS radius is not perspective/world-scale projected");
        // Put an emitter immediately left of a material/mask/normal/depth edge.
        // Identical pixel proximity must not leak across these boundaries.
        for(unsigned y=0;y<Size;++y)for(unsigned x=0;x<Size;++x)for(int c=0;c<3;++c)source[(y*Size+x)*4+c]=x<Size/2?1.f:0.f;
        const auto boundary=[&](int mode){auto guarded=profile;
            for(unsigned y=0;y<Size;++y)for(unsigned x=Size/2;x<Size;++x){auto p=(y*Size+x)*4;
                if(mode==0)guarded[p+3]=2; // separate material
                if(mode==1)guarded[p+2]=0; // control-R zero
                if(mode==2){guarded[p]=1;guarded[p+1]=.5f;} // orthogonal normal
            }
            const auto out=Run(source,guarded);
            for(unsigned y=3;y<Size-3;++y)Check(Pixel(out,Size/2,y,0)<1e-6,"SSS crosses material/mask/normal boundary");
        };
        boundary(0);boundary(1);boundary(2);
        auto radialGuard=profile;
        for(unsigned y=0;y<Size;++y)for(unsigned x=Size/2;x<Size;++x)radialGuard[(y*Size+x)*4+3]=2;
        const auto radialBoundary=Run(source,radialGuard,.5f,.02f,false,nullptr,.5f,true);
        for(unsigned y=3;y<Size-3;++y)Check(Pixel(radialBoundary,Size/2,y,0)<1e-6,"Source radial gather crosses material boundary");
        const auto separated=Run(source,profile,.5f,.02f,true);
        for(unsigned y=3;y<Size-3;++y)Check(Pixel(separated,Size/2,y,0)<1e-6,"SSS crosses depth discontinuity");
        std::vector<float> painted(source.size(),1),uniformLight(source.size(),1);
        for(unsigned y=0;y<Size;++y)for(unsigned x=0;x<Size;++x)for(int c=0;c<3;++c)
            painted[(y*Size+x)*4+c]=((x/2+y/2)%2)?(c==0?.03f:.2f):(c==2?.9f:.6f);
        const auto lightOnly=Run(uniformLight,profile,.5f,.02f,false,&painted,1);
        for(unsigned i=0;i<lightOnly.size();++i)Check(std::abs(lightOnly[i]-1)<1e-5,"Post-scatter albedo contaminates light diffusion");
        const auto off=Run(source,profile,.5f,0);
        for(unsigned i=0;i<source.size();++i)Check(std::abs(off[i]-source[i])<1e-6,"Zero SSS radius changes radiance");
        std::cout<<"SSS kernel: finite constant energy, RGB widths, world/perspective scale, material/mask/normal/depth boundaries, zero-radius identity passed\n";
    }
};
void PipelineTest(RHIDevice& device){
    PipelineDetail::ResourceBuilder builder(device);auto declaration=Material::MakePbrTemplate();Check(bool(declaration),"PBR schema after eye extension");
    Material::MaterialPassResources resources;resources.expectedTemplate=declaration;
    resources.parameterBuffer=builder.Uniform(declaration->ByteSize());resources.parameterBufferBytes=declaration->ByteSize();
    const auto makePipeline=[&](std::string fragment){CreateGraphicShaderDesc d;d.vertexShaderSource=builder.Source(ShaderSourceType::Vertex,Material::PbrVertexShader());
        d.fragmentShaderSource=builder.Source(ShaderSourceType::Fragment,fragment);
        auto p=builder.Create<ShaderProgramSpec>([&](auto cb){device.async_CreateGraphicShader(d,cb);});
        auto spec=Material::PbrPipelineSpec(p);return builder.Create<PipelineSpec>([&](auto cb){device.async_CreatePipeline({spec},cb);});};
    resources.pipeline=makePipeline(Material::PbrFragmentShader());resources.normalPipeline=makePipeline(Material::PbrFragmentShader(Material::PbrOutput::WorldNormal));
    resources.diffuseRadiancePipeline=makePipeline(Material::PbrFragmentShader(Material::PbrOutput::DiffuseRadiance));
    // Black/white albedo edge produces spatially varying diffuse under a
    // spatially uniform sun; the specular response is independent of albedo.
    std::array<float,8> pixels{0,0,0,1,1,1,1,1};CreateRHITextureSpec texture;texture.width=2;texture.height=1;texture.mipmaps=false;
    texture.textureDataStoreType=texture.textureUseType=RHITextureFormat::RGBA32F;texture.filterMode=RHIFilterMode::Nearest;texture.data=pixels.data();
    auto albedo=builder.Create<RHITextureSpec>([&](auto cb){device.async_CreateTexture(texture,cb);});
    RenderResourceHandle<RHITextureSpec> white;
    std::array<float,4> solid{1,1,1,1};texture.width=1;texture.data=solid.data();white=builder.Create<RHITextureSpec>([&](auto cb){device.async_CreateTexture(texture,cb);});
    for(unsigned i=0;i<8;++i)resources.textures.push_back({i,i==0?albedo:white});
    const std::array<float,48> vertices{-1,-1,0,0,0,1,0,0,1,0,0,1, 1,-1,0,0,0,1,1,0,1,0,0,1,
        1,1,0,0,0,1,1,1,1,0,0,1, -1,1,0,0,0,1,0,1,1,0,0,1};
    const std::array<unsigned,6> indices{0,1,2,0,2,3};CreateMeshBufferDesc mesh;mesh.vertexLayout=Material::PbrVertexLayout();mesh.vertexCount=4;mesh.vertexData=vertices.data();mesh.vertexByteSize=sizeof(vertices);
    mesh.indexCount=6;mesh.indexData=indices.data();mesh.indexByteSize=sizeof(indices);
    auto geometry=builder.Create<VertexBufferSpec>([&](auto cb){device.async_CreateMeshBuffer(mesh,cb);});auto owner=builder.Finish();resources.lifetime=owner;
    auto asset=Material::MaterialAsset::Create(declaration);Material::MaterialInstance instance(asset);
    const auto set=[&](const char* name,const auto& value){Check(instance.Set(name,value),std::string("Set ")+name);};
    set("baseColorIsSRGB",false);set("skinEnabled",true);set("skinScatterStrength",0.f);set("skinSpecularMix",0.f);
    set("reSkinEnabled",true);set("skinDiffusionEnabled",true);set("roughness",.25f);
    auto skin=Material::MaterialSnapshot::Create(instance,resources);Check(bool(skin),"Skin snapshot");
    RenderItem item;item.mesh=geometry;item.draw.indexCount=6;item.material=skin;item.model=glm::scale(glm::mat4(1),glm::vec3(.2f));
    RenderFrame frame;frame.items={item};PipelineCamera camera;camera.position={0,0,.5f};camera.nearPlane=.05f;camera.farPlane=10;
    camera.view=glm::lookAtRH(camera.position,glm::vec3(0),glm::vec3(0,1,0));camera.projection=glm::perspectiveRH_NO(glm::radians(60.f),1.f,.05f,10.f);
    MaterialPipelineSettings settings;settings.shadows.enabled=false;settings.shadows.atlasResolution=128;settings.shadows.sunDirection={0,0,-1};settings.shadows.sunIntensity=1;
    settings.reflection.enabled=false;settings.post.bloomEnabled=settings.post.ssaoEnabled=false;settings.sky.enabled=false;
    settings.indirect.enabled=settings.probes.enabled=settings.realtimeGi.enabled=settings.lumenGi.enabled=settings.surfaceCacheGi.enabled=false;
    settings.temporal.antialiasing=AntialiasingMode::None;settings.lighting.ambientAndExposure={0,0,0,1};settings.lighting.lightColors.fill(glm::vec4(0));
    settings.skinDiffusion.radiusMillimeters=20;
    MaterialRenderPipeline pipeline(device);Check(pipeline.Initialize(),pipeline.LastError());Check(pipeline.Resize(Size,Size,128),pipeline.LastError());
    auto draw=[&]{auto e=device.BeginFrame({1,Size,Size});Check(e.KeepAlive(owner),"Pin PBR fixture");Check(pipeline.Record(e,frame,camera,settings),pipeline.LastError());auto token=pipeline.RecordedFrameToken();Submit(device,e);Check(pipeline.CompleteFrame(token),pipeline.LastError());
        return OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(pipeline.DebugTextures().sceneColor);});};
    const auto before=draw();Check(pipeline.Statistics().skinDiffusionPasses==0,"Disabled SSS records work");
    settings.skinDiffusion.enabled=true;const auto after=draw();Check(pipeline.Statistics().skinDiffusionPasses==6&&pipeline.Statistics().skinDiffusionDraws==3,"SSS production passes absent");
    auto textures=pipeline.DebugTextures();auto raw=OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(textures.skinDiffuse);});
    auto blurred=OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(textures.skinDiffused);});
    auto localAlbedo=OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(textures.skinAlbedo);});
    auto mask=OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(textures.skinProfile);});
    double changed=0;float residual=0;
    for(unsigned i=0;i<Size*Size;++i)for(int c=0;c<3;++c){float a=localAlbedo[i*4+c];float exitColor=std::pow(a,settings.skinDiffusion.albedoPreservation);
        float delta=(blurred[i*4+c]*exitColor-raw[i*4+c]*a)*mask[i*4+2];
        changed+=std::abs(after[i*4+c]-before[i*4+c]);residual=std::max(residual,std::abs((after[i*4+c]-before[i*4+c])-delta));}
    Check(changed>1,"Skin diffusion capture/composition had no spatial effect");Check(residual<.01f,"SSS modifies radiance beyond diffuse delta (specular changed)");
    settings.skinDiffusion.kernel=SkinDiffusionKernel::SourceBurley;settings.skinDiffusion.diskSamples=32;
    settings.skinDiffusion.albedoPreservation=1;
    const auto preserved=draw();float preservationError=0;
    for(unsigned i=0;i<preserved.size();++i)preservationError=std::max(preservationError,std::abs(preserved[i]-before[i]));
    Check(preservationError<.003f,"SSS erases painted albedo under uniform lighting");
    // A second opaque card is only ~3 depth ULP in front of the skin, well
    // inside the old 5e-7 epsilon. It must never acquire the skin's mask/delta.
    set("skinEnabled",false);set("skinDiffusionEnabled",false);set("baseColor",glm::vec4(.1f,.3f,.7f,1));
    auto occluder=item;occluder.material=Material::MaterialSnapshot::Create(instance,resources);
    occluder.model=glm::translate(glm::mat4(1),glm::vec3(.015f,0,1e-6f))*glm::scale(glm::mat4(1),glm::vec3(.035f,.12f,.2f));
    frame.items.push_back(occluder);settings.skinDiffusion.albedoPreservation=0;
    settings.skinDiffusion.enabled=false;const auto occludedBefore=draw();
    settings.skinDiffusion.enabled=true;const auto occludedAfter=draw();
    const auto nearMask=OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(pipeline.DebugTextures().skinProfile);});
    for(unsigned y=42;y<54;++y)for(unsigned x=48;x<53;++x){Check(Pixel(nearMask,x,y,2)==0,"Near non-skin card inherits skin capture ownership");
        for(unsigned c=0;c<3;++c)Check(std::abs(Pixel(occludedBefore,x,y,c)-Pixel(occludedAfter,x,y,c))<.002f,"SSS changes near non-skin occluder");}
    OnThread(device,[&]{const auto* spec=device.GetResourcePool().PipelineTable.Get(resources.diffuseRadiancePipeline);
        Check(spec&&spec->depthWrite&&spec->depthCompare==CompareOp::LessEqual,"SSS mutated shared GI capture depth state");});
    frame.items.resize(1);
    // Ordinary eye/clothing materials carry no diffusion flag, hence cannot
    // enter the captures or receive any correction even with SSS enabled.
    set("skinEnabled",false);set("skinDiffusionEnabled",false);frame.items[0].material=Material::MaterialSnapshot::Create(instance,resources);
    const auto ordinary=draw();Check(pipeline.Statistics().skinDiffusionPasses==0,"Non-skin material entered diffusion");
    settings.skinDiffusion.enabled=false;const auto ordinaryOff=draw();
    for(unsigned i=0;i<ordinary.size();++i)Check(std::abs(ordinary[i]-ordinaryOff[i])<1e-6,"SSS affects ordinary eye/clothing shading");
    settings.skinDiffusion.strength=2;auto invalid=device.BeginFrame({2,Size,Size});Check(!pipeline.Record(invalid,frame,camera,settings)&&invalid.Cancel(),"Invalid SSS setting accepted");
    settings.skinDiffusion.strength=1;settings.skinDiffusion.enabled=true;
    Check(pipeline.Resize(Size,Size,128,4),pipeline.LastError());settings.temporal.msaaSamples=4;
    auto multisampled=device.BeginFrame({3,Size,Size});
    Check(!pipeline.Record(multisampled,frame,camera,settings)&&pipeline.LastError().find("TAA or FXAA")!=std::string::npos&&multisampled.Cancel(),
        "MSAA skin capture must be rejected with an actionable diagnostic");
    // Same dimensions, new single-sample depth generation: skin attachments
    // must rebuild instead of retaining a deleted depth image from before MSAA.
    Check(pipeline.Resize(Size,Size,128,1),pipeline.LastError());settings.temporal.msaaSamples=1;
    frame.items[0].material=skin;draw();
    const auto refreshedMask=OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(pipeline.DebugTextures().skinProfile);});
    Check(pipeline.Statistics().skinDiffusionPasses==6&&Pixel(refreshedMask,Size/2,Size/2,2)>.9f,"Skin attachments did not follow depth generation after same-size resize");
    pipeline.Shutdown();Check(!pipeline.DebugTextures().skinDiffuse.IsValid(),"SSS resource lease persists after shutdown");
    std::cout<<"SSS pipeline: captures visible skin diffuse, preserves specular/non-skin, composites before post, validates settings and releases resources passed\n";
}
}
int main(int argc,char** argv){try{
    bool vulkan=false,validation=false;for(int i=1;i<argc;++i){vulkan|=std::string(argv[i])=="--vulkan";validation|=std::string(argv[i])=="--validation";}
    if(vulkan)Check(RegisterVulkanBackend(),"Vulkan unavailable");Check(glfwInit(),"GLFW");glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    ApplicationWindow::Window window({Size,Size,"Skin diffusion RHI regression",false,vulkan?BackendType::Vulkan:BackendType::Opengl});Check(window.Activate(),"SSS test window");
    RHIDevice device;if(vulkan){auto c=*window.GetRenderContextAsVulkan();c.validation=validation;c.hardwareRayTracing=false;device.Run(BackendType::Vulkan,c);}else device.Run(BackendType::Opengl,*window.GetRenderContextAsOpengl());
    {KernelFixture kernel(device);kernel.Test();PipelineTest(device);Check(OnThread(device,[&]{return device.BackendDiagnostics()->ValidationErrorCount();})==0,"Native SSS validation errors");}
    device.StopAndRelease();return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
