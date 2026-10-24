#include "ApplicationWindow/public/window.h"
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Private/Pipeline/probe_sdf_shaders.h"
#include "Render/Private/Pipeline/realtime_gi_shaders.h"
#include "Render/Public/Pipeline/probe_sdf_tracing.h"
#include <array>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <glad/glad.h>
#include <iostream>
#include <limits>
#include <vector>
using namespace Render;
using namespace Render::PipelineDetail;
void Require(bool x, const char *s) {
  if (!x)
    throw std::runtime_error(s);
}
template <class F> void OnThread(RHIDevice &d, F f) {
  bool done = false;
  d.async_ExecuteCode(f, [&] { done = true; });
  while (!done) {
    d.returnSystem.DrainCallbacks();
    if (!done)
      d.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));
  }
}
void DeferredTextureUploadContract(RHIDevice& device) {
  ResourceBuilder build(device);
  const auto wait=[&](const bool& done){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!done){device.returnSystem.DrainCallbacks();
      Require(std::chrono::steady_clock::now()<deadline,"Deferred upload callback timed out");
      if(!done)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(1));}
  };
  const auto emptyFrame=[&]{
    auto encoder=device.BeginFrame({0,64,32});encoder.End(false);bool done=false;
    device.async_SubmitFrameCommands(encoder.GetCommandBuffer(),[&]{done=true;});wait(done);
  };
  const auto create=[&](unsigned width,unsigned height,const std::vector<glm::vec4>& pixels,bool tracked){
    CreateRHITextureSpec desc;desc.width=width;desc.height=height;desc.mipmaps=false;
    desc.textureDataStoreType=desc.textureUseType=RHITextureFormat::RGBA32F;desc.data=pixels.data();
    if(tracked)return build.Create<RHITextureSpec>([&](auto callback){device.async_CreateTexture(desc,callback);});
    RenderResourceHandle<RHITextureSpec> result;bool done=false;
    device.async_CreateTexture(desc,[&](auto handle){result=handle;done=true;});wait(done);
    Require(result.IsValid(),"Deferred upload temporary texture creation failed");return result;
  };
  // Mutate and release each caller buffer after the RHI accepted its owned
  // copy. Deferred callbacks must return before any frame/readback flush.
  const auto update=[&](auto texture,unsigned x,unsigned y,unsigned width,unsigned height,
                        std::vector<glm::vec4> source,bool deferred,bool expectedSuccess=true){
    UpdateRHITextureDesc desc;desc.x=x;desc.y=y;desc.width=width;desc.height=height;
    desc.format=RHITextureFormat::RGBA32F;desc.data=source.data();
    desc.byteSize=source.size()*sizeof(glm::vec4);desc.deferUntilFrame=deferred;
    bool done=false,success=false;
    device.async_UpdateTexture(texture,desc,[&](bool result){success=result;done=true;});
    std::fill(source.begin(),source.end(),glm::vec4(-123.5f));wait(done);
    Require(success==expectedSuccess,"Deferred upload acceptance/rejection contract failed");
  };
  const auto readExact=[&](auto texture,const std::vector<glm::vec4>& expected){
    std::vector<float> actual;
    OnThread(device,[&]{actual=device.BackendDiagnostics()->ReadTexture(texture);});
    static_assert(sizeof(glm::vec4)==4*sizeof(float));
    Require(actual.size()==expected.size()*4&&
        std::memcmp(actual.data(),expected.data(),expected.size()*sizeof(glm::vec4))==0,
        "Deferred upload bytes/order/lifetime changed during readback");
  };
  const auto statistics=[&]{TextureUploadStatistics result;
    OnThread(device,[&]{result=device.BackendDiagnostics()->LastTextureUploadStatistics();});return result;};
  std::vector<glm::vec4> expected(8,glm::vec4(0,0,0,1));
  auto texture=create(4,2,expected,true),discarded=create(4,2,expected,false);
  std::vector<glm::vec4> largeExpected(128*64,glm::vec4(.125f,.25f,.5f,1));
  auto largeTexture=create(128,64,largeExpected,true);
  emptyFrame();emptyFrame(); // Retire/bootstrap, then clear creation counters.

  const std::vector<glm::vec4> first{{-0.f,.125f,.25f,1},{.25f,.5f,.75f,1},
      {.5f,.25f,.125f,1},{.75f,.5f,.25f,1}};
  update(texture,0,0,2,2,first,true);
  update(texture,4,0,1,1,{glm::vec4(99)},true,false); // Preserve pending valid work.
  const std::vector<glm::vec4> overlap{{1,.5f,.25f,1},{.25f,1,.5f,1}};
  update(texture,1,1,2,1,overlap,true);
  expected[0]=first[0];expected[1]=first[1];expected[4]=first[2];
  expected[5]=overlap[0];expected[6]=overlap[1];
  // Flush through diagnostic readback, without starting a frame. Original RHI
  // command payloads and caller vectors have already gone out of scope.
  readExact(texture,expected);emptyFrame();
  auto counts=statistics();
  Require(counts.regions==2&&counts.bytes==6*sizeof(glm::vec4)&&counts.deferredRegions==2&&
      counts.immediateSubmissions==0&&counts.immediateFenceWaits==0&&counts.stagingAllocations==1,
      "Deferred copies unexpectedly submitted/waited individually or counted an invalid update");

  const glm::vec4 deferredValue(.125f,.75f,.5f,1),immediateValue(.5f,.125f,.75f,1);
  update(texture,0,0,2,1,{deferredValue,deferredValue},true);
  update(texture,0,0,1,1,{immediateValue},false); // Flush pending, then overwrite later.
  expected[0]=immediateValue;expected[1]=deferredValue;readExact(texture,expected);emptyFrame();
  counts=statistics();
  Require(counts.regions==2&&counts.deferredRegions==1&&counts.immediateSubmissions==1&&
      counts.immediateFenceWaits==1,"Mixed deferred/immediate upload ordering or submission contract changed");

  // Reallocate the arena after a retired submission, then reuse it on the
  // frame path. A cancelled upload must never retain a destroyed image id.
  std::fill(largeExpected.begin(),largeExpected.end(),glm::vec4(.75f,.5f,.125f,1));
  update(largeTexture,0,0,128,64,largeExpected,true);readExact(largeTexture,largeExpected);emptyFrame();
  counts=statistics();
  Require(counts.deferredRegions==1&&counts.stagingAllocations==1&&
      counts.stagingCapacityBytes>=largeExpected.size()*sizeof(glm::vec4),"Deferred staging arena growth failed");
  update(discarded,0,0,1,1,{glm::vec4(9)},true);
  bool deleted=false;device.async_DeleteTexture(discarded,[&]{deleted=true;});wait(deleted);
  expected[7]=glm::vec4(.5f,.75f,.25f,1);update(texture,3,1,1,1,{expected[7]},true);
  emptyFrame();counts=statistics();
  Require(counts.regions==1&&counts.deferredRegions==1&&counts.immediateSubmissions==0&&
      counts.immediateFenceWaits==0&&counts.stagingAllocations==0,
      "Pending deletion did not cancel its copy or the staging arena was not reused");
  readExact(texture,expected);
  OnThread(device,[&]{Require(device.BackendDiagnostics()->ReadTexture(discarded).empty(),
      "Deleted pending-upload texture remained readable");
    Require(!device.BackendDiagnostics()->ValidationErrorCount(),"Deferred upload Vulkan validation error");});
  std::cout<<"Deferred texture uploads passed: owned callbacks, overlapping writes, invalid update, "
      "readback flush, mixed submission, arena growth/reuse and pending deletion\n";
}
void FrameComputeContract(RHIDevice& device) {
  ResourceBuilder build(device);auto compute=Compute::Device::Borrow(device);
  Require(compute->Diagnostics().frameAvailable,"Frame compute unavailable");
  auto input=build.Texture(8,1,RHITextureFormat::RGBA32F);
  auto sourceTarget=build.Target(input,{});
  auto first=build.StorageTexture(8,1,RHITextureFormat::RGBA32F),second=build.StorageTexture(8,1,RHITextureFormat::RGBA32F);
  auto uniform=build.Uniform(16);const auto control=compute->CreateStorageBuffer(64,nullptr,"frame control");
  Compute::FramePipelineDesc reset;reset.label="frame reset";reset.storageBuffers=1;reset.pushConstantBytes=4;
  reset.source=R"(#version 450
layout(local_size_x=1) in;layout(std430,set=2,binding=0) buffer C{uint w[];};
layout(push_constant) uniform P{uint groups;}p;
void main(){w[0]=0;w[1]=p.groups;w[2]=1;w[3]=1;})";
  const auto resetProgram=compute->CreateFramePipeline(reset);
  Compute::FramePipelineDesc pass;pass.label="frame sampled output";pass.uniformMask=1;pass.sampledMask=1;
  pass.storageBuffers=1;pass.storageImages=1;
  pass.source=R"(#version 450
