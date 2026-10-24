#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <glad/glad.h>
#include "ApplicationWindow/public/window.h"
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Public/Material/eye_shading.h"

#if defined(_WIN32)
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

namespace {
using namespace Render;
using namespace std::chrono_literals;
template<class T> struct Result { T value{}; bool done=false; };
void Check(bool value,const std::string& message) { if(!value)throw std::runtime_error(message); }
struct alignas(16) TestInputs { glm::vec4 view{0,0,1,0}; glm::vec4 uv{.55f,.5f,0,0}; };
class Fixture {
public:
    explicit Fixture(OpenglBackendContext context) { device_.Run(BackendType::Opengl,std::move(context)); }
    ~Fixture() { Shutdown(); }
    void Shutdown() { if(device_.IsRunning()) { owner_.reset();device_.StopAndRelease();device_.returnSystem.DrainCallbacks(); } }
    void Run() {
        Material::MaterialTemplateDesc description;
        description.name="EyeHelperTest";
        description.parameters={{"ior",Material::ParameterType::Float,1.4f,"IOR","Eye",1.f,2.5f}};
        Material::AppendEyeMaterialParameters(description.parameters);
        schema_=Material::MaterialTemplate::Create(std::move(description)); Check(bool(schema_),"Invalid eye schema");
        PipelineDetail::ResourceBuilder builder(device_);
        const std::string vertex=R"GLSL(#version 450 core
layout(location=0) in vec2 position;
void main(){gl_Position=vec4(position,0,1);}
)GLSL";
        std::string fragment="#version 450 core\nlayout(binding=5)uniform sampler2D packedMap;\n"
            "layout(std140,binding=0)uniform TestInputs{vec4 testView;vec4 testUV;};\n"
            "layout(location=0)out vec4 outColor;\n";
        fragment+=schema_->GenerateGLSLUniformBlock();fragment+=Material::EyeShadingGLSL();
        fragment+=R"GLSL(
void main(){
    EyeCoordinates eye=EyePrepareCoordinates(testUV.xy,testView.xyz,packedMap);
    outColor=vec4(eye.irisUV,EyeDielectricF0(eye).r,1);
}
)GLSL";
        pipeline_=builder.FullscreenPipeline(vertex,fragment);mesh_=builder.FullscreenMesh();
        inputs_=builder.Uniform(sizeof(TestInputs));material_=builder.Uniform(schema_->ByteSize());
        output_=builder.Texture(4,4,RHITextureFormat::RGBA32F);target_=builder.Target(output_,{});
        const std::array<float,4> texel{.5f,1,.5f,.5f};
        CreateRHITextureSpec texture;texture.width=texture.height=1;
        texture.textureDataStoreType=texture.textureUseType=RHITextureFormat::RGBA32F;
        texture.mipmaps=false;texture.filterMode=RHIFilterMode::Nearest;texture.addressMode=RHIAddressMode::ClampToEdge;texture.data=texel.data();
        packed_=builder.Create<RHITextureSpec>([&](auto callback){device_.async_CreateTexture(texture,callback);});
        owner_=builder.Finish();
        auto driver=std::make_shared<Result<bool>>();
        device_.async_ExecuteCode([] {
            const auto* vendor=glGetString(GL_VENDOR);const auto* renderer=glGetString(GL_RENDERER);
            std::cout<<"Eye GPU: "<<(vendor?reinterpret_cast<const char*>(vendor):"unknown")<<" / "
                <<(renderer?reinterpret_cast<const char*>(renderer):"unknown")<<'\n';
        },[driver]{driver->done=true;});
        Wait([driver]{return driver->done;});
        const auto front=Draw(true,1,{0,0,1},1.4f);
        const auto off=Draw(true,0,{.6f,0,.8f},1.4f);
        Check(std::abs(front.x-off.x)<1e-6&&std::abs(front.y-off.y)<1e-6,"Zero refraction must remove view-dependent iris displacement");
        const auto oblique=Draw(true,1,{.6f,0,.8f},1.4f);
        const auto opposite=Draw(true,1,{-.6f,0,.8f},1.4f);
        Check(oblique.x<front.x-.05f&&opposite.x>front.x+.05f,"Oblique view does not move the iris in the expected directions");
        // Independent closed form for this constant-depth fixture: the ray
        // crosses at t=1-(.5*.2)=.9; no loop transcription is used here.
        const double q=.1/.56,angular=.36*.6*std::pow(1-q,2)*.4;
        const double expected=.5+.5*.83*(q-angular-.9*.44*.6);
        Check(std::abs(oblique.x-expected)<3e-5,"GPU iris intersection differs from constant-height analytic ray intersection");
        for(float strength:{0.f,1.f,2.f}) {
            const auto legacy=Draw(false,strength,{.99f,0,.01f},1.4f);
            Check(std::abs(legacy.x-.55f)<1e-6&&std::abs(legacy.y-.5f)<1e-6,"Inactive eye helper changed ordinary UVs");
        }
        const auto unity=Draw(true,1,{0,0,1},1.f),source=Draw(true,1,{0,0,1},1.4f),high=Draw(true,1,{0,0,1},2.5f);
        Check(unity.z<1e-7&&std::abs(source.z-.03f)<1e-5&&high.z>source.z&&high.z<=1,"Eye IOR normal reflectance is not finite / monotonic / source-calibrated");
        for(float index:{1.f,1.4f,2.5f})for(glm::vec3 angle:{glm::vec3(0,0,1),glm::vec3(.999f,0,.001f),glm::vec3(0,.999f,.001f)})Draw(true,1,angle,index);
        std::cout<<"Eye material GPU test passed: source constant-height intersection, angle shift, zero strength, inactive UV and finite IOR controls\n";
    }
