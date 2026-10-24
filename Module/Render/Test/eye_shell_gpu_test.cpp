#include "ApplicationWindow/public/window.h"
#include "Render/Private/Pipeline/eye_shell_render_pass.h"
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Public/Pipeline/material_render_pipeline.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <future>
#include <iostream>
#include <stdexcept>
#include <glm/ext/matrix_clip_space.hpp>
#if defined(_WIN32)
extern "C" {__declspec(dllexport) unsigned long NvOptimusEnablement=1;__declspec(dllexport) int AmdPowerXpressRequestHighPerformance=1;}
#endif
using namespace Render;
namespace {
constexpr unsigned Size=32;
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>auto Thread(RHIDevice& device,F fn){using T=std::invoke_result_t<F>;auto promise=std::make_shared<std::promise<T>>();auto future=promise->get_future();
 device.async_ExecuteCode([promise,fn]{try{promise->set_value(fn());}catch(...){promise->set_exception(std::current_exception());}});return future.get();}
void Submit(RHIDevice& d,RHIFrameEncoder& e){Check(e.End(false),"End eye-shell frame");auto done=std::make_shared<bool>(false);Check(d.async_SubmitFrameCommands(e.GetCommandBuffer(),[done]{*done=true;}),"Submit eye-shell frame");auto end=std::chrono::steady_clock::now()+std::chrono::seconds(30);
 while(!*done){d.returnSystem.DrainCallbacks();Check(std::chrono::steady_clock::now()<end,"Eye-shell frame timeout");if(!*done)d.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));}}