layout(local_size_x=8) in;layout(std140,binding=0) uniform P{vec4 add;};
layout(binding=0) uniform sampler2D source;
layout(std430,set=2,binding=0) buffer C{uint w[];};
layout(rgba32f,set=3,binding=0) uniform writeonly image2D result;
void main(){int i=int(gl_GlobalInvocationID.x);imageStore(result,ivec2(i,0),texelFetch(source,ivec2(i,0),0)+add);atomicAdd(w[0],1);})";
  const auto program=compute->CreateFramePipeline(pass);
  const auto submit=[&](uint32_t groups,bool initialize) {
    auto e=device.BeginFrame({0,64,32});
    if(initialize)Require(e.SetRenderTarget({sourceTarget,8,1,RHICommand::ClearColor,glm::vec4(.125f,.25f,.5f,1)}),"Frame compute input clear");
    RHICommand::DispatchFrameCompute command;command.pipeline=resetProgram;command.buffers[0]=control;command.pushBytes=4;
    std::memcpy(command.pushConstants.data(),&groups,4);Require(e.DispatchCompute(command),"Frame compute reset record");
    Require(e.BindUniformBuffer(uniform,0)&&e.BindTexture(input,0),"Frame compute resource binding");
    if(initialize) {
      Require(e.UpdateUniformBuffer(uniform,glm::vec4(.5f)) ,"Frame compute first uniform");
      command={};command.pipeline=program;command.buffers[0]=control;command.images[0]=first;
      Require(e.DispatchCompute(command),"Frame compute direct record");
    }
    Require(e.UpdateUniformBuffer(uniform,glm::vec4(.25f))&&e.BindTexture(first,0),"Frame compute second snapshot");
    command={};command.pipeline=program;command.buffers[0]=control;command.images[0]=second;command.indirect=control;command.indirectOffset=4;
    Require(e.DispatchCompute(command)&&e.End(false),"Frame compute indirect record");
    bool done=false;device.async_SubmitFrameCommands(e.GetCommandBuffer(),[&]{done=true;});
    while(!done){device.returnSystem.DrainCallbacks();if(!done)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));}
    OnThread(device,[&]{const auto words=device.BackendDiagnostics()->CompletedComputeWords(control,4);
      Require(words[0]==(initialize?16u:0u)&&words[1]==groups,"Frame compute counter/barrier/zero-indirect contract");});
  };
  submit(1,true);submit(0,false);
  OnThread(device,[&]{const auto pixels=device.BackendDiagnostics()->ReadTexture(second);
    for(size_t i=0;i<8;++i)for(unsigned c=0;c<4;++c)
      Require(pixels[i*4+c]==glm::vec4(.875f,1.f,1.25f,1.75f)[c],"Frame compute UBO snapshots or image visibility changed");
    Require(!device.BackendDiagnostics()->ValidationErrorCount(),"Frame compute Vulkan validation error");});
  Require(compute->Diagnostics().submittedBatches==0,"Frame compute unexpectedly used standalone submissions");
  // Exercise the real frame descriptor path. Three immutable A/B/A uploads
  // share one logical UBO and the same sampled/storage binding set; every
  // dispatch writes a separate row, so the final mutation cannot hide aliasing.
  Compute::FramePipelineDesc cached;cached.label="frame descriptor content contract";
  cached.uniformMask=1;cached.sampledMask=1;cached.storageBuffers=1;cached.storageImages=1;cached.pushConstantBytes=4;
  cached.source=R"(#version 450
