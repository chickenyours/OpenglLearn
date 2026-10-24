#include "ApplicationWindow/public/window.h"
#if defined(_WIN32)
extern "C" { __declspec(dllexport) unsigned long NvOptimusEnablement=1; }
#endif
#include "Render/Public/Compute/gpu_compute.h"
#include "Render/Public/RHICommand/FrameCommand/rhi_frame_encoder.h"
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Private/Pipeline/strand_native_oit_shaders.h"
#include "Render/Test/strand_native_oit_goldens.h"
#include "Render/Test/strand_native_oit_render_test.h"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstring>
#include <future>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace Render;
namespace Golden=Render::TestData::NativeOitGolden;
namespace {
void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
template<class F> auto OnThread(RHIDevice& device,F operation){using T=std::invoke_result_t<F>;
    auto promise=std::make_shared<std::promise<T>>();auto result=promise->get_future();
    device.async_ExecuteCode([promise,operation]{try{promise->set_value(operation());}catch(...){promise->set_exception(std::current_exception());}});return result.get();}
void Submit(RHIDevice& device,RHIFrameEncoder& encoder){Check(encoder.End(false),"End native OIT arithmetic frame");
    auto complete=std::make_shared<bool>(false);device.async_SubmitFrameCommands(encoder.GetCommandBuffer(),[complete]{*complete=true;});
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while(!*complete){device.returnSystem.DrainCallbacks();Check(std::chrono::steady_clock::now()<deadline,"Native OIT arithmetic GPU timeout");
        if(!*complete)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));}}
