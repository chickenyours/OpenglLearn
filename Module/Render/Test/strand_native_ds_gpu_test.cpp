#include "ApplicationWindow/public/window.h"
#include "Render/Public/Compute/gpu_compute.h"
#include "Render/Public/RHICommand/FrameCommand/rhi_frame_encoder.h"
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Test/strand_native_ds_math_test.h"
#include "Render/Test/strand_direct_shadow_test.h"
#include "Render/Test/directional_source_shadow_test.h"
#include "Render/Test/strand_opaque_shadow_power_test.h"
#include "Render/Test/strand_opacity_shadow_test.h"
#include "Render/Test/strand_roughness_test.h"
#include "Render/Test/strand_native_ds_query_test.h"
#include "Render/Test/strand_native_ds_producer_test.h"
#include "Render/Test/strand_native_ds_render_test.h"
#include <chrono>
#include <cmath>
#include <cstring>
#include <future>
#include <iostream>

#ifdef _WIN32
extern "C" { __declspec(dllexport) unsigned long NvOptimusEnablement=1; }
#endif

using namespace Render;
namespace {
void Check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
// Only outputs containing division (and its directly observed intermediates)
// have a cross-driver arithmetic tolerance. Quantized dose, Intpij, dimensions,
// rounded log2/LOD, comparisons, copy/mip and add/multiply-only values stay exact.
bool DivisionFloatOutput(std::uint32_t kind,unsigned word){
    return (kind==0&&word==0)||(kind==4&&word>=3&&word<=8)||
        (kind==5&&word==0)||(kind==6&&word<3);
}
std::uint64_t FloatUlpDistance(std::uint32_t a,std::uint32_t b){
    const auto ordered=[](std::uint32_t u){return (u&0x80000000u)?~u:(u|0x80000000u);};
    const std::uint64_t x=ordered(a),y=ordered(b);return x>y?x-y:y-x;
}
template<class F> auto OnThread(RHIDevice& device,F operation){using T=std::invoke_result_t<F>;
    auto promise=std::make_shared<std::promise<T>>();auto result=promise->get_future();
    device.async_ExecuteCode([promise,operation]{try{promise->set_value(operation());}catch(...){promise->set_exception(std::current_exception());}});return result.get();}
void Submit(RHIDevice& device,RHIFrameEncoder& encoder){Check(encoder.End(false),"End native DS arithmetic frame");
    auto complete=std::make_shared<bool>(false);device.async_SubmitFrameCommands(encoder.GetCommandBuffer(),[complete]{*complete=true;});
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while(!*complete){device.returnSystem.DrainCallbacks();Check(std::chrono::steady_clock::now()<deadline,"Native DS arithmetic GPU timeout");
        if(!*complete)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));}}
}
int main(int argc,char** argv){try{
    bool vulkan=false,validation=false,opacityOnly=false,roughnessOnly=false;for(int i=1;i<argc;++i){vulkan|=std::string(argv[i])=="--vulkan";validation|=std::string(argv[i])=="--validation";opacityOnly|=std::string(argv[i])=="--opacity-only";roughnessOnly|=std::string(argv[i])=="--roughness-only";}
    if(vulkan)Check(RegisterVulkanBackend(),"Vulkan unavailable for native DS arithmetic");
    Check(glfwInit(),"GLFW initialization");glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    ApplicationWindow::Window window({16,16,"RHI native Texture DSVolume source contract",false,vulkan?BackendType::Vulkan:BackendType::Opengl});
    Check(window.Activate(),"Native DS test context");RHIDevice device;
    if(vulkan){auto context=*window.GetRenderContextAsVulkan();context.validation=validation;context.hardwareRayTracing=false;device.Run(BackendType::Vulkan,context);}
    else device.Run(BackendType::Opengl,*window.GetRenderContextAsOpengl());
    if(roughnessOnly){
        TestData::RunStrandRoughnessGpuOracle(device);
        Check(OnThread(device,[&]{return device.BackendDiagnostics()->ValidationErrorCount();})==0,"Source roughness backend validation errors");
        device.StopAndRelease();return 0;
    }
    if(opacityOnly){
        TestData::RunStrandOpacityShadowGpuOracle(device);
        Check(OnThread(device,[&]{return device.BackendDiagnostics()->ValidationErrorCount();})==0,"Deep opacity shadow backend validation errors");
        device.StopAndRelease();return 0;
    }
    {
        using namespace TestData::NativeDsAudit;namespace Golden=TestData::NativeDsGolden;
        std::vector<Input> input;for(const auto& q:Golden::Rows)input.push_back(q.input);
        auto compute=std::shared_ptr<Compute::Device>(Compute::Device::Borrow(device));
        Check(compute->Diagnostics().frameAvailable,"RHI FrameCompute unavailable for native DS oracle");
        Compute::FramePipelineDesc desc;desc.source=Shader();desc.label="native Texture DSVolume literal SSA math oracle";
        desc.storageBuffers=2;desc.pushConstantBytes=16;const auto pipeline=compute->CreateFramePipeline(desc);
        const auto source=compute->CreateStorageBuffer(input.size()*sizeof(Input),input.data(),"native DS SSA inputs");
        const auto destination=compute->CreateStorageBuffer(input.size()*sizeof(Output),nullptr,"native DS actual arithmetic");
        auto encoder=device.BeginFrame({0,16,16});Check(encoder.KeepAlive(compute),"Pin native DS arithmetic resources");
        RHICommand::DispatchFrameCompute dispatch;dispatch.pipeline=pipeline;dispatch.buffers[0]=source;dispatch.buffers[1]=destination;
        dispatch.groupsX=std::uint32_t((input.size()+63)/64);const std::array<std::uint32_t,4> constants{std::uint32_t(input.size()),0,0,0};
        dispatch.pushBytes=sizeof(constants);std::memcpy(dispatch.pushConstants.data(),constants.data(),sizeof(constants));
        Check(encoder.DispatchCompute(dispatch),"Record native DS arithmetic kernel");Submit(device,encoder);
        const auto bytes=compute->Readback(destination);Check(bytes.size()==input.size()*sizeof(Output),"Native DS SSBO readback size");
        std::vector<Output> actual(input.size());std::memcpy(actual.data(),bytes.data(),bytes.size());
        std::uint64_t floatOutputs=0,floatNonExact=0,maxFloatUlps=0;double maxFloatRelative=0;
        for(std::size_t row=0;row<actual.size();++row)for(unsigned word=0;word<16;++word){
            const auto expected=Golden::Rows[row].expected[word],observed=actual[row][word];
            // GLSL roundEven may preserve -0; source +0 has the same numeric
            // rounded LOD and exact unsigned level0. Accept only this zero sign.
            if(input[row][0]==5u&&word==1u&&F(observed)==0.f&&F(expected)==0.f)continue;
            const bool arithmetic=DivisionFloatOutput(input[row][0],word);bool accepted=observed==expected;
            if(arithmetic){
                ++floatOutputs;const double a=F(observed),e=F(expected);const auto ulps=FloatUlpDistance(observed,expected);
                const double relative=std::abs(a-e)/std::max(std::abs(e),1e-30);
                maxFloatUlps=std::max(maxFloatUlps,ulps);maxFloatRelative=std::max(maxFloatRelative,relative);
                if(observed!=expected)++floatNonExact;
                accepted=std::isfinite(a)&&std::isfinite(e)&&(accepted||ulps<=2||relative<=2e-6);
            }
            if(!accepted){
                std::cerr<<"Native DS SSA mismatch row="<<row<<" kind="<<input[row][0]<<" word="<<word<<" actual=0x"<<std::hex<<observed
                    <<" expected=0x"<<expected<<std::dec<<" comparison="<<(arithmetic?"float <=2ULP or <=2e-6 relative":"strict exact")<<'\n';
                throw std::runtime_error("Native DS GPU arithmetic or exact quantization differs from source SSA golden");
            }
        }
        Check(compute->Diagnostics().validationErrors==0&&OnThread(device,[&]{return device.BackendDiagnostics()->ValidationErrorCount();})==0,
            "Native DS arithmetic validation error");
        compute->Destroy(source);compute->Destroy(destination);compute->Destroy(pipeline);
        std::cout<<"RHI native Texture DS "<<(vulkan?"Vulkan":"OpenGL")<<": "<<actual.size()
            <<" source SSA math rows passed; quantized integers/LOD/copy/mip/add-mul strict exact; division float outputs="<<floatOutputs
            <<" nonexact="<<floatNonExact<<" maxULP="<<maxFloatUlps<<" maxRelative="<<maxFloatRelative
            <<" (limit <=2ULP OR <=2e-6 relative; not GPU bit-exact floating SSA)\n";
        TestData::RunNativeDsQueryGpuOracle(device);
        TestData::RunNativeDsProducerGpuOracle(device);
        TestData::RunNativeDsRenderGpuOracle(device);
        TestData::RunStrandDirectShadowGpuOracle(device);
        TestData::RunDirectionalSourceShadowGpuOracle(device);
        TestData::RunStrandOpaqueShadowPowerGpuOracle(device);
        TestData::RunStrandOpacityShadowGpuOracle(device);
        TestData::RunStrandRoughnessGpuOracle(device);
        Check(OnThread(device,[&]{return device.BackendDiagnostics()->ValidationErrorCount();})==0,
            "Native DS complete suite generated backend validation errors");
    }
    device.StopAndRelease();return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