layout(local_size_x=8) in;layout(std140,binding=0) uniform P{vec4 add;};
layout(binding=0) uniform sampler2D source;
layout(std430,set=2,binding=0) buffer C{uvec4 values[];};
layout(rgba32f,set=3,binding=0) uniform writeonly image2D result;
layout(push_constant) uniform Q{uint row;}q;
void main(){int i=int(gl_GlobalInvocationID.x);vec4 value=texelFetch(source,ivec2(i,0),0)+add;
imageStore(result,ivec2(i,int(q.row)),value);values[q.row*8u+uint(i)]=floatBitsToUint(value);})";
  const auto cacheProgram=compute->CreateFramePipeline(cached);
  for(unsigned generation=0;generation<2;++generation){
    ResourceBuilder resources(device);
    const auto sampled=resources.Texture(8,1,RHITextureFormat::RGBA32F);
    const auto sampledTarget=resources.Target(sampled,{});
    const auto resultImage=resources.StorageTexture(8,3,RHITextureFormat::RGBA32F);
    const auto resultBuffer=compute->CreateStorageBuffer(3*8*sizeof(glm::uvec4),nullptr,"frame descriptor generation");
    const auto source=generation?glm::vec4(.25f,.5f,.125f,1):glm::vec4(.125f,.25f,.5f,1);
    const std::array<bool,3> modes=generation?std::array<bool,3>{true,true,false}:std::array<bool,3>{false,true,true};
    for(unsigned frame=0;frame<modes.size();++frame){
      RHICommand::BeginFrame begin;begin.frameIndex=600+generation*3+frame;begin.framebufferWidth=64;begin.framebufferHeight=32;
      begin.reuseUniformSnapshotContents=modes[frame];auto e=device.BeginFrame(begin);
      Require(e.SetRenderTarget({sampledTarget,8,1,RHICommand::ClearColor,source})&&
          e.BindUniformBuffer(uniform,0,0,16)&&e.BindTexture(sampled,0),"Frame descriptor setup");
      for(uint32_t row=0;row<3;++row){
        const auto add=glm::vec4(row==1?.75f:.5f);
        Require(e.UpdateUniformBuffer(uniform,add),"Frame descriptor immutable UBO upload");
        RHICommand::DispatchFrameCompute command;command.pipeline=cacheProgram;command.buffers[0]=resultBuffer;
        command.images[0]=resultImage;command.pushBytes=4;std::memcpy(command.pushConstants.data(),&row,4);
        Require(e.DispatchCompute(command),"Frame descriptor content dispatch");
      }
      Require(e.End(false),"Frame descriptor end");bool done=false;
      device.async_SubmitFrameCommands(e.GetCommandBuffer(),[&]{done=true;});
      const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
      while(!done){device.returnSystem.DrainCallbacks();Require(std::chrono::steady_clock::now()<deadline,"Frame descriptor timeout");
        if(!done)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));}
      std::vector<float> pixels;std::vector<uint32_t> words;UniformSnapshotStatistics stats;HostFrameStatistics host;
      OnThread(device,[&]{pixels=device.BackendDiagnostics()->ReadTexture(resultImage);
        words=device.BackendDiagnostics()->CompletedComputeWords(resultBuffer,96);
        stats=device.BackendDiagnostics()->LastUniformSnapshotStatistics();host=device.BackendDiagnostics()->LastHostFrameStatistics();
        Require(!device.BackendDiagnostics()->ValidationErrorCount(),"Frame descriptor Vulkan validation error");});
      Require(pixels.size()==96&&words.size()==96,"Frame descriptor output extent");
      for(unsigned row=0;row<3;++row)for(unsigned i=0;i<8;++i)for(unsigned channel=0;channel<4;++channel){
        const float expected=source[channel]+(row==1?.75f:.5f);const auto at=(row*8+i)*4+channel;
        Require(pixels[at]==expected&&words[at]==std::bit_cast<uint32_t>(expected),
            "Frame descriptors reused stale UBO version, sampled view, SSBO or storage image generation");
      }
      Require(stats.enabled==modes[frame]&&host.frameIndex==begin.frameIndex,"Frame descriptor diagnostics frame");
      if(!modes[frame]){
        Require(host.descriptorAllocations==12&&stats.descriptorWrites==12&&stats.copiedBytes==48,
            "Frame compute baseline allocation/write/copy accounting changed");
      }else{
        const bool warm=(generation==0&&frame==2)||(generation==1&&frame==1);
        Require(stats.contentReuses>=1&&stats.copiedBytes==32,
            "Frame compute immutable A/B/A content was not exactly reused");
        Require(host.descriptorAllocations==(warm?0u:5u)&&stats.descriptorWrites==(warm?0u:5u)&&
            stats.descriptorCacheHits==(warm?12u:7u),"Frame compute persistent descriptor allocation/write/hit accounting");
      }
    }
    compute->Destroy(resultBuffer); // The next enabled frame uses new buffer/view lifetimes.
  }

  const auto liveBuffers=compute->Diagnostics().liveBuffers;
  auto pinned=std::shared_ptr<Compute::Device>(Compute::Device::Borrow(device));
  const auto pinnedBuffer=pinned->CreateStorageBuffer(64,nullptr,"frame pinned generation");
  auto lease=device.BeginFrame({0,64,32});Require(lease.KeepAlive(pinned),"Cannot pin frame compute owner");
  RHICommand::DispatchFrameCompute command;command.pipeline=resetProgram;command.buffers[0]=pinnedBuffer;command.pushBytes=4;
  Require(lease.DispatchCompute(command)&&lease.End(false),"Cannot record pinned frame compute");
  pinned.reset(); // Only the unsubmitted frame now owns this buffer generation.
  bool retired=false;device.async_SubmitFrameCommands(lease.GetCommandBuffer(),[&]{retired=true;});
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
  while(!retired){device.returnSystem.DrainCallbacks();Require(std::chrono::steady_clock::now()<deadline,"Pinned compute owner retirement deadlocked");
    if(!retired)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));}
  OnThread(device,[&]{Require(device.BackendDiagnostics()->ComputeDiagnostics().liveBuffers==liveBuffers,
      "Retired frame compute owner leaked its buffer generation");});
  std::cout<<"Frame compute contract passed: ended attachment sampling, UBO snapshots, storage images, direct/indirect/zero dispatch, retired counters, "
      "off/on A-B-A exact bytes, persistent descriptor counts, recreated sampled/storage image and SSBO lifetimes\n";
}
void UniformSnapshotContentContract(RHIDevice& device) {
  ResourceBuilder build(device);
  const auto mesh=build.FullscreenMesh();
  const auto uniform=build.Uniform(512),other=build.Uniform(16);
  const auto color=build.Texture(64,32,RHITextureFormat::RGBA8);
  const auto target=build.Target(color,{});
  const std::string vertex=R"(#version 450 core
layout(location=0) in vec2 p;void main(){gl_Position=vec4(p,0,1);})";
  const auto shader=build.FullscreenPipeline(vertex,R"(#version 450 core
layout(std140,binding=0) uniform Bits {uvec4 bits;};
layout(binding=1) uniform sampler2D source;layout(location=0) out vec4 o;
void main(){o=vec4(vec3(uvec3(bits.x,bits.y,bits.w)&uvec3(255u))/255.,1.)*texelFetch(source,ivec2(0),0);})");
  const auto wideShader=build.FullscreenPipeline(vertex,R"(#version 450 core
layout(std140,binding=0) uniform Bits {uvec4 bits;uvec4 extra;};
layout(binding=1) uniform sampler2D source;layout(location=0) out vec4 o;
void main(){uvec4 b=bits|extra;o=vec4(vec3(uvec3(b.x,b.y,b.w)&uvec3(255u))/255.,1.)*texelFetch(source,ivec2(0),0);})");
  const auto inverseShader=build.FullscreenPipeline(vertex,R"(#version 450 core
layout(std140,binding=0) uniform Bits {uvec4 bits;};
layout(binding=1) uniform sampler2D source;layout(location=0) out vec4 o;
void main(){o=vec4(1.-vec3(uvec3(bits.x,bits.y,bits.w)&uvec3(255u))/255.,1.)*texelFetch(source,ivec2(0),0);})");
  const auto texel=[&](std::array<uint8_t,4> rgba){CreateRHITextureSpec desc;
    desc.width=desc.height=1;desc.mipmaps=false;desc.data=rgba.data();
    desc.textureDataStoreType=desc.textureUseType=RHITextureFormat::RGBA8;
    return build.Create<RHITextureSpec>([&](auto cb){device.async_CreateTexture(desc,cb);});};
  const auto white=texel({255,255,255,255}),cyan=texel({0,255,255,255}),magenta=texel({255,0,255,255});
  using Bits=std::array<uint64_t,2>;
  constexpr uint64_t seed=14695981039346656037ull^sizeof(Bits),prime=1099511628211ull;
  constexpr Bits a{0,0},b{255,((seed^255ull)*prime)^(seed*prime)};
  // These different full 16-byte payloads collide before the production hash's
  // finalizer. Do not add a test-only hash override to the backend. Integer UBO
  // reads keep every payload bit meaningful without introducing NaN arithmetic.
  static_assert((seed*prime)*prime==(((seed^b[0])*prime)^b[1])*prime);
  constexpr Bits green{255ull<<32,0},blue{0,255ull<<32},yellow{255ull|(255ull<<32),0};
  const std::array<glm::vec4,16> expected{
    glm::vec4(0,0,0,1),glm::vec4(1,0,0,1),glm::vec4(0,0,0,1),glm::vec4(1,0,0,1),
    glm::vec4(0,1,0,1),glm::vec4(0,0,1,1),glm::vec4(0,1,0,1),glm::vec4(1,1,0,1),
    glm::vec4(0,0,0,1),glm::vec4(1,0,0,1),glm::vec4(0,0,0,1),glm::vec4(1,0,0,1),
    glm::vec4(0,1,1,1),glm::vec4(0,1,0,1),glm::vec4(1,0,0,1),glm::vec4(1,0,0,1)};
  uint64_t baselineCopiedBytes=0;
  // A second enabled frame must rebuild its own immutable snapshots even when
  // several logical buffers retain the same version, followed by the old path.
  const std::array<bool,4> modes{false,true,true,false};
  for(size_t frame=0;frame<modes.size();++frame) {
    RHICommand::BeginFrame begin;begin.frameIndex=700+frame;begin.framebufferWidth=64;begin.framebufferHeight=32;
    begin.reuseUniformSnapshotContents=modes[frame];auto e=device.BeginFrame(begin);
    Require(e.SetRenderTarget({target,64,32,RHICommand::ClearColor,glm::vec4(.5f)}),"Snapshot target record");
    Require(e.BindMesh(mesh)&&e.BindPipeline(shader)&&e.BindTexture(white,1),"Snapshot pipeline record");
    Require(e.UpdateUniformBuffer(uniform,a)&&e.UpdateUniformBuffer(uniform,blue,256)&&
        e.UpdateUniformBuffer(uniform,yellow,16)&&e.UpdateUniformBuffer(other,b),"Snapshot initial values record");
    for(uint32_t cell=0;cell<expected.size();++cell) {
      Require(e.SetViewport({int32_t((cell%8)*8),int32_t((cell/8)*16),8,16}),"Snapshot viewport record");
      if(cell==1)Require(e.UpdateUniformBuffer(uniform,b),"Snapshot collision B record");
      if(cell==2)Require(e.UpdateUniformBuffer(uniform,a),"Snapshot return to collision A record");
      if(cell==4)Require(e.UpdateUniformBuffer(uniform,green),"Snapshot changed version record");
      if(cell==7)Require(e.UpdateUniformBuffer(uniform,a),"Snapshot range mutation record");
      if(cell==13)Require(e.UpdateUniformBuffer(other,green),"Snapshot second logical buffer mutation record");
      if(cell>=14)Require(e.UpdateUniformBuffer(other,b),"Snapshot identical repeated update record");
      const bool useOther=cell==3||cell>=9;
      Require(e.BindUniformBuffer(useOther?other:uniform,0,cell==5?256:0,cell==7?32:16),"Snapshot range binding record");
      Require(e.BindPipeline(cell==7?wideShader:cell==12?inverseShader:shader),"Snapshot distinct shader record");
      Require(e.BindTexture(cell==10?cyan:cell==11?magenta:white,1)&&e.DrawIndexed({3}),"Snapshot draw record");
    }
    Require(e.End(false),"Snapshot frame end record");
    bool done=false;device.async_SubmitFrameCommands(e.GetCommandBuffer(),[&]{done=true;});
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!done){device.returnSystem.DrainCallbacks();Require(std::chrono::steady_clock::now()<deadline,"Snapshot frame timeout");
      if(!done)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));}
    std::vector<float> pixels;UniformSnapshotStatistics stats;HostFrameStatistics host;uint64_t errors=0;
    OnThread(device,[&]{pixels=device.BackendDiagnostics()->ReadTexture(color);
      stats=device.BackendDiagnostics()->LastUniformSnapshotStatistics();host=device.BackendDiagnostics()->LastHostFrameStatistics();
      errors=device.BackendDiagnostics()->ValidationErrorCount();});
    Require(pixels.size()==64*32*4&&!errors,"Snapshot readback/validation contract");
    // Check interiors of all sixteen viewports after every later mutation has
    // executed; sampling only the final logical buffer would miss stale aliasing.
    for(size_t cell=0;cell<expected.size();++cell)for(size_t y=1;y<15;++y)for(size_t x=1;x<7;++x)
      for(size_t channel=0;channel<4;++channel)
        Require(std::abs(pixels[(((cell/8)*16+y)*64+(cell%8)*8+x)*4+channel]-expected[cell][channel])<.01f,
            "Content snapshots changed collision, immutable version/range, logical buffer, shader or texture semantics");
    Require(host.frameIndex==begin.frameIndex&&host.drawCalls==expected.size()&&stats.enabled==modes[frame],
        "Snapshot frame diagnostics belong to a different frame or switch");
    if(modes[frame]) {
      Require(stats.contentHashCollisions>=1&&stats.contentReuses>=5&&
          stats.contentComparisons>=stats.contentReuses+stats.contentHashCollisions,
          "Content hash collision did not receive exact byte comparison or identical payloads were not reused");
      Require(host.uniformSnapshotCopies==5&&stats.copiedBytes==96&&stats.copiedBytes<baselineCopiedBytes,
          "Content snapshot unique byte counts or per-frame cache retirement changed");
      // Descriptor objects persist across retired enabled frames even though
      // the five immutable payloads are copied into the ring again each frame.
      // Six UBO sets + five sampled sets are new in the cold frame; the next
      // identical frame must hit all two sets of all sixteen draws.
      const bool warm=frame==2;
      const std::uint64_t allocations=warm?0u:11u,hits=warm?32u:21u;
      std::cout<<"Content snapshot descriptors frame="<<begin.frameIndex<<" warm="<<warm
          <<" alloc="<<host.descriptorAllocations<<" writes="<<stats.descriptorWrites
          <<" hits="<<stats.descriptorCacheHits<<" ring="<<stats.ringUsedBytes
          <<" copied="<<stats.copiedBytes<<'\n';
      Require(host.descriptorAllocations==allocations&&stats.descriptorWrites==allocations&&
          stats.descriptorCacheHits==hits&&stats.ringUsedBytes>=stats.copiedBytes,
          "Content snapshot descriptor/ring accounting contract");
    } else {
      Require(!stats.contentLookups&&!stats.contentReuses&&!stats.contentComparisons&&!stats.contentHashCollisions,
          "Disabled content optimization did not retain the version/range baseline");
      if(frame==0)baselineCopiedBytes=stats.copiedBytes;
      Require(stats.copiedBytes==baselineCopiedBytes&&stats.copiedBytes>96,"Baseline snapshot copies changed after switch retirement");
    }
  }
  std::cout<<"Uniform snapshot content contract passed: real hash collision A/B/A, logical buffers, immutable versions/ranges, "
      "shader/texture descriptors, unchanged updates and frame retirement in both modes\n";
}
void IndexedDrawRangeContract(RHIDevice& device) {
  ResourceBuilder build(device);
  // Prefix vertices and indices are deliberate decoys. Drawing all indices,
  // ignoring firstIndex or dropping baseVertex cannot produce the two quads.
  const std::array<float,32> vertices{4,4,0,0, 4,4,0,0, 4,4,0,0, 4,4,0,0,
      -1,-1,0,0, 1,-1,1,0, 1,1,1,1, -1,1,0,1};
  const std::array<uint32_t,9> indices{0,0,0,0,1,2,0,2,3};
  CreateMeshBufferDesc desc;desc.vertexLayout={{VertexFieldType::Vec2,VertexFieldType::Vec2}};
  desc.vertexCount=8;desc.vertexData=vertices.data();desc.vertexByteSize=sizeof(vertices);
  desc.indexCount=indices.size();desc.indexData=indices.data();desc.indexByteSize=sizeof(indices);
  const auto mesh=build.Create<VertexBufferSpec>([&](auto cb){device.async_CreateMeshBuffer(desc,cb);});
  const auto uniform=build.Uniform(16);
  const auto color=build.Texture(64,32,RHITextureFormat::RGBA8);
  const auto target=build.Target(color,{});
  const auto shader=build.FullscreenPipeline(R"(#version 450 core
layout(location=0) in vec2 p;layout(location=0) flat out vec4 instanceColor;
void main(){gl_Position=vec4(p.x*.5-.5+float(gl_InstanceID-5),p.y,0,1);
instanceColor=gl_InstanceID==5?vec4(1,0,0,1):gl_InstanceID==6?vec4(0,1,0,1):vec4(0,0,1,1);})",
      R"(#version 450 core
layout(std140,binding=0) uniform Color {vec4 tint;};layout(location=0) flat in vec4 instanceColor;
layout(location=0) out vec4 o;void main(){o=instanceColor*tint;})");
  for(unsigned frame=0;frame<4;++frame) {
    RHICommand::BeginFrame begin;begin.frameIndex=800+frame;begin.framebufferWidth=64;begin.framebufferHeight=32;
    begin.reuseUniformSnapshotContents=(frame&1)!=0;auto e=device.BeginFrame(begin);
    Require(e.SetRenderTarget({target,64,32,RHICommand::ClearColor,glm::vec4(0,0,1,1)})&&
        e.SetViewport({0,0,64,32})&&e.BindMesh(mesh)&&e.BindPipeline(shader)&&e.BindUniformBuffer(uniform,0)&&
        e.UpdateUniformBuffer(uniform,glm::vec4(1)),"Indexed range/instance setup record");
    Require(e.DrawIndexed({frame<2?6u:0u,3,4,2,5})&&e.End(false),"Indexed range/instance draw record");
    bool done=false;device.async_SubmitFrameCommands(e.GetCommandBuffer(),[&]{done=true;});
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!done){device.returnSystem.DrainCallbacks();Require(std::chrono::steady_clock::now()<deadline,"Indexed contract timeout");
      if(!done)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));}
    std::vector<float> pixels;uint64_t errors=0;
    OnThread(device,[&]{pixels=device.BackendDiagnostics()->ReadTexture(color);errors=device.BackendDiagnostics()->ValidationErrorCount();});
    Require(pixels.size()==64*32*4&&!errors,"Indexed range/instance readback/validation contract");
    for(size_t y=4;y<28;++y)for(size_t x=4;x<60;++x)for(size_t channel=0;channel<4;++channel) {
      const auto expected=x<32?glm::vec4(1,0,0,1):glm::vec4(0,1,0,1);
      Require(std::abs(pixels[(y*64+x)*4+channel]-expected[channel])<.01f,
          "DrawIndexed lost firstIndex/baseVertex/instanceCount/firstInstance or zero-count remainder semantics");
    }
  }
  std::cout<<"Indexed draw contract passed in both snapshot modes: firstIndex/baseVertex, two instances, nonzero firstInstance and zero-count remainder\n";
}
void ProbeSdfAtlasAddressContract(RHIDevice& device) {
  ResourceBuilder build(device);const auto mesh=build.FullscreenMesh();
  constexpr uint32_t caseCount=16,sourceRows=9;
  const auto casesUniform=build.Uniform(caseCount*sizeof(glm::ivec4));
  const auto sdfUniform=build.Uniform(sizeof(ProbeSdfTraceConstants));
  std::array<RenderResourceHandle<RHITextureSpec>,4> outputs;
  std::array<RenderResourceHandle<RenderTargetSpec>,4> singleTargets;
  for(size_t part=0;part<outputs.size();++part){outputs[part]=build.Texture(caseCount,1,RHITextureFormat::RGBA32F);
    singleTargets[part]=build.Target(outputs[part],{});}
  const auto mrtTarget=build.TargetColors(outputs);
  const std::string vertex=R"(#version 450 core
layout(location=0) in vec2 p;void main(){gl_Position=vec4(p,0,1);})";
  const std::string body=R"(