using Input=std::array<std::uint32_t,64>;
using Output=std::array<std::uint32_t,16>;
const char* AuditBody=R"GLSL(
layout(local_size_x=64) in;
layout(std430,set=2,binding=0) readonly buffer NativeAuditInput {uint nativeInput[];};
layout(std430,set=2,binding=1) writeonly buffer NativeAuditOutput {uint nativeOutput[];};
layout(push_constant) uniform NativeAuditParameters {uvec4 nativeAuditParameters;};
uint AuditWord(uint row,uint word){return nativeInput[row*64u+word];}
float AuditFloat(uint row,uint word){return uintBitsToFloat(AuditWord(row,word));}
void AuditStoreNode(uint row,uint word,uvec2 node){nativeOutput[row*16u+word]=node.x;nativeOutput[row*16u+word+1u]=node.y;}
void main(){
    uint row=gl_GlobalInvocationID.x;if(row>=nativeAuditParameters.x)return;
    for(uint i=0u;i<16u;++i)nativeOutput[row*16u+i]=0u;
    uint kind=AuditWord(row,0u);
    if(kind==0u){float alpha=AuditFloat(row,2u);vec3 shade=vec3(AuditFloat(row,3u),AuditFloat(row,4u),AuditFloat(row,5u));
        AuditStoreNode(row,0u,StrandNativePack(AuditFloat(row,1u),StrandNativeScale(alpha,shade),StrandNativeAdd(1.0,-alpha)));return;}
    if(kind==1u){AuditStoreNode(row,0u,StrandNativeMerge(uvec2(AuditWord(row,1u),AuditWord(row,2u)),
        uvec2(AuditWord(row,3u),AuditWord(row,4u))));return;}
    if(kind==2u){StrandNativeWuSegment s;
        s.p0=vec2(AuditFloat(row,1u),AuditFloat(row,2u));s.p1=vec2(AuditFloat(row,3u),AuditFloat(row,4u));
        s.width=vec2(AuditFloat(row,5u),AuditFloat(row,6u));s.reverseZ=vec2(AuditFloat(row,7u),AuditFloat(row,8u));
        s.shade0=vec3(AuditFloat(row,9u),AuditFloat(row,10u),AuditFloat(row,11u));
        s.shade1=vec3(AuditFloat(row,12u),AuditFloat(row,13u),AuditFloat(row,14u));
        ivec2 pixel=ivec2(AuditWord(row,16u),AuditWord(row,17u));vec4 ct;float z,t;
        bool accepted=StrandNativeWuSample(s,pixel,AuditFloat(row,15u),ct,z,t);uvec2 node;
        bool nodeAccepted=StrandNativeWuNode(s,pixel,AuditFloat(row,15u),node);
        nativeOutput[row*16u]=accepted?1u:0u;AuditStoreNode(row,1u,node);
        nativeOutput[row*16u+3u]=floatBitsToUint(ct.r);nativeOutput[row*16u+4u]=floatBitsToUint(ct.g);
        nativeOutput[row*16u+5u]=floatBitsToUint(ct.b);nativeOutput[row*16u+6u]=floatBitsToUint(ct.a);
        nativeOutput[row*16u+7u]=floatBitsToUint(z);nativeOutput[row*16u+8u]=floatBitsToUint(t);
        nativeOutput[row*16u+14u]=nodeAccepted?1u:0u;return;}
    if(kind==3u){uvec2 globalLayers[4];for(uint i=0u;i<4u;++i)globalLayers[i]=StrandNativeEmpty;
        uint offset=8u,closed=0u;uvec2 consumed=uvec2(0u);
        for(uint object=0u;object<2u;++object){uvec2 localLayers[8];for(uint i=0u;i<8u;++i)localLayers[i]=StrandNativeEmpty;
            uint count=AuditWord(row,1u),size=AuditWord(row,2u+object);vec3 c=vec3(0.0);float t=1.0,z=0.0;
            for(uint i=0u;i<size;++i){uvec2 node=uvec2(AuditWord(row,offset+i*2u),AuditWord(row,offset+i*2u+1u));
                consumed[object]++;StrandNativeInnerInsert(localLayers,count,node);
                if(AuditWord(row,6u)!=0u){StrandNativeResolve(localLayers,count,c,t,z);
                    if(t<=.01){t=0.0;closed|=1u<<object;break;}}}
            offset+=size*2u;if(AuditWord(row,6u)==0u)StrandNativeResolve(localLayers,count,c,t,z);
            if(t<=.01){t=0.0;closed|=1u<<object;}StrandNativeOpacity(c,t,AuditFloat(row,4u+object));
            if(t<1.0)StrandNativeGlobalInsert(globalLayers,StrandNativePack(z,c,t));
        }
        for(uint i=0u;i<4u;++i)AuditStoreNode(row,i*2u,globalLayers[i]);
        vec3 c;float t,z;StrandNativeGlobalResolve(globalLayers,c,t,z);
        nativeOutput[row*16u+8u]=floatBitsToUint(c.r);nativeOutput[row*16u+9u]=floatBitsToUint(c.g);
        nativeOutput[row*16u+10u]=floatBitsToUint(c.b);nativeOutput[row*16u+11u]=floatBitsToUint(t);
        nativeOutput[row*16u+12u]=floatBitsToUint(z);nativeOutput[row*16u+13u]=closed;
        nativeOutput[row*16u+14u]=consumed.x;nativeOutput[row*16u+15u]=consumed.y;
    }
    if(kind==4u){uvec2 globalLayers[4];for(uint i=0u;i<4u;++i)globalLayers[i]=StrandNativeEmpty;
        uint count=AuditWord(row,1u);for(uint i=0u;i<count;++i)StrandNativeGlobalInsert(globalLayers,
            uvec2(AuditWord(row,8u+i*2u),AuditWord(row,9u+i*2u)));
        for(uint i=0u;i<4u;++i)AuditStoreNode(row,i*2u,globalLayers[i]);vec3 c;float t,z;StrandNativeGlobalResolve(globalLayers,c,t,z);
        nativeOutput[row*16u+8u]=floatBitsToUint(c.r);nativeOutput[row*16u+9u]=floatBitsToUint(c.g);
        nativeOutput[row*16u+10u]=floatBitsToUint(c.b);nativeOutput[row*16u+11u]=floatBitsToUint(t);
        nativeOutput[row*16u+12u]=floatBitsToUint(z);nativeOutput[row*16u+14u]=count;
    }
}
)GLSL";
}
int main(int argc,char** argv){try{
    bool vulkan=false,validation=false;for(int i=1;i<argc;++i){vulkan|=std::string(argv[i])=="--vulkan";validation|=std::string(argv[i])=="--validation";}
    if(vulkan)Check(RegisterVulkanBackend(),"Vulkan unavailable for native OIT arithmetic");
    Check(glfwInit(),"GLFW initialization");glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    ApplicationWindow::Window window({16,16,"RHI native Wu and hierarchical packed OIT contract",false,vulkan?BackendType::Vulkan:BackendType::Opengl});
    Check(window.Activate(),"Native OIT test context");RHIDevice device;
    if(vulkan){auto context=*window.GetRenderContextAsVulkan();context.validation=validation;context.hardwareRayTracing=false;device.Run(BackendType::Vulkan,context);}
    else device.Run(BackendType::Opengl,*window.GetRenderContextAsOpengl());
    {
        std::vector<Input> input;std::vector<Output> expected;
        for(const auto& q:Golden::Packs){Input row{};std::copy(q.input.begin(),q.input.end(),row.begin()+1);input.push_back(row);
            Output out{};std::copy(q.node.begin(),q.node.end(),out.begin());expected.push_back(out);}
        for(const auto& q:Golden::Merges){Input row{};row[0]=1;row[1]=q.front[0];row[2]=q.front[1];row[3]=q.back[0];row[4]=q.back[1];input.push_back(row);
            Output out{};std::copy(q.node.begin(),q.node.end(),out.begin());expected.push_back(out);}
        for(const auto& q:Golden::WuCases)for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x){
            Input row{};row[0]=2;std::copy(q.input.begin(),q.input.end(),row.begin()+1);row[15]=q.opaque;row[16]=x;row[17]=y;input.push_back(row);
            Output out{};out[2]=0x3c00u;out[6]=std::bit_cast<std::uint32_t>(1.f);
            for(unsigned i=0;i<q.count;++i){const auto& d=Golden::Deposits[q.first+i];if(d.x!=int(x)||d.y!=int(y))continue;
                Check(out[0]==0,"SSA Wu fixture has two deposits at one segment/pixel");out[0]=out[14]=1;
                out[1]=d.node[0];out[2]=d.node[1];std::copy(d.values.begin(),d.values.end(),out.begin()+3);}
            expected.push_back(out);
        }
        for(const auto& q:Golden::Hierarchies){input.push_back(q.input);expected.push_back(q.expected);}
        auto compute=std::shared_ptr<Compute::Device>(Compute::Device::Borrow(device));
        Check(compute->Diagnostics().frameAvailable,"RHI FrameCompute unavailable for native OIT oracle");
        const auto source=std::string("#version 450 core\n")+PipelineDetail::StrandNativeOitMathGLSL()+AuditBody;
        Compute::FramePipelineDesc desc;desc.source=source;desc.label="native Wu packed hierarchical OIT SSA oracle";
        desc.storageBuffers=2;desc.pushConstantBytes=16;const auto pipeline=compute->CreateFramePipeline(desc);
        const auto in=compute->CreateStorageBuffer(input.size()*sizeof(Input),input.data(),"native OIT SSA inputs");
        const auto out=compute->CreateStorageBuffer(expected.size()*sizeof(Output),nullptr,"native OIT actual results");
        auto encoder=device.BeginFrame({0,16,16});Check(encoder.KeepAlive(compute),"Pin native OIT arithmetic resources");
        RHICommand::DispatchFrameCompute dispatch;dispatch.pipeline=pipeline;dispatch.buffers[0]=in;dispatch.buffers[1]=out;
        dispatch.groupsX=std::uint32_t((input.size()+63)/64);const std::array<std::uint32_t,4> constants{std::uint32_t(input.size()),0,0,0};
        dispatch.pushBytes=sizeof(constants);std::memcpy(dispatch.pushConstants.data(),constants.data(),sizeof(constants));
        Check(encoder.DispatchCompute(dispatch),"Record native Wu arithmetic kernel");Submit(device,encoder);
        const auto bytes=compute->Readback(out);Check(bytes.size()==expected.size()*sizeof(Output),"Native OIT SSBO readback size");
        std::vector<Output> actual(expected.size());std::memcpy(actual.data(),bytes.data(),bytes.size());
        unsigned matched=0;for(std::size_t i=0;i<actual.size();++i){
            for(unsigned word=0;word<16;++word)if(actual[i][word]!=expected[i][word]){
                std::cerr<<"Native OIT SSA mismatch case="<<i<<" kind="<<input[i][0]<<" word="<<word<<" actual=0x"<<std::hex<<actual[i][word]
                    <<" expected=0x"<<expected[i][word]<<std::dec<<'\n';throw std::runtime_error("Native OIT/Wu arithmetic differs from independent literal SSA golden");}
            ++matched;
        }
        Check(compute->Diagnostics().validationErrors==0&&OnThread(device,[&]{return device.BackendDiagnostics()->ValidationErrorCount();})==0,
            "Native OIT arithmetic validation error");
        compute->Destroy(in);compute->Destroy(out);compute->Destroy(pipeline);
        std::cout<<"RHI native OIT "<<(vulkan?"Vulkan":"OpenGL")<<": "<<matched<<" exact SSA cases ("<<Golden::Packs.size()<<" pack, "
            <<Golden::Merges.size()<<" full-DAG tail, "<<Golden::WuCases.size()<<" Wu x256pixels, "<<Golden::Hierarchies.size()
            <<" local4/8 -> object -> global4/tie/opacity/threshold) passed\n";
        TestData::RunNativeOitRenderTests(device);
    }
    device.StopAndRelease();return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
