#pragma once
#include "Render/Private/Pipeline/strand_native_oit_kernels.h"
#include "Render/Private/Pipeline/strand_curve_pass.h"
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Private/Pipeline/post_process_shaders.h"
#include "Render/Private/Pipeline/strand_temporal_shaders.h"
#include "Render/Public/Compute/gpu_compute.h"
#include <array>
#include <cstring>
#include <functional>
#include <limits>

namespace Render::PipelineDetail {
struct StrandNativeOitDebugBuffers {
    std::shared_ptr<Compute::Device> owner;
    std::shared_ptr<const void> globalLease;
    std::shared_ptr<const void> targetsLease;
    Compute::BufferHandle lines,tiles,pairs,global,motion,meta;
    std::uint32_t width=0,height=0,pieces=0,pairCapacity=0;
};
// Native fine arithmetic with deterministic GPU scheduling. Worklists, Wu,
// packed local4/8 and global4 stay on GPU. No CPU rasterization/readback.
class StrandNativeOitPass {
    struct alignas(16) Push {
        glm::uvec4 extent{0},work{0};glm::vec4 parameters{1,.95f,1,0},forward{0,0,-1,0};
    };
    static_assert(sizeof(Push)==64);
    struct Entry {
        std::shared_ptr<Compute::Device> owner;
        Compute::BufferHandle lines,tiles,pairs,meta;
        std::uint32_t capacity=0,pairCapacity=0,tilesCount=0,requestedBudget=0;
        std::uint64_t bytes=0;
    };
    RHIDevice& device_;
    std::shared_ptr<Compute::Device> programs_,globalOwner_;
    Compute::PipelineHandle project_,fine_,resolve_;
    std::array<Compute::PipelineHandle,7> scheduling_{};
    Compute::BufferHandle global_,motion_;
    std::vector<Entry> entries_;
    std::shared_ptr<const void> targetsOwner_,rasterOwner_;
    RenderResourceHandle<RHITextureSpec> color_,aux_,motionImage_,coverage_,nearest_,composite_;
    RenderResourceHandle<RenderTargetSpec> nearestTarget_,temporalTarget_;
    RenderResourceHandle<PipelineSpec> compositePipeline_,straightPipeline_,nearestPipeline_,depthPipeline_;
    RenderResourceHandle<VertexBufferSpec> fullscreen_;
    std::uint32_t width_=0,height_=0,computePasses_=0,layers_=0,rasterPasses_=0;
    std::uint64_t bytes_=0;
    bool active_=false,temporal_=false,motionActive_=false;
    StrandNativeOitDebugBuffers debug_{};
    static void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
    static std::uint32_t Groups(std::uint32_t n,std::uint32_t group=256){return (n+group-1u)/group;}
    void Initialize(){
        if(programs_)return;
        auto owner=std::shared_ptr<Compute::Device>(Compute::Device::Borrow(device_));
        Require(owner->Diagnostics().frameAvailable,"Native hair OIT requires RHI in-frame compute");
        const auto create=[&](const std::string& source,std::uint32_t uniforms,std::uint32_t samples,std::uint32_t images,const char* label){
            Compute::FramePipelineDesc d;d.source=source;d.uniformMask=uniforms;d.sampledMask=samples;
            d.storageBuffers=6;d.storageImages=images;d.pushConstantBytes=sizeof(Push);d.label=label;
            return owner->CreateFramePipeline(d);
        };
        const auto project=create(StrandNativeOitProjectShader(),(1u<<0)|(1u<<1)|(1u<<2)|(1u<<8)|(1u<<13),
                                  (1u<<6)|(1u<<8),0,"native Wu endpoint projection");
        std::array<Compute::PipelineHandle,7> scheduling;
        for(unsigned i=0;i<scheduling.size();++i)scheduling[i]=create(StrandNativeOitSchedulingShader(i),0,0,0,"native OIT GPU tile lists");
        const auto fine=create(StrandNativeOitFineShader(),0,1u<<3,0,"native Wu local4/8 object global4");
        const auto resolve=create(StrandNativeOitResolveShader(),0,1u<<3,3,"native OIT linear resolve");
        ResourceBuilder builder(device_);const auto fullscreen=builder.FullscreenMesh();
        const auto straight=builder.FullscreenPipeline(FullscreenVertexShader(),StrandResolvedColorCopyFragmentShader());
        CreateGraphicShaderDesc source;source.vertexShaderSource=builder.Source(ShaderSourceType::Vertex,FullscreenVertexShader());
        source.fragmentShaderSource=builder.Source(ShaderSourceType::Fragment,StrandResolvedColorCopyFragmentShader());
        auto program=builder.Create<ShaderProgramSpec>([&](auto cb){device_.async_CreateGraphicShader(source,cb);});
        PipelineSpec spec;spec.shaderProgram=program;spec.expectVertexLayout={{VertexFieldType::Vec2,VertexFieldType::Vec2}};
        spec.depthTest=spec.depthWrite=false;spec.cullMode=CullMode::None;spec.blendEnable=true;spec.blendMode=BlendMode::AlphaComposite;
        const auto composite=builder.Create<PipelineSpec>([&](auto cb){device_.async_CreatePipeline({spec},cb);});
        const auto nearest=builder.FullscreenPipeline(FullscreenVertexShader(),StrandNativeOitDepthShader(true),true);
        const auto depth=builder.FullscreenPipeline(FullscreenVertexShader(),StrandNativeOitDepthShader(false),true);
        auto raster=builder.Finish();project_=project;fine_=fine;resolve_=resolve;scheduling_=scheduling;
        programs_=std::move(owner);rasterOwner_=std::move(raster);fullscreen_=fullscreen;
        straightPipeline_=straight;compositePipeline_=composite;nearestPipeline_=nearest;depthPipeline_=depth;
    }
    void Resize(std::uint32_t w,std::uint32_t h){
        if(targetsOwner_&&w==width_&&h==height_)return;
        const auto pixels=std::uint64_t(w)*h;
        const auto limit=programs_->Diagnostics().maxStorageBufferBytes;
        if(pixels*32u>limit)throw std::invalid_argument("Native hair global OIT exceeds the RHI storage-buffer range");
        auto owner=std::shared_ptr<Compute::Device>(Compute::Device::Borrow(device_));
        const auto global=owner->CreateStorageBuffer(pixels*32u,nullptr,"native global OIT4");
        const auto motion=owner->CreateStorageBuffer(pixels*16u,nullptr,"native global motion");
        ResourceBuilder builder(device_);
        auto color=builder.StorageTexture(w,h,RHITextureFormat::RGBA32F),aux=builder.StorageTexture(w,h,RHITextureFormat::RGBA32F);
        auto mv=builder.StorageTexture(w,h,RHITextureFormat::RGBA32F),coverage=builder.Texture(w,h,RHITextureFormat::RGBA32F);
        auto nearestDepth=builder.Texture(w,h,RHITextureFormat::Depth32F),depth=builder.Texture(w,h,RHITextureFormat::Depth32F);
        auto nearestTarget=builder.Target({},nearestDepth),temporalTarget=builder.Target(coverage,depth);
        auto targets=builder.Finish();globalOwner_=std::move(owner);global_=global;motion_=motion;
        color_=color;aux_=aux;motionImage_=mv;coverage_=coverage;nearest_=nearestDepth;composite_=depth;
        nearestTarget_=nearestTarget;temporalTarget_=temporalTarget;targetsOwner_=std::move(targets);width_=w;height_=h;
        entries_.clear();
    }
    void PrepareEntry(Entry& e,std::uint32_t pieces,std::uint32_t tiles,std::uint32_t budget){
        if(e.owner&&e.capacity>=std::max(pieces,1u)&&e.tilesCount==tiles&&e.requestedBudget==budget)return;
        const auto count=std::max(pieces,1u);const auto limit=programs_->Diagnostics().maxStorageBufferBytes;
        if(pieces>=0x8000000u||std::uint64_t(count)*112u>limit)
            throw std::invalid_argument("Native hair projected lines exceed the RHI storage-buffer range");
        auto pairCapacity=static_cast<std::uint32_t>(std::min<std::uint64_t>(limit/4u,
            std::max<std::uint64_t>(256u,std::uint64_t(count)*16u+std::uint64_t(tiles)*4u)));
        if(budget)pairCapacity=std::min(pairCapacity,budget);
        auto owner=std::shared_ptr<Compute::Device>(Compute::Device::Borrow(device_));
        auto lines=owner->CreateStorageBuffer(std::uint64_t(count)*112u,nullptr,"native projected curves");
        auto tile=owner->CreateStorageBuffer(std::uint64_t(tiles)*16u,nullptr,"native tile counters and offsets");
        auto pairs=owner->CreateStorageBuffer(std::uint64_t(pairCapacity)*4u,nullptr,"native sorted tile candidates");
        auto meta=owner->CreateStorageBuffer(32,nullptr,"native candidate overflow diagnostics");
        e={std::move(owner),lines,tile,pairs,meta,count,pairCapacity,tiles,budget,
            std::uint64_t(count)*112u+std::uint64_t(tiles)*16u+std::uint64_t(pairCapacity)*4u+32u};
    }
    void Dispatch(RHIFrameEncoder& encoder,Compute::PipelineHandle pipeline,const Entry& entry,const Push& push,
                  std::uint32_t x,std::uint32_t y=1,std::uint32_t images=0){
        RHICommand::DispatchFrameCompute command;command.pipeline=pipeline;command.groupsX=x;command.groupsY=y;
        if(y==1&&x>65535u){command.groupsX=65535u;command.groupsY=Groups(x,65535u);}
        command.buffers={entry.lines,entry.tiles,entry.pairs,global_,motion_,entry.meta};
        if(images){command.images[0]=color_;command.images[1]=aux_;command.images[2]=motionImage_;}
        command.pushBytes=sizeof(push);std::memcpy(command.pushConstants.data(),&push,sizeof(push));
        Require(encoder.DispatchCompute(command),"Cannot record native hair OIT compute");++computePasses_;
    }
    void Raster(RHIFrameEncoder& encoder,RenderResourceHandle<RenderTargetSpec> target,RenderResourceHandle<PipelineSpec> pipeline,
                RenderResourceHandle<RHITextureSpec> input){
        RHICommand::SetRenderTarget t;t.target=target;t.width=width_;t.height=height_;
        Require(encoder.SetRenderTarget(t)&&encoder.SetViewport({0,0,width_,height_})&&encoder.BindPipeline(pipeline)&&
            encoder.BindMesh(fullscreen_)&&encoder.BindTexture(input,0)&&encoder.DrawIndexed({3}),"Cannot resolve native hair OIT attachment");
        ++rasterPasses_;
    }
public:
    explicit StrandNativeOitPass(RHIDevice& device):device_(device){}
    void Begin(){active_=temporal_=motionActive_=false;computePasses_=rasterPasses_=layers_=0;bytes_=0;debug_={};}
    template<class BindItem>void Record(RHIFrameEncoder& encoder,std::span<const StrandRenderItem> items,
                  const StrandPassTarget& target,const StrandPassView& view,StrandCurvePass& curves,BindItem bindItem){
        Initialize();Resize(target.width,target.height);entries_.resize(items.size());
        const auto tilesX=Groups(width_,16),tilesY=Groups(height_,16),tiles=tilesX*tilesY;
        Push push;push.extent={width_,height_,tilesX,tilesY};push.work.z=view.nativeOitLayers;
        push.parameters.y=view.depthWriteThreshold;
        Require(encoder.KeepAlive(programs_)&&encoder.KeepAlive(globalOwner_)&&encoder.KeepAlive(targetsOwner_)&&
            encoder.KeepAlive(rasterOwner_),"Cannot pin native OIT frame resources");
        for(std::size_t i=0;i<items.size();++i){
            const auto input=curves.Input(i);auto& e=entries_[i];PrepareEntry(e,input.pieces,tiles,view.nativeOitPairBudget);
            Require(encoder.KeepAlive(e.owner),"Cannot pin native OIT scratch generation");
            push.work={input.pieces,e.pairCapacity,view.nativeOitLayers,items[i].material.sourceCurveGeometry?1u:0u};
            push.parameters.x=items[i].material.opacity;
            const auto source=items[i].geometry->SegmentSource();
            auto forward=glm::normalize(glm::length(view.cameraForward)>1e-7f?view.cameraForward:glm::vec3(0,0,-1));
            float depthMinimum=std::numeric_limits<float>::max(),depthMaximum=0;
            for(unsigned c=0;c<8;++c){auto p=glm::mix(items[i].geometry->BoundsMin(),items[i].geometry->BoundsMax(),
                glm::vec3(float(c&1u),float((c>>1u)&1u),float((c>>2u)&1u)));
                auto world=glm::vec3(items[i].model*glm::vec4(p,1));float d=std::abs(glm::dot(world-view.cameraPosition,forward));
                depthMinimum=std::min(depthMinimum,d);depthMaximum=std::max(depthMaximum,d);}
            // Runtime StrandBoundary Scale/Offset remain unknown. This explicit
            // host normalization preserves source bucket domain/order, not scene values.
            push.parameters.z=1.f/std::max(depthMaximum-depthMinimum,1e-5f);push.parameters.w=depthMinimum*push.parameters.z;
            push.forward=glm::vec4(forward,0);
            bindItem(i,true,true);curves.Bind(encoder,i);
            Require(encoder.BindTexture(view.opaqueDepth,3),"Cannot bind native opaque depth");
            Dispatch(encoder,scheduling_[i?6:0],e,push,Groups(i?tiles:std::max(tiles,width_*height_)));
            if(input.pieces){
                Dispatch(encoder,project_,e,push,Groups(input.pieces,64));
                Dispatch(encoder,scheduling_[1],e,push,Groups(input.pieces));
                Dispatch(encoder,scheduling_[3],e,push,1);
                Dispatch(encoder,scheduling_[2],e,push,Groups(e.pairCapacity));
                Dispatch(encoder,scheduling_[4],e,push,Groups(input.pieces));
                Dispatch(encoder,scheduling_[5],e,push,tiles);
                Dispatch(encoder,fine_,e,push,tilesX,tilesY);
            }
            bytes_+=e.bytes;
            debug_={e.owner,globalOwner_,targetsOwner_,e.lines,e.tiles,e.pairs,global_,motion_,e.meta,width_,height_,input.pieces,e.pairCapacity};
        }
        Require(encoder.BindTexture(view.opaqueDepth,3),"Cannot restore native resolve depth");
        Dispatch(encoder,resolve_,entries_.back(),push,tilesX,tilesY,3);
        Raster(encoder,target.framebuffer,view.outputStraightColor&&!view.outputMotionData?straightPipeline_:compositePipeline_,color_);
        temporal_=view.outputTemporalData||view.outputMotionData;motionActive_=view.outputMotionData;
        if(temporal_){Raster(encoder,nearestTarget_,nearestPipeline_,aux_);Raster(encoder,temporalTarget_,depthPipeline_,aux_);}
        RHICommand::SetRenderTarget restore;restore.target=target.framebuffer;restore.width=width_;restore.height=height_;
        Require(encoder.SetRenderTarget(restore),"Cannot restore scene after native hair");
        bytes_+=std::uint64_t(width_)*height_*(48u+64u+8u);active_=true;layers_=view.nativeOitLayers;
    }
    bool Active()const{return active_;}std::uint32_t ComputePasses()const{return computePasses_;}
    std::uint32_t RasterPasses()const{return rasterPasses_;}std::uint32_t Layers()const{return layers_;}
    std::uint64_t Bytes()const{return bytes_;}
    StrandNativeOitDebugBuffers DebugBuffers()const{return debug_;}
    StrandTemporalTextures Textures()const{return {temporal_?nearest_:RenderResourceHandle<RHITextureSpec>{},
        temporal_?coverage_:RenderResourceHandle<RHITextureSpec>{},temporal_?composite_:RenderResourceHandle<RHITextureSpec>{},
        motionActive_?motionImage_:RenderResourceHandle<RHITextureSpec>{},color_,width_,height_};}
    void Shutdown(){entries_.clear();debug_={};programs_.reset();globalOwner_.reset();targetsOwner_.reset();rasterOwner_.reset();
        width_=height_=0;Begin();}
};
} // namespace Render::PipelineDetail