layout(std140,binding=0) uniform AddressCases {ivec4 addressCases[16];};
void AddressResults(out vec4 address,out vec4 objectValue,out vec4 meshValue,out vec4 geometryValue) {
    ivec4 c=addressCases[int(gl_FragCoord.x)];int index=c.x;
    address=vec4(vec2(PSAtlasAddress(index,psAtlas.x)),float(psAtlas.x),1);
    objectValue=vec4(0);meshValue=vec4(0);geometryValue=vec4(0);
    // Negative and INT_MAX cases exercise integer addressing only. Never issue
    // an undefined out-of-bounds texture read, even in the baseline variant.
    if(c.y!=0) {
        objectValue=PSObject(index/12,index%12);
        ivec3 cell=ivec3(index%7,(index/7)%5,index/35);
        meshValue=vec4(PSMeshTexel(cell,ivec3(7,5,1000),0),0,0,1);
        geometryValue=PSGeometry(index);
    }
}
)";
  std::array<std::array<RenderResourceHandle<PipelineSpec>,2>,2> programs;
  for(unsigned fast=0;fast<2;++fast)for(unsigned mrt=0;mrt<2;++mrt) {
    // Use the production source and all three production fetch helpers. The
    // custom main only exposes their result; it does not copy the address code.
    auto fragment=std::string("#version 450 core\n")+ProbeSdfDetail::FallbackCommonGLSL(false,fast!=0)+body;
    fragment+=mrt?R"(
layout(location=0) out vec4 a;layout(location=1) out vec4 b;
layout(location=2) out vec4 c;layout(location=3) out vec4 d;
void main(){AddressResults(a,b,c,d);})":R"(
layout(location=0) out vec4 o;
void main(){vec4 a,b,c,d;AddressResults(a,b,c,d);o=psDisplay.x==0?a:psDisplay.x==1?b:psDisplay.x==2?c:d;})";
    programs[fast][mrt]=build.FullscreenPipeline(vertex,fragment);
  }
  const auto genericAddressProgram=build.FullscreenPipeline(vertex,
      std::string("#version 450 core\n")+ProbeSdfDetail::FallbackCommonGLSL(false,true)+body+R"(
layout(location=0) out vec4 o;
void main(){int index=addressCases[int(gl_FragCoord.x)].x;
    o=vec4(vec2(PSAtlasAddress(index,psAtlas.x)),float(psAtlas.x),1);})");
  size_t sampledRecords=0,addressRecords=0;uint64_t frameIndex=900;
  for(const uint32_t width:{1024u,257u,1031u}) {
    const uint32_t texels=width*sourceRows;
    std::array<std::vector<glm::vec4>,3> source;
    std::array<RenderResourceHandle<RHITextureSpec>,3> textures;
    for(size_t part=0;part<source.size();++part){source[part].resize(texels);
      for(uint32_t index=0;index<texels;++index)for(unsigned channel=0;channel<4;++channel)
        source[part][index][channel]=std::bit_cast<float>(0x3e800001u+uint32_t(part)*0x80000u+index*4+channel);
      CreateRHITextureSpec desc;desc.width=width;desc.height=sourceRows;desc.mipmaps=false;
      desc.textureDataStoreType=desc.textureUseType=RHITextureFormat::RGBA32F;desc.data=source[part].data();
      textures[part]=build.Create<RHITextureSpec>([&](auto cb){device.async_CreateTexture(desc,cb);});
    }
    const std::array<int32_t,caseCount> indices{0,1,1023,1024,2047,2048,int32_t(texels-1),
      int32_t(width-1),int32_t(width),int32_t(width+1),int32_t(2*width-1),int32_t(2*width),
      -1,-1023,std::numeric_limits<int32_t>::min(),std::numeric_limits<int32_t>::max()};
    std::array<glm::ivec4,caseCount> cases;
    for(size_t i=0;i<indices.size();++i)cases[i]={indices[i],indices[i]>=0&&uint32_t(indices[i])<texels?1:0,0,0};
    std::array<std::vector<float>,4> baseline;
    for(unsigned fast=0;fast<2;++fast)for(unsigned mrt=0;mrt<2;++mrt) {
      // The production CPU gate selects a branchless 1024 specialization only
      // for eligible resident layouts. A fast request on NPOT selects legacy.
      const unsigned effectiveFast=fast&&width==1024?1u:0u;
      RHICommand::BeginFrame begin;begin.frameIndex=frameIndex++;begin.framebufferWidth=64;begin.framebufferHeight=32;
      auto e=device.BeginFrame(begin);Require(e.UpdateUniformBuffer(casesUniform,cases)&&e.BindUniformBuffer(casesUniform,0)&&
          e.BindUniformBuffer(sdfUniform,ProbeSdfTracingBinding),"Atlas address uniforms record");
      Require(e.BindTexture(textures[0],7)&&e.BindTexture(textures[1],6)&&e.BindTexture(textures[2],9),"Atlas address textures record");
      ProbeSdfTraceConstants constants;constants.atlas={int(width),int(width),1,1};constants.geometry.y=int(width);
      for(unsigned part=0;part<(mrt?1u:4u);++part) {
        constants.display.x=int(part);
        // The production block is 1264 bytes; respect the frame command's
        // 256-byte inline limit instead of issuing one oversized update.
        const auto* bytes=reinterpret_cast<const std::byte*>(&constants);
        for(uint32_t offset=0;offset<sizeof(constants);offset+=RHICommand::MaxInlineUniformBytes) {
          const auto count=uint32_t(std::min<size_t>(RHICommand::MaxInlineUniformBytes,sizeof(constants)-offset));
          Require(e.UpdateUniformBufferBytes(sdfUniform,bytes+offset,count,offset),"Atlas production UBO chunk record");
        }
        Require(e.SetRenderTarget({mrt?mrtTarget:singleTargets[part],caseCount,1,RHICommand::ClearColor,glm::vec4(0)})&&
            e.SetViewport({0,0,caseCount,1})&&e.BindMesh(mesh)&&e.BindPipeline(programs[effectiveFast][mrt])&&e.DrawIndexed({3}),
            "Production atlas helper draw record");
      }
      // Independently exercise the generic helper's enabled NPOT fallback.
      // This main calls only PSAtlasAddress, so none of the width-specialized
      // texture helpers can fetch from an ineligible layout or negative index.
      const bool genericProbe=fast&&width!=1024&&mrt;
      if(genericProbe)Require(e.SetRenderTarget({singleTargets[0],caseCount,1,RHICommand::ClearColor,glm::vec4(0)})&&
          e.SetViewport({0,0,caseCount,1})&&e.BindMesh(mesh)&&e.BindPipeline(genericAddressProgram)&&e.DrawIndexed({3}),
          "Generic atlas NPOT integer-only fallback record");
      std::cout<<"SDF ATLAS ADDRESS selection width="<<width<<" requested_fast="<<fast<<" effective_hot_fast="<<effectiveFast
          <<" mrt="<<mrt<<" generic_integer_fallback_probe="<<genericProbe<<std::endl;
      Require(e.End(false),"Atlas address frame end record");
      bool done=false;device.async_SubmitFrameCommands(e.GetCommandBuffer(),[&]{done=true;});
      const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
      while(!done){device.returnSystem.DrainCallbacks();Require(std::chrono::steady_clock::now()<deadline,"Atlas address frame timeout");
        if(!done)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));}
      std::array<std::vector<float>,4> actual;uint64_t errors=0;
      OnThread(device,[&]{for(size_t part=0;part<actual.size();++part)actual[part]=device.BackendDiagnostics()->ReadTexture(outputs[part]);
        errors=device.BackendDiagnostics()->ValidationErrorCount();});
      Require(!errors,"Atlas address Vulkan validation error");
      for(const auto& part:actual)Require(part.size()==caseCount*4,"Atlas helper MRT/single readback dimensions");
      for(size_t i=0;i<indices.size();++i) {
        const int32_t index=indices[i],divisor=int32_t(width),remainder=index%divisor,quotient=index/divisor;
        const bool quotientMatches=actual[0][i*4+1]==float(quotient)&&actual[0][i*4+2]==float(width)&&actual[0][i*4+3]==1;
        // Negative GLSL '%' operands are undefined, including a remainder of
        // zero for -1/257 observed on the legacy shader. Do not invent a range
        // or sign oracle for that domain; compare the measured variants below.
        const bool remainderMatches=index<0||actual[0][i*4]==float(remainder);
        if(index<0&&!fast&&!mrt)
          std::cout<<"SDF ATLAS ADDRESS legacy_undefined_remainder width="<<width<<" index="<<index
              <<" actual_remainder="<<actual[0][i*4]<<" actual_quotient="<<actual[0][i*4+1]
              <<" actual_remainder_fp32_bits="<<std::bit_cast<uint32_t>(actual[0][i*4])
              <<" actual_quotient_fp32_bits="<<std::bit_cast<uint32_t>(actual[0][i*4+1])
              <<" negative_texel_fetch=0 mathematical_remainder_oracle=0"<<std::endl;
        if(!quotientMatches||!remainderMatches) {
          std::cout<<"SDF ATLAS ADDRESS mismatch width="<<width<<" index="<<index<<" fast="<<fast<<" effective_hot_fast="<<effectiveFast<<" mrt="<<mrt
              <<" case="<<i<<" nonnegative="<<(index>=0)<<" actual_remainder="<<actual[0][i*4]
              <<" actual_quotient="<<actual[0][i*4+1]<<" actual_remainder_fp32_bits="<<std::bit_cast<uint32_t>(actual[0][i*4])
              <<" actual_quotient_fp32_bits="<<std::bit_cast<uint32_t>(actual[0][i*4+1])
              <<" cpu_remainder="<<remainder<<" cpu_quotient="<<quotient<<" cpu_remainder_fp32_bits="<<std::bit_cast<uint32_t>(float(remainder))
              <<" cpu_quotient_fp32_bits="<<std::bit_cast<uint32_t>(float(quotient))<<" legacy_available="<<!baseline[0].empty();
          if(!baseline[0].empty())std::cout<<" legacy_remainder="<<baseline[0][i*4]<<" legacy_quotient="<<baseline[0][i*4+1]
              <<" legacy_remainder_fp32_bits="<<std::bit_cast<uint32_t>(baseline[0][i*4])
              <<" legacy_quotient_fp32_bits="<<std::bit_cast<uint32_t>(baseline[0][i*4+1]);
          std::cout<<std::endl;
        }
        Require(quotientMatches,
            "Production atlas quotient differs from CPU, including maximum signed index");
        Require(remainderMatches,
            "Production nonnegative atlas remainder differs from CPU");
        for(size_t part=1;part<actual.size();++part) {
          glm::vec4 expected(0);
          if(cases[i].y)expected=part==1?source[0][uint32_t(index)]:part==2?
              glm::vec4(source[1][uint32_t(index)].x,0,0,1):source[2][uint32_t(index)];
          Require(std::memcmp(actual[part].data()+i*4,&expected,sizeof(expected))==0,
              "Production PSObject/PSMeshTexel/PSGeometry changed uploaded FP32 bits");
          sampledRecords+=cases[i].y!=0;
        }
        ++addressRecords;
      }
      if(!fast&&!mrt)baseline=actual;
      else for(size_t part=0;part<actual.size();++part) {
        Require(actual[part].size()==baseline[part].size(),"Atlas transport changed readback capacity");
        const bool exact=std::memcmp(actual[part].data(),baseline[part].data(),actual[part].size()*sizeof(float))==0;
        if(!exact)for(size_t at=0;at<actual[part].size();++at)
          if(std::bit_cast<uint32_t>(actual[part][at])!=std::bit_cast<uint32_t>(baseline[part][at])) {
            const int32_t index=indices[at/4];
            std::cout<<"SDF ATLAS ADDRESS variant_mismatch width="<<width<<" index="<<index<<" fast="<<fast<<" effective_hot_fast="<<effectiveFast<<" mrt="<<mrt
                <<" output="<<part<<" channel="<<at%4<<" negative_undefined_remainder_domain="<<(part==0&&index<0&&at%4==0)
                <<" before_fp32_bits="<<std::bit_cast<uint32_t>(baseline[part][at])
                <<" after_fp32_bits="<<std::bit_cast<uint32_t>(actual[part][at])<<std::endl;break;
          }
        Require(exact,"Fast/default atlas addressing or MRT/single transport changed exact GPU results");
      }
    }
  }
  std::cout<<"Production SDF atlas address contract passed: widths=1024,257,1031; boundary/last/negative/INT_MIN/INT_MAX, "
      "actual object/mesh/geometry FP32 fetches, single/MRT and both modes; address_records="<<addressRecords
      <<" sampled_helper_records="<<sampledRecords<<" negative_remainder_reference=legacy_gpu\n";
}
void ProbeSdfTraceSampleReuseContract(RHIDevice& device) {
  ResourceBuilder build(device);const auto mesh=build.FullscreenMesh();
  struct Case {
    const char* name;std::vector<glm::vec4> geometry;int nodes=1;
    glm::vec3 origin{0},direction{0,0,1};float minimum=0,maximum=8;
    bool found=true,exhausted=false;int triangle=0;float hit=2,safeFetches=5;
  };
  const auto triangle=[](std::vector<glm::vec4>& data,int offset,float x,float z) {
    data[offset]=glm::vec4(x-1,-1,z,0);data[offset+1]=glm::vec4(x+1,-1,z,0);
    data[offset+2]=glm::vec4(x,1,z,0);
  };
  const auto leaf=[&](const char* name,float z=2.f) {
    Case c;c.name=name;c.geometry.resize(7);c.geometry[0]=glm::vec4(-1,-1,z,0);
    c.geometry[1]=glm::vec4(1,1,z,-1);triangle(c.geometry,2,0,z);c.hit=z;return c;
  };
  const auto pair=[&](const char* name,float leftX,float leftZ,float rightX,float rightZ) {
    Case c;c.name=name;c.nodes=3;c.geometry.resize(16);
    c.geometry[0]=glm::vec4(std::min(leftX,rightX)-1,-1,std::min(leftZ,rightZ),1);
    c.geometry[1]=glm::vec4(std::max(leftX,rightX)+1,1,std::max(leftZ,rightZ),2);
    c.geometry[2]=glm::vec4(leftX-1,-1,leftZ,0);c.geometry[3]=glm::vec4(leftX+1,1,leftZ,-1);
    c.geometry[4]=glm::vec4(rightX-1,-1,rightZ,1);c.geometry[5]=glm::vec4(rightX+1,1,rightZ,-1);
    triangle(c.geometry,6,leftX,leftZ);triangle(c.geometry,11,rightX,rightZ);return c;
  };
  std::vector<Case> cases;
  cases.push_back(leaf("root_leaf_hit"));
  auto c=leaf("root_box_miss");c.origin.x=4;c.found=false;c.triangle=-1;c.hit=8;c.safeFetches=2;cases.push_back(c);
  c=leaf("leaf_triangle_miss");c.origin={.75f,.75f,0};c.found=false;c.triangle=-1;c.hit=8;cases.push_back(c);
  c=pair("left_only",0,2,4,4);c.safeFetches=11;cases.push_back(c);
  c=pair("right_only",-4,4,0,2);c.triangle=1;c.safeFetches=11;cases.push_back(c);
  c=pair("both_left_near_far_box_rejected",0,2,0,4);c.safeFetches=13;cases.push_back(c);
  c=pair("both_right_near_far_box_rejected",0,4,0,2);c.triangle=1;c.safeFetches=13;cases.push_back(c);
  c=pair("equal_entry_right_first_then_left_equal_hit",0,2,0,2);c.safeFetches=16;cases.push_back(c);
  c=pair("both_child_boxes_miss",-4,2,4,4);c.found=false;c.triangle=-1;c.hit=8;c.safeFetches=6;cases.push_back(c);
  c=leaf("parallel_inside_box_triangle_parallel");c.origin={0,-2,2};c.direction={0,1,0};c.found=false;c.triangle=-1;c.hit=8;cases.push_back(c);
  c=leaf("parallel_outside_box");c.origin={0,-2,3};c.direction={0,1,0};c.found=false;c.triangle=-1;c.hit=8;c.safeFetches=2;cases.push_back(c);
  c=leaf("minimum_clips_leaf");c.minimum=3;c.found=false;c.triangle=-1;c.hit=8;c.safeFetches=2;cases.push_back(c);
  c=leaf("hit_at_closed_maximum");c.maximum=2;cases.push_back(c);
  c=pair("far_box_still_overlaps_but_triangle_beyond_shrunk_max",0,4,0,2);
  c.geometry[2].z=1;c.geometry[3].z=5;c.geometry[4].z=1;c.geometry[5].z=5;
  c.geometry[0].z=1;c.geometry[1].z=5;c.triangle=1;c.safeFetches=16;cases.push_back(c);
  c=leaf("nested_immediate_children_and_two_far_siblings");c.nodes=5;c.geometry.assign(25,glm::vec4(0));
  c.geometry[0]={-1,-1,2,1};c.geometry[1]={1,1,6,2};
  c.geometry[2]={-1,-1,2,3};c.geometry[3]={1,1,4,4};
  c.geometry[4]={-1,-1,6,2};c.geometry[5]={1,1,6,-1};
  c.geometry[6]={-1,-1,2,0};c.geometry[7]={1,1,2,-1};
  c.geometry[8]={-1,-1,4,1};c.geometry[9]={1,1,4,-1};
  triangle(c.geometry,10,0,2);triangle(c.geometry,15,0,4);triangle(c.geometry,20,0,6);c.safeFetches=21;cases.push_back(c);
  c=leaf("disabled_geometry");c.nodes=0;c.found=false;c.triangle=-1;c.hit=8;c.safeFetches=0;cases.push_back(c);
  // Deliberately synthetic graphs, not valid scene BVHs. All indices and leaf
  // storage are valid; the self edge isolates the existing traversal guards.
  c=pair("synthetic_self_edge_visit_limit",0,2,4,4);c.geometry[0]={-1,-1,1,0};c.geometry[1]={5,1,6,2};
  c.found=false;c.exhausted=true;c.triangle=-1;c.hit=8;c.safeFetches=6.f*16384;cases.push_back(c);
  c=pair("synthetic_self_edge_pending_sibling_stack_limit",0,2,0,6);
  c.geometry[0]={-1,-1,1,0};c.geometry[1]={1,1,6,2};
  c.found=false;c.exhausted=true;c.triangle=-1;c.hit=8;c.safeFetches=62.f*6+2;cases.push_back(c);

  const auto createTexture=[&](const std::vector<glm::vec4>& pixels) {
    CreateRHITextureSpec desc;desc.width=1024;desc.height=1;desc.mipmaps=false;
    desc.textureDataStoreType=desc.textureUseType=RHITextureFormat::RGBA32F;desc.data=pixels.data();
    return build.Create<RHITextureSpec>([&](auto callback){device.async_CreateTexture(desc,callback);});
  };
  const auto meshTexture=createTexture(std::vector<glm::vec4>(1024,glm::vec4(.5f,0,0,1)));
  struct MetadataCase {RenderResourceHandle<RHITextureSpec> objects,geometry;glm::vec3 query,clamped;};
  std::vector<MetadataCase> metadata;
  for(size_t i=0;i<cases.size();++i) {
    const glm::vec3 scale=i%3==0?glm::vec3(1):i%3==1?glm::vec3(2):glm::vec3(2,1,.5f);
    const glm::vec3 translation(4,-2,1);
    const glm::vec3 local=i%4==0?glm::vec3(0):i%4==1?glm::vec3(3,0,0):
        i%4==2?glm::vec3(3,3,0):glm::vec3(0,0,-3);
    const float minimum=std::min({scale.x,scale.y,scale.z}),bias=float(i%3)*.125f;
    std::vector<glm::vec4> objects(1024,glm::vec4(0));
    glm::mat4 inverse(1);for(int axis=0;axis<3;++axis){inverse[axis][axis]=1.f/scale[axis];inverse[3][axis]=-translation[axis]/scale[axis];}
    for(int column=0;column<4;++column)objects[column]=inverse[column];
    objects[4]={-1,-1,-1,0};objects[5]={2,2,2,1};objects[6]=glm::vec4(2,2,2,minimum);
    objects[7]={-1,-1,-1,0};objects[8]={1,1,1,2};objects[9]=glm::vec4(.0625f,.0625f*minimum,0,0);
    objects[10]=glm::vec4(translation-scale,bias);objects[11]=glm::vec4(translation+scale,2);
    auto geometry=cases[i].geometry;geometry.resize(1024,glm::vec4(0));
    metadata.push_back({createTexture(objects),createTexture(geometry),translation+local*scale,
        glm::clamp(local,glm::vec3(-1),glm::vec3(1))});
  }
  struct alignas(16) Input {glm::vec4 origin,direction,interval,query;};
  const auto input=build.Uniform(sizeof(Input)),sdf=build.Uniform(sizeof(ProbeSdfTraceConstants));
  const uint32_t width=uint32_t(cases.size());
  std::array<RenderResourceHandle<RHITextureSpec>,4> outputs;
  for(auto& output:outputs)output=build.Texture(width,1,RHITextureFormat::RGBA32F);
  const auto target=build.TargetColors(outputs);
  const std::string vertex=R"(#version 450 core
layout(location=0) in vec2 p;void main(){gl_Position=vec4(p,0,1);})";
  const std::string body=R"(
