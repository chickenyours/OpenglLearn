#pragma once
#include "Render/Private/Pipeline/fiber_volume_shaders.h"
#include "Render/Private/Pipeline/post_process_shaders.h"
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Private/Pipeline/strand_render_pass.h"
#include <glm/gtc/matrix_inverse.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Render::PipelineDetail {
struct alignas(16) FiberVolumeConstants {
    glm::mat4 inverseModel{1};
    glm::vec4 minimumStep{0},size{1},atlas{1,1,1,0},trace{1.1f,1,0,0},sun{0};
    std::array<glm::vec4,4> points{};
    glm::vec4 cameraLocal{0}; // xyz camera; w legacy far-start bias in voxels
    glm::mat4 model{1},viewProjection{1},inverseViewProjection{1};
    glm::vec4 cameraForward{0,0,-1,0};
    glm::vec4 opaque{0,.05f,0,0}; // enabled, shell threshold, trace pass, hardware filter
    glm::vec4 quality{0}; // exact integration, screen shell, proxy enabled, capsule count
    std::array<glm::vec4,32> capsules{}; // <=16 local-space startRadius/endRadius pairs
};
static_assert(sizeof(FiberVolumeConstants)==976);
static_assert(offsetof(FiberVolumeConstants,cameraLocal)==208);
static_assert(offsetof(FiberVolumeConstants,model)==224);
static_assert(offsetof(FiberVolumeConstants,opaque)==432);
static_assert(offsetof(FiberVolumeConstants,quality)==448);
static_assert(offsetof(FiberVolumeConstants,capsules)==464);
inline void FiberVolumeUpload(RHIFrameEncoder& encoder,RenderResourceHandle<UniformBufferSpec> buffer,const FiberVolumeConstants& data) {
    const auto* bytes=reinterpret_cast<const std::byte*>(&data);
    for(std::uint32_t offset=0;offset<sizeof(data);offset+=256)
        if(!encoder.UpdateUniformBufferBytes(buffer,bytes+offset,std::min(256u,std::uint32_t(sizeof(data))-offset),offset))
            throw std::runtime_error("Cannot upload spatial fiber volume constants");
    if(!encoder.BindUniformBuffer(buffer,5))throw std::runtime_error("Cannot bind spatial fiber volume constants");
}
class FiberVolumePass {
    RHIDevice& device_;
    std::shared_ptr<const void> programs_;
    RenderResourceHandle<PipelineSpec> raster_,trace_,opaqueShell_,opaqueTrace_;
    RenderResourceHandle<VertexBufferSpec> fullscreen_;
    RenderResourceHandle<UniformBufferSpec> buffer_;
    RenderResourceHandle<RHITextureSpec> fallback_;
    struct Cache {
        std::shared_ptr<const void> owner;
        std::shared_ptr<const void> blockersOwner;
        RenderResourceHandle<VertexBufferSpec> mesh;
        std::uint64_t revision=0;
        FiberVolumeConstants constants;
        StrandVolumeDebugTextures textures;
        std::array<RenderResourceHandle<RenderTargetSpec>,4> targets;
        std::array<RenderResourceHandle<RenderTargetSpec>,3> blockerTargets;
        RenderResourceHandle<RHITextureSpec> opaqueDepth;
        std::uint64_t opaqueRevision=0;
        std::uint32_t blockerWidth=0,blockerHeight=0;
        RHITextureFormat densityFormat=RHITextureFormat::RGBA8;
        bool blockersActive=false;
        bool stableBlockerView=false;
        glm::mat4 blockerPhysicalViewProjection{1},blockerCapturedViewProjection{1};
    };
    std::vector<Cache> caches_;
    StrandVolumeStatistics statistics_;
    StrandVolumeDebugTextures debug_;
    static void Require(bool success,const char* message){if(!success)throw std::runtime_error(message);}
    void Initialize(bool spatial) {
        if(programs_)return;
        ResourceBuilder builder(device_);
        buffer_=builder.Uniform(sizeof(FiberVolumeConstants));fallback_=builder.Texture(1,1,RHITextureFormat::RGBA16F);
        fullscreen_=builder.FullscreenMesh();
        // Defer GS creation until a caller requests spatial transport. Legacy
        // strand rendering remains available on backends without GS support.
        programs_=builder.Finish();
    }
    void InitializeSpatial() {
        if(raster_.IsValid())return;
        struct Feature {bool done=false,supported=false;};auto feature=std::make_shared<Feature>();
        device_.async_ExecuteCode([this,feature]{feature->supported=device_.BackendDiagnostics()->GeometryShadersAvailable();},[feature]{feature->done=true;});
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(!feature->done){device_.returnSystem.DrainCallbacks();if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Fiber volume capability query timed out");if(!feature->done)device_.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));}
        Require(feature->supported,"Spatial fiber volume requires the RHI geometry-shader capability");
        ResourceBuilder builder(device_);
        CreateGraphicShaderDesc shader;
        shader.vertexShaderSource=builder.Source(ShaderSourceType::Vertex,FiberVolumeVertexShader());
        shader.geometryShaderSource=builder.Source(ShaderSourceType::Geometry,FiberVolumeGeometryShader());
        shader.fragmentShaderSource=builder.Source(ShaderSourceType::Fragment,FiberVolumeDensityShader());
        auto program=builder.Create<ShaderProgramSpec>([&](auto cb){device_.async_CreateGraphicShader(shader,cb);});
        PipelineSpec spec;spec.shaderProgram=program;spec.expectVertexLayout={{VertexFieldType::Vec4,VertexFieldType::Vec4,VertexFieldType::Vec2,VertexFieldType::Vec4,VertexFieldType::Vec4}};
        spec.depthTest=spec.depthWrite=false;spec.cullMode=CullMode::None;spec.blendEnable=true;spec.blendMode=BlendMode::AdditiveRaw;
        raster_=builder.Create<PipelineSpec>([&](auto cb){device_.async_CreatePipeline({spec},cb);});
        trace_=builder.FullscreenPipeline(FullscreenVertexShader(),FiberVolumePathsShader());
        opaqueShell_=builder.FullscreenPipeline(FullscreenVertexShader(),FiberVolumeOpaqueShellShader());
        opaqueTrace_=builder.FullscreenPipeline(FullscreenVertexShader(),FiberVolumeOpaquePathsShader());
        // Keep both bootstrap groups, including their asynchronous delete owners.
        programs_=std::make_shared<std::array<std::shared_ptr<const void>,2>>(std::array{programs_,builder.Finish()});
    }
public:
    explicit FiberVolumePass(RHIDevice& device):device_(device){}
    void Begin(std::size_t count) {statistics_={};debug_={};caches_.resize(count);Initialize(false);}
    const StrandVolumeStatistics& Statistics()const{return statistics_;}
    StrandVolumeDebugTextures DebugTextures()const{return debug_;}
    void Invalidate(){for(auto& cache:caches_){cache.mesh={};cache.revision=0;cache.blockersActive=false;cache.stableBlockerView=false;}}
    void Prepare(RHIFrameEncoder& encoder,std::size_t index,const StrandRenderItem& item,const StrandPassView& view) {
        if(!item.material.spatialVolume||item.guides)return;
        if(item.material.sourceDSVolume) {
            // Native DS owns both occupancy and sampling. Publish only the
            // shared lighting-space flags; never build old inverse-metre paths
            // and accidentally consume them as native dimensionless N.
            auto& cache=caches_[index];cache={};
            cache.constants.inverseModel=glm::inverse(item.model);
            cache.constants.atlas.w=1.f;
            cache.constants.trace.w=item.material.volumeSkyTransmission?(item.material.dualScatteringIbl?2.f:1.f):0.f;
            return;
        }
        InitializeSpatial();auto& cache=caches_[index];const auto& m=item.material;
        if(!std::isfinite(m.volumeVoxelSize)||m.volumeVoxelSize<=0||m.volumeResolution<8||m.volumeResolution>128||
            !std::isfinite(m.volumeRasterWidthScale)||m.volumeRasterWidthScale<=0||m.volumeRasterWidthScale>16||
            !std::isfinite(m.volumeAverageWidth)||m.volumeAverageWidth<0||m.volumeAverageWidth>1||
            !std::isfinite(m.volumeTraceStep)||m.volumeTraceStep<1||m.volumeTraceStep>2||
            !std::isfinite(m.volumeBiasVoxels)||m.volumeBiasVoxels<0||m.volumeBiasVoxels>8||
            !std::isfinite(m.volumeDepthThreshold)||m.volumeDepthThreshold<0||m.volumeDepthThreshold>10)
            throw std::invalid_argument("Invalid spatial fiber volume quality or tracing parameters");
        const float determinant=glm::determinant(item.model);
        if(!std::isfinite(determinant)||std::abs(determinant)<1e-12f)throw std::invalid_argument("Spatial fiber volume requires an invertible model transform");
        FiberVolumeConstants constants;constants.inverseModel=glm::inverse(item.model);
        glm::vec3 extent=glm::max(item.geometry->BoundsMax()-item.geometry->BoundsMin(),glm::vec3(1e-6f));
        float h=std::max(m.volumeVoxelSize,std::max({extent.x,extent.y,extent.z})/float(m.volumeResolution-2));
        glm::uvec3 size=glm::min(glm::uvec3(glm::ceil(extent/h))+glm::uvec3(2),glm::uvec3(m.volumeResolution));
        std::uint32_t columns=std::uint32_t(std::ceil(std::sqrt(float(size.z))));
        std::uint32_t width=size.x*columns,height=size.y*((size.z+columns-1)/columns);
        constants.minimumStep={item.geometry->BoundsMin()-glm::vec3(h),h};
        constants.size=glm::vec4(glm::vec3(size),float(columns));
        // Native DS raster normalizes packed width by DSInfo_StrandWidthAve
        // and uses a .005 cm reference width; hm_scaleWidth is absent there.
        // Recovered assets can supply their serialized average full width.
        // Generic assets fall back to twice the unweighted mean radius,
        // including zero-width points; this fallback is a host convention.
        const float meanRadius=item.geometry->MeanRadius();
        const float averageWidth=m.volumeAverageWidth>0?m.volumeAverageWidth:2.f*meanRadius;
        const float densityWidthScale=m.normalizedVolumeWidth?
            (meanRadius>1e-12f&&averageWidth>1e-12f?m.volumeRasterWidthScale*(.00005f/averageWidth):0.f):
            m.volumeRasterWidthScale*m.widthScale;
        constants.atlas={float(width),float(height),densityWidthScale,1};
        // StrandLights.strandAmbient is the same PbrPassConstants RGB at byte128.
        // It remains incident environment light when Sky and the cube are off.
        const bool ambientActive=glm::any(glm::greaterThan(glm::vec3(view.lighting.ambientAndExposure),glm::vec3(0.f)));
        const float skyMode=m.volumeSkyTransmission&&(view.sky.options.x>.5f||view.indirect.enabled||ambientActive)?(m.dualScatteringIbl?2.f:1.f):0.f;
        // Preserve the source bias coefficient. This opt-in host adapter only
        // prevents a coarser budget from expanding the requested physical
        // distance: the shader still expresses that distance in actual voxels.
        const float biasMetres=(m.volumePreserveNearField?std::min(h,m.volumeVoxelSize):h)*m.volumeBiasVoxels;
        constants.trace={m.volumeTraceStep,biasMetres/h,0,skyMode};
        // Axis-forward transport is camera independent. Native two-direction
        // paths depend on the camera-derived radial basis of each voxel.
        if(skyMode>1.5f)constants.cameraLocal=constants.inverseModel*glm::vec4(view.cameraPosition,1);
        // Add the omitted near interval without shifting the established far
        // quadrature. Only xyz is a camera position; w is a private trace value.
        constants.cameraLocal.w=m.volumePreserveNearField?m.volumeBiasVoxels:0.f;
        constants.sun=constants.inverseModel*glm::vec4(-view.sunDirection,0);
        constants.sun.w=view.sunIntensity>0?1.f:0.f;
        for(int i=0;i<4;++i){constants.points[i]=constants.inverseModel*glm::vec4(glm::vec3(view.lighting.lightPositions[i]),1);constants.points[i].w=glm::length(glm::vec3(view.lighting.lightColors[i]))>0?1.f:0.f;}
        const bool proxyEnabled=m.volumeProxyBlockers&&item.opaqueCapsules&&!item.opaqueCapsules->empty();
        if(proxyEnabled) {
            if(item.opaqueCapsules->size()>16)throw std::invalid_argument("Fiber opaque proxy supports at most 16 tapered capsules");
            for(std::size_t i=0;i<item.opaqueCapsules->size();++i) {
                const auto& capsule=(*item.opaqueCapsules)[i];
                for(const auto& value:{capsule.startRadius,capsule.endRadius})
                    if(!std::isfinite(value.x)||!std::isfinite(value.y)||!std::isfinite(value.z)||!std::isfinite(value.w)||
                        value.w<0.f||value.w>100.f||glm::any(glm::greaterThan(glm::abs(glm::vec3(value)),glm::vec3(1e6f))))
                        throw std::invalid_argument("Fiber opaque proxy requires finite local positions and nonnegative metre radii");
                constants.capsules[2*i]=capsule.startRadius;constants.capsules[2*i+1]=capsule.endRadius;
            }
            constants.quality.z=1.f;constants.quality.w=float(item.opaqueCapsules->size());
        }
        if(m.volumeOpaqueBlockers) {
            Require(view.opaqueDepth.IsValid(),"Opaque fiber blockers require independent resolved opaque depth");
            const float forwardLength=glm::length(view.cameraForward),cameraDeterminant=glm::determinant(view.viewProjection);
            if(!std::isfinite(forwardLength)||forwardLength<1e-8f||!std::isfinite(cameraDeterminant)||std::abs(cameraDeterminant)<1e-20f)
                throw std::invalid_argument("Opaque fiber blockers require a finite camera forward and invertible view projection");
            for(int c=0;c<4;++c)for(int r=0;r<4;++r)if(!std::isfinite(view.viewProjection[c][r]))
                throw std::invalid_argument("Opaque fiber blocker view projection must be finite");
            constants.model=item.model;constants.viewProjection=view.viewProjection;constants.inverseViewProjection=glm::inverse(view.viewProjection);
            constants.cameraForward=glm::vec4(view.cameraForward/forwardLength,0);
            constants.opaque={1.f,m.volumeDepthThreshold,0,0};
            constants.quality.y=1.f;
        }
        const bool blockersEnabled=m.volumeOpaqueBlockers||proxyEnabled;
        const bool stableBlockerView=m.volumeOpaqueBlockers&&m.volumeStableOpaqueCache&&view.blockerStableViewValid;
        if(stableBlockerView) {
            for(int c=0;c<4;++c)for(int r=0;r<4;++r)
                if(!std::isfinite(view.blockerUnjitteredViewProjection[c][r]))
                    throw std::invalid_argument("Stable opaque blocker view must be finite");
            const float stableDeterminant=glm::determinant(view.blockerUnjitteredViewProjection);
            if(!std::isfinite(stableDeterminant)||std::abs(stableDeterminant)<1e-20f)
                throw std::invalid_argument("Stable opaque blocker view must be invertible");
        }
        constants.opaque.x=blockersEnabled?1.f:0.f;
        constants.opaque.w=m.volumeHardwareFiltering?1.f:0.f;
        constants.quality.x=m.volumeExactIntegration?1.f:0.f;
        // Density is local geometry only. Inverse model/light/camera changes
        // invalidate paths without re-voxelizing the immutable mesh stream.
        const bool densityFormatDirty=cache.densityFormat!=RHITextureFormat::RGBA32F;
        const bool geometryDirty=!cache.owner||densityFormatDirty||cache.mesh!=item.geometry->Mesh()||cache.revision!=item.geometry->Revision()||
            std::memcmp(&cache.constants.minimumStep,&constants.minimumStep,3*sizeof(glm::vec4))!=0;
        const bool filterDirty=cache.constants.opaque.w!=constants.opaque.w;
        const bool integrationDirty=cache.constants.quality.x!=constants.quality.x;
        const bool pathsDirty=geometryDirty||filterDirty||integrationDirty||std::memcmp(&cache.constants,&constants,offsetof(FiberVolumeConstants,model))!=0;
        const bool proxyDirty=std::memcmp(&cache.constants.quality.y,&constants.quality.y,3*sizeof(float))!=0||
            std::memcmp(cache.constants.capsules.data(),constants.capsules.data(),sizeof(constants.capsules))!=0;
        const bool blockerViewModeDirty=cache.stableBlockerView!=stableBlockerView;
        // A stable cache key is not a substitute projection for the input
        // depth. Injection still uses this frame's real jittered VP/depth.
        // Retaining that first sampled world occupancy avoids retracing G for
        // TAA phases alone; it intentionally does not average those phases.
        const bool blockerViewDirty=stableBlockerView?
            (std::memcmp(&cache.blockerPhysicalViewProjection,&view.blockerUnjitteredViewProjection,sizeof(glm::mat4))!=0||
                std::memcmp(&cache.constants.model,&constants.model,sizeof(glm::mat4))!=0||
                std::memcmp(&cache.constants.cameraForward,&constants.cameraForward,sizeof(glm::vec4))!=0):
            std::memcmp(&cache.constants.model,&constants.model,offsetof(FiberVolumeConstants,opaque)-offsetof(FiberVolumeConstants,model))!=0;
        const bool screenDirty=m.volumeOpaqueBlockers&&(cache.opaqueDepth!=view.opaqueDepth||cache.opaqueRevision!=view.opaqueContentRevision||
            blockerViewModeDirty||blockerViewDirty||cache.constants.opaque.y!=constants.opaque.y);
        const bool blockerShellDirty=blockersEnabled&&(geometryDirty||!cache.blockersActive||proxyDirty||screenDirty);
        const bool blockerPathsDirty=blockersEnabled&&(blockerShellDirty||pathsDirty);
        if(!cache.owner||densityFormatDirty||cache.textures.width!=width||cache.textures.height!=height) {
            ResourceBuilder builder(device_);cache.textures.width=width;cache.textures.height=height;
            // Repeated half-float framebuffer additions lose small strand
            // contributions in occupied cells. Accumulate density and signed
            // tangent moments in float32; optical-path storage stays half.
            cache.textures.density=builder.Texture(width,height,RHITextureFormat::RGBA32F);
            cache.textures.paths=builder.Texture(width,height,RHITextureFormat::RGBA16F);
            cache.textures.pathsExtra=builder.Texture(width,height,RHITextureFormat::RGBA16F);
            cache.textures.pathsAmbient=builder.Texture(width,height,RHITextureFormat::RGBA16F);
            cache.targets={builder.Target(cache.textures.density,{}),builder.Target(cache.textures.paths,{}),builder.Target(cache.textures.pathsExtra,{}),builder.Target(cache.textures.pathsAmbient,{})};
            cache.owner=builder.Finish();
            cache.densityFormat=RHITextureFormat::RGBA32F;
        }
        Require(encoder.KeepAlive(programs_)&&encoder.KeepAlive(cache.owner)&&encoder.KeepAlive(item.geometry),"Cannot pin spatial fiber resources");
        if(pathsDirty) {
            auto bind=[&](std::size_t pass){RHICommand::SetRenderTarget target;target.target=cache.targets[pass];target.width=width;target.height=height;target.clearFlags=RHICommand::ClearColor;target.clearColor=glm::vec4(0);Require(encoder.SetRenderTarget(target),"Cannot bind spatial fiber atlas");};
            if(geometryDirty) {
                FiberVolumeUpload(encoder,buffer_,constants);bind(0);
                Require(encoder.BindPipeline(raster_)&&encoder.BindMesh(item.geometry->Mesh())&&encoder.DrawIndexed({item.geometry->IndexCount()}),"Cannot voxelize physical fiber segments");
                ++statistics_.passes;
            }
            for(int pass=0;pass<3;++pass){auto traceConstants=constants;traceConstants.trace.z=float(pass);FiberVolumeUpload(encoder,buffer_,traceConstants);bind(pass+1);
                // Switch away from the density attachment before sampling it.
                // All direct and environment destinations are independent.
                Require(encoder.BindTexture(cache.textures.density,12),"Cannot bind spatial fiber density");
                Require(encoder.BindPipeline(trace_)&&encoder.BindMesh(fullscreen_)&&encoder.DrawIndexed({3}),"Cannot integrate spatial fiber optical paths");}
            cache.mesh=item.geometry->Mesh();cache.revision=item.geometry->Revision();
            statistics_.passes+=3;++statistics_.updated;
        }
        if(blockersEnabled) {
            if(!cache.blockersOwner||cache.blockerWidth!=width||cache.blockerHeight!=height) {
                ResourceBuilder builder(device_);
                cache.textures.opaqueBlockers=builder.Texture(width,height,RHITextureFormat::RGBA16F);
                cache.textures.directVisibility=builder.Texture(width,height,RHITextureFormat::RGBA16F);
                cache.textures.extraVisibility=builder.Texture(width,height,RHITextureFormat::RGBA16F);
                cache.blockerTargets={builder.Target(cache.textures.opaqueBlockers,{}),
                    builder.Target(cache.textures.directVisibility,{}),builder.Target(cache.textures.extraVisibility,{})};
                cache.blockersOwner=builder.Finish();cache.blockerWidth=width;cache.blockerHeight=height;
            }
            Require(encoder.KeepAlive(cache.blockersOwner),"Cannot pin opaque fiber blocker resources");
            const auto bindBlocker=[&](std::size_t pass) {
                RHICommand::SetRenderTarget target;target.target=cache.blockerTargets[pass];target.width=width;target.height=height;
                Require(encoder.SetRenderTarget(target),"Cannot bind opaque fiber blocker atlas");
            };
            if(blockerShellDirty) {
                bindBlocker(0);FiberVolumeUpload(encoder,buffer_,constants);
                Require(encoder.BindTexture(m.volumeOpaqueBlockers?view.opaqueDepth:fallback_,12)&&encoder.BindPipeline(opaqueShell_)&&encoder.BindMesh(fullscreen_)&&
                    encoder.DrawIndexed({3}),"Cannot inject screen shell / local opaque proxy fiber blockers");
                if(m.volumeOpaqueBlockers)cache.blockerCapturedViewProjection=constants.viewProjection;
                ++statistics_.passes;++statistics_.blockerPasses;
            }
            if(blockerPathsDirty) {
                for(int pass=0;pass<2;++pass) {
                    auto blockerConstants=constants;blockerConstants.opaque.z=float(pass);
                    bindBlocker(pass+1);FiberVolumeUpload(encoder,buffer_,blockerConstants);
                    Require(encoder.BindTexture(cache.textures.density,12)&&encoder.BindTexture(cache.textures.opaqueBlockers,13)&&
                        encoder.BindPipeline(opaqueTrace_)&&encoder.BindMesh(fullscreen_)&&encoder.DrawIndexed({3}),
                        "Cannot trace opaque fiber blocker visibility");
                }
                statistics_.passes+=2;statistics_.blockerPasses+=2;++statistics_.blockerUpdated;
            }
        }
        cache.constants=constants;cache.blockersActive=blockersEnabled;
        cache.stableBlockerView=stableBlockerView;
        cache.blockerPhysicalViewProjection=stableBlockerView?view.blockerUnjitteredViewProjection:glm::mat4(1);
        cache.opaqueDepth=view.opaqueDepth;cache.opaqueRevision=view.opaqueContentRevision;
        statistics_.resolution=std::max(statistics_.resolution,std::max({size.x,size.y,size.z}));
        statistics_.voxelCount+=size.x*size.y*size.z;statistics_.segments+=item.geometry->IndexCount()/6;
        const float modelScale=std::max({glm::length(glm::vec3(item.model[0])),glm::length(glm::vec3(item.model[1])),glm::length(glm::vec3(item.model[2]))});
        statistics_.effectiveVoxelSize=std::max(statistics_.effectiveVoxelSize,h*modelScale);debug_=cache.textures;
        statistics_.effectiveBiasMetres=std::max(statistics_.effectiveBiasMetres,biasMetres*modelScale);
        if(!blockersEnabled){debug_.opaqueBlockers={};debug_.directVisibility={};debug_.extraVisibility={};}
    }
    void Bind(RHIFrameEncoder& encoder,std::size_t index,const StrandRenderItem& item) {
        auto& cache=caches_[index];const bool active=item.material.spatialVolume&&!item.guides&&cache.owner;
        FiberVolumeConstants constants;if(active||(item.material.sourceDSVolume&&item.material.spatialVolume&&!item.guides))constants=cache.constants;
        const bool blockersActive=active&&cache.blockersActive&&cache.blockersOwner;
        constants.opaque.x=blockersActive?1.f:0.f;
        FiberVolumeUpload(encoder,buffer_,constants);
        Require(encoder.KeepAlive(programs_)&&(!active||encoder.KeepAlive(cache.owner))&&
            (!blockersActive||encoder.KeepAlive(cache.blockersOwner)),"Cannot pin spatial fiber bindings");
        Require(encoder.BindTexture(active?cache.textures.density:fallback_,4)&&
            encoder.BindTexture(blockersActive?cache.textures.opaqueBlockers:fallback_,5)&&
            encoder.BindTexture(active?cache.textures.paths:fallback_,12)&&encoder.BindTexture(active?cache.textures.pathsExtra:fallback_,13)&&
            encoder.BindTexture(active?cache.textures.pathsAmbient:fallback_,15)&&
            encoder.BindTexture(blockersActive?cache.textures.directVisibility:fallback_,10)&&
            encoder.BindTexture(blockersActive?cache.textures.extraVisibility:fallback_,11),"Cannot bind fiber optical paths and opaque visibility");
    }
    void Reset(){caches_.clear();programs_.reset();raster_={};trace_={};opaqueShell_={};opaqueTrace_={};statistics_={};debug_={};}
};
} // namespace Render::PipelineDetail
