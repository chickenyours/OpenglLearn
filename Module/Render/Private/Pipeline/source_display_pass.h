#pragma once
#include "Render/Public/Pipeline/source_display.h"
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Private/Pipeline/post_process_shaders.h"
#include "Render/Private/Pipeline/source_display_shaders.h"
#include "Render/Public/RHICommand/FrameCommand/rhi_frame_encoder.h"
namespace Render::PipelineDetail {
class SourceDisplayPass {
    RHIDevice& device_;
    std::shared_ptr<const void> owner_,cubeOwner_;
    std::shared_ptr<const DisplayColorCube> source_;
    RenderResourceHandle<PipelineSpec> pipeline_;
    RenderResourceHandle<UniformBufferSpec> constants_;
    RenderResourceHandle<VertexBufferSpec> mesh_;
    RenderResourceHandle<RHITextureSpec> cube_;
    struct alignas(16) Constants {glm::vec4 p0,p1,p2,options,output;};
    static void Check(bool ok){if(!ok)throw std::runtime_error("Cannot record source display study");}
public:
    explicit SourceDisplayPass(RHIDevice& d):device_(d){}
    void Record(RHIFrameEncoder& encoder,RenderResourceHandle<RHITextureSpec> hdr,
                RenderResourceHandle<RHITextureSpec> bloom,const PostProcessConstants& post,
                const SourceDisplaySettings& settings){
        // The caller selects the target and uploads PostProcessData. RHI owns
        // shader/texture lifetimes and the original encoded cube stays linear.
        if(!owner_){ResourceBuilder b(device_);
            auto fs=PostProcessDetail::FragmentPreamble()+SourceDisplayGLSL()+R"GLSL(
layout(binding=3) uniform sampler2D sourceCube;
layout(std140,binding=7) uniform SourceDisplayData {vec4 curve0,curve1,curve2,sourceOptions,sourceOutput;};
void main(){
    vec3 inputValue=texture(inputTexture,vUV).rgb;
    // Original PostTonemap guards nonfinite radiance before curve arithmetic.
    if(any(isnan(inputValue))||any(isinf(inputValue)))inputValue=vec3(1.0);
    vec3 linear=max(inputValue,vec3(0.0));
    if(postScreen.w>.5)linear+=max(texture(secondTexture,vUV).rgb,vec3(0.0))*postBloom.y;
    linear=SourceDisplayThreeSection(linear*postOutput.x,curve0,curve1,curve2);
    if(sourceOptions.x>.5)linear=SourceDisplayApplyCube(sourceCube,linear,int(sourceOptions.y));
    vec3 display=SourceDisplaySrgb(max(linear,vec3(0.0)),sourceOutput.x,sourceOutput.y,sourceOutput.z);
    outColor=vec4(clamp(display,vec3(0.0),vec3(1.0)),1.0);
}
)GLSL";
            pipeline_=b.FullscreenPipeline(FullscreenVertexShader(),fs);mesh_=b.FullscreenMesh();constants_=b.Uniform(sizeof(Constants));
            owner_=b.Finish();
        }
        if(settings.cube!=source_){
            if(settings.cube){const auto& source=*settings.cube;
                if(source.edge<2||source.edge>65||source.texels.size()!=std::size_t(source.edge)*source.edge*source.edge)
                    throw std::invalid_argument("Invalid encoded source display color cube");
                for(const auto& v:source.texels)for(int i=0;i<4;++i)if(!std::isfinite(v[i]))throw std::invalid_argument("Nonfinite source display cube");
                ResourceBuilder b(device_);CreateRHITextureSpec desc;
                desc.width=source.edge;desc.height=source.edge*source.edge;desc.mipmaps=false;
                desc.addressMode=RHIAddressMode::ClampToEdge;
                desc.textureDataStoreType=desc.textureUseType=RHITextureFormat::RGBA32F;
                desc.data=source.texels.data();
                cube_=b.Create<RHITextureSpec>([&](auto cb){device_.async_CreateTexture(desc,cb);});cubeOwner_=b.Finish();
            }else{cube_={};cubeOwner_.reset();}
            source_=settings.cube;
        }
        Constants data{settings.curve0,settings.curve1,settings.curve2,
            glm::vec4(source_?1.f:0.f,source_?float(source_->edge):0.f,0,0),
            glm::vec4(settings.displayGamma,settings.convertToLimit,settings.lowerLimit,0)};
        for(const auto& v:{data.p0,data.p1,data.p2,data.output})for(int i=0;i<4;++i)
            if(!std::isfinite(v[i]))throw std::invalid_argument("Invalid source display coefficients");
        Check(encoder.KeepAlive(owner_)&&(!cubeOwner_||encoder.KeepAlive(cubeOwner_))&&
            encoder.UpdateUniformBufferBytes(constants_,&data,sizeof(data),0)&&encoder.BindUniformBuffer(constants_,7)&&
            encoder.BindPipeline(pipeline_)&&encoder.BindMesh(mesh_)&&encoder.BindTexture(hdr,0)&&encoder.BindTexture(bloom,1)&&
            encoder.BindTexture(cube_,3)&&encoder.DrawIndexed({3}));
    }
    void Reset(){owner_.reset();cubeOwner_.reset();source_.reset();pipeline_={};constants_={};mesh_={};cube_={};}
};
}