layout(std140,binding=0) uniform ReuseCase {vec4 rcOrigin,rcDirection,rcInterval,rcQuery;};
layout(location=0) out vec4 result;layout(location=1) out vec4 normalFetches;
layout(location=2) out vec4 distances;layout(location=3) out vec4 sampled;
void main(){float maximum=rcInterval.y,fetches=0;int triangle;vec3 normal;bool exhausted;
    bool found=PSRefine(rcOrigin.xyz,rcDirection.xyz,rcInterval.x,maximum,triangle,normal,fetches,exhausted);
    result=vec4(found?1:0,maximum,float(triangle),exhausted?1:0);normalFetches=vec4(normal,fetches);
    float meshFetches=0,objectFetches=0;
    float meshDistance=PSMeshDistance(rcQuery.xyz,meshFetches),objectDistance;vec3 localPosition,clampedPosition;float raw;
#if PS_REUSE_TRACE_SAMPLES
    vec4 countsScale=PSObject(0,6),worldLowBias=PSObject(0,10),worldHigh=PSObject(0,11);
    objectDistance=PSObjectDistance(0,rcQuery.xyz,true,countsScale,worldLowBias,worldHigh,objectFetches);
    raw=PSMeshSample(0,rcQuery.xyz,countsScale,localPosition,clampedPosition);
#else
    objectDistance=PSObjectDistance(0,rcQuery.xyz,true,objectFetches);
    raw=PSMeshSample(0,rcQuery.xyz,localPosition,clampedPosition);
#endif
    distances=vec4(meshDistance,meshFetches,objectDistance,objectFetches);sampled=vec4(raw,clampedPosition);
})";
  size_t comparedChannels=0;uint64_t frameIndex=1200;
  for(unsigned fast=0;fast<2;++fast)for(unsigned safe=0;safe<2;++safe) {
    std::array<std::vector<float>,4> baseline;
    for(unsigned reuse=0;reuse<2;++reuse) {
      const auto program=build.FullscreenPipeline(vertex,std::string("#version 450 core\n")+
          ProbeSdfDetail::FallbackCommonGLSL(safe!=0,fast!=0,reuse!=0)+body);
      RHICommand::BeginFrame begin;begin.frameIndex=frameIndex++;begin.framebufferWidth=64;begin.framebufferHeight=32;
      auto e=device.BeginFrame(begin);
      Require(e.SetRenderTarget({target,width,1,RHICommand::ClearColor,glm::vec4(0)})&&e.BindMesh(mesh)&&e.BindPipeline(program)&&
          e.BindUniformBuffer(input,0)&&e.BindUniformBuffer(sdf,ProbeSdfTracingBinding)&&e.BindTexture(meshTexture,6),
          "Trace sample reuse helper bindings");
      for(size_t i=0;i<cases.size();++i) {
        const auto& item=cases[i];Input values{glm::vec4(item.origin,0),glm::vec4(item.direction,0),
            glm::vec4(item.minimum,item.maximum,float(i),0),glm::vec4(metadata[i].query,0)};
        ProbeSdfTraceConstants constants;constants.atlas={1024,1024,1,1};constants.geometry={item.nodes*2,1024,item.nodes,0};
        constants.flags.z=12;constants.trace.z=.0001f;
        const auto* bytes=reinterpret_cast<const std::byte*>(&constants);
        for(uint32_t offset=0;offset<sizeof(constants);offset+=RHICommand::MaxInlineUniformBytes) {
          const auto count=uint32_t(std::min<size_t>(RHICommand::MaxInlineUniformBytes,sizeof(constants)-offset));
          Require(e.UpdateUniformBufferBytes(sdf,bytes+offset,count,offset),"Reuse helper production UBO chunk");
        }
        Require(e.UpdateUniformBuffer(input,values)&&e.BindTexture(metadata[i].objects,7)&&e.BindTexture(metadata[i].geometry,9)&&
            e.SetViewport({int32_t(i),0,1,1})&&e.DrawIndexed({3}),"Trace sample reuse helper draw");
      }
      Require(e.End(false),"Trace sample reuse frame end");bool done=false;
      device.async_SubmitFrameCommands(e.GetCommandBuffer(),[&]{done=true;});
      const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
      while(!done){device.returnSystem.DrainCallbacks();Require(std::chrono::steady_clock::now()<deadline,"Trace sample reuse timeout");
        if(!done)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));}
      std::array<std::vector<float>,4> actual;uint64_t errors=0;
      OnThread(device,[&]{for(size_t part=0;part<outputs.size();++part)actual[part]=device.BackendDiagnostics()->ReadTexture(outputs[part]);
        errors=device.BackendDiagnostics()->ValidationErrorCount();});
      Require(!errors,"Trace sample reuse Vulkan validation error");
      for(const auto& part:actual)Require(part.size()==cases.size()*4,"Trace sample reuse MRT extent");
      for(size_t i=0;i<cases.size();++i) {
        const auto& item=cases[i];const auto at=i*4;
        if(safe)Require(actual[0][at]==float(item.found)&&actual[0][at+1]==item.hit&&
            actual[0][at+2]==float(item.triangle)&&actual[0][at+3]==float(item.exhausted)&&actual[1][at+3]==item.safeFetches,
            "Trace reuse baseline/candidate differs from expected BVH hit/order/logical fetch count");
        Require(actual[2][at+1]==8&&actual[2][at+3]==8&&std::isfinite(actual[2][at])&&std::isfinite(actual[2][at+2]),
            "Trace reuse metadata inside/outside did not execute both eight-tap samplers");
        const glm::vec4 expected(.5f,metadata[i].clamped);
        Require(std::memcmp(actual[3].data()+at,&expected,sizeof(expected))==0,"Trace reuse production sample/clamp FP32 oracle");
      }
      if(!reuse)baseline=actual;
      else for(size_t part=0;part<outputs.size();++part)for(size_t at=0;at<actual[part].size();++at) {
        const auto before=std::bit_cast<uint32_t>(baseline[part][at]),after=std::bit_cast<uint32_t>(actual[part][at]);
        if(before!=after)std::cout<<"SDF TRACE REUSE mismatch case="<<cases[at/4].name<<" fast="<<fast<<" safe="<<safe
            <<" output="<<part<<" channel="<<at%4<<" before_fp32_bits="<<before<<" after_fp32_bits="<<after<<std::endl;
        Require(before==after,"Production trace sample reuse changed strict FP32/helper metadata/logical fetches");++comparedChannels;
      }
    }
  }
  std::cout<<"Production SDF trace sample reuse passed: cases="<<cases.size()<<" variants=8 compared_fp32_channels="<<comparedChannels
      <<" metadata=inside,outside,uniform,nonuniform,thin_surface bvh=root,leaf,miss,left,right,both,near_shrink,tie,parallel,nested "
      "fetches=logical_requests_not_physical_reads\n";
}
#include "probe_native_contract.inl"
#include "surface_cache_acceleration_update_test.inl"
#include "surface_cache_closest_reuse_test.inl"
#include "surface_cache_receiver_lookup_test.inl"
#include "surface_cache_receiver_origin_test.inl"
int main(int argc, char **argv) {
  try {
    Require(RegisterVulkanBackend(), "Vulkan backend was not built");
    Require(glfwInit(), "GLFW initialization");
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    ApplicationWindow::Window window(
        {64, 32, "Vulkan RHI contract", false, BackendType::Vulkan});
    Require(window.Activate(), "Vulkan window");
    auto context = *window.GetRenderContextAsVulkan();
    context.validation = argc > 1 && std::string(argv[1]) == "--validation";
    RHIDevice device;
    device.Run(BackendType::Vulkan, context);
    DeferredTextureUploadContract(device);
    FrameComputeContract(device);
    UniformSnapshotContentContract(device);
    IndexedDrawRangeContract(device);
    ProbeSdfAtlasAddressContract(device);
    ProbeSdfTraceSampleReuseContract(device);
    ProbeNativeIntersectionContract(device);
    SurfaceCacheAccelerationUpdateContract(device);
    SurfaceCacheClosestReuseContract(device);
    SurfaceCacheReceiverLookupContract(device);
    SurfaceCacheReceiverOriginContract(device);
    {
      ResourceBuilder build(device);
      auto mesh = build.FullscreenMesh();
      auto u = build.Uniform(16);
      auto shader = build.FullscreenPipeline(R"(#version 450 core
layout(location=0) in vec2 p;layout(location=1) in vec2 uv;
void main(){gl_Position=vec4(p,-.5,1);}
)",
                                             R"(#version 450 core
layout(std140,binding=0) uniform Color {vec4 color;};layout(location=0) out vec4 o;
void main(){o=color;}
)",
                                             true);
      auto color = build.Texture(64, 32, RHITextureFormat::RGBA8);
      auto depth = build.Texture(64, 32, RHITextureFormat::Depth32F);
      auto target = build.Target(color, depth);
      auto mc = build.Texture(64, 32, RHITextureFormat::RGBA8, 4);
      auto md = build.Texture(64, 32, RHITextureFormat::Depth32F, 4);
      auto mt = build.Target(mc, md);
      float drawAlpha = 1;
      glm::vec4 clearColor(0);
      auto draw = [&](bool msaa, bool present) {
        auto e = device.BeginFrame({0, 64, 32});
        e.SetRenderTarget({msaa ? mt : target, 64, 32,
                           RHICommand::ClearColor | RHICommand::ClearDepth,
                           clearColor});
        e.BindMesh(mesh);
        e.BindPipeline(shader);
        e.BindUniformBuffer(u, 0);
        e.SetViewport({0, 0, 32, 32});
        e.UpdateUniformBuffer(u, glm::vec4(1, 0, 0, drawAlpha));
        e.DrawIndexed({3});
        e.SetViewport({32, 0, 32, 32});
        e.UpdateUniformBuffer(u, glm::vec4(0, 1, 0, drawAlpha));
        e.DrawIndexed({3});
        if (msaa)
          e.ResolveRenderTarget({mt, target, true, true});
        e.End(present);
        bool done = false;
        device.async_SubmitFrameCommands(e.GetCommandBuffer(),
                                         [&] { done = true; });
        while (!done) {
          device.returnSystem.DrainCallbacks();
          if (!done)
            device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));
        }
      };
      for (bool msaa : {false, true}) {
        draw(msaa, false);
        OnThread(device, [&] {
          auto c = device.BackendDiagnostics()->ReadTexture(color);
          auto z = device.BackendDiagnostics()->ReadTexture(depth);
          Require(c.size() == 64 * 32 * 4 && z.size() == 64 * 32,
                  "readback dimensions");
          Require(c[(16 * 64 + 16) * 4] > .99 &&
                      c[(16 * 64 + 48) * 4 + 1] > .99,
                  "per-draw uniform snapshots");
          Require(c[(16 * 64 + 16) * 4 + 1] < .01 &&
                      c[(16 * 64 + 48) * 4] < .01,
                  "color/viewport contracts");
          Require(std::abs(z[16 * 64 + 32] - .25) < 1e-5,
                  "OpenGL projection depth conversion / MSAA resolve");
        });
      }
      // Repeated bindings/identical updates can share descriptor and UBO
      // snapshots, but offsets, later mutations and texture changes cannot.
      auto rangedUniform = build.Uniform(512);
      auto whiteUniform = build.Uniform(16);
      auto sampledShader = build.FullscreenPipeline(
          R"(#version 450 core
layout(location=0) in vec2 p;void main(){gl_Position=vec4(p,0,1);}
)",
          R"(#version 450 core