private:
    template<class Predicate>void Wait(Predicate done) {
        const auto end=std::chrono::steady_clock::now()+10s;
        while(!done()){device_.returnSystem.DrainCallbacks();Check(std::chrono::steady_clock::now()<end,"Eye GPU callback timeout");if(!done()){glfwPollEvents();device_.returnSystem.WaitForCallbacks(5ms);}}
    }
    glm::vec3 Draw(bool enabled,float strength,glm::vec3 view,float index) {
        Material::ParameterBlock material(schema_);
        const auto set=[&](const char* name,const auto& value){std::string error;Check(material.Set(name,value,&error),std::string(name)+": "+error);};
        set("ior",index);set("eyeEnabled",enabled);set("eyeRefractionStrength",strength);
        set("eyeIrisShape",glm::vec4(.56f,0,.83f,0));set("eyeParallax",glm::vec4(0,0,.4f,.44f));
        set("eyeDisplacement",glm::vec4(16,.2f,0,0));set("eyePupilOffset",glm::vec2(0));
        TestInputs inputs;inputs.view=glm::vec4(view,0);
        RHICommand::BeginFrame begin;begin.frameIndex=++frame_;begin.framebufferWidth=begin.framebufferHeight=4;
        auto encoder=device_.BeginFrame(begin);
        try {
            RHICommand::SetRenderTarget target;target.target=target_;target.width=target.height=4;target.clearFlags=RHICommand::ClearColor;
            Check(encoder.KeepAlive(owner_)&&encoder.SetRenderTarget(target),"Eye target failed");
            Check(encoder.UpdateUniformBuffer(inputs_,inputs)&&encoder.BindUniformBuffer(inputs_,0),"Eye inputs failed");
            for(std::uint32_t offset=0;offset<material.Bytes().size();offset+=RHICommand::MaxInlineUniformBytes)
                Check(encoder.UpdateUniformBufferBytes(material_,material.Bytes().data()+offset,std::min<std::uint32_t>(RHICommand::MaxInlineUniformBytes,material.Bytes().size()-offset),offset),"Eye material upload failed");
            Check(encoder.BindUniformBuffer(material_,Material::MaterialParameterBinding)&&encoder.BindTexture(packed_,5)&&encoder.BindPipeline(pipeline_)&&encoder.BindMesh(mesh_)&&encoder.DrawIndexed(RHICommand::DrawIndexed{3})&&encoder.End(false),"Eye draw failed");
            auto done=std::make_shared<Result<bool>>();Check(device_.async_SubmitFrameCommands(encoder.GetCommandBuffer(),[done]{done->done=true;}),"Eye submit failed");Wait([done]{return done->done;});
        } catch(...) {encoder.Cancel();throw;}
        auto result=std::make_shared<Result<std::array<float,64>>>();auto valid=std::make_shared<bool>(false);
        device_.async_ExecuteCode([this,result,valid]{const auto* texture=device_.GetResourcePool().TextureTable.Get(output_);if(texture){glGetTextureImage(texture->rhi_id,0,GL_RGBA,GL_FLOAT,sizeof(result->value),result->value.data());*valid=glGetError()==GL_NO_ERROR;}},[result]{result->done=true;});
        Wait([result]{return result->done;});Check(*valid,"Eye readback failed");
        for(std::size_t i=0;i<16;++i){for(int c=0;c<3;++c)Check(std::isfinite(result->value[4*i+c]),"Nonfinite eye result");Check(result->value[4*i+3]==1,"Eye target missing coverage");}
        return {result->value[0],result->value[1],result->value[2]};
    }
    RHIDevice device_;std::shared_ptr<const void>owner_;std::shared_ptr<const Material::MaterialTemplate>schema_;
    RenderResourceHandle<PipelineSpec>pipeline_;RenderResourceHandle<VertexBufferSpec>mesh_;
    RenderResourceHandle<UniformBufferSpec>inputs_,material_;RenderResourceHandle<RHITextureSpec>packed_,output_;
    RenderResourceHandle<RenderTargetSpec>target_;std::uint64_t frame_=0;
};
}
int main(){int result=0;try{Check(glfwInit()==GLFW_TRUE,"glfwInit failed");glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);ApplicationWindow::Window window({4,4,"Eye material GPU test",false});Check(window.Activate(),"Cannot activate OpenGL");auto context=window.GetRenderContextAsOpengl();Check(context.has_value(),"No OpenGL context");Fixture fixture(std::move(*context));fixture.Run();fixture.Shutdown();}catch(const std::exception& error){std::cerr<<"Eye material GPU test failed: "<<error.what()<<'\n';result=1;}glfwTerminate();return result;}
