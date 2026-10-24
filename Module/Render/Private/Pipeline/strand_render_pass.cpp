#include "Render/Private/Pipeline/strand_render_pass.h"
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Private/Pipeline/post_process_shaders.h"
#include "Render/Private/Pipeline/strand_shaders.h"
#include "Render/Private/Pipeline/strand_temporal_shaders.h"
#include "Render/Private/Pipeline/strand_motion_shaders.h"
#include "Render/Private/Pipeline/fiber_moment_lut.h"
#include "Render/Private/Pipeline/fiber_native_moment_lut.h"
#include "Render/Private/Pipeline/fiber_volume.h"
#include "Render/Private/Pipeline/strand_segment_pass.h"
#include "Render/Private/Pipeline/strand_curve_pass.h"
#include "Render/Private/Pipeline/strand_native_oit_pass.h"
#include "Render/Private/Pipeline/strand_native_ds_volume.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Render::PipelineDetail {
namespace {
void StrandRequire(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct alignas(16) StrandViewConstants {
    glm::mat4 viewProjection{1}; glm::vec4 camera{0}, viewport{1};
    glm::mat4 previousViewProjection{1},currentUnjitteredViewProjection{1},previousUnjitteredViewProjection{1};
    glm::vec4 previousCamera{0},motionState{0};
};
static_assert(sizeof(StrandViewConstants)==320);
static_assert(offsetof(StrandViewConstants,previousViewProjection)==96);
static_assert(offsetof(StrandViewConstants,motionState)==304);
struct alignas(16) StrandMotionObjectConstants {glm::mat4 previousModel{1};glm::vec4 flags{0};};
static_assert(sizeof(StrandMotionObjectConstants)==80);
struct alignas(16) StrandMaterialConstants {
    glm::vec4 rootColor{1}, tipColor{1}, surface{1}, volume{1}, raster{1}, fiber{1}, scattering{1}, shadows{0};
    glm::vec4 ibl{1,0,0,0};
    glm::vec4 colorWeights{1,0,0,0},colorOffsets{0},colorVariation{0};
    glm::vec4 nativeMoments{0};
    glm::vec4 spatialIncident{0};
    glm::vec4 opaqueShadowParameters{1,0,0,0};
    FiberMomentTable moments{};
};
static_assert(sizeof(StrandMaterialConstants)==240+sizeof(FiberMomentTable));
static_assert(offsetof(StrandMaterialConstants,ibl)==128);
static_assert(offsetof(StrandMaterialConstants,colorWeights)==144);
static_assert(offsetof(StrandMaterialConstants,nativeMoments)==192);
static_assert(offsetof(StrandMaterialConstants,spatialIncident)==208);
static_assert(offsetof(StrandMaterialConstants,opaqueShadowParameters)==224);
static_assert(offsetof(StrandMaterialConstants,moments)==240);
struct alignas(16) StrandSunConstants { glm::vec4 direction{0}, color{0}; };
struct alignas(16) StrandIndirectConstants {
    std::array<glm::vec4,6> coefficients{};
    glm::vec4 options{0,1,1,0};
};
static_assert(sizeof(StrandIndirectConstants)==112);
static_assert(offsetof(StrandIndirectConstants,options)==96);
bool StrandFinite(glm::vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
StrandIndirectConstants IndirectConstants(const StrandIndirectLightingSettings& settings) {
    if(!std::isfinite(settings.multiplier)||settings.multiplier<0||settings.multiplier>64||
        !std::isfinite(settings.exposureEV)||settings.exposureEV<-32||settings.exposureEV>32||
        !std::isfinite(settings.rotation)||std::abs(settings.rotation)>1e6f||
        !std::isfinite(settings.hostAmbientScale)||settings.hostAmbientScale<0||settings.hostAmbientScale>1)
        throw std::invalid_argument("Invalid strand indirect producer gain, unit exposure, rotation or host ambient scale");
    StrandIndirectConstants result;
    for(std::size_t i=0;i<settings.coefficients.size();++i) {
        const auto& value=settings.coefficients[i];
        if(!StrandFinite(value)||glm::any(glm::lessThan(value,glm::vec3(0)))||glm::any(glm::greaterThan(value,glm::vec3(1e12f))))
            throw std::invalid_argument("Invalid scene-linear strand ambient cube coefficients");
        result.coefficients[i]=glm::vec4(value,0);
    }
    result.options={settings.enabled?1.f:0.f,settings.multiplier*std::exp2(settings.exposureEV),std::cos(settings.rotation),std::sin(settings.rotation)};
    return result;
}
void StrandUpload(RHIFrameEncoder& encoder, RenderResourceHandle<UniformBufferSpec> buffer,
                  const void* data, uint32_t bytes, uint32_t binding) {
    const auto* source = static_cast<const std::byte*>(data);
    for (uint32_t offset = 0; offset < bytes; offset += 256)
        StrandRequire(encoder.UpdateUniformBufferBytes(buffer, source+offset, std::min(256u, bytes-offset), offset),
            "Cannot upload strand pass constants");
    StrandRequire(encoder.BindUniformBuffer(buffer, binding), "Cannot bind strand pass constants");
}
bool CachedValidNativeMomentTable(const std::shared_ptr<const StrandTextureTable>& table) {
    struct Entry {
        std::weak_ptr<const StrandTextureTable> owner;
        const StrandTextureTable* address;
        bool valid;
    };
    // Tables are immutable. Retain only weak ownership, and compare both the
    // control block and alias address so allocator/address reuse cannot turn a
    // different table into a cache hit. Recording on separate threads has no
    // shared mutable validation state.
    static thread_local std::vector<Entry> validated;
    for(const auto& entry:validated)
        if(entry.address==table.get()&&!entry.owner.owner_before(table)&&!table.owner_before(entry.owner))
            return entry.valid;
    std::erase_if(validated,[](const Entry& entry){return entry.owner.expired();});
    const bool valid=ValidFiberNativeMomentTable(*table);
    validated.push_back({table,table.get(),valid});
    return valid;
}
StrandMaterialConstants StrandConstants(const StrandRenderItem& item,StrandDebugMode debugMode) {
    const auto& material = item.material;
    if(!std::isfinite(item.strandCountFraction)||item.strandCountFraction<0||item.strandCountFraction>1||
        !std::isfinite(material.minWidthMetres)||material.minWidthMetres<0||
        !std::isfinite(material.maxWidthMetres)||material.maxWidthMetres<0||
        (material.maxWidthMetres>0&&material.maxWidthMetres<material.minWidthMetres))
        throw std::invalid_argument("Invalid strand LOD fraction or physical full-width bounds");
    const float widthCompensation=item.strandCountFraction>1e-5f?1.f/item.strandCountFraction:100000.f;
    if(!std::isfinite(material.widthScale*widthCompensation)||
        !std::isfinite(material.minWidthMetres*widthCompensation)||!std::isfinite(material.maxWidthMetres*widthCompensation))
        throw std::invalid_argument("Strand LOD compensated width overflow");
    const std::array<float, 20> values{material.roughness, material.secondaryRoughness,
        material.specularStrength, material.secondarySpecular, material.transmission,
        material.widthScale, material.opacity, material.cuticleShift, material.minPixelWidth, material.maxPixelWidth,
        material.ior,material.longitudinalRoughness,material.azimuthalRoughness,material.absorptionScale,
        material.multipleScattering,material.scatteringDensity,material.albedoBlendRate,material.indirectLightingMultiplier,material.backscatterScale,
        material.opaqueShadowPower};
    if (!StrandFinite(material.rootColor) || !StrandFinite(material.tipColor) ||
        !StrandFinite(material.gradientWeights)||!StrandFinite(material.hsvVariation)||
        !StrandFinite(glm::vec3(material.colorRandomOffsets))||!std::isfinite(material.colorRandomOffsets.w)||
        glm::any(glm::lessThan(material.gradientWeights,glm::vec3(0)))||
        glm::any(glm::greaterThan(material.gradientWeights,glm::vec3(1e6f)))||
        glm::any(glm::lessThan(material.hsvVariation,glm::vec3(0)))||glm::any(glm::greaterThan(material.hsvVariation,glm::vec3(1)))||
        (material.sourceColor&&!material.gradientTable)||
        std::any_of(values.begin(), values.end(), [](float value) { return !std::isfinite(value); }) ||
        material.roughness < .045f || material.roughness > 1 || material.secondaryRoughness < .045f || material.secondaryRoughness > 1 ||
        material.specularStrength < 0 || material.secondarySpecular < 0 || material.transmission < 0 ||
        material.widthScale <= 0 || material.opacity < 0 || material.opacity > 1 ||
        material.minPixelWidth < .05f || material.maxPixelWidth < material.minPixelWidth || material.maxPixelWidth > 128 ||
        material.ior < 1.f || material.ior > 3.f || material.longitudinalRoughness < .02f || material.longitudinalRoughness > 1.f ||
        material.azimuthalRoughness < .02f || material.azimuthalRoughness > 1.f || material.absorptionScale < 0.f || material.absorptionScale > 10.f ||
        material.multipleScattering < 0.f || material.multipleScattering > 1.f || material.scatteringDensity < 0.f || material.scatteringDensity > 16.f ||
        material.albedoBlendRate < 0.f || material.albedoBlendRate > 1.f || material.indirectLightingMultiplier < 0.f || material.indirectLightingMultiplier > 8.f ||
        material.backscatterScale < 0.f || material.backscatterScale > 4.f ||
        material.opaqueShadowPower < .05f || material.opaqueShadowPower > 4.f)
        throw std::invalid_argument("Invalid strand material or raster coverage parameters");
    for (int c=0;c<4;++c) for (int r=0;r<4;++r)
        if (!std::isfinite(item.model[c][r])) throw std::invalid_argument("Strand model matrix must be finite");
    StrandMaterialConstants result;
    result.rootColor=glm::vec4(glm::max(material.rootColor,glm::vec3(0)),1);
    result.tipColor=glm::vec4(glm::max(material.tipColor,glm::vec3(0)),1);
    result.surface={material.roughness,material.secondaryRoughness,material.specularStrength,material.secondarySpecular};
    result.volume={material.transmission,material.widthScale*widthCompensation,material.opacity,material.cuticleShift};
    result.raster={material.minPixelWidth,material.maxPixelWidth,item.guides?1.f:0.f,material.indirectLightingMultiplier};
    // The source CPU writer maps the authored longitudinal control to CB24
    // hm_v before every selected single/MS/IBL consumer. Generic callers keep
    // the previous raw-v contract; neither the source raw field nor shader
    // formulas are rewritten here.
    result.fiber={material.ior,EffectiveStrandLongitudinalRoughness(material),material.azimuthalRoughness,material.absorptionScale};
    result.scattering={material.physicalScattering?1.f:0.f,material.multipleScattering,material.scatteringDensity,material.albedoBlendRate};
    result.shadows.x=material.receiveOpaqueShadows?1.f:0.f;
    result.shadows.y=material.areaPreservingCoverage?1.f:0.f;
    result.shadows.z=material.minWidthMetres*widthCompensation;
    result.shadows.w=material.maxWidthMetres*widthCompensation;
    result.opaqueShadowParameters.x=material.opaqueShadowPower;
    result.ibl={material.backscatterScale,material.dualScatteringIbl?1.f:0.f,material.volumeSkyTransmission?1.f:0.f,material.nttTable&&material.useNttLookup?1.f:0.f};
    result.colorWeights=glm::vec4(material.gradientWeights,material.sourceColor?1.f:0.f);
    result.colorOffsets=material.colorRandomOffsets;
    result.colorVariation=glm::vec4(material.hsvVariation,material.sourceDirectScattering?1.f:0.f);
    if(material.sourceSpatialIbl) {
        // Native StrandShadingInfoSRV[1].mCenterPos is a runtime value. Use an
        // explicit host estimate instead of substituting a scene cubemap's
        // unrelated placement, and transform it in the same space as P.
        const auto center=item.model*glm::vec4(.5f*(item.geometry->BoundsMin()+item.geometry->BoundsMax()),1.f);
        if(!StrandFinite(glm::vec3(center)))throw std::invalid_argument("Strand spatial indirect center overflow");
        result.spatialIncident=glm::vec4(glm::vec3(center),1.f);
    }
    bool nativeActive=false;
    if(material.useNativeMomentLut&&material.nativeMomentTable) {
        if(!CachedValidNativeMomentTable(material.nativeMomentTable))throw std::invalid_argument("Invalid native fiber moment table dimensions or texels");
        const FiberNativeMomentMaterialKey key{material.nativeMomentParameters.x,material.nativeMomentParameters.y,
            material.nativeMomentParameters.z,material.nativeMomentParameters.w};
        for(float value:key)if(!std::isfinite(value))throw std::invalid_argument("Native fiber moment material key must be finite");
        nativeActive=material.useNativeMomentLut&&FiberNativeMomentKeyMatches(material,key);
    }
    result.nativeMoments.x=nativeActive?1.f:0.f;
    // y was reserved; keep the shared material prefix and moment-table offsets
    // intact for depth, motion, shadow and volume consumers.
    result.nativeMoments.y=static_cast<float>(debugMode);
    result.nativeMoments.z=material.volumePointIbl?1.f:0.f;
    result.nativeMoments.w=material.sourceSegmentShading?1.f:0.f;
    if(material.physicalScattering&&!nativeActive)result.moments=CachedFiberMomentTable(material);
    return result;
}
} // namespace
struct StrandRenderPass::Impl {
    RHIDevice& device;
    FiberVolumePass fiberVolume;
    NativeDSVolumePass nativeDSVolume;
    StrandVolumeStatistics volumeStatistics;
    StrandSegmentPass segmentShading;
    StrandCurvePass curves;
    StrandNativeOitPass nativeOit;
    std::shared_ptr<const void> programsOwner, targetsOwner, peelTargetsOwner, frontProgramsOwner,pairedProgramsOwner,temporalTargetsOwner,motionProgramsOwner,motionTargetsOwner;
    std::array<std::shared_ptr<const void>,4> temporalProgramsOwners;
    std::array<std::shared_ptr<const void>,8> resolvedColorProgramsOwners;
    // Feature bit0=diagnostics, bit1=actual-point transport, bit2=segment
    // colors, bit3=source curve geometry. Each of the five
    // color stages is built only when used; coverage/motion stays shared.
    struct ColorVariant {
        std::array<RenderResourceHandle<PipelineSpec>,5> pipelines{};
        std::array<std::shared_ptr<const void>,5> owners{};
    };
    std::array<ColorVariant,16> colorVariants;
    std::array<RenderResourceHandle<PipelineSpec>,4> curveCoverage;
    std::shared_ptr<const void> curveCoverageOwner,curveMotionOwner;
    RenderResourceHandle<PipelineSpec> curveMotionAccumulate;
    RenderResourceHandle<PipelineSpec> reveal, composite;
    RenderResourceHandle<PipelineSpec> peel, groupReveal, groupResolve;
    RenderResourceHandle<PipelineSpec> tailReveal,pairedGroupResolve;
    std::array<RenderResourceHandle<PipelineSpec>,3> frontComposite;
    std::array<RenderResourceHandle<PipelineSpec>,3> pairedFrontComposite;
    std::array<RenderResourceHandle<PipelineSpec>,4> temporalResolve;
    std::array<RenderResourceHandle<PipelineSpec>,8> resolvedColorPipelines;
    RenderResourceHandle<PipelineSpec> motionAccumulate,motionResolve,resolvedColorCopy;
    RenderResourceHandle<UniformBufferSpec> viewBuffer, objectBuffer, materialBuffer, lightBuffer, sunBuffer, skyBuffer, shadowBuffer, indirectBuffer,sourceDirectBuffer;
    RenderResourceHandle<UniformBufferSpec> motionObjectBuffer;
    RenderResourceHandle<RHITextureSpec> shadowFallback,lookupFallback;
    struct SampledTable {
        std::weak_ptr<const StrandTextureTable> source;
        std::shared_ptr<const void> owner;
        RenderResourceHandle<RHITextureSpec> texture;
        RHITextureFormat format=RHITextureFormat::RGBA32F;
    };
    std::vector<SampledTable> sampledTables;
    RenderResourceHandle<VertexBufferSpec> fullscreen;
    struct Target { RenderResourceHandle<RenderTargetSpec> target; RenderResourceHandle<RHITextureSpec> color; };
    Target accumulation, revealage, resolvedAccumulation, resolvedRevealage;
    struct PeelTarget {
        Target image;
        RenderResourceHandle<RHITextureSpec> depth;
        RenderResourceHandle<RenderTargetSpec> colorOnly;
        RenderResourceHandle<RenderTargetSpec> pairedAccumulation;
    };
    std::vector<PeelTarget> frontLayers;
    RenderResourceHandle<RenderTargetSpec> tailAccumulationTarget,tailRevealageTarget;
    RenderResourceHandle<RenderTargetSpec> pairedTailTarget;
    RenderResourceHandle<RenderTargetSpec> temporalTarget,temporalNearestTarget;
    RenderResourceHandle<RHITextureSpec> temporalCoverage,temporalCompositeDepth,temporalOwnNearestDepth;
    Target motionAccumulation,motionOutput,resolvedHairColor;
    std::uint32_t width=0, height=0, samples=0;
    std::uint32_t passCount=0,rasterPassCount=0,temporalPassCount=0,motionPassCount=0,colourResolvePassCount=0,activeFrontDepthGroups=0;
    bool pairedActive=false,pairedScratch=false;
    bool frontLayersActive=false,multisampleFallback=false,temporalActive=false,temporalMultisampleFallback=false,temporalUsesOwnNearest=false,motionActive=false;
    RenderResourceHandle<RHITextureSpec> depth;
    explicit Impl(RHIDevice& d) : device(d),fiberVolume(d),nativeDSVolume(d),segmentShading(d),curves(d),nativeOit(d) {}
    RenderResourceHandle<PipelineSpec> RasterPipeline(ResourceBuilder& builder,bool revealage,int mode=0,unsigned features=0) {
        CreateGraphicShaderDesc shaders;
        shaders.vertexShaderSource=builder.Source(ShaderSourceType::Vertex,StrandVertexShader(revealage,(features&4u)!=0,(features&8u)!=0));
        auto fragment=StrandFragmentShader(revealage,mode,(features&1u)!=0,(features&2u)!=0);
        if((features&4u)!=0&&!revealage)fragment=StrandSegmentColorConsumer(std::move(fragment));
        shaders.fragmentShaderSource=builder.Source(ShaderSourceType::Fragment,fragment);
        auto program=builder.Create<ShaderProgramSpec>([&](auto cb) { device.async_CreateGraphicShader(shaders,cb); });
        PipelineSpec spec;
        spec.shaderProgram=program; spec.expectVertexLayout={{VertexFieldType::Vec4,VertexFieldType::Vec4,VertexFieldType::Vec2,VertexFieldType::Vec4,VertexFieldType::Vec4}};
        spec.depthTest=mode==0||mode==1; spec.depthWrite=mode==1;
        spec.depthCompare=mode==1?CompareOp::Less:CompareOp::LessEqual;
        spec.cullMode=CullMode::None; spec.blendEnable=mode!=1; spec.blendMode=BlendMode::AdditiveRaw;
        return builder.Create<PipelineSpec>([&](auto cb) { device.async_CreatePipeline({spec},cb); });
    }
    RenderResourceHandle<PipelineSpec> PairedRasterPipeline(ResourceBuilder& builder,int mode,unsigned features) {
        CreateGraphicShaderDesc shaders;
        shaders.vertexShaderSource=builder.Source(ShaderSourceType::Vertex,StrandVertexShader(false,(features&4u)!=0,(features&8u)!=0));
        auto fragment=StrandColorFeatures(StrandPairedAccumulationFragmentShader(mode),(features&1u)!=0,(features&2u)!=0);
        if((features&4u)!=0)fragment=StrandSegmentColorConsumer(std::move(fragment));
        shaders.fragmentShaderSource=builder.Source(ShaderSourceType::Fragment,fragment);
        auto program=builder.Create<ShaderProgramSpec>([&](auto cb){device.async_CreateGraphicShader(shaders,cb);});
        PipelineSpec spec;spec.shaderProgram=program;
        spec.expectVertexLayout={{VertexFieldType::Vec4,VertexFieldType::Vec4,VertexFieldType::Vec2,VertexFieldType::Vec4,VertexFieldType::Vec4}};
        spec.depthTest=true;spec.depthWrite=false;spec.depthCompare=mode==3?CompareOp::Equal:CompareOp::Greater;
        spec.cullMode=CullMode::None;spec.blendEnable=true;spec.blendMode=BlendMode::AdditiveRaw;
        return builder.Create<PipelineSpec>([&](auto cb){device.async_CreatePipeline({spec},cb);});
    }
    RenderResourceHandle<PipelineSpec> ColorPipeline(RHIFrameEncoder& encoder,unsigned features,unsigned stage) {
        auto& variant=colorVariants.at(features);
        if(!variant.owners.at(stage)){
            ResourceBuilder builder(device);
            const auto pipeline=stage<3?RasterPipeline(builder,false,stage==0?0:stage==1?3:2,features):
                PairedRasterPipeline(builder,stage==3?3:2,features);
            auto owner=builder.Finish();variant.pipelines[stage]=pipeline;variant.owners[stage]=std::move(owner);
        }
        StrandRequire(encoder.KeepAlive(variant.owners[stage]),"Cannot pin strand color feature variant");
        return variant.pipelines[stage];
    }
    void PrepareCurveCoverage(RHIFrameEncoder& encoder){
        if(!curveCoverageOwner){
            ResourceBuilder builder(device);
            const std::array<int,4> modes{0,1,3,2};
            for(std::size_t i=0;i<modes.size();++i)curveCoverage[i]=RasterPipeline(builder,true,modes[i],8u);
            curveCoverageOwner=builder.Finish();
        }
        StrandRequire(encoder.KeepAlive(curveCoverageOwner),"Cannot pin curve coverage variants");
    }
    void PrepareCurveMotion(RHIFrameEncoder& encoder){
        if(!curveMotionOwner){
            ResourceBuilder builder(device);CreateGraphicShaderDesc shaders;
            shaders.vertexShaderSource=builder.Source(ShaderSourceType::Vertex,StrandMotionVertexShader(true));
            shaders.fragmentShaderSource=builder.Source(ShaderSourceType::Fragment,StrandMotionFragmentShader());
            const auto program=builder.Create<ShaderProgramSpec>([&](auto cb){device.async_CreateGraphicShader(shaders,cb);});
            PipelineSpec spec;spec.shaderProgram=program;
            spec.expectVertexLayout={{VertexFieldType::Vec4,VertexFieldType::Vec4,VertexFieldType::Vec2,VertexFieldType::Vec4,VertexFieldType::Vec4}};
            spec.depthTest=spec.depthWrite=false;spec.cullMode=CullMode::None;spec.blendEnable=true;spec.blendMode=BlendMode::AdditiveRaw;
            curveMotionAccumulate=builder.Create<PipelineSpec>([&](auto cb){device.async_CreatePipeline({spec},cb);});
            curveMotionOwner=builder.Finish();
        }
        StrandRequire(encoder.KeepAlive(curveMotionOwner),"Cannot pin curve motion variant");
    }
    RenderResourceHandle<PipelineSpec> CompositePipeline(ResourceBuilder& builder,std::uint32_t groups=0,bool blend=true,bool stableAccumulation=false) {
        CreateGraphicShaderDesc sources;
        sources.vertexShaderSource=builder.Source(ShaderSourceType::Vertex,FullscreenVertexShader());
        sources.fragmentShaderSource=builder.Source(ShaderSourceType::Fragment,groups?
            StrandLayeredCompositeFragmentShader(groups,stableAccumulation):
            stableAccumulation?StrandStableGroupCompositeFragmentShader():StrandCompositeFragmentShader(false));
        const auto program=builder.Create<ShaderProgramSpec>([&](auto cb) { device.async_CreateGraphicShader(sources,cb); });
        PipelineSpec spec;
        spec.shaderProgram=program; spec.expectVertexLayout={{VertexFieldType::Vec2,VertexFieldType::Vec2}};
        spec.depthTest=spec.depthWrite=false; spec.cullMode=CullMode::None;
        spec.blendEnable=blend; spec.blendMode=blend?BlendMode::AlphaComposite:BlendMode::Opaque;
        return builder.Create<PipelineSpec>([&](auto cb) { device.async_CreatePipeline({spec},cb); });
    }
    void Initialize() {
        if (programsOwner) return;
        ResourceBuilder builder(device);
        reveal=RasterPipeline(builder,true);
        composite=CompositePipeline(builder,false);
        fullscreen=builder.FullscreenMesh();
        viewBuffer=builder.Uniform(sizeof(StrandViewConstants)); objectBuffer=builder.Uniform(sizeof(glm::mat4));
        motionObjectBuffer=builder.Uniform(sizeof(StrandMotionObjectConstants));
        materialBuffer=builder.Uniform(sizeof(StrandMaterialConstants));
        lightBuffer=builder.Uniform(sizeof(Material::PbrPassConstants)); sunBuffer=builder.Uniform(sizeof(StrandSunConstants));
        skyBuffer=builder.Uniform(sizeof(SkyLightConstants));
        indirectBuffer=builder.Uniform(sizeof(StrandIndirectConstants));
        sourceDirectBuffer=builder.Uniform(sizeof(StrandSourceDirectConstants));
        shadowBuffer=builder.Uniform(sizeof(SceneEffectsConstants));shadowFallback=builder.Texture(1,1,RHITextureFormat::RGBA16F);
        const glm::vec4 white(1);CreateRHITextureSpec lookup;lookup.width=lookup.height=1;
        lookup.textureDataStoreType=lookup.textureUseType=RHITextureFormat::RGBA32F;
        lookup.filterMode=RHIFilterMode::Linear;lookup.addressMode=RHIAddressMode::ClampToEdge;lookup.mipmaps=false;lookup.data=&white;
        lookupFallback=builder.Create<RHITextureSpec>([&](auto cb){device.async_CreateTexture(lookup,cb);});
        programsOwner=builder.Finish();
    }
    RenderResourceHandle<RHITextureSpec> Table(RHIFrameEncoder& encoder,const std::shared_ptr<const StrandTextureTable>& source,bool gradient,
        RHITextureFormat format=RHITextureFormat::RGBA32F) {
        if(!source)return lookupFallback;
        if(!source->width||!source->height||source->width>4096||source->height>4096||
            source->texels.size()!=std::size_t(source->width)*source->height||(gradient&&source->height!=3))
            throw std::invalid_argument("Invalid immutable strand lookup table dimensions");
        for(auto& cached:sampledTables)if(auto published=cached.source.lock();published==source&&cached.format==format) {
            StrandRequire(encoder.KeepAlive(cached.owner),"Cannot pin strand lookup table");return cached.texture;
        }
        for(const auto& texel:source->texels)for(int channel=0;channel<4;++channel)
            if(!std::isfinite(texel[channel]))throw std::invalid_argument("Strand lookup table contains nonfinite texels");
        ResourceBuilder builder(device);CreateRHITextureSpec desc;
        desc.width=source->width;desc.height=source->height;desc.data=source->texels.data();
        // Preserve the original signed Ntt G channel and float precision. The
        // gradient is small enough to keep its once-decoded linear values in
        // the same format rather than adding an unnecessary half conversion.
        desc.textureDataStoreType=desc.textureUseType=format;
        desc.filterMode=RHIFilterMode::Linear;desc.mipmapMode=RHIMipmapMode::Nearest;
        desc.addressMode=RHIAddressMode::ClampToEdge;desc.mipmaps=false;
        const auto texture=builder.Create<RHITextureSpec>([&](auto cb){device.async_CreateTexture(desc,cb);});
        auto owner=builder.Finish();StrandRequire(encoder.KeepAlive(owner),"Cannot pin uploaded strand lookup table");
        sampledTables.push_back({source,std::move(owner),texture,format});return texture;
    }
    void Resize(const StrandPassTarget& scene,bool paired) {
        if (targetsOwner && width==scene.width && height==scene.height && samples==scene.samples && depth==scene.depth&&pairedScratch==paired) return;
        ResourceBuilder builder(device);
        const auto make=[&](uint32_t count, RenderResourceHandle<RHITextureSpec> z,RHITextureFormat format=RHITextureFormat::RGBA16F) {
            Target result;
            result.color=builder.Texture(scene.width,scene.height,format,count);
            result.target=builder.Target(result.color,z);
            return result;
        };
        auto newAccumulation=make(scene.samples,scene.depth,paired?RHITextureFormat::RGBA32F:RHITextureFormat::RGBA16F);
        auto newRevealage=make(scene.samples,scene.depth);
        auto newResolvedAccumulation=scene.samples>1?make(1,{}):newAccumulation;
        auto newResolvedRevealage=scene.samples>1?make(1,{}):newRevealage;
        auto owner=builder.Finish();
        // Every old in-flight frame pins its owners; retire the optional
        // single-sample peel targets when any target dependency changes.
        peelTargetsOwner.reset();frontLayers={};tailAccumulationTarget={};tailRevealageTarget={};pairedTailTarget={};
        temporalTargetsOwner.reset();temporalTarget={};temporalNearestTarget={};
        temporalCoverage={};temporalCompositeDepth={};temporalOwnNearestDepth={};temporalUsesOwnNearest=false;
        motionTargetsOwner.reset();motionAccumulation={};motionOutput={};resolvedHairColor={};
        accumulation=newAccumulation; revealage=newRevealage;
        resolvedAccumulation=newResolvedAccumulation; resolvedRevealage=newResolvedRevealage;
        width=scene.width; height=scene.height; samples=scene.samples; depth=scene.depth;pairedScratch=paired;
        targetsOwner=std::move(owner);
    }
    void PrepareLayerPrograms() {
        if(!frontProgramsOwner) {
            ResourceBuilder builder(device);
            // Depth discovery only evaluates coverage, never fiber lighting.
            peel=RasterPipeline(builder,true,1);
            groupReveal=RasterPipeline(builder,true,3);
            groupResolve=CompositePipeline(builder,false,false);
            tailReveal=RasterPipeline(builder,true,2);
            for(std::size_t i=0;i<frontComposite.size();++i)
                frontComposite[i]=CompositePipeline(builder,std::array<std::uint32_t,3>{2,3,7}[i]);
            frontProgramsOwner=builder.Finish();
        }
    }
    void PreparePairedPrograms() {
        if(pairedProgramsOwner)return;
        ResourceBuilder builder(device);
        pairedGroupResolve=CompositePipeline(builder,0,false,true);
        for(std::size_t i=0;i<pairedFrontComposite.size();++i)
            pairedFrontComposite[i]=CompositePipeline(builder,std::array<std::uint32_t,3>{2,3,7}[i],true,true);
        pairedProgramsOwner=builder.Finish();
    }
    void PrepareFrontLayers(std::uint32_t count,bool paired) {
        PrepareLayerPrograms();
        if(paired)PreparePairedPrograms();
        if(peelTargetsOwner&&frontLayers.size()==count)return;
        ResourceBuilder builder(device);
        std::vector<PeelTarget> layers(count);
        for(auto& layer:layers) {
            layer.image.color=builder.Texture(width,height,RHITextureFormat::RGBA16F);
            layer.depth=builder.Texture(width,height,RHITextureFormat::Depth32F);
            layer.image.target=builder.Target(layer.image.color,layer.depth);
            layer.colorOnly=builder.Target(layer.image.color,{});
            if(paired)layer.pairedAccumulation=builder.TargetColors({accumulation.color,revealage.color,{},{}},layer.depth);
        }
        // The weighted tail performs opaque-depth clipping in its shader.
        // Color-only aliases prevent sampling any active depth attachment.
        auto tailColor=builder.Target(accumulation.color,{});
        auto tailAlpha=builder.Target(revealage.color,{});
        auto tailPair=paired?builder.TargetColors({accumulation.color,revealage.color,{},{}},layers.back().depth):RenderResourceHandle<RenderTargetSpec>{};
        auto owner=builder.Finish();
        frontLayers=layers;tailAccumulationTarget=tailColor;tailRevealageTarget=tailAlpha;
        pairedTailTarget=tailPair;
        peelTargetsOwner=std::move(owner);
    }
    static std::size_t FrontProgramIndex(std::uint32_t count) {return count==2?0:count==3?1:2;}
    static std::size_t TemporalProgramIndex(std::uint32_t count) {return count?1+FrontProgramIndex(count):0;}
    static std::size_t ColourProgramIndex(std::uint32_t count,bool paired) {return TemporalProgramIndex(count)+(paired?4:0);}
    void PrepareTemporalData(std::uint32_t count) {
        const auto index=TemporalProgramIndex(count);
        if(!temporalProgramsOwners[index]) {
            ResourceBuilder builder(device);
            temporalResolve[index]=builder.FullscreenPipeline(FullscreenVertexShader(),StrandTemporalResolveFragmentShader(count),true);
            temporalProgramsOwners[index]=builder.Finish();
        }
        const bool ownNearest=count==0;
        if(ownNearest)PrepareLayerPrograms();
        if(temporalTargetsOwner&&temporalUsesOwnNearest==ownNearest)return;
        ResourceBuilder builder(device);
        const auto coverage=builder.Texture(width,height,RHITextureFormat::RGBA16F);
        const auto compositeDepth=builder.Texture(width,height,RHITextureFormat::Depth32F);
        const auto target=builder.Target(coverage,compositeDepth);
        RenderResourceHandle<RHITextureSpec> nearest;
        RenderResourceHandle<RenderTargetSpec> nearestTarget;
        if(ownNearest) {
            nearest=builder.Texture(width,height,RHITextureFormat::Depth32F);
            // The coverage-only discovery shader has a color output. Reuse
            // this pass's scratch coverage image; the final resolve overwrites
            // it without sampling it, so no extra allocation or feedback.
            nearestTarget=builder.Target(coverage,nearest);
        }
        auto owner=builder.Finish();
        temporalCoverage=coverage;temporalCompositeDepth=compositeDepth;temporalTarget=target;
        temporalOwnNearestDepth=nearest;temporalNearestTarget=nearestTarget;temporalUsesOwnNearest=ownNearest;
        temporalTargetsOwner=std::move(owner);
    }
    void PrepareResolvedColor(std::uint32_t count,bool paired) {
        const auto index=ColourProgramIndex(count,paired);
        if(!resolvedColorProgramsOwners[index]) {
            ResourceBuilder builder(device);
            resolvedColorPipelines[index]=CompositePipeline(builder,count,false,paired);
            resolvedColorProgramsOwners[index]=builder.Finish();
        }
    }
    void PrepareMotionData(std::uint32_t count,bool paired) {
        if(!motionProgramsOwner) {
            ResourceBuilder builder(device);
            CreateGraphicShaderDesc shaders;
            shaders.vertexShaderSource=builder.Source(ShaderSourceType::Vertex,StrandMotionVertexShader());
            shaders.fragmentShaderSource=builder.Source(ShaderSourceType::Fragment,StrandMotionFragmentShader());
            const auto program=builder.Create<ShaderProgramSpec>([&](auto cb){device.async_CreateGraphicShader(shaders,cb);});
            PipelineSpec spec;spec.shaderProgram=program;
            spec.expectVertexLayout={{VertexFieldType::Vec4,VertexFieldType::Vec4,VertexFieldType::Vec2,VertexFieldType::Vec4,VertexFieldType::Vec4}};
            spec.depthTest=spec.depthWrite=false;spec.cullMode=CullMode::None;spec.blendEnable=true;spec.blendMode=BlendMode::AdditiveRaw;
            motionAccumulate=builder.Create<PipelineSpec>([&](auto cb){device.async_CreatePipeline({spec},cb);});
            motionResolve=builder.FullscreenPipeline(FullscreenVertexShader(),StrandMotionResolveFragmentShader());
            shaders.vertexShaderSource=builder.Source(ShaderSourceType::Vertex,FullscreenVertexShader());
            shaders.fragmentShaderSource=builder.Source(ShaderSourceType::Fragment,StrandResolvedColorCopyFragmentShader());
            const auto copyProgram=builder.Create<ShaderProgramSpec>([&](auto cb){device.async_CreateGraphicShader(shaders,cb);});
            spec.shaderProgram=copyProgram;spec.expectVertexLayout={{VertexFieldType::Vec2,VertexFieldType::Vec2}};
            spec.blendMode=BlendMode::AlphaComposite;
            resolvedColorCopy=builder.Create<PipelineSpec>([&](auto cb){device.async_CreatePipeline({spec},cb);});
            motionProgramsOwner=builder.Finish();
        }
        PrepareResolvedColor(count,paired);
        if(motionTargetsOwner)return;
        ResourceBuilder builder(device);
        Target accumulated,output,hairColor;
        accumulated.color=builder.Texture(width,height,RHITextureFormat::RGBA32F);
        // The tail has already been consumed by color/coverage resolves before
        // this MRT reuses its accumulation color for total/invalid alpha sums.
        accumulated.target=builder.TargetColors({accumulated.color,accumulation.color,{},{}});
        output.color=builder.Texture(width,height,RHITextureFormat::RGBA32F);output.target=builder.Target(output.color,{});
        hairColor.color=builder.Texture(width,height,RHITextureFormat::RGBA16F);hairColor.target=builder.Target(hairColor.color,{});
        auto owner=builder.Finish();motionAccumulation=accumulated;motionOutput=output;resolvedHairColor=hairColor;
        motionTargetsOwner=std::move(owner);
    }
    void Bind(RHIFrameEncoder& encoder, RenderResourceHandle<RenderTargetSpec> target, uint8_t clear=RHICommand::ClearNone) {
        RHICommand::SetRenderTarget command;
        command.target=target; command.width=width; command.height=height;
        command.clearFlags=clear; command.clearColor=glm::vec4(0);
        StrandRequire(encoder.SetRenderTarget(command),"Cannot bind strand accumulation target");
    }
};
StrandRenderPass::StrandRenderPass(RHIDevice& device) : impl_(std::make_unique<Impl>(device)) {}
StrandRenderPass::~StrandRenderPass() = default;
void StrandRenderPass::Record(RHIFrameEncoder& encoder, std::span<const StrandRenderItem> items,
    const StrandPassTarget& target, const StrandPassView& view) {
    impl_->passCount=impl_->rasterPassCount=impl_->temporalPassCount=impl_->motionPassCount=impl_->colourResolvePassCount=impl_->activeFrontDepthGroups=0;
    impl_->pairedActive=false;
    impl_->frontLayersActive=impl_->multisampleFallback=impl_->temporalActive=impl_->temporalMultisampleFallback=impl_->motionActive=false;
    impl_->fiberVolume.Begin(items.size());
    impl_->nativeDSVolume.Begin(items.size());impl_->volumeStatistics={};
    impl_->segmentShading.Begin(items.size());
    impl_->curves.Begin(items.size());
    impl_->nativeOit.Begin();
    if(items.empty()) return;
    if(static_cast<std::uint32_t>(view.debugMode)>14u)
        throw std::invalid_argument("Invalid strand diagnostic mode");
    const auto indirect=IndirectConstants(view.indirect);
    const auto sourceDirect=MakeStrandSourceDirectConstants(view.sourceDirect);
    StrandRequire(target.width && target.height && target.framebuffer.IsValid() && target.depth.IsValid(),
        "Strand pass requires HDR scene color and opaque depth");
    const bool nativeOit=view.nativeOit&&target.samples==1;
    if(view.nativeOit&&view.nativeOitLayers!=4&&view.nativeOitLayers!=8)
        throw std::invalid_argument("Native strand local OIT requires 4 or 8 layers");
    if(nativeOit&&std::any_of(items.begin(),items.end(),[](const auto& item){return !item.material.sourceSegmentShading;}))
        throw std::invalid_argument("Native Wu OIT requires source segment shading for every strand object");
    const bool frontLayers=view.frontLayerTransparency&&target.samples==1&&!nativeOit;
    const bool paired=frontLayers&&view.pairedLayerTransparency;
    const bool motionData=view.outputMotionData&&target.samples==1;
    const bool straightOutput=view.outputStraightColor&&target.samples==1;
    const bool shutterActive=view.shutterSample<1.f&&view.motionHistoryValid;
    const bool temporalData=(view.outputTemporalData||view.outputMotionData)&&target.samples==1;
    const bool curvesEnabled=std::any_of(items.begin(),items.end(),[](const auto& item){return item.material.sourceCurveGeometry;});
    if(view.frontLayerTransparency&&!StrandSupportedFrontDepthGroups(view.frontDepthGroups))
        throw std::invalid_argument("Strand front depth groups must be 2, 3 or 7");
    if((view.outputTemporalData||view.outputMotionData)&&(!std::isfinite(view.depthWriteThreshold)||view.depthWriteThreshold<0||view.depthWriteThreshold>1))
        throw std::invalid_argument("Strand depth-write coverage threshold must be in [0,1]");
    if(!std::isfinite(view.shutterSample)||view.shutterSample<0.f||view.shutterSample>1.f||
        (view.outputStraightColor&&target.samples!=1))throw std::invalid_argument("Invalid strand shutter sample or straight resolve target");
    if((motionData||shutterActive)&&view.motionHistoryValid) {
        if(!StrandFinite(view.previousCameraPosition))throw std::invalid_argument("Previous strand camera must be finite");
        for(int c=0;c<4;++c)for(int r=0;r<4;++r)
            if(!std::isfinite(view.previousViewProjection[c][r])||!std::isfinite(view.currentUnjitteredViewProjection[c][r])||
                !std::isfinite(view.previousUnjitteredViewProjection[c][r]))throw std::invalid_argument("Strand motion history matrices must be finite");
    }
    if(frontLayers||temporalData||nativeOit)StrandRequire(view.opaqueDepth.IsValid(),"Layered or temporal strands require independent resolved opaque depth");
    std::vector<StrandMaterialConstants> materials;
    materials.reserve(items.size());
    for(const auto& item:items) {
        StrandRequire(item.geometry && item.geometry->Mesh().IsValid() && item.geometry->IndexCount(),"Invalid strand geometry snapshot");
        materials.push_back(StrandConstants(item,view.debugMode));
    }
    impl_->Initialize(); impl_->Resize(target,paired);
    if(frontLayers)impl_->PrepareFrontLayers(view.frontDepthGroups,paired);
    if(temporalData&&!nativeOit)impl_->PrepareTemporalData(frontLayers?view.frontDepthGroups:0);
    if(motionData&&!nativeOit)impl_->PrepareMotionData(frontLayers?view.frontDepthGroups:0,paired);
    else if(straightOutput&&!nativeOit)impl_->PrepareResolvedColor(frontLayers?view.frontDepthGroups:0,paired);
    if(curvesEnabled&&!nativeOit)impl_->PrepareCurveCoverage(encoder);
    if(curvesEnabled&&motionData&&!nativeOit)impl_->PrepareCurveMotion(encoder);
    const std::array<RenderResourceHandle<PipelineSpec>,4> coveragePipelines=curvesEnabled?impl_->curveCoverage:
        std::array<RenderResourceHandle<PipelineSpec>,4>{impl_->reveal,impl_->peel,impl_->groupReveal,impl_->tailReveal};
    const unsigned colorFeatures=(view.debugMode!=StrandDebugMode::Beauty?1u:0u)|
        (view.sourceDirect.enabled||std::any_of(items.begin(),items.end(),[](const auto& item){const auto& m=item.material;
            return !item.guides&&(m.volumePointIbl||m.sourceDSVolume)&&m.spatialVolume&&m.physicalScattering&&m.dualScatteringIbl&&m.volumeSkyTransmission;})?2u:0u)|
        (std::any_of(items.begin(),items.end(),[](const auto& item){return item.material.sourceSegmentShading;})?4u:0u)|
        (curvesEnabled?8u:0u);
    std::array<RenderResourceHandle<PipelineSpec>,5> colorPipelines{};
    if(frontLayers){
        const unsigned first=paired?3u:1u;
        colorPipelines[first]=impl_->ColorPipeline(encoder,colorFeatures,first);
        colorPipelines[first+1]=impl_->ColorPipeline(encoder,colorFeatures,first+1);
    }else if(!nativeOit)colorPipelines[0]=impl_->ColorPipeline(encoder,colorFeatures,0);
    std::erase_if(impl_->sampledTables,[](const auto& cached){return cached.source.expired();});
    for(size_t i=0;i<items.size();++i) {
        // Camera visibility is produced before the optical shade table. It
        // creates new shade-start ownership without altering original P0/P3.
        impl_->curves.Prepare(i,items[i],view,target.width,target.height,true,nativeOit);
        impl_->fiberVolume.Prepare(encoder,i,items[i],view);
        const auto& m=items[i].material;
        NativeDSVolumeSettings ds;
        ds.enabled=m.sourceDSVolume&&m.spatialVolume&&!items[i].guides;
        if(ds.enabled&&(!std::isfinite(m.volumeTraceLimit)||m.volumeTraceLimit<1||m.volumeTraceLimit>1024||
            std::floor(m.volumeTraceLimit)!=m.volumeTraceLimit||!std::isfinite(m.volumeInterpolationSamples)||
            m.volumeInterpolationSamples<0||m.volumeInterpolationSamples>1024||std::floor(m.volumeInterpolationSamples)!=m.volumeInterpolationSamples))
            throw std::invalid_argument("Native DSVolume trace limit / interpolation cursor threshold must be finite integers");
        ds.requestedVoxelSize=m.volumeVoxelSize;ds.resolution=m.volumeResolution;
        ds.averageWidth=m.volumeAverageWidth>0?m.volumeAverageWidth:2.f*items[i].geometry->MeanRadius();
        ds.widthScale=m.volumeRasterWidthScale;ds.rasterTraceStepScale=m.volumeRasterTraceStep;
        ds.traceGrowth=m.volumeTraceStep;ds.offsetScale=m.volumeBiasVoxels;
        ds.traceLimit=ds.enabled?static_cast<std::uint32_t>(m.volumeTraceLimit):32u;
        ds.interpolation=ds.enabled?static_cast<std::uint32_t>(m.volumeInterpolationSamples):4u;
        ds.injectionRate=m.volumeInjectionRate;ds.injectionLodFraction=m.volumeInjectionLodFraction;
        ds.injectionMinLodFactor=m.volumeInjectionMinLodFactor;ds.injectionQuality=m.volumeInjectionQuality;
        ds.opaque=m.volumeOpaqueBlockers;
        if(ds.enabled&&(!std::isfinite(view.worldUnitsPerMetre)||view.worldUnitsPerMetre<=0.f))
            throw std::invalid_argument("Native DSVolume scene display-unit conversion must be positive and finite");
        ds.threshold=m.volumeDepthThreshold*(ds.enabled?view.worldUnitsPerMetre:1.f);
        impl_->nativeDSVolume.Prepare(encoder,i,items[i],view,ds);
        impl_->segmentShading.Prepare(i,items[i],impl_->curves.VisibilityWords(i));
    }
    impl_->volumeStatistics=impl_->fiberVolume.Statistics();
    const auto& nativeVolumeStats=impl_->nativeDSVolume.Statistics();auto& volumeStats=impl_->volumeStatistics;
    volumeStats.passes+=nativeVolumeStats.passes;volumeStats.blockerPasses+=nativeVolumeStats.blockerPasses;
    volumeStats.updated+=nativeVolumeStats.updated;volumeStats.blockerUpdated+=nativeVolumeStats.blockerUpdated;
    volumeStats.nativeInputSegments=nativeVolumeStats.nativeInputSegments;volumeStats.nativeInjectedSegments=nativeVolumeStats.nativeInjectedSegments;
    volumeStats.nativeInjectionFraction=nativeVolumeStats.nativeInjectionFraction;volumeStats.nativeRasterWidthScale=nativeVolumeStats.nativeRasterWidthScale;
    volumeStats.nativeAverageWidth=nativeVolumeStats.nativeAverageWidth;
    volumeStats.segments+=nativeVolumeStats.segments;volumeStats.voxelCount+=nativeVolumeStats.voxelCount;
    volumeStats.resolution=std::max(volumeStats.resolution,nativeVolumeStats.resolution);
    volumeStats.effectiveVoxelSize=std::max(volumeStats.effectiveVoxelSize,nativeVolumeStats.effectiveVoxelSize);
    volumeStats.effectiveBiasMetres=std::max(volumeStats.effectiveBiasMetres,nativeVolumeStats.effectiveBiasMetres);
    StrandRequire(encoder.KeepAlive(impl_->programsOwner) && encoder.KeepAlive(impl_->targetsOwner),"Cannot pin strand pass resources");
    if(frontLayers)StrandRequire(encoder.KeepAlive(impl_->peelTargetsOwner)&&encoder.KeepAlive(impl_->frontProgramsOwner),"Cannot pin strand front-layer resources");
    if(paired)StrandRequire(encoder.KeepAlive(impl_->pairedProgramsOwner),"Cannot pin paired strand transparency programs");
    if(temporalData&&!nativeOit)StrandRequire(encoder.KeepAlive(impl_->temporalTargetsOwner)&&
        encoder.KeepAlive(impl_->temporalProgramsOwners[Impl::TemporalProgramIndex(frontLayers?view.frontDepthGroups:0)])&&
        encoder.KeepAlive(impl_->frontProgramsOwner),"Cannot pin strand temporal resources");
    if(motionData&&!nativeOit)StrandRequire(encoder.KeepAlive(impl_->motionProgramsOwner)&&encoder.KeepAlive(impl_->motionTargetsOwner)&&
        encoder.KeepAlive(impl_->resolvedColorProgramsOwners[Impl::ColourProgramIndex(frontLayers?view.frontDepthGroups:0,paired)]),"Cannot pin strand motion resources");
    else if(straightOutput&&!nativeOit)StrandRequire(encoder.KeepAlive(impl_->resolvedColorProgramsOwners[Impl::ColourProgramIndex(frontLayers?view.frontDepthGroups:0,paired)]),"Cannot pin straight strand resolve program");
    StrandViewConstants camera{view.viewProjection,glm::vec4(view.cameraPosition,1),
        glm::vec4(float(target.width),float(target.height),0,0)};
    camera.previousViewProjection=view.motionHistoryValid?view.previousViewProjection:view.viewProjection;
    camera.currentUnjitteredViewProjection=view.currentUnjitteredViewProjection;
    camera.previousUnjitteredViewProjection=view.motionHistoryValid?view.previousUnjitteredViewProjection:view.currentUnjitteredViewProjection;
    camera.previousCamera=glm::vec4(view.motionHistoryValid?view.previousCameraPosition:view.cameraPosition,1);
    camera.motionState={view.motionHistoryValid?1.f:0.f,motionData?1.f:0.f,shutterActive?1.f-view.shutterSample:0.f,0};
    if(shutterActive){
        const float determinant=glm::determinant(view.currentUnjitteredViewProjection);
        if(!std::isfinite(determinant)||std::abs(determinant)<1e-20f)throw std::invalid_argument("Shutter projection must be invertible");
        const auto jitter=view.viewProjection*glm::inverse(view.currentUnjitteredViewProjection)*glm::vec4(0,0,0,1);
        if(!StrandFinite(glm::vec3(jitter))||!std::isfinite(jitter.w)||std::abs(jitter.w)<1e-8f)throw std::invalid_argument("Invalid shutter projection jitter");
        camera.previousCamera.w=jitter.x/jitter.w;camera.motionState.w=jitter.y/jitter.w;
    }
    const StrandSunConstants sun{glm::vec4(view.sunDirection,std::max(view.sunIntensity,0.f)),glm::vec4(view.sunColor,1)};
    StrandUpload(encoder,impl_->viewBuffer,&camera,sizeof(camera),0);
    auto hairLighting=view.lighting;
    if(view.indirect.enabled) {
        // Native t19 incident is the probe response times Material168, without
        // a second constant ambient. Keep this policy in the hair-only buffer
        // so opaque skin/cloth lighting and the retained projection are intact.
        for(unsigned c=0;c<3;++c)hairLighting.ambientAndExposure[c]*=view.indirect.hostAmbientScale;
    }
    StrandUpload(encoder,impl_->lightBuffer,&hairLighting,sizeof(hairLighting),3);
    StrandUpload(encoder,impl_->sunBuffer,&sun,sizeof(sun),4);
    StrandUpload(encoder,impl_->skyBuffer,&view.sky,sizeof(view.sky),SkyLightBinding);
    StrandUpload(encoder,impl_->indirectBuffer,&indirect,sizeof(indirect),7);
    StrandUpload(encoder,impl_->sourceDirectBuffer,&sourceDirect,sizeof(sourceDirect),StrandSourceDirectBinding);
    StrandRequire(encoder.BindTexture(impl_->Table(encoder,view.sourceDirect.enabled?view.sourceDirect.iesTable:nullptr,false),
        StrandSourceIesTextureSlot),"Cannot bind source strand IES table");
    auto shadowEffects=view.effects;if(!view.shadowAtlas.IsValid())shadowEffects.effects.x=0;
    StrandUpload(encoder,impl_->shadowBuffer,&shadowEffects,sizeof(shadowEffects),6);
    StrandRequire(encoder.BindTexture(view.shadowAtlas.IsValid()?view.shadowAtlas:impl_->shadowFallback,14),"Cannot bind opaque shadow atlas for fibers");
    const auto bindItem=[&](size_t i,bool motion,bool lighting) {
            const auto& item=items[i];
            StrandRequire(encoder.KeepAlive(item.geometry),"Cannot pin strand mesh snapshot");
            StrandUpload(encoder,impl_->objectBuffer,&item.model,sizeof(item.model),1);
            // The native branch returns before reading strandMomentLut. Keep
            // the complete UBO allocation, but upload only its scalar prefix.
            // Every lit fallback draw still publishes its complete table,
            // including native -> fallback -> native material transitions.
            // Coverage/depth/motion shaders never read the moment table.
            const auto materialBytes=static_cast<std::uint32_t>(!lighting||materials[i].nativeMoments.x>.5f?
                offsetof(StrandMaterialConstants,moments):sizeof(StrandMaterialConstants));
            StrandUpload(encoder,impl_->materialBuffer,&materials[i],materialBytes,2);
            {
                const bool valid=(motion||shutterActive)&&view.motionHistoryValid&&item.previousTransformValid&&item.geometry->PreviousInputMatched();
                StrandMotionObjectConstants old;old.previousModel=valid?item.previousModel:item.model;old.flags.x=valid?1.f:0.f;
                if(valid)for(int c=0;c<4;++c)for(int r=0;r<4;++r)if(!std::isfinite(old.previousModel[c][r]))
                    throw std::invalid_argument("Previous strand model must be finite");
                StrandUpload(encoder,impl_->motionObjectBuffer,&old,sizeof(old),8);
            }
            StrandRequire(encoder.BindTexture(impl_->Table(encoder,item.material.nttTable,false),0)&&
                encoder.BindTexture(impl_->Table(encoder,item.material.sourceColor?item.material.gradientTable:nullptr,true),1),
                "Cannot bind strand material lookup tables");
            const auto nativeTable=materials[i].nativeMoments.x>.5f?item.material.nativeMomentTable:nullptr;
            if(nativeTable)StrandRequire(encoder.KeepAlive(nativeTable),"Cannot pin native immutable fiber moment table");
            StrandRequire(encoder.BindTexture(impl_->Table(encoder,nativeTable,false,RHITextureFormat::RGBA16F),9),"Cannot bind native fiber moment table");
            impl_->fiberVolume.Bind(encoder,i,item);
            impl_->nativeDSVolume.Bind(encoder,i);
            impl_->segmentShading.Bind(encoder,i,impl_->lookupFallback,item.material.sourceSegmentShading);
    };
    if((colorFeatures&4u)!=0) {
        // End the volume producer attachment before sampling its N/G fields.
        // These outputs are separate storage images, never active attachments.
        impl_->Bind(encoder,target.framebuffer);
        for(size_t i=0;i<items.size();++i)if(items[i].material.sourceSegmentShading) {
            bindItem(i,false,true);
            impl_->segmentShading.Record(encoder,i,items[i],colorFeatures&3u);
        }
    }
    if(nativeOit){
        // The OpenGL endpoint producer used its color atlas as an attachment.
        // End that target before binding the same atlas as native compute input.
        impl_->Bind(encoder,target.framebuffer);
        impl_->nativeOit.Record(encoder,items,target,view,impl_->curves,bindItem);
        impl_->passCount=impl_->nativeOit.ComputePasses()+impl_->nativeOit.RasterPasses();
        impl_->temporalActive=temporalData;impl_->motionActive=motionData;
        impl_->temporalPassCount=temporalData?2u:0u;impl_->motionPassCount=motionData?1u:0u;
        impl_->colourResolvePassCount=1;
        return;
    }
    const auto drawItems=[&](bool motion=false,bool lighting=true) {
        for(size_t i=0;i<items.size();++i) {
            const auto& item=items[i];bindItem(i,motion,lighting);
            if(curvesEnabled){impl_->curves.Bind(encoder,i);impl_->curves.Draw(encoder,i);}
            else StrandRequire(encoder.BindMesh(item.geometry->Mesh()) && encoder.DrawIndexed({item.geometry->IndexCount()}),"Cannot draw strand ribbons");
        }
    };
    const auto bindLayerInputs=[&](RenderResourceHandle<RHITextureSpec> previous,bool rejectPrevious) {
        camera.viewport.z=rejectPrevious?1.f:0.f;
        StrandUpload(encoder,impl_->viewBuffer,&camera,sizeof(camera),0);
        StrandRequire(encoder.BindTexture(previous,2)&&encoder.BindTexture(view.opaqueDepth,3),
            "Cannot bind strand layer depth inputs");
    };
    if(frontLayers) {
        for(std::size_t layer=0;layer<impl_->frontLayers.size();++layer) {
            impl_->Bind(encoder,impl_->frontLayers[layer].image.target,RHICommand::ClearColor|RHICommand::ClearDepth);
            StrandRequire(encoder.BindPipeline(coveragePipelines[1]),"Cannot bind strand front-layer pipeline");
            bindLayerInputs(layer?impl_->frontLayers[layer-1].depth:impl_->lookupFallback,layer!=0);
            drawItems(false,false);
            // Discovery chooses a depth, not a single fragment's coverage.
            // Every equal-depth fragment contributes to the same aggregate.
            // Reuse the tail scratch colors. Legacy samples selected depth
            // independently; paired tests it as a read-only attachment.
            if(paired) {
                // Early hardware Equal rejects other depths before expensive
                // fiber lighting. Both additive outputs share one traversal.
                // The selected depth is an attachment, not a sampled input.
                impl_->Bind(encoder,impl_->frontLayers[layer].pairedAccumulation,RHICommand::ClearColor);
                StrandRequire(encoder.BindPipeline(colorPipelines[3]),"Cannot bind paired strand depth group");
                bindLayerInputs(impl_->lookupFallback,false);
                drawItems();
            } else for(int groupPass=0;groupPass<2;++groupPass) {
                impl_->Bind(encoder,groupPass?impl_->tailRevealageTarget:impl_->tailAccumulationTarget,RHICommand::ClearColor);
                StrandRequire(encoder.BindPipeline(groupPass?coveragePipelines[2]:colorPipelines[1]),
                    "Cannot bind equal-depth strand group pipeline");
                bindLayerInputs(impl_->frontLayers[layer].depth,false);
                drawItems(false,groupPass==0);
            }
            // Write the group's straight RGB and 1-product(T) without blend:
            // AlphaComposite here would square coverage before final over.
            impl_->Bind(encoder,impl_->frontLayers[layer].colorOnly);
            StrandRequire(encoder.BindPipeline(paired?impl_->pairedGroupResolve:impl_->groupResolve)&&encoder.BindMesh(impl_->fullscreen)&&
                encoder.BindTexture(impl_->accumulation.color,0)&&encoder.BindTexture(impl_->revealage.color,1)&&
                encoder.DrawIndexed({3}),"Cannot resolve equal-depth strand group");
        }
    }
    if(temporalData&&!frontLayers) {
        // Weighted transparency still has an independent nearest-depth output.
        // Discovery evaluates coverage only and does not alter its color path.
        impl_->Bind(encoder,impl_->temporalNearestTarget,RHICommand::ClearDepth);
        StrandRequire(encoder.BindPipeline(coveragePipelines[1]),"Cannot bind strand nearest-depth discovery");
        bindLayerInputs(impl_->lookupFallback,false);
        drawItems(false,false);
        ++impl_->temporalPassCount;
    }
    if(paired) {
        impl_->Bind(encoder,impl_->pairedTailTarget,RHICommand::ClearColor);
        StrandRequire(encoder.BindPipeline(colorPipelines[4]),"Cannot bind paired strand weighted tail");
        bindLayerInputs(impl_->lookupFallback,false);
        drawItems();
    } else for (int pass=0;pass<2;++pass) {
        const auto framebuffer=frontLayers?(pass?impl_->tailRevealageTarget:impl_->tailAccumulationTarget):
            (pass?impl_->revealage.target:impl_->accumulation.target);
        impl_->Bind(encoder,framebuffer,RHICommand::ClearColor);
        const auto pipeline=frontLayers?(pass?coveragePipelines[3]:colorPipelines[2]):
            (pass?coveragePipelines[0]:colorPipelines[0]);
        StrandRequire(encoder.BindPipeline(pipeline),"Cannot bind strand raster pipeline");
        if(frontLayers)bindLayerInputs(impl_->frontLayers.back().depth,true);
        drawItems(false,pass==0);
    }
    if(target.samples>1) {
        StrandRequire(encoder.ResolveRenderTarget({impl_->accumulation.target,impl_->resolvedAccumulation.target,true,false}) &&
            encoder.ResolveRenderTarget({impl_->revealage.target,impl_->resolvedRevealage.target,true,false}),"Cannot resolve strand coverage");
    }
    impl_->Bind(encoder,motionData?impl_->resolvedHairColor.target:target.framebuffer);
    const auto colorPipeline=(motionData||straightOutput)?impl_->resolvedColorPipelines[Impl::ColourProgramIndex(frontLayers?view.frontDepthGroups:0,paired)]:
        frontLayers?(paired?impl_->pairedFrontComposite[Impl::FrontProgramIndex(view.frontDepthGroups)]:
            impl_->frontComposite[Impl::FrontProgramIndex(view.frontDepthGroups)]):impl_->composite;
    StrandRequire(encoder.BindPipeline(colorPipeline) && encoder.BindMesh(impl_->fullscreen) &&
        encoder.BindTexture(impl_->resolvedAccumulation.color,0) && encoder.BindTexture(impl_->resolvedRevealage.color,1),
        "Cannot bind linear HDR strand compositing");
    if(frontLayers)for(std::size_t i=0;i<impl_->frontLayers.size();++i)
        StrandRequire(encoder.BindTexture(impl_->frontLayers[i].image.color,2+std::uint32_t(i)),"Cannot bind selected strand front layers");
    StrandRequire(encoder.DrawIndexed({3}),"Cannot composite linear HDR strand lighting");
    if(straightOutput)++impl_->colourResolvePassCount;
    if(motionData) {
        impl_->Bind(encoder,target.framebuffer);
        StrandRequire(encoder.BindPipeline(impl_->resolvedColorCopy)&&encoder.BindMesh(impl_->fullscreen)&&
            encoder.BindTexture(impl_->resolvedHairColor.color,0)&&encoder.DrawIndexed({3}),"Cannot composite resolved hair color");
        ++impl_->temporalPassCount;++impl_->colourResolvePassCount;
    }
    if(temporalData) {
        // This target owns different color/depth attachments from every input.
        // Keep scene.depth opaque for earlier effects; downstream callers may
        // explicitly choose this merged raw-grid depth for temporal/history.
        impl_->Bind(encoder,impl_->temporalTarget);
        camera.viewport.w=view.depthWriteThreshold;
        StrandUpload(encoder,impl_->viewBuffer,&camera,sizeof(camera),0);
        const auto nearest=frontLayers?impl_->frontLayers.front().depth:impl_->temporalOwnNearestDepth;
        StrandRequire(encoder.BindPipeline(impl_->temporalResolve[Impl::TemporalProgramIndex(frontLayers?view.frontDepthGroups:0)])&&
            encoder.BindMesh(impl_->fullscreen)&&encoder.BindTexture(impl_->resolvedRevealage.color,0)&&
            encoder.BindTexture(nearest,1)&&encoder.BindTexture(view.opaqueDepth,2),"Cannot bind strand temporal depth inputs");
        if(frontLayers)for(std::size_t i=0;i<impl_->frontLayers.size();++i)
            StrandRequire(encoder.BindTexture(impl_->frontLayers[i].image.color,3+std::uint32_t(i)),"Cannot bind strand temporal group coverage");
        StrandRequire(encoder.DrawIndexed({3}),"Cannot resolve strand coverage and high-coverage depth");
        ++impl_->temporalPassCount;impl_->temporalActive=true;
        if(motionData) {
            impl_->Bind(encoder,impl_->motionAccumulation.target,RHICommand::ClearColor);
            StrandRequire(encoder.BindPipeline(curvesEnabled?impl_->curveMotionAccumulate:impl_->motionAccumulate),"Cannot bind nearest-group strand motion accumulation");
            bindLayerInputs(nearest,false);
            drawItems(true,false);
            impl_->Bind(encoder,impl_->motionOutput.target);
            StrandRequire(encoder.BindPipeline(impl_->motionResolve)&&encoder.BindMesh(impl_->fullscreen)&&
                encoder.BindTexture(impl_->motionAccumulation.color,0)&&encoder.BindTexture(impl_->accumulation.color,1)&&
                encoder.DrawIndexed({3}),"Cannot resolve nearest-group strand motion");
            impl_->temporalPassCount+=2;impl_->motionPassCount=2;impl_->motionActive=true;
        }
        // Match Record's established postcondition for callers recording more
        // draws after the strand pass; auxiliary attachments are never left active.
        impl_->Bind(encoder,target.framebuffer);
    }
    impl_->passCount=frontLayers?(paired?3u*view.frontDepthGroups+2u:4u*view.frontDepthGroups+3u):3u;
    impl_->rasterPassCount=frontLayers?(paired?2u*view.frontDepthGroups+1u:3u*view.frontDepthGroups+2u):2u;
    impl_->pairedActive=paired;impl_->frontLayersActive=frontLayers;
    impl_->activeFrontDepthGroups=frontLayers?view.frontDepthGroups:0;
    impl_->multisampleFallback=(view.frontLayerTransparency||view.nativeOit)&&target.samples>1;
    impl_->temporalMultisampleFallback=(view.outputTemporalData||view.outputMotionData)&&target.samples>1;
}
void StrandRenderPass::Shutdown() {
    impl_->nativeDSVolume.Shutdown();
    impl_->nativeOit.Shutdown();
    impl_->fiberVolume.Reset();
    impl_->segmentShading.Reset();
    impl_->curves.Shutdown();
    impl_->curveCoverageOwner.reset();impl_->curveMotionOwner.reset();impl_->curveCoverage={};impl_->curveMotionAccumulate={};
    impl_->peelTargetsOwner.reset();impl_->frontProgramsOwner.reset();impl_->targetsOwner.reset(); impl_->programsOwner.reset();
    impl_->pairedProgramsOwner.reset();impl_->pairedGroupResolve={};impl_->pairedFrontComposite={};impl_->colorVariants={};
    impl_->temporalTargetsOwner.reset();impl_->temporalProgramsOwners={};
    impl_->motionProgramsOwner.reset();impl_->motionTargetsOwner.reset();impl_->resolvedColorProgramsOwners={};
    impl_->frontLayers={};impl_->tailAccumulationTarget={};impl_->tailRevealageTarget={};
    impl_->pairedTailTarget={};
    impl_->sampledTables.clear();impl_->lookupFallback={};
    impl_->width=impl_->height=impl_->samples=0; impl_->depth={};
    impl_->temporalTarget={};impl_->temporalNearestTarget={};impl_->temporalCoverage={};
    impl_->temporalCompositeDepth={};impl_->temporalOwnNearestDepth={};impl_->temporalResolve={};impl_->frontComposite={};
    impl_->motionAccumulation={};impl_->motionOutput={};impl_->resolvedHairColor={};impl_->resolvedColorPipelines={};
    impl_->motionAccumulate={};impl_->motionResolve={};impl_->resolvedColorCopy={};impl_->motionObjectBuffer={};
    impl_->passCount=impl_->rasterPassCount=impl_->temporalPassCount=impl_->motionPassCount=impl_->colourResolvePassCount=impl_->activeFrontDepthGroups=0;
    impl_->pairedActive=impl_->pairedScratch=false;
    impl_->frontLayersActive=impl_->multisampleFallback=impl_->temporalActive=impl_->temporalMultisampleFallback=impl_->temporalUsesOwnNearest=impl_->motionActive=false;
}
const StrandVolumeStatistics& StrandRenderPass::VolumeStatistics() const {return impl_->volumeStatistics;}
StrandVolumeDebugTextures StrandRenderPass::VolumeDebugTextures() const {return impl_->fiberVolume.DebugTextures();}
StrandSegmentShadingStatistics StrandRenderPass::SegmentShadingStatistics() const {return impl_->segmentShading.Statistics();}
StrandSegmentShadingDebugTextures StrandRenderPass::SegmentShadingDebugTextures() const {return impl_->segmentShading.DebugTextures();}
const StrandCurveStatistics& StrandRenderPass::CurveStatistics() const {return impl_->curves.Statistics();}
StrandCurveDebugTextures StrandRenderPass::CurveDebugTextures() const {return impl_->curves.DebugTextures();}
bool StrandRenderPass::NativeOitActive()const{return impl_->nativeOit.Active();}
std::uint32_t StrandRenderPass::NativeOitComputePasses()const{return impl_->nativeOit.ComputePasses();}
std::uint32_t StrandRenderPass::NativeOitLayers()const{return impl_->nativeOit.Layers();}
std::uint64_t StrandRenderPass::NativeOitBytes()const{return impl_->nativeOit.Bytes();}
StrandNativeOitDebugBuffers StrandRenderPass::NativeOitDebug()const{return impl_->nativeOit.DebugBuffers();}
std::array<std::uint32_t,4> StrandRenderPass::CaptureNativeOitCounters()const{
    std::array<std::uint32_t,4> result{};if(!impl_->nativeOit.Active())return result;
    const auto debug=impl_->nativeOit.DebugBuffers();const auto bytes=debug.owner->Readback(debug.meta,sizeof(result));
    if(bytes.size()==sizeof(result))std::memcpy(result.data(),bytes.data(),sizeof(result));return result;
}
std::uint32_t StrandRenderPass::PassCount() const {return impl_->passCount;}
std::uint32_t StrandRenderPass::TransparencyRasterPassCount() const {return impl_->rasterPassCount;}
bool StrandRenderPass::PairedLayerTransparencyActive() const {return impl_->pairedActive;}
std::uint32_t StrandRenderPass::FrontDepthGroups() const {return impl_->activeFrontDepthGroups;}
std::uint32_t StrandRenderPass::TemporalPassCount() const {return impl_->temporalPassCount;}
std::uint32_t StrandRenderPass::MotionPassCount() const {return impl_->motionPassCount;}
std::uint32_t StrandRenderPass::ColourResolvePassCount() const {return impl_->colourResolvePassCount;}
StrandTemporalTextures StrandRenderPass::TemporalTextures() const {
    if(impl_->nativeOit.Active())return impl_->nativeOit.Textures();
    if(!impl_->temporalActive)return {};
    return {impl_->frontLayersActive?impl_->frontLayers.front().depth:impl_->temporalOwnNearestDepth,
        impl_->temporalCoverage,impl_->temporalCompositeDepth,impl_->motionActive?impl_->motionOutput.color:RenderResourceHandle<RHITextureSpec>{},
        impl_->motionActive?impl_->resolvedHairColor.color:RenderResourceHandle<RHITextureSpec>{},impl_->width,impl_->height};
}
bool StrandRenderPass::TemporalDataMultisampleFallback() const {return impl_->temporalMultisampleFallback;}
bool StrandRenderPass::FrontLayerTransparencyActive() const {return impl_->frontLayersActive;}
bool StrandRenderPass::MultisampleTransparencyFallback() const {return impl_->multisampleFallback;}
void StrandRenderPass::InvalidateVolume(){impl_->fiberVolume.Invalidate();impl_->nativeDSVolume.Invalidate();}
} // namespace Render::PipelineDetail