layout(std140,binding=0) uniform Color {vec4 color;};
layout(binding=1) uniform sampler2D source;
layout(location=0) out vec4 o;void main(){o=color*texelFetch(source,ivec2(0),0);}
)");
      const auto texel = [&](std::array<uint8_t, 4> rgba) {
        CreateRHITextureSpec spec;
        spec.width = spec.height = 1;
        spec.mipmaps = false;
        spec.textureDataStoreType = spec.textureUseType = RHITextureFormat::RGBA8;
        spec.data = rgba.data();
        return build.Create<RHITextureSpec>(
            [&](auto cb) { device.async_CreateTexture(spec, cb); });
      };
      auto white = texel({255, 255, 255, 255});
      auto cyan = texel({0, 255, 255, 255});
      auto magenta = texel({255, 0, 255, 255});
      for (unsigned frame = 0; frame < 2; ++frame) {
        const glm::vec4 first = frame == 0 ? glm::vec4(1, 0, 0, 1)
                                           : glm::vec4(0, 1, 0, 1);
        const glm::vec4 offset = frame == 0 ? glm::vec4(0, 0, 1, 1)
                                            : glm::vec4(1, 0, 0, 1);
        const glm::vec4 changed = frame == 0 ? glm::vec4(0, 1, 0, 1)
                                             : glm::vec4(0, 0, 1, 1);
        const std::array<glm::vec4, 8> expected{
            first, offset, first, first, changed, offset,
            glm::vec4(0, 1, 1, 1), glm::vec4(1, 0, 1, 1)};
        auto e = device.BeginFrame({frame, 64, 32});
        e.SetRenderTarget({target, 64, 32, RHICommand::ClearColor,
                           glm::vec4(0)});
        e.BindMesh(mesh);
        e.BindPipeline(sampledShader);
        e.BindTexture(white, 1);
        e.UpdateUniformBuffer(rangedUniform, first);
        e.UpdateUniformBuffer(rangedUniform, offset, 256);
        e.UpdateUniformBuffer(whiteUniform, glm::vec4(1));
        for (unsigned slice = 0; slice < expected.size(); ++slice) {
          e.SetViewport({int32_t(slice * 8), 0, 8, 32});
          if (slice == 3)
            e.UpdateUniformBuffer(rangedUniform, first); // unchanged bytes
          if (slice == 4)
            e.UpdateUniformBuffer(rangedUniform, changed);
          e.BindUniformBuffer(slice < 6 ? rangedUniform : whiteUniform, 0,
                              slice == 1 || slice == 5 ? 256 : 0, 16);
          if (slice >= 6)
            e.BindTexture(slice == 6 ? cyan : magenta, 1);
          e.DrawIndexed({3});
        }
        e.End(false);
        bool done = false;
        device.async_SubmitFrameCommands(e.GetCommandBuffer(),
                                         [&] { done = true; });
        while (!done) {
          device.returnSystem.DrainCallbacks();
          if (!done)
            device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));
        }
        OnThread(device, [&] {
          auto pixels = device.BackendDiagnostics()->ReadTexture(color);
          for (size_t slice = 0; slice < expected.size(); ++slice)
            for (size_t channel = 0; channel < 4; ++channel)
              Require(std::abs(pixels[(16 * 64 + slice * 8 + 4) * 4 + channel] -
                                   expected[slice][channel]) < .01f,
                      "Reusable descriptors preserve UBO versions/ranges and texture bindings");
        });
      }
      PipelineSpec blendSpec;
      OnThread(device, [&] {
        blendSpec = *device.GetResourcePool().PipelineTable.Get(shader);
      });
      blendSpec.blendEnable = true;
      blendSpec.blendMode = BlendMode::Alpha;
      auto opaqueShader = shader;
      shader = build.Create<PipelineSpec>(
          [&](auto cb) { device.async_CreatePipeline({blendSpec}, cb); });
      drawAlpha = .5f;
      clearColor = {0, 0, 1, 1};
      draw(false, false);
      OnThread(device, [&] {
        auto c = device.BackendDiagnostics()->ReadTexture(color);
        auto at = (16 * 64 + 16) * 4;
        Require(std::abs(c[at] - .5f) < .01f &&
                    std::abs(c[at + 2] - .5f) < .01f &&
                    std::abs(c[at + 3] - .75f) < .01f,
                "OpenGL-compatible color/alpha blending");
      });
      blendSpec.blendMode = BlendMode::AlphaComposite;
      shader = build.Create<PipelineSpec>(
          [&](auto cb) { device.async_CreatePipeline({blendSpec}, cb); });
      draw(false, false);
      OnThread(device, [&] {
        auto c = device.BackendDiagnostics()->ReadTexture(color);
        auto at = (16 * 64 + 16) * 4;
        Require(std::abs(c[at] - .5f) < .01f &&
                    std::abs(c[at + 2] - .5f) < .01f &&
                    std::abs(c[at + 3] - 1.f) < .01f,
                "UI alpha compositing must preserve opaque destination coverage");
      });
      drawAlpha = 1;
      clearColor = glm::vec4(0);
      shader = opaqueShader;
      CreateGraphicShaderDesc stages;
      stages.vertexShaderSource =
          build.Source(ShaderSourceType::Vertex, R"(#version 450 core
layout(location=0) in vec2 p;void main(){gl_Position=vec4(p,-.5,1);})");
      stages.fragmentShaderSource =
          build.Source(ShaderSourceType::Fragment, R"(#version 450 core
layout(std140,binding=0) uniform Color {vec4 color;};layout(location=0) out vec4 o;void main(){o=color;})");
      stages.geometryShaderSource =
          build.Source(ShaderSourceType::Geometry, R"(#version 450 core
layout(triangles) in;layout(triangle_strip,max_vertices=3) out;
void main(){for(int i=0;i<3;++i){gl_Position=gl_in[i].gl_Position;EmitVertex();}EndPrimitive();})");
      auto program = build.Create<ShaderProgramSpec>(
          [&](auto cb) { device.async_CreateGraphicShader(stages, cb); });
      blendSpec.shaderProgram = program;
      blendSpec.blendEnable = false;
      shader = build.Create<PipelineSpec>(
          [&](auto cb) { device.async_CreatePipeline({blendSpec}, cb); });
      draw(false, false);
      OnThread(device, [&] {
        auto z = device.BackendDiagnostics()->ReadTexture(depth);
        Require(std::abs(z[16 * 64 + 32] - .25) < 1e-5,
                "Geometry output must convert projection depth exactly once");
      });
      shader = opaqueShader;
      for(unsigned frame=0;frame<200;++frame){glfwPollEvents();draw(frame%2==0,true);}
      OnThread(device,[&]{Require(!device.BackendDiagnostics()->ValidationErrorCount(),"Continuous presentation semaphore reuse failed");});
      ProbeTriangle t;
      t.a = {-1, -1, 0};
      t.b = {1, -1, 0};
      t.c = {0, 1, 0};
      auto scene = BuildRealtimeGiScene(std::span(&t, 1));
      Require(bool(scene), "ray scene");
      CreateRHITextureSpec td;
      td.width = scene->Width();
      td.height = scene->Height();
      td.mipmaps = false;
      td.textureDataStoreType = td.textureUseType = RHITextureFormat::RGBA32F;
      td.data = scene->Pixels().data();
      auto geom = build.Create<RHITextureSpec>(
          [&](auto cb) { device.async_CreateTexture(td, cb); });
      auto ru = build.Uniform(sizeof(RealtimeGiConstants));
      RealtimeGiConstants rc;
      rc.geometry = {int(scene->NodeCount() * 2), 1024, int(scene->NodeCount()),
                     1};
      auto rays = build.FullscreenPipeline(R"(#version 450 core
layout(location=0) in vec2 p;layout(location=1) in vec2 uv;void main(){gl_Position=vec4(p,0,1);}
)",
                                           std::string("#version 450 core\n") +
                                               RealtimeGiQueryGLSL() +
                                               RealtimeGiGeometryGLSL() + R"(
