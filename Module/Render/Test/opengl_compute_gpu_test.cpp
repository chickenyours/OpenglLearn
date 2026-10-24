#include "ApplicationWindow/public/window.h"
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Private/Pipeline/post_process_shaders.h"
#include "Render/Public/Compute/gpu_compute.h"
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <future>
#include <iostream>
#include <stdexcept>
#if defined(_WIN32)
extern "C" { __declspec(dllexport) unsigned long NvOptimusEnablement=1; }
#endif
using namespace Render;
using namespace Render::PipelineDetail;
namespace {
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> void Reject(F&& action,const char* message){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}Check(rejected,message);}
template<class F> auto OnThread(RHIDevice& device,F action) {
    using T=std::invoke_result_t<F>;auto done=std::make_shared<std::promise<T>>();auto value=done->get_future();
    device.async_ExecuteCode([done,action]{try{if constexpr(std::is_void_v<T>){action();done->set_value();}else done->set_value(action());}catch(...){done->set_exception(std::current_exception());}});
    return value.get();
}
void Submit(RHIDevice& device,RHIFrameEncoder& encoder) {
    Check(encoder.End(false),"End GL compute frame");bool done=false;
    Check(device.async_SubmitFrameCommands(encoder.GetCommandBuffer(),[&]{done=true;}),"Submit GL compute frame");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while(!done){device.returnSystem.DrainCallbacks();Check(std::chrono::steady_clock::now()<deadline,"GL compute frame timeout");
        if(!done)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));}
}
void Standalone(RHIDevice& device) {
    auto compute=Compute::Device::Borrow(device);const auto initial=compute->Diagnostics();
    Check(initial.available&&initial.frameAvailable&&initial.maxStorageBindings>=2,"OpenGL Borrow compute unavailable");
    std::array<uint32_t,256> input{};for(uint32_t i=0;i<input.size();++i)input[i]=i;
    auto a=compute->CreateStorageBuffer(sizeof(input),input.data(),"standalone input");
    auto b=compute->CreateStorageBuffer(sizeof(input),nullptr,"standalone intermediate");
    const std::string prefix=R"GLSL(#version 450 core
layout(local_size_x=64) in;
layout(std430,set=0,binding=0) buffer A {uint a[];};
layout(std430,set=0,binding=1) buffer B {uint b[];};
layout(push_constant) uniform Params {uint n;} p;
)GLSL";
    auto add=compute->CreatePipeline(prefix+"void main(){uint i=gl_GlobalInvocationID.x;b[i]=a[i]+p.n;}",2,4,"GL add");
    auto multiply=compute->CreatePipeline(prefix+"void main(){uint i=gl_GlobalInvocationID.x;a[i]=b[i]*p.n;}",2,4,"GL multiply");
    Compute::Dispatch first{add,{{0,a,Compute::Access::ReadOnly},{1,b,Compute::Access::WriteOnly}},4};
    Compute::Dispatch second{multiply,{{0,a,Compute::Access::WriteOnly},{1,b,Compute::Access::ReadOnly}},4};
    const uint32_t plus=7,factor=3;first.pushConstants.resize(4);second.pushConstants.resize(4);
    std::memcpy(first.pushConstants.data(),&plus,4);std::memcpy(second.pushConstants.data(),&factor,4);
    compute->Submit({{first,second}});const auto bytes=compute->Readback(a);
    for(size_t i=0;i<input.size();++i){uint32_t value=0;std::memcpy(&value,bytes.data()+i*4,4);Check(value==(input[i]+plus)*factor,"GL dependent compute kernels/readback");}
    uint32_t replacement=99;compute->Upload(b,&replacement,4,12);auto partial=compute->Readback(b,3,13);
    Check(std::memcmp(partial.data(),reinterpret_cast<const std::byte*>(&replacement)+1,3)==0,"GL byte-range upload/readback");
    auto broken=first;broken.bindings.pop_back();Reject([&]{compute->Submit({{broken}});},"GL missing descriptors accepted");
    broken=first;broken.bindings[0].buffer.device+=1;Reject([&]{compute->Submit({{broken}});},"GL foreign buffer accepted");
    broken=first;broken.groupsX=0;Reject([&]{compute->Submit({{broken}});},"GL zero dispatch accepted");
    Reject([&]{compute->Readback(a,sizeof(input)+1);},"GL oversized readback accepted");
    const auto live=compute->Diagnostics();Check(live.submittedBatches==initial.submittedBatches+1&&live.dispatchedKernels==initial.dispatchedKernels+2,"Rejected GL batches changed statistics");
    Reject([&]{compute->CreatePipeline(R"GLSL(#version 450 core
layout(local_size_x=1) in;layout(std430,set=0,binding=0) buffer A{uint words[];};
layout(push_constant) uniform P{vec2 unsupported[2];}p;void main(){words[0]=uint(p.unsupported[0].x);}
)GLSL",1,32,"unsupported push array");},"GL silently changed push-array layout");
    compute->Destroy(add);compute->Destroy(multiply);compute->Destroy(a);compute->Destroy(b);
    Reject([&]{compute->Readback(a);},"GL stale buffer accepted");
    auto final=compute->Diagnostics();Check(final.liveBuffers==initial.liveBuffers&&final.livePipelines==initial.livePipelines&&
        final.allocatedBytes==initial.allocatedBytes&&!final.validationErrors,"GL standalone resource leak/validation error");
    std::cout<<"OpenGL standalone compute: two dependent kernels, push scalar, byte ranges, validation and release passed\n";
}
void InFrame(RHIDevice& device) {
    ResourceBuilder builder(device);auto compute=std::shared_ptr<Compute::Device>(Compute::Device::Borrow(device));
    const auto initial=compute->Diagnostics(); // Backend counters include prior standalone fixtures.
    const auto control=compute->CreateStorageBuffer(64,nullptr,"GL frame control");
    const std::array<glm::vec4,8> pixels{glm::vec4(.125f,.25f,.5f,1),glm::vec4(.125f,.25f,.5f,1),
        glm::vec4(.125f,.25f,.5f,1),glm::vec4(.125f,.25f,.5f,1),glm::vec4(.125f,.25f,.5f,1),glm::vec4(.125f,.25f,.5f,1),
        glm::vec4(.125f,.25f,.5f,1),glm::vec4(.125f,.25f,.5f,1)};
    CreateRHITextureSpec upload;upload.width=8;upload.height=1;upload.mipmaps=false;
    upload.textureDataStoreType=upload.textureUseType=RHITextureFormat::RGBA32F;upload.data=pixels.data();
    auto source=builder.Create<RHITextureSpec>([&](auto callback){device.async_CreateTexture(upload,callback);});
    auto first=builder.StorageTexture(8,1,RHITextureFormat::RGBA32F),second=builder.StorageTexture(8,1,RHITextureFormat::RGBA32F);
    auto uniform=builder.Uniform(16),graphicsUniform=builder.Uniform(16);
    auto color=builder.Texture(16,16,RHITextureFormat::RGBA32F);auto target=builder.Target(color,{});
    auto graphics=builder.FullscreenPipeline(FullscreenVertexShader(),R"GLSL(#version 450 core
layout(std140,binding=15) uniform GraphicsColor{vec4 fill;};layout(location=0) out vec4 result;
void main(){result=fill;})GLSL");auto triangle=builder.FullscreenMesh();auto owner=builder.Finish();
    Compute::FramePipelineDesc reset;reset.label="GL frame reset";reset.storageBuffers=1;reset.pushConstantBytes=4;
    reset.source=R"GLSL(#version 450 core
layout(local_size_x=1) in;layout(std430,set=2,binding=0) buffer C{uint w[];};
layout(push_constant) uniform P{uint groups;}p;
void main(){w[0]=0u;w[1]=p.groups;w[2]=1u;w[3]=1u;})GLSL";
    const auto resetProgram=compute->CreateFramePipeline(reset);
    auto conflict=reset;conflict.uniformMask=1u<<15;Reject([&]{compute->CreateFramePipeline(conflict);},"GL UBO15 push collision accepted");
    Compute::FramePipelineDesc pass;pass.label="GL frame image chain";pass.uniformMask=1;pass.sampledMask=1;pass.storageBuffers=1;pass.storageImages=1;
    pass.source=R"GLSL(#version 450 core
layout(local_size_x=8) in;layout(std140,set=0,binding=0) uniform P{vec4 add;};
layout(set=1,binding=0) uniform sampler2D source;
layout(std430,set=2,binding=0) buffer C{uint w[];};
layout(rgba32f,set=3,binding=0) uniform writeonly image2D result;
void main(){int i=int(gl_GlobalInvocationID.x);imageStore(result,ivec2(i,0),texelFetch(source,ivec2(i,0),0)+add);atomicAdd(w[0],1u);})GLSL";
    const auto program=compute->CreateFramePipeline(pass);
    GLuint sentinel=0;GLuint sentinelImage=0;
    OnThread(device,[&]{glGenBuffers(1,&sentinel);glBindBuffer(GL_SHADER_STORAGE_BUFFER,sentinel);glBufferData(GL_SHADER_STORAGE_BUFFER,64,nullptr,GL_DYNAMIC_COPY);
        glBindBufferRange(GL_SHADER_STORAGE_BUFFER,0,sentinel,0,16);glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER,sentinel);
        sentinelImage=device.GetResourcePool().TextureTable.Get(source)->rhi_id;
        glBindImageTexture(0,sentinelImage,0,GL_FALSE,0,GL_READ_ONLY,GL_RGBA32F);});
    const auto frame=[&](uint32_t groups,bool initialize) {
        auto encoder=device.BeginFrame({0,16,16});Check(encoder.KeepAlive(owner)&&encoder.KeepAlive(compute),"Pin GL compute resources");
        const glm::vec4 fill(.2f,.3f,.4f,.5f);
        Check(encoder.SetRenderTarget({target,16,16,RHICommand::ClearColor})&&encoder.BindPipeline(graphics)&&encoder.BindMesh(triangle)&&
            encoder.UpdateUniformBuffer(graphicsUniform,fill)&&encoder.BindUniformBuffer(graphicsUniform,15)&&encoder.DrawIndexed({3}),"Draw before GL compute");
        RHICommand::DispatchFrameCompute command;command.pipeline=resetProgram;command.buffers[0]=control;command.pushBytes=4;
        std::memcpy(command.pushConstants.data(),&groups,4);Check(encoder.DispatchCompute(command),"Record GL push reset");
        Check(encoder.BindUniformBuffer(uniform,0),"Bind GL compute uniform");
        if(initialize) {
            Check(encoder.UpdateUniformBuffer(uniform,glm::vec4(.5f))&&encoder.BindTexture(source,0),"Bind GL compute source");
            command={};command.pipeline=program;command.buffers[0]=control;command.images[0]=first;
            Check(encoder.DispatchCompute(command),"Record first GL image store");
        }
        Check(encoder.UpdateUniformBuffer(uniform,glm::vec4(.25f))&&encoder.BindTexture(first,0),"Bind GL compute chained image");
        command={};command.pipeline=program;command.buffers[0]=control;command.images[0]=second;command.indirect=control;command.indirectOffset=4;
        Check(encoder.DispatchCompute(command),"Record GL indirect dispatch");
        // No rebind: the exact same graphics pipeline and UBO15 must survive
        // the compute program/push-constant binding and shader image writes.
        Check(encoder.DrawIndexed({3}),"Draw after GL compute");Submit(device,encoder);
        OnThread(device,[&]{const auto words=device.BackendDiagnostics()->CompletedComputeWords(control,4);
            Check(words[0]==(initialize?16u:0u)&&words[1]==groups,"GL indirect/zero dispatch or shader barrier failed");
            const auto output=device.BackendDiagnostics()->ReadTexture(color);for(size_t i=0;i<output.size();++i)
                Check(std::abs(output[i]-fill[i%4])<1e-6f,"GL compute polluted following graphics program/UBO15");
            GLint buffer=0,image=0,indirect=0,access=0;GLint64 start=0,size=0;
            glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,0,&buffer);glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_START,0,&start);glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_SIZE,0,&size);
            glGetIntegeri_v(GL_IMAGE_BINDING_NAME,0,&image);glGetIntegeri_v(GL_IMAGE_BINDING_ACCESS,0,&access);glGetIntegerv(GL_DISPATCH_INDIRECT_BUFFER_BINDING,&indirect);
            Check(GLuint(buffer)==sentinel&&start==0&&size==16&&GLuint(image)==sentinelImage&&access==GL_READ_ONLY&&GLuint(indirect)==sentinel,
                "GL compute polluted storage/image/indirect bindings");});
    };
    frame(1,true);frame(0,false);
    OnThread(device,[&]{const auto actual=device.BackendDiagnostics()->ReadTexture(second);const glm::vec4 expected(.875f,1.f,1.25f,1.75f);
        for(size_t i=0;i<actual.size();++i)Check(actual[i]==expected[i%4],"GL compute UBO/image-to-sampler dependency failed");
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,0);glBindBuffer(GL_SHADER_STORAGE_BUFFER,0);glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER,0);
        glBindImageTexture(0,0,0,GL_FALSE,0,GL_READ_ONLY,GL_RGBA32F);glDeleteBuffers(1,&sentinel);});
    const auto live=compute->Diagnostics();
    Check(live.submittedBatches==initial.submittedBatches&&live.dispatchedKernels==initial.dispatchedKernels+5,
        "GL frame compute made standalone submissions or lost dispatch statistics");
    auto pinned=std::shared_ptr<Compute::Device>(Compute::Device::Borrow(device));const auto transient=pinned->CreateStorageBuffer(64,nullptr,"GL frame owner pin");
    auto encoder=device.BeginFrame({0,16,16});Check(encoder.KeepAlive(pinned),"Pin transient GL compute owner");
    RHICommand::DispatchFrameCompute command;command.pipeline=resetProgram;command.buffers[0]=transient;command.pushBytes=4;
    Check(encoder.DispatchCompute(command),"Record pinned GL compute");pinned.reset();Submit(device,encoder);
    const auto retired=compute->Diagnostics();
    Check(retired.liveBuffers==live.liveBuffers&&retired.livePipelines==live.livePipelines&&retired.allocatedBytes==live.allocatedBytes,
        "GL frame owner retirement leaked/deadlocked");
    Check(retired.submittedBatches==initial.submittedBatches,"Pinned GL frame compute made standalone submissions");
    std::cout<<"OpenGL frame compute: UBO snapshots, images, direct/indirect/zero dispatch, graphics state and owner retirement passed\n";
}
}
int main(){try {
    Check(glfwInit(),"GLFW initialization");glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    ApplicationWindow::Window window({32,32,"OpenGL compute RHI regression",false,BackendType::Opengl});Check(window.Activate(),"OpenGL compute window");
    RHIDevice device;device.Run(BackendType::Opengl,*window.GetRenderContextAsOpengl());
    Standalone(device);InFrame(device);
    Check(OnThread(device,[&]{return device.BackendDiagnostics()->ValidationErrorCount();})==0,"OpenGL compute validation error");
    device.StopAndRelease();std::cout<<"OpenGL compute RHI regression passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