void Test(RHIDevice& device){
 PipelineDetail::ResourceBuilder b(device);auto schema=Material::MakePbrTemplate();Check(bool(schema)&&schema->ByteSize()<=1024,"Eye-shell material schema exceeds capacity");
 std::cout<<"PBR schema after eye shell: "<<schema->ByteSize()<<" bytes\n";
 CreateGraphicShaderDesc source;source.vertexShaderSource=b.Source(ShaderSourceType::Vertex,"#version 450 core\nlayout(location=0)in vec3 p;void main(){gl_Position=vec4(p,1);}");
 source.fragmentShaderSource=b.Source(ShaderSourceType::Fragment,"#version 450 core\nlayout(location=0)out vec4 c;void main(){c=vec4(1);}");
 auto program=b.Create<ShaderProgramSpec>([&](auto cb){device.async_CreateGraphicShader(source,cb);});auto spec=Material::PbrPipelineSpec(program);
 auto pipe=b.Create<PipelineSpec>([&](auto cb){device.async_CreatePipeline({spec},cb);});
 auto frontSpec=spec;frontSpec.cullMode=CullMode::Front;auto frontPipe=b.Create<PipelineSpec>([&](auto cb){device.async_CreatePipeline({frontSpec},cb);});
 const std::array<float,48> vertices{-.2f,-.2f,-.5f,0,0,1,.5f,.5f,1,0,0,1, .2f,-.2f,-.5f,0,0,1,.5f,.5f,1,0,0,1,
 .2f,.2f,-.5f,0,0,1,.5f,.5f,1,0,0,1, -.2f,.2f,-.5f,0,0,1,.5f,.5f,1,0,0,1};
 const std::array<unsigned,6> indices{0,1,2,0,2,3};CreateMeshBufferDesc mesh;mesh.vertexLayout=Material::PbrVertexLayout();mesh.vertexCount=4;mesh.vertexData=vertices.data();mesh.vertexByteSize=sizeof(vertices);mesh.indexCount=6;mesh.indexData=indices.data();mesh.indexByteSize=sizeof(indices);
 auto geometry=b.Create<VertexBufferSpec>([&](auto cb){device.async_CreateMeshBuffer(mesh,cb);});
 auto varyingVertices=vertices;const std::array<glm::vec2,4> varyingUV{glm::vec2(.03f,.38f),glm::vec2(.25f,.38f),glm::vec2(.25f,.62f),glm::vec2(.03f,.62f)};
 for(unsigned i=0;i<4;++i){varyingVertices[i*12+6]=varyingUV[i].x;varyingVertices[i*12+7]=varyingUV[i].y;}
 mesh.vertexData=varyingVertices.data();auto varyingGeometry=b.Create<VertexBufferSpec>([&](auto cb){device.async_CreateMeshBuffer(mesh,cb);});
 auto color=b.Texture(Size,Size,RHITextureFormat::RGBA32F);auto target=b.Target(color,{});auto projection=glm::perspectiveRH_NO(glm::radians(60.f),1.f,.05f,10.f);
 std::array<RenderResourceHandle<RHITextureSpec>,4> depths;
 const std::array<float,4> distances{.55f,.65f,.45f,.5005f};
 for(unsigned i=0;i<depths.size();++i){auto clip=projection*glm::vec4(0,0,-distances[i],1);std::vector<float> values(Size*Size*4,clip.z/clip.w*.5f+.5f);
  CreateRHITextureSpec tex;tex.width=tex.height=Size;tex.mipmaps=false;tex.textureDataStoreType=tex.textureUseType=RHITextureFormat::RGBA32F;tex.data=values.data();tex.filterMode=RHIFilterMode::Nearest;tex.addressMode=RHIAddressMode::ClampToEdge;
  depths[i]=b.Create<RHITextureSpec>([&](auto cb){device.async_CreateTexture(tex,cb);});}
 // Distinct source/output aliases ensure each fixture starts from identical,
 // nonconstant HDR. The left-side UV band has a nonzero radial ray and blur
 // mask; a bright impulse exercises highlight rejection without specular light.
 std::vector<float> pattern(Size*Size*4,1.f);
 for(unsigned y=0;y<Size;++y)for(unsigned x=0;x<Size;++x){auto k=(y*Size+x)*4;float checker=((x/2+y/2)%2)?1.5f:.1f;
  pattern[k]=checker;pattern[k+1]=.3f+checker*.4f;pattern[k+2]=.2f+checker*.2f;}
 for(unsigned c=0;c<3;++c)pattern[(Size/2*Size+12)*4+c]=4000.f;
 std::array<RenderResourceHandle<RHITextureSpec>,3> patternedColor;std::array<RenderResourceHandle<RenderTargetSpec>,3> patternedTarget;
 for(unsigned i=0;i<patternedColor.size();++i){CreateRHITextureSpec tex;tex.width=tex.height=Size;tex.mipmaps=false;tex.textureDataStoreType=tex.textureUseType=RHITextureFormat::RGBA32F;
  tex.data=pattern.data();tex.filterMode=RHIFilterMode::Linear;tex.addressMode=RHIAddressMode::ClampToEdge;
  patternedColor[i]=b.Create<RHITextureSpec>([&](auto cb){device.async_CreateTexture(tex,cb);});patternedTarget[i]=b.Target(patternedColor[i],{});}
 auto parameters=b.Uniform(schema->ByteSize());const std::array<float,4> neutralPixels{.5f,.5f,1,1};CreateRHITextureSpec neutral;
 neutral.width=neutral.height=1;neutral.mipmaps=false;neutral.textureDataStoreType=neutral.textureUseType=RHITextureFormat::RGBA32F;neutral.data=neutralPixels.data();
 auto white=b.Create<RHITextureSpec>([&](auto cb){device.async_CreateTexture(neutral,cb);});
 auto owner=b.Finish();Material::MaterialPassResources resources;resources.expectedTemplate=schema;resources.pipeline=pipe;resources.lifetime=owner;resources.parameterBuffer=parameters;resources.parameterBufferBytes=schema->ByteSize();
 for(const auto& slot:schema->Desc().textures)if(slot.required)resources.textures.push_back({slot.slot,white});
 auto asset=Material::MaterialAsset::Create(schema);Material::MaterialInstance instance(asset);
 Check(instance.Set("eyeShellEnabled",true),"Set eye-shell routing");Check(instance.Set("eyeShellSpecular",glm::vec4(.01f,0,0,1)),"Disable specular fixture");
 RenderItem item;item.mesh=geometry;item.draw.indexCount=6;
 PipelineDetail::EyeShellRenderPass pass(device);std::uint64_t frame=0;
 auto draw=[&](bool enabled,unsigned depthIndex,bool routed=true,float depthUnits=1.f){
  Check(instance.Set("eyeShellEnabled",routed)&&instance.Set("eyeShellCavity",glm::vec4(1,.54f,.2f,depthUnits)),"Set eye-shell fixture parameters");
  item.material=Material::MaterialSnapshot::Create(instance,resources);Check(bool(item.material),"Create eye-shell snapshot");
  auto encoder=device.BeginFrame({++frame,Size,Size});Check(encoder.KeepAlive(owner),"Pin eye-shell fixture");RHICommand::SetRenderTarget clear;clear.target=target;clear.width=clear.height=Size;clear.clearFlags=RHICommand::ClearColor;clear.clearColor={.5f,.5f,.5f,1};Check(encoder.SetRenderTarget(clear),"Clear fixture");
  PipelineDetail::EyeShellPassInputs input;input.color=color;input.depth=depths[depthIndex];input.projection=projection;input.nearPlane=.05f;input.lighting.lightColors.fill(glm::vec4(0));
  EyeShellSettings settings;settings.enabled=enabled;const auto stats=pass.Record(encoder,std::span<const RenderItem>(&item,1),{target,color,{},Size,Size,1},input,settings);
  Check(stats.passes==((enabled&&routed)?2u:0u)&&stats.draws==((enabled&&routed)?1u:0u),"Eye-shell routing or pass statistics mismatch");
  Submit(device,encoder);return Thread(device,[&]{return device.BackendDiagnostics()->ReadTexture(color);});};
 const auto baseline=draw(false,0),processed=draw(true,0);
 // Independent scalar evaluation at UV(.5,.5), using the source MDF defaults.
 // Max outline=.15/(.65*1.92); minimum fill=(.5/1.1+.9-1)/.9.
 const double shape=std::max(.15/(.65*1.92),(.5/1.1+.9-1)/.9);
 const double visibility=std::pow(std::clamp(1-.47+shape,0.0,1.0),2.0);
 const auto center=(Size/2*Size+Size/2)*4;
 for(unsigned c=0;c<3;++c)Check(std::abs(processed[center+c]-.5*visibility)<.002,"Eye-shell differs from source UV occlusion formula");
 Check(std::abs(processed[0]-.5f)<1e-6,"Eye shell changes pixels outside its mesh");
 for(unsigned depth:{1u,2u}){const auto rejected=draw(true,depth);for(unsigned i=0;i<rejected.size();++i)Check(std::abs(rejected[i]-baseline[i])<1e-6,"Eye-shell depth window changes foreground/distant HDR");}
 const auto notRouted=draw(true,0,false);for(unsigned i=0;i<baseline.size();++i)Check(std::abs(notRouted[i]-baseline[i])<1e-6,"Unrouted material enters eye-shell pass");
 const auto cmRejected=draw(true,0,true,100.f),cmNear=draw(true,3,true,100.f);
 Check(std::abs(cmRejected[center]-.5f)<1e-6&&cmNear[center]<.49f,"Eye-shell source depth unit conversion is ineffective");
 for(auto x:processed)Check(std::isfinite(x)&&x>=0,"Invalid eye-shell HDR output");
 resources.pipeline=frontPipe;const auto frontCulled=draw(true,0);resources.pipeline=pipe;
 for(unsigned i=0;i<baseline.size();++i)Check(std::abs(frontCulled[i]-baseline[i])<1e-6,"Eye shell ignores source pipeline culling metadata");
 auto drawPattern=[&](unsigned fixture,float blur,float rejection){
  Check(instance.Set("eyeShellEnabled",true)&&instance.Set("eyeShellCavity",glm::vec4(1,.54f,.2f,1))&&
   instance.Set("eyeShellSurface",glm::vec4(.1f,.47f,blur,rejection)),"Set real HDR blur fixture");
  auto varyingItem=item;varyingItem.mesh=varyingGeometry;varyingItem.material=Material::MaterialSnapshot::Create(instance,resources);Check(bool(varyingItem.material),"Create varying-UV eye shell");
  auto encoder=device.BeginFrame({++frame,Size,Size});Check(encoder.KeepAlive(owner),"Pin patterned HDR eye fixture");
  PipelineDetail::EyeShellPassInputs input;input.color=patternedColor[fixture];input.depth=depths[0];input.projection=projection;input.nearPlane=.05f;input.lighting.lightColors.fill(glm::vec4(0));
  const auto stats=pass.Record(encoder,std::span<const RenderItem>(&varyingItem,1),{patternedTarget[fixture],patternedColor[fixture],{},Size,Size,1},input,{});
  Check(stats.passes==2&&stats.draws==1,"Patterned HDR pass was not executed");Submit(device,encoder);
  return Thread(device,[&]{return device.BackendDiagnostics()->ReadTexture(patternedColor[fixture]);});
 };
 const auto noBlur=drawPattern(0,0.f,8.f),sourceBlur=drawPattern(1,.86f,8.f),noRejection=drawPattern(2,.86f,1e7f);
 unsigned changed=0,rejectedHighlights=0;float blurDelta=0,rejectionDelta=0;
 for(unsigned y=0;y<Size;++y)for(unsigned x=0;x<Size;++x)for(unsigned c=0;c<4;++c){auto k=(y*Size+x)*4+c;
  Check(std::isfinite(sourceBlur[k])&&sourceBlur[k]>=0&&sourceBlur[k]<=60000.f,"Highlight rejection produced invalid HDR");
  Check(std::isfinite(noRejection[k]),"Unrejected HDR blur produced nonfinite color");
  if(x<4||x>=Size-4||y<4||y>=Size-4){Check(std::abs(sourceBlur[k]-pattern[k])<1e-6f&&std::abs(noBlur[k]-pattern[k])<1e-6f&&std::abs(noRejection[k]-pattern[k])<1e-6f,"HDR blur changes pixels outside shell mesh");}
  if(c<3&&x>=7&&x<25&&y>=7&&y<25){float delta=std::abs(sourceBlur[k]-noBlur[k]);blurDelta=std::max(blurDelta,delta);changed+=delta>.005f;
   delta=std::abs(sourceBlur[k]-noRejection[k]);rejectionDelta=std::max(rejectionDelta,delta);rejectedHighlights+=delta>.05f;}
 }
 Check(changed>12&&blurDelta>.01f,"Nonzero radial HDR blur does not change the patterned neighborhood");
 Check(rejectedHighlights>0&&rejectionDelta>.05f,"HDR highlight rejection is not exercised by the impulse");
 std::cout<<"Eye-shell HDR blur changed "<<changed<<" channels, max delta "<<blurDelta<<"; rejection affected "<<rejectedHighlights<<", max delta "<<rejectionDelta<<'\n';
 pass.Shutdown();std::cout<<"Eye shell: finite patterned HDR blur/rejection, snapshot aliasing, original UV mask, depth window, routing/disable A-B, source unit conversion and outside-mesh preservation passed\n";
}
}
int main(int argc,char** argv){try{bool vulkan=false,validation=false;for(int i=1;i<argc;++i){vulkan|=std::string(argv[i])=="--vulkan";validation|=std::string(argv[i])=="--validation";}
 if(vulkan)Check(RegisterVulkanBackend(),"Vulkan unavailable");Check(glfwInit(),"GLFW initialization");glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
 ApplicationWindow::Window window({Size,Size,"Eye-shell RHI test",false,vulkan?BackendType::Vulkan:BackendType::Opengl});Check(window.Activate(),"Activate eye-shell window");RHIDevice device;
 if(vulkan){auto context=*window.GetRenderContextAsVulkan();context.validation=validation;context.hardwareRayTracing=false;device.Run(BackendType::Vulkan,context);}else device.Run(BackendType::Opengl,*window.GetRenderContextAsOpengl());
 Test(device);Check(Thread(device,[&]{return device.BackendDiagnostics()->ValidationErrorCount();})==0,"Eye-shell native validation errors");device.StopAndRelease();glfwTerminate();return 0;
 }catch(const std::exception& e){std::cerr<<"Eye-shell test failed: "<<e.what()<<'\n';return 1;}}