layout(location=0) out vec4 o;void main(){float d=10;vec3 n;int t;bool front;
bool hit=RtIntersect(vec3(0,0,2),vec3(0,0,-1),d,n,t,front);o=vec4(hit?d:0,float(t),front?1:0,1);}
)");
      auto rayColor = build.Texture(8, 8, RHITextureFormat::RGBA32F);
      auto rayTarget = build.Target(rayColor, {});
      auto e = device.BeginFrame({0, 64, 32});
      e.SetRenderTarget({rayTarget, 8, 8});
      e.BindPipeline(rays);
      e.BindMesh(mesh);
      e.BindTexture(geom, 0);
      e.UpdateUniformBuffer(ru, rc);
      e.BindUniformBuffer(ru, 15);
      e.DrawIndexed({3});
      e.End(true);
      bool done = false;
      device.async_SubmitFrameCommands(e.GetCommandBuffer(),
                                       [&] { done = true; });
      while (!done) {
        device.returnSystem.DrainCallbacks();
        if (!done)
          device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));
      }
      OnThread(device, [&] {
        auto pixels = device.BackendDiagnostics()->ReadTexture(rayColor);
        Require(std::abs(pixels[0] - 2) < 1e-5 && pixels[1] == 0 &&
                    pixels[2] == 1,
                "native ray query closest triangle/distance/front");
        Require(!device.BackendDiagnostics()->ValidationErrorCount(),
                "Vulkan validation error");
        std::cout << "Native ray query available="
                  << device.BackendDiagnostics()->HardwareRayTracingAvailable()
                  << "\n";
      });
      glfwSetWindowSize(window.GetNativeWindow(), 96, 48);
      glfwPollEvents();
      auto resized = device.BeginFrame({1, 96, 48});
      resized.End(true);
      bool resizedDone = false;
      device.async_SubmitFrameCommands(resized.GetCommandBuffer(),
                                       [&] { resizedDone = true; });
      while (!resizedDone) {
        device.returnSystem.DrainCallbacks();
        if (!resizedDone)
          device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));
      }
      OnThread(device, [&] {
        Require(device.BackendDiagnostics()->ReadWindow().size() == 96 * 48 * 4,
                "Window resize/readback contract");
        Require(!device.BackendDiagnostics()->ValidationErrorCount(),
                "Vulkan validation error after resize");
      });
      std::cout << "Vulkan RHI contract passed: indexed draw, UBO snapshots, "
                   "target/depth, alpha blend, geometry shader, resize, 4xMSAA "
                   "resolve, presentation, ray queries\n";
    }
    device.StopAndRelease();
    window.Shutdown();
    glfwTerminate();
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
