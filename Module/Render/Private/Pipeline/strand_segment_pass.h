#pragma once
#include "Render/Private/Pipeline/strand_segment_source.h"
#include "Render/Private/Pipeline/strand_segment_shaders.h"
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Private/Pipeline/strand_render_pass.h"
#include "Render/Private/Pipeline/post_process_shaders.h"
#include "Render/Public/Compute/gpu_compute.h"
#include <algorithm>
#include <array>
#include <cstring>

namespace Render::PipelineDetail {
class StrandSegmentPass {
    struct Program {
        std::shared_ptr<Compute::Device> owner;Compute::PipelineHandle pipeline;
        std::shared_ptr<const void> rasterOwner;RenderResourceHandle<PipelineSpec> rasterPipeline;
        RenderResourceHandle<VertexBufferSpec> fullscreen;
    };
    struct Generation {
        std::weak_ptr<const StrandSegmentSourceData> source;
        std::shared_ptr<Compute::Device> inputOwner;
        Compute::BufferHandle points,segments;
        std::shared_ptr<const void> rasterInputOwner;
        RenderResourceHandle<RHITextureSpec> rasterPoints,rasterMapping;
        std::shared_ptr<const void> outputOwner;
        RenderResourceHandle<RHITextureSpec> output;
        RenderResourceHandle<RenderTargetSpec> rasterTarget;
        std::uint32_t width=0,height=0;
        std::vector<std::uint32_t> visibility;
        std::size_t segmentCount=0,evaluations=0;
    };
    RHIDevice& device_;
    bool compute_=false;
    std::array<Program,4> programs_{};
    std::vector<Generation> entries_;
    StrandSegmentShadingStatistics statistics_{};
    StrandSegmentShadingDebugTextures debug_{};
    static void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
public:
    explicit StrandSegmentPass(RHIDevice& device):device_(device){}
    void Begin(std::size_t count){entries_.resize(count);statistics_={};debug_={};}
    void Prepare(std::size_t index,const StrandRenderItem& item,std::span<const std::uint32_t> visibility={}) {
        if(!item.material.sourceSegmentShading)return;
        // A pipeline object may be constructed before RHIDevice::Run selects
        // its backend. Query at first actual use, not during construction.
        compute_=device_.GetBackendType()==BackendType::Vulkan;
        const auto source=item.geometry->SegmentSource();
        if(!source||!source->directedChains||source->segments.empty())
            throw std::invalid_argument("Source segment shading requires directed nonbranching acyclic strand chains");
        auto& entry=entries_.at(index);
        if(!visibility.empty()&&visibility.size()!=(source->segments.size()+31u)/32u)
            throw std::invalid_argument("Segment visibility must use original compact-segment ordinal words");
        const auto width=static_cast<std::uint32_t>(std::min<std::size_t>(1024,source->points.size()));
        const auto height=static_cast<std::uint32_t>((source->points.size()+width-1)/width);
        if(entry.source.lock()!=source||entry.visibility.size()!=visibility.size()||
           !std::equal(entry.visibility.begin(),entry.visibility.end(),visibility.begin())) {
            const auto visible=[&](std::size_t s){return visibility.empty()||(visibility[s/32u]&(1u<<(s%32u)))!=0;};
            constexpr auto absent=std::numeric_limits<std::uint32_t>::max();
            std::vector<std::uint32_t> incoming(source->points.size(),absent);
            for(std::size_t s=0;s<source->segments.size();++s)incoming[source->segments[s].y]=static_cast<std::uint32_t>(s);
            std::vector<glm::uvec4> records;records.reserve(source->segments.size());
            for(std::size_t s=0;s<source->segments.size();++s)if(visible(s)){
                auto r=source->segments[s];
                const auto previous=incoming[r.x];
                const bool duplicate=r.w!=0u||previous==absent||!visible(previous);
                // Bit0 preserves ORIGINAL tangent/control extrapolation. A
                // hidden predecessor only sets bit1 (shade-start ownership).
                // Native ordered streams have predecessor ordinal s-1; the
                // topology map also safely supports shuffled host chains.
                r.w=(r.w?1u:0u)|(duplicate?2u:0u);records.push_back(r);
            }
            entry.segmentCount=records.size();entry.evaluations=0;
            if(compute_) {
            auto owner=std::shared_ptr<Compute::Device>(Compute::Device::Borrow(device_));
            if(!owner->Diagnostics().frameAvailable)throw std::runtime_error("Source segment shading requires RHI in-frame compute");
            auto points=owner->CreateStorageBuffer(source->points.size()*sizeof(StrandSegmentPoint),source->points.data(),"strand segment source points");
            const glm::uvec4 empty(0);
            auto segments=owner->CreateStorageBuffer(std::max<std::size_t>(records.size(),1)*sizeof(glm::uvec4),
                records.empty()?&empty:records.data(),"strand segment source topology");
            entry.points=points;entry.segments=segments;entry.inputOwner=std::move(owner);
            } else {
                const auto count=std::size_t(width)*height;
                std::vector<glm::vec4> points(count*3,glm::vec4(0)),mapping(count,glm::vec4(-1));
                for(std::size_t i=0;i<source->points.size();++i){
                    points[i]=source->points[i].positionArc;points[count+i]=source->points[i].previousPositionArc;
                    points[2*count+i]=source->points[i].colorCoordinates;
                }
                for(const auto& segment:records){mapping[segment.y]=glm::vec4(segment);if(segment.w&2u)mapping[segment.x]=glm::vec4(segment);}
                for(std::size_t p=0;p<source->points.size();++p)entry.evaluations+=mapping[p].x>=0.f;
                ResourceBuilder builder(device_);
                const auto texture=[&](const std::vector<glm::vec4>& values,std::uint32_t h){
                    CreateRHITextureSpec desc;desc.width=width;desc.height=h;desc.mipmaps=false;desc.data=values.data();
                    desc.textureDataStoreType=desc.textureUseType=RHITextureFormat::RGBA32F;
                    desc.filterMode=RHIFilterMode::Nearest;desc.addressMode=RHIAddressMode::ClampToEdge;
                    return builder.Create<RHITextureSpec>([&](auto callback){device_.async_CreateTexture(desc,callback);});
                };
                const auto pointsTexture=texture(points,height*3),mapTexture=texture(mapping,height);
                auto owner=builder.Finish();entry.rasterPoints=pointsTexture;entry.rasterMapping=mapTexture;
                entry.rasterInputOwner=std::move(owner);
            }
            entry.source=source;
            entry.visibility.assign(visibility.begin(),visibility.end());
        }
        if(!entry.outputOwner||width!=entry.width||height!=entry.height) {
            ResourceBuilder builder(device_);
            // Every stored value has already been quantized to source unsigned
            // 11/11/10. RGBA32F avoids requiring optional rgba16f image stores.
            const auto output=compute_?builder.StorageTexture(width,height,RHITextureFormat::RGBA32F):
                builder.Texture(width,height,RHITextureFormat::RGBA32F);
            const auto target=compute_?RenderResourceHandle<RenderTargetSpec>{}:builder.Target(output,{});
            auto outputOwner=builder.Finish();entry.output=output;entry.outputOwner=std::move(outputOwner);
            entry.rasterTarget=target;
            entry.width=width;entry.height=height;
        }
    }
    void Record(RHIFrameEncoder& encoder,std::size_t index,const StrandRenderItem& item,unsigned features) {
        auto& entry=entries_.at(index);
        if(!item.material.sourceSegmentShading)return;
        if(!entry.segmentCount)return;
        const auto source=item.geometry->SegmentSource();
        auto& program=programs_.at(features&3u);
        if(!compute_) {
            if(!program.rasterOwner) {
                ResourceBuilder builder(device_);
                const auto pipeline=builder.FullscreenPipeline(FullscreenVertexShader(),
                    StrandSegmentFragmentShader((features&1u)!=0,(features&2u)!=0));
                const auto fullscreen=builder.FullscreenMesh();auto owner=builder.Finish();
                program.rasterPipeline=pipeline;program.fullscreen=fullscreen;program.rasterOwner=std::move(owner);
            }
            Require(encoder.KeepAlive(program.rasterOwner)&&encoder.KeepAlive(entry.rasterInputOwner)&&
                encoder.KeepAlive(entry.outputOwner),"Cannot pin segment raster producer");
            // Slot6 is the consumer output. Unbind it before making that same
            // image an attachment; input2/3 are immutable independent images.
            Require(encoder.BindTexture(entry.rasterPoints,6),"Cannot detach segment color consumer");
            RHICommand::SetRenderTarget target;target.target=entry.rasterTarget;target.width=entry.width;target.height=entry.height;
            target.clearFlags=RHICommand::ClearColor;target.clearColor=glm::vec4(0);
            Require(encoder.SetRenderTarget(target)&&encoder.BindPipeline(program.rasterPipeline)&&
                encoder.BindMesh(program.fullscreen)&&encoder.BindTexture(entry.rasterPoints,2)&&
                encoder.BindTexture(entry.rasterMapping,3)&&encoder.DrawIndexed({3}),"Cannot record segment raster endpoint shading");
            ++statistics_.passes;++statistics_.rasterPasses;
            statistics_.points+=entry.evaluations;statistics_.outputPoints+=source->points.size();
            if(!debug_.color.IsValid())debug_={entry.output,entry.width,entry.height};
            return;
        }
        if(!program.owner) {
            auto owner=std::shared_ptr<Compute::Device>(Compute::Device::Borrow(device_));
            Compute::FramePipelineDesc desc;desc.label="native segment endpoint shading";
            desc.source=StrandSegmentComputeShader((features&1u)!=0,(features&2u)!=0);
            desc.uniformMask=0x1ffu|(1u<<11)|(1u<<12)|(1u<<14);
            desc.sampledMask=3u|(3u<<4)|(1u<<7)|(0x7fu<<9);
            desc.storageBuffers=2;desc.storageImages=1;desc.pushConstantBytes=16;
            const auto pipeline=owner->CreateFramePipeline(desc);program.pipeline=pipeline;program.owner=std::move(owner);
        }
        Require(encoder.KeepAlive(program.owner)&&encoder.KeepAlive(entry.inputOwner)&&encoder.KeepAlive(entry.outputOwner)&&
            encoder.KeepAlive(item.geometry),"Cannot pin segment producer resources");
        RHICommand::DispatchFrameCompute command;command.pipeline=program.pipeline;
        command.buffers[0]=entry.points;command.buffers[1]=entry.segments;command.images[0]=entry.output;
        command.groupsX=static_cast<std::uint32_t>((entry.segmentCount+63)/64);
        const glm::uvec4 push(static_cast<std::uint32_t>(entry.segmentCount),entry.width,0,0);
        command.pushBytes=sizeof(push);std::memcpy(command.pushConstants.data(),&push,sizeof(push));
        Require(encoder.DispatchCompute(command),"Cannot record segment endpoint shading");
        ++statistics_.passes;++statistics_.computePasses;statistics_.points+=entry.segmentCount;statistics_.outputPoints+=source->points.size();
        if(!debug_.color.IsValid())debug_={entry.output,entry.width,entry.height};
    }
    void Bind(RHIFrameEncoder& encoder,std::size_t index,RenderResourceHandle<RHITextureSpec> fallback,bool active) {
        const auto& entry=entries_.at(index);
        if(active)Require(encoder.KeepAlive(entry.outputOwner),"Cannot pin segment endpoint color atlas");
        Require(encoder.BindTexture(active?entry.output:fallback,6),"Cannot bind segment endpoint color atlas");
    }
    StrandSegmentShadingStatistics Statistics()const{return statistics_;}
    StrandSegmentShadingDebugTextures DebugTextures()const{return debug_;}
    void Reset(){entries_.clear();programs_={};statistics_={};debug_={};}
};
} // namespace Render::PipelineDetail
