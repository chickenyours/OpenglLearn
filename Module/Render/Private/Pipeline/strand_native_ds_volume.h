#pragma once
#include "Render/Private/Pipeline/strand_native_ds_volume_shaders.h"
#include "Render/Private/Pipeline/strand_native_ds_inputs.h"
#include "Render/Private/Pipeline/strand_segment_source.h"
#include "Render/Private/Pipeline/strand_render_pass.h"
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Public/Compute/gpu_compute.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

namespace Render::PipelineDetail {
struct NativeDSVolumeSettings {
    bool enabled=false;
    float requestedVoxelSize=.005f;
    std::uint32_t resolution=64;
    float averageWidth=0; // Explicit authored ORIGINAL full width in metres.
    float widthScale=1.5f,rasterTraceStepScale=.5f;
    float traceGrowth=1.1f,offsetScale=1.f;
    std::uint32_t traceLimit=32,interpolation=4;
    float injectionRate=1.f,injectionLodFraction=1.f;
    bool injectionMinLodFactor=false;
    std::uint32_t injectionQuality=2;
    bool interpolationInjection=true,opaque=true;
    float threshold=.05f;
    bool hostNegativeLodClamp=true;
};
struct alignas(16) NativeDSVolumeConstants {
    glm::mat4 worldToDS{1};
    glm::vec4 minimum{0},maximum{0};
    glm::uvec4 resolution{0}; // actual xyz, mip count
    glm::vec4 voxel{0},inverseExtent{0},atlas{0}; // voxel.w=min voxel; atlas=width,height,resourceR,enabled
    std::array<glm::uvec4,6> levels{}; // offsetY, edge, slice columns, slice rows
    // Inactive binding initializer. Every active Layout replaces the trace
    // fields with settings, whose recovered source cap is32.
    glm::vec4 trace{1.1f,1,8,4},opaqueInfo{0,.05f,1,0};
};
static_assert(sizeof(NativeDSVolumeConstants)==288);
static_assert(offsetof(NativeDSVolumeConstants,resolution)==96);
static_assert(offsetof(NativeDSVolumeConstants,voxel)==112);
static_assert(offsetof(NativeDSVolumeConstants,atlas)==144);
static_assert(offsetof(NativeDSVolumeConstants,levels)==160);
static_assert(offsetof(NativeDSVolumeConstants,trace)==256);
static_assert(offsetof(NativeDSVolumeConstants,opaqueInfo)==272);

struct NativeDSVolumeSnapshot {
    std::shared_ptr<Compute::Device> owner;
    std::shared_ptr<const void> textureLease;
    RenderResourceHandle<RHITextureSpec> atlas;
    Compute::BufferHandle points,segments,raw;
    NativeDSVolumeConstants constants;
    std::uint32_t width=0,height=0,pointCount=0,segmentCount=0;
    std::uint64_t bytes=0;
    bool active=false;
    std::uint32_t injectionSegmentCount=0;
    float injectionWidthScale=0;
};

// Texture DSVolume producer: packed R24/G8 storage, one explicit resource cube,
// and six floating RG mip levels. It never consumes rendered curve children,
// evaluates scattering, or substitutes proxies for the camera depth shell.
class NativeDSVolumePass {
    struct alignas(16) ProducerConstants {
        glm::mat4 localToDS{1},dsToWorld{1},viewProjection{1},inverseViewProjection{1};
        glm::vec4 forwardThreshold{0,0,-1,.05f},injection{1.5f,.5f,.5f,1};
        glm::uvec4 counts{0};
    };
    static_assert(sizeof(ProducerConstants)==304);
    struct Entry {
        std::weak_ptr<const StrandSegmentSourceData> source;
        std::shared_ptr<Compute::Device> owner;
        std::shared_ptr<const void> textureLease;
        Compute::BufferHandle points,segments,raw;
        RenderResourceHandle<RHITextureSpec> atlas;
        NativeDSVolumeConstants constants;
        glm::vec3 minimum{0},maximum{0};
        float averageWidth=0,maxNormalizedWidth=0;
        std::uint32_t resourceR=0,pointCount=0,segmentCount=0,width=0,height=0;
        std::uint64_t bytes=0;
        bool active=false;
        std::uint32_t injectionSegmentCount=0;
        float injectionWidthScale=0;
    };
    RHIDevice& device_;
    std::shared_ptr<Compute::Device> programs_;
    std::shared_ptr<const void> uniformOwner_;
    RenderResourceHandle<UniformBufferSpec> volumeBuffer_,producerBuffer_;
    Compute::PipelineHandle clear_,raster_,opaque_,copy_,mip_;
    std::vector<Entry> entries_;
    StrandVolumeStatistics statistics_;
    std::uint64_t bytes_=0;
    static void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
    static bool Finite(glm::vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
    static void ValidateMatrix(const glm::mat4& m,const char* message){
        for(int c=0;c<4;++c)for(int r=0;r<4;++r)if(!std::isfinite(m[c][r]))throw std::invalid_argument(message);
        const float determinant=glm::determinant(m);
        if(!std::isfinite(determinant)||std::abs(determinant)<1e-20f)throw std::invalid_argument(message);
    }
    static float RoundEven(float p){
        const float low=std::floor(p),part=p-low;
        return part<.5f?low:part>.5f?low+1.f:std::fmod(low,2.f)==0.f?low:low+1.f;
    }
    static std::uint32_t Groups(std::uint32_t count,std::uint32_t local){return (count+local-1u)/local;}
    static void Upload(RHIFrameEncoder& encoder,RenderResourceHandle<UniformBufferSpec> buffer,const void* data,
                       std::uint32_t bytes,std::uint32_t binding){
        for(std::uint32_t offset=0;offset<bytes;offset+=256)
            Require(encoder.UpdateUniformBufferBytes(buffer,static_cast<const std::byte*>(data)+offset,
                std::min(256u,bytes-offset),offset),"Cannot upload native DSVolume constants");
        Require(encoder.BindUniformBuffer(buffer,binding),"Cannot bind native DSVolume constants");
    }
    void InitializeUniforms(){
        if(uniformOwner_)return;
        ResourceBuilder builder(device_);const auto volume=builder.Uniform(sizeof(NativeDSVolumeConstants));
        const auto producer=builder.Uniform(sizeof(ProducerConstants));auto owner=builder.Finish();
        volumeBuffer_=volume;producerBuffer_=producer;uniformOwner_=std::move(owner);
    }
    void Initialize(){
        if(programs_)return;InitializeUniforms();
        auto owner=std::shared_ptr<Compute::Device>(Compute::Device::Borrow(device_));
        Require(owner->Diagnostics().frameAvailable,"Native DSVolume requires RHI in-frame compute");
        const auto create=[&](const std::string& source,std::uint32_t uniforms,std::uint32_t sampled,std::uint32_t images,const char* label){
            Compute::FramePipelineDesc d;d.source=source;d.label=label;d.uniformMask=uniforms;
            d.sampledMask=sampled;d.storageBuffers=3;d.storageImages=images;d.pushConstantBytes=16;
            return owner->CreateFramePipeline(d);
        };
        const auto clear=create(NativeDSVolumeClearShader(),1u<<14,0,1,"native DSVolume clear packed and atlas");
        const auto raster=create(NativeDSVolumeRasterShader(),(1u<<14)|1u,0,0,"native DSVolume raw segment injection");
        const auto opaque=create(NativeDSVolumeOpaqueShader(),(1u<<14)|1u,1u<<3,0,"native DSVolume camera opaque shell");
        const auto copy=create(NativeDSVolumeCopyShader(),1u<<14,0,1,"native DSVolume packed-to-float copy");
        const auto mip=create(NativeDSVolumeMipShader(),1u<<14,0,1,"native DSVolume RG box mip");
        programs_=std::move(owner);clear_=clear;raster_=raster;opaque_=opaque;copy_=copy;mip_=mip;
    }
    static void ValidateSettings(const NativeDSVolumeSettings& s){
        if((s.resolution!=32&&s.resolution!=64&&s.resolution!=128)||
           !std::isfinite(s.requestedVoxelSize)||s.requestedVoxelSize<=0||
           !std::isfinite(s.averageWidth)||s.averageWidth<=0||
           !std::isfinite(s.widthScale)||s.widthScale<0||s.widthScale>32||
           !std::isfinite(s.rasterTraceStepScale)||s.rasterTraceStepScale<=0||s.rasterTraceStepScale>16||
           !std::isfinite(s.traceGrowth)||s.traceGrowth<1||s.traceGrowth>4||
           !std::isfinite(s.offsetScale)||s.offsetScale<0||s.offsetScale>16||
           !s.traceLimit||s.traceLimit>1024||s.interpolation>1024||
           !std::isfinite(s.threshold)||s.threshold<0||s.threshold>10)
            throw std::invalid_argument("Invalid native DSVolume resolution, authored width or raster/tracing settings");
    }
    static NativeDSVolumeConstants Layout(glm::vec3 minimum,glm::vec3 maximum,const NativeDSVolumeSettings& settings){
        NativeDSVolumeConstants c;c.minimum=glm::vec4(minimum,0);c.maximum=glm::vec4(maximum,0);
        const glm::vec3 extent=maximum-minimum;
        if(!Finite(extent)||glm::any(glm::lessThanEqual(extent,glm::vec3(0))))
            throw std::invalid_argument("Native DSVolume requires nondegenerate raw segment bounds without padding");
        for(int axis=0;axis<3;++axis){
            const float count=RoundEven(std::abs(extent[axis])/settings.requestedVoxelSize);
            if(!std::isfinite(count)||count<2)
                throw std::invalid_argument("Native DSVolume requires at least two logical cells per axis; decrease the requested voxel size");
            c.resolution[axis]=count<float(settings.resolution)?std::uint32_t(count):settings.resolution;
            c.voxel[axis]=count<float(settings.resolution)?settings.requestedVoxelSize:std::abs(extent[axis])/float(settings.resolution);
            c.inverseExtent[axis]=1.f/extent[axis];
        }
        c.resolution.w=6;c.voxel.w=std::min({c.voxel.x,c.voxel.y,c.voxel.z});
        std::uint32_t width=0,height=0;
        for(unsigned level=0;level<6;++level){
            const auto edge=settings.resolution>>level;
            const auto columns=std::uint32_t(std::ceil(std::sqrt(float(edge)))),rows=Groups(edge,columns);
            c.levels[level]={height,edge,columns,rows};width=std::max(width,edge*columns);height+=edge*rows;
        }
        c.atlas={float(width),float(height),float(settings.resolution),1};
        c.trace={settings.traceGrowth,settings.offsetScale,float(settings.traceLimit),float(settings.interpolation)};
        c.opaqueInfo={settings.opaque?1.f:0.f,settings.threshold,settings.hostNegativeLodClamp?1.f:0.f,0};
        return c;
    }
    void PrepareEntry(Entry& e,const StrandRenderItem& item,const NativeDSVolumeSettings& settings){
        const auto source=item.geometry->SegmentSource();
        if(!source||!source->directedChains||source->points.empty()||source->segments.empty()||source->radii.size()!=source->points.size())
            throw std::invalid_argument("Native DSVolume requires immutable original directed segments and physical point radii");
        if(e.owner&&e.source.lock()==source&&e.resourceR==settings.resolution&&e.averageWidth==settings.averageWidth)return;
        if(source->points.size()>std::numeric_limits<std::uint32_t>::max()||source->segments.size()>std::numeric_limits<std::uint32_t>::max())
            throw std::invalid_argument("Native DSVolume input count exceeds uint32 indexing");
        glm::vec3 minimum(std::numeric_limits<float>::max()),maximum(-std::numeric_limits<float>::max());
        std::vector<glm::vec4> points;points.reserve(source->points.size());float maxWidth=0;
        for(std::size_t i=0;i<source->points.size();++i){
            const glm::vec3 position(source->points[i].positionArc);const float radius=source->radii[i],width=2.f*radius/settings.averageWidth;
            if(!Finite(position)||!std::isfinite(width)||!std::isfinite(radius)||radius<0||width<0)
                throw std::invalid_argument("Native DSVolume input position/radius/normalized width must be finite and nonnegative");
            points.emplace_back(position,width);maxWidth=std::max(maxWidth,width);
        }
        for(const auto& segment:source->segments){
            if(segment.x>=points.size()||segment.y>=points.size()||segment.x==segment.y)
                throw std::invalid_argument("Native DSVolume original segment references invalid point ordinals");
            for(const auto id:{segment.x,segment.y}){minimum=glm::min(minimum,glm::vec3(points[id]));maximum=glm::max(maximum,glm::vec3(points[id]));}
        }
        auto constants=Layout(minimum,maximum,settings);const auto limit=programs_->Diagnostics().maxStorageBufferBytes;
        const std::uint64_t pointsBytes=std::uint64_t(points.size())*sizeof(glm::vec4),segmentsBytes=std::uint64_t(source->segments.size())*sizeof(glm::uvec4);
        const auto rawBytes=std::uint64_t(settings.resolution)*settings.resolution*settings.resolution*4u;
        if(pointsBytes>limit||segmentsBytes>limit||rawBytes>limit)throw std::invalid_argument("Native DSVolume storage buffers exceed the RHI device range");
        auto owner=std::shared_ptr<Compute::Device>(Compute::Device::Borrow(device_));
        const auto inputPoints=owner->CreateStorageBuffer(std::size_t(pointsBytes),points.data(),"native DS raw positions / normalized physical widths");
        const auto segments=owner->CreateStorageBuffer(std::size_t(segmentsBytes),source->segments.data(),"native DS original directed segments");
        const auto raw=owner->CreateStorageBuffer(std::size_t(rawBytes),nullptr,"native DS packed R24 G8 resource cube");
        ResourceBuilder builder(device_);const auto width=std::uint32_t(constants.atlas.x),height=std::uint32_t(constants.atlas.y);
        const auto atlas=builder.StorageTexture(width,height,RHITextureFormat::RGBA32F);auto textures=builder.Finish();
        e.source=source;e.owner=std::move(owner);e.textureLease=std::move(textures);e.points=inputPoints;e.segments=segments;e.raw=raw;e.atlas=atlas;
        e.minimum=minimum;e.maximum=maximum;e.maxNormalizedWidth=maxWidth;e.averageWidth=settings.averageWidth;e.resourceR=settings.resolution;
        e.pointCount=std::uint32_t(points.size());e.segmentCount=std::uint32_t(source->segments.size());e.width=width;e.height=height;
        e.bytes=pointsBytes+segmentsBytes+rawBytes+std::uint64_t(width)*height*16u;
    }
    void Dispatch(RHIFrameEncoder& encoder,Compute::PipelineHandle pipeline,const Entry& e,
                  std::uint32_t x,std::uint32_t y=1,std::uint32_t z=1,bool image=false,std::uint32_t level=0){
        RHICommand::DispatchFrameCompute command;command.pipeline=pipeline;command.groupsX=x;command.groupsY=y;command.groupsZ=z;
        if(y==1&&z==1&&x>65535){command.groupsX=65535;command.groupsY=Groups(x,65535);}
        command.buffers[0]=e.points;command.buffers[1]=e.segments;command.buffers[2]=e.raw;
        if(image)command.images[0]=e.atlas;
        const glm::uvec4 push(level,0,0,0);command.pushBytes=sizeof(push);std::memcpy(command.pushConstants.data(),&push,sizeof(push));
        Require(encoder.DispatchCompute(command),"Cannot record native DSVolume compute");++statistics_.passes;
    }
public:
    explicit NativeDSVolumePass(RHIDevice& device):device_(device){}
    // CPU bookkeeping only: disabled legacy materials do not require compute.
    void Begin(std::size_t count){entries_.resize(count);for(auto& e:entries_)e.active=false;statistics_={};bytes_=0;}
    void Prepare(RHIFrameEncoder& encoder,std::size_t index,const StrandRenderItem& item,const StrandPassView& view,const NativeDSVolumeSettings& settings){
        auto& entry=entries_.at(index);entry.active=false;if(!settings.enabled||item.guides)return;
        ValidateSettings(settings);if(!item.geometry)throw std::invalid_argument("Native DSVolume requires strand geometry");
        ValidateMatrix(item.model,"Native DSVolume requires an invertible finite model matrix");
        if(settings.opaque){
            Require(view.opaqueDepth.IsValid(),"Native DSVolume opaque shell requires independent resolved scene depth");
            ValidateMatrix(view.viewProjection,"Native DSVolume opaque shell requires invertible finite view projection");
            if(!Finite(view.cameraForward)||glm::length(view.cameraForward)<1e-8f)throw std::invalid_argument("Native DSVolume opaque shell requires finite camera forward");
        }
        if(!std::isfinite(settings.injectionLodFraction)||settings.injectionLodFraction<0.f||settings.injectionLodFraction>1.f)
            throw std::invalid_argument("Native DSVolume incoming LOD fraction must be finite and within [0,1]");
        Initialize();PrepareEntry(entry,item,settings);auto constants=Layout(entry.minimum,entry.maximum,settings);
        // The original draw caller uses double multiplication and truncation.
        // Keep the full immutable ordered buffer; only the submitted prefix
        // changes, independently of rendered curve tessellation/visibility.
        const auto incoming=std::uint32_t(double(entry.segmentCount)*double(settings.injectionLodFraction));
        const auto injection=NativeDs::ResolveInjectionInputs(entry.segmentCount,incoming,settings.injectionRate,
            settings.widthScale,settings.injectionMinLodFactor,settings.injectionQuality);
        entry.injectionSegmentCount=injection.count;entry.injectionWidthScale=injection.rasterScale;
        constants.worldToDS=glm::inverse(item.model);
        // Guard the source float-to-uint dose domain, without replacing its
        // whole uint32 wrap-add with saturation or clamping its low24 sum.
        const double maximumDose=double(injection.rasterScale)*.005*double(entry.maxNormalizedWidth)/double(constants.voxel.w*100.f)*1000.;
        if(injection.count&&(!std::isfinite(maximumDose)||maximumDose>4000000000.))throw std::invalid_argument("Native DSVolume individual dose exceeds the supported uint32 conversion domain");
        ProducerConstants producer;producer.dsToWorld=item.model;producer.viewProjection=view.viewProjection;
        producer.inverseViewProjection=settings.opaque?glm::inverse(view.viewProjection):glm::mat4(1);
        producer.forwardThreshold=glm::vec4(settings.opaque?glm::normalize(view.cameraForward):glm::vec3(0,0,-1),settings.threshold);
        producer.injection={injection.rasterScale,std::clamp(settings.rasterTraceStepScale,.1f,1.f),constants.voxel.w*100.f,settings.interpolationInjection?1.f:0.f};
        producer.counts={entry.pointCount,injection.count,entry.resourceR*entry.resourceR*entry.resourceR,0};
        Require(encoder.KeepAlive(programs_)&&encoder.KeepAlive(uniformOwner_)&&encoder.KeepAlive(entry.owner)&&
            encoder.KeepAlive(entry.textureLease)&&encoder.KeepAlive(item.geometry),"Cannot pin native DSVolume producer resources");
        Upload(encoder,volumeBuffer_,&constants,sizeof(constants),14);Upload(encoder,producerBuffer_,&producer,sizeof(producer),0);
        const auto cube=entry.resourceR*entry.resourceR*entry.resourceR;
        Dispatch(encoder,clear_,entry,Groups(std::max(cube,entry.width*entry.height),256),1,1,true);
        if(injection.count)Dispatch(encoder,raster_,entry,Groups(injection.count,64));
        if(settings.opaque){
            Require(encoder.BindTexture(view.opaqueDepth,3),"Cannot bind native DSVolume camera depth");
            Dispatch(encoder,opaque_,entry,Groups(constants.resolution.x,4),Groups(constants.resolution.y,4),Groups(constants.resolution.z,4));
            ++statistics_.blockerPasses;++statistics_.blockerUpdated;
        }
        const auto groups=Groups(entry.resourceR,4);Dispatch(encoder,copy_,entry,groups,groups,groups,true);
        for(std::uint32_t level=1;level<6;++level){const auto count=Groups(constants.levels[level].y,4);Dispatch(encoder,mip_,entry,count,count,count,true,level);}
        entry.constants=constants;entry.active=true;++statistics_.updated;statistics_.segments+=injection.count;
        statistics_.nativeInputSegments+=entry.segmentCount;statistics_.nativeInjectedSegments+=injection.count;
        statistics_.nativeInjectionFraction=injection.fraction;statistics_.nativeRasterWidthScale=injection.rasterScale;
        statistics_.nativeAverageWidth=settings.averageWidth;
        statistics_.resolution=std::max(statistics_.resolution,entry.resourceR);
        statistics_.voxelCount+=constants.resolution.x*constants.resolution.y*constants.resolution.z;
        const float modelScale=std::max({glm::length(glm::vec3(item.model[0])),glm::length(glm::vec3(item.model[1])),glm::length(glm::vec3(item.model[2]))});
        const float effectiveVoxel=std::max({constants.voxel.x,constants.voxel.y,constants.voxel.z})*modelScale;
        statistics_.effectiveVoxelSize=std::max(statistics_.effectiveVoxelSize,effectiveVoxel);
        statistics_.effectiveBiasMetres=std::max(statistics_.effectiveBiasMetres,effectiveVoxel*settings.offsetScale);bytes_+=entry.bytes;
    }
    void Bind(RHIFrameEncoder& encoder,std::size_t index,std::uint32_t textureSlot=4){
        InitializeUniforms();const auto& entry=entries_.at(index);const NativeDSVolumeConstants disabled;
        const auto& constants=entry.active?entry.constants:disabled;
        Require(encoder.KeepAlive(uniformOwner_),"Cannot pin native DSVolume binding constants");
        Upload(encoder,volumeBuffer_,&constants,sizeof(constants),14);
        // Legacy Bind has already supplied texture4. A disabled native item
        // must not replace that density/fallback image in a mixed batch.
        if(entry.active)Require(encoder.KeepAlive(entry.owner)&&encoder.KeepAlive(entry.textureLease)&&
            encoder.BindTexture(entry.atlas,textureSlot),"Cannot bind native DSVolume atlas");
    }
    NativeDSVolumeSnapshot Snapshot(std::size_t index)const{
        const auto& e=entries_.at(index);return {e.owner,e.textureLease,e.atlas,e.points,e.segments,e.raw,e.constants,e.width,e.height,e.pointCount,e.segmentCount,e.bytes,e.active,e.injectionSegmentCount,e.injectionWidthScale};
    }
    const StrandVolumeStatistics& Statistics()const{return statistics_;}
    std::uint64_t Bytes()const{return bytes_;}
    void Invalidate(){for(auto& e:entries_)e.active=false;}
    void Shutdown(){entries_.clear();programs_.reset();uniformOwner_.reset();volumeBuffer_={};producerBuffer_={};clear_={};raster_={};opaque_={};copy_={};mip_={};statistics_={};bytes_=0;}
};
} // namespace Render::PipelineDetail
