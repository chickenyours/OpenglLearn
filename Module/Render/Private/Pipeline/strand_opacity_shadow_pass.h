#pragma once
#include "Render/Private/Pipeline/strand_opacity_shadow_shaders.h"
#include "Render/Private/Pipeline/post_process_shaders.h"
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace Render::PipelineDetail {
struct StrandOpacityShadowDebugTextures {
    RenderResourceHandle<RHITextureSpec> nearest,lowTau,highTau,combinedAtlas;
    std::uint32_t atlasResolution=0,opacityResolution=0;
};
struct StrandOpacityShadowResult {
    RenderResourceHandle<RHITextureSpec> combinedAtlas;
    glm::vec4 depthSpans{0};
    std::uint32_t nearestDraws=0,tauDraws=0,packPasses=0;
    StrandCurveStatistics curveStatistics{};
};

// HOST eight-node cumulative optical-depth representation, not the original
// game's unobserved hair shadow PSO or character visibility law. The device
// and the caller-owned original opaque depth lease must outlive queued work.
class StrandOpacityShadowPass {
    RHIDevice& device_;
    std::shared_ptr<const void> programsOwner_,targetsOwner_;
    RenderResourceHandle<PipelineSpec> nearestPipeline_,tauPipeline_,packPipeline_;
    RenderResourceHandle<PipelineSpec> nearestCurvePipeline_,tauCurvePipeline_;
    RenderResourceHandle<UniformBufferSpec> view_,object_,material_;
    RenderResourceHandle<VertexBufferSpec> fullscreen_;
    RenderResourceHandle<RenderTargetSpec> nearestTarget_,tauTarget_,combinedTarget_;
    StrandOpacityShadowDebugTextures textures_;
    struct alignas(16) View {glm::mat4 vp{1};glm::vec4 direction{0},viewport{1};};
    static_assert(sizeof(View)==96);
    struct CurveCache {
        std::unique_ptr<StrandCurvePass> pass;
        glm::mat4 projection{1};std::uint32_t tileSize=0;
        std::uint64_t lastUse=0;bool valid=false;
    };
    std::array<CurveCache,4> curves_;
    std::uint64_t curveUse_=0;
    struct BoundsEntry {
        std::weak_ptr<const StrandGeometry> geometry;
        glm::vec3 minimum{0},maximum{0};float radius=0;
    };
    std::vector<BoundsEntry> bounds_;
    static void Require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
    static bool Finite(const glm::mat4& m){for(int c=0;c<4;++c)for(int r=0;r<4;++r)if(!std::isfinite(m[c][r]))return false;return true;}
    static bool Caster(const StrandRenderItem& item){return item.geometry&&item.geometry->IndexCount()&&!item.guides&&item.material.castDirectionalShadows;}
    static bool Same(const glm::mat4& a,const glm::mat4& b){for(int c=0;c<4;++c)for(int r=0;r<4;++r)if(a[c][r]!=b[c][r])return false;return true;}
    StrandCurvePass& Curves(const glm::mat4& vp,std::uint32_t tileSize){
        CurveCache* chosen=nullptr;
        for(auto& cache:curves_)if(cache.valid&&cache.tileSize==tileSize&&Same(cache.projection,vp)){chosen=&cache;break;}
        if(!chosen)for(auto& cache:curves_)if(!cache.valid){chosen=&cache;break;}
        if(!chosen)chosen=&*std::min_element(curves_.begin(),curves_.end(),[](const auto& a,const auto& b){return a.lastUse<b.lastUse;});
        if(!chosen->pass)chosen->pass=std::make_unique<StrandCurvePass>(device_);
        chosen->projection=vp;chosen->tileSize=tileSize;chosen->valid=true;chosen->lastUse=++curveUse_;
        return *chosen->pass;
    }
    void Initialize(){
        if(programsOwner_)return;
        ResourceBuilder b(device_);
        const auto vertex=StrandShadowVertexShader();
        auto curveVertex=vertex;
        curveVertex.insert(curveVertex.find("void main()"),"layout(location=2) in vec2 colorCoordinates;\nlayout(location=3) in vec4 previousPositionRadius;\nlayout(location=4) in vec4 previousTangentArc;\n");
        curveVertex=StrandCurveVertexMain(std::move(curveVertex));
        const auto pipeline=[&](const std::string& vs,const std::string& fs,bool nearest){
            CreateGraphicShaderDesc desc;desc.vertexShaderSource=b.Source(ShaderSourceType::Vertex,vs);desc.fragmentShaderSource=b.Source(ShaderSourceType::Fragment,fs);
            const auto shader=b.Create<ShaderProgramSpec>([&](auto cb){device_.async_CreateGraphicShader(desc,cb);});
            PipelineSpec spec;spec.shaderProgram=shader;
            spec.expectVertexLayout={{VertexFieldType::Vec4,VertexFieldType::Vec4,VertexFieldType::Vec2,VertexFieldType::Vec4,VertexFieldType::Vec4}};
            spec.cullMode=CullMode::None;spec.depthTest=spec.depthWrite=nearest;spec.depthCompare=CompareOp::LessEqual;
            spec.blendEnable=!nearest;spec.blendMode=nearest?BlendMode::Opaque:BlendMode::AdditiveRaw;
            return b.Create<PipelineSpec>([&](auto cb){device_.async_CreatePipeline({spec},cb);});
        };
        const auto nearest=StrandOpacityShadowNearestFragmentShader(),tau=StrandOpacityShadowTauFragmentShader();
        nearestPipeline_=pipeline(vertex,nearest,true);tauPipeline_=pipeline(vertex,tau,false);
        nearestCurvePipeline_=pipeline(curveVertex,nearest,true);tauCurvePipeline_=pipeline(curveVertex,tau,false);
        packPipeline_=b.FullscreenPipeline(FullscreenVertexShader(),StrandOpacityShadowPackFragmentShader());
        fullscreen_=b.FullscreenMesh();view_=b.Uniform(sizeof(View));object_=b.Uniform(sizeof(glm::mat4));material_=b.Uniform(sizeof(glm::vec4));
        programsOwner_=b.Finish();
    }
    void Resize(std::uint32_t a){
        if(targetsOwner_&&textures_.atlasResolution==a)return;
        ResourceBuilder b(device_);const auto o=a/2;
        StrandOpacityShadowDebugTextures textures;textures.atlasResolution=a;textures.opacityResolution=o;
        textures.nearest=b.Texture(o,o,RHITextureFormat::Depth32F);
        textures.lowTau=b.Texture(o,o,RHITextureFormat::RGBA32F);textures.highTau=b.Texture(o,o,RHITextureFormat::RGBA32F);
        textures.combinedAtlas=b.Texture(a,a,RHITextureFormat::RGBA32F);
        const auto nearest=b.Target({},textures.nearest);
        const auto tau=b.TargetColors({textures.lowTau,textures.highTau,{},{}});
        const auto combined=b.Target(textures.combinedAtlas,{});
        const auto owner=b.Finish();
        textures_=textures;nearestTarget_=nearest;tauTarget_=tau;combinedTarget_=combined;targetsOwner_=owner;
        // New dimensions change light-space source subdivision and visibility.
        for(auto& cache:curves_)cache.valid=false;
    }
    BoundsEntry LocalBounds(const std::shared_ptr<const StrandGeometry>& geometry){
        for(const auto& entry:bounds_)if(auto existing=entry.geometry.lock();existing&&existing.get()==geometry.get())return entry;
        std::erase_if(bounds_,[](const auto& b){return b.geometry.expired();});
        BoundsEntry result;result.geometry=geometry;result.minimum=geometry->BoundsMin();result.maximum=geometry->BoundsMax();
        const auto source=geometry->SegmentSource();
        if(source){
            for(float radius:source->radii)result.radius=std::max(result.radius,std::abs(radius));
            // Endpoint AABBs need not contain the interior of a source curve.
            // Its equivalent cubic Bezier control hull supplies a conservative
            // position bound; this does not rasterize/evaluate the curve on CPU.
            if(source->directedChains&&!source->points.empty()){
                const auto absent=std::numeric_limits<std::uint32_t>::max();
                std::vector<std::uint32_t> next(source->points.size(),absent);
                for(const auto& r:source->segments)next.at(r.x)=r.y;
                for(const auto& r:source->segments){
                    const auto successor=next.at(r.y);
                    const glm::vec3 p1(source->points.at(r.x).positionArc),p2(source->points.at(r.y).positionArc);
                    const glm::vec3 p0=r.w?2.f*p1-p2:glm::vec3(source->points.at(r.z).positionArc);
                    const glm::vec3 p3=successor==absent?2.f*p2-p1:glm::vec3(source->points.at(successor).positionArc);
                    const auto d1=p1-p0,d2=p2-p1,d3=p3-p2;
                    const float a=std::sqrt(glm::length(d1)),b=std::sqrt(glm::length(d2)),c=std::sqrt(glm::length(d3));
                    if(std::min(a,std::min(b,c))<1e-6f)continue;
                    const glm::vec3 m1=d1*(b*b/(a*(a+b)))+d2*(a/(a+b));
                    const glm::vec3 m2=d2*(c/(b+c))+d3*(b*b/(c*(b+c)));
                    for(const auto& p:{p1+m1/3.f,p2-m2/3.f}){result.minimum=glm::min(result.minimum,p);result.maximum=glm::max(result.maximum,p);}
                }
            }
        }else{
            const auto extent=glm::max(result.maximum-result.minimum,glm::vec3(0));
            result.radius=.5f*std::min(extent.x,std::min(extent.y,extent.z));
        }
        bounds_.push_back(result);return result;
    }
    glm::vec4 DepthSpans(std::span<const StrandRenderItem> items,const std::array<glm::mat4,4>& vp){
        glm::vec4 lo(std::numeric_limits<float>::max()),hi(-std::numeric_limits<float>::max());bool any=false;
        for(const auto& item:items){if(!Caster(item))continue;any=true;
            const auto bounds=LocalBounds(item.geometry);const auto& m=item.material;
            const float q=item.strandCountFraction>1e-5f?1.f/item.strandCountFraction:100000.f;
            float width=std::max(2.f*bounds.radius*m.widthScale*q,m.minWidthMetres*q);
            if(m.maxWidthMetres>0)width=std::min(width,m.maxWidthMetres*q);
            // The shared VS expands a normalized world-space side by width
            // times the largest model-column length. A local-space padding
            // would underbound that ribbon under nonuniform model scaling.
            glm::vec3 minimum(std::numeric_limits<float>::max()),maximum(-std::numeric_limits<float>::max());
            for(unsigned corner=0;corner<8;++corner){
                const glm::vec3 p((corner&1)?bounds.maximum.x:bounds.minimum.x,(corner&2)?bounds.maximum.y:bounds.minimum.y,(corner&4)?bounds.maximum.z:bounds.minimum.z);
                const glm::vec3 world(item.model*glm::vec4(p,1));
                minimum=glm::min(minimum,world);maximum=glm::max(maximum,world);
            }
            const float modelScale=std::max(glm::length(glm::vec3(item.model[0])),std::max(glm::length(glm::vec3(item.model[1])),glm::length(glm::vec3(item.model[2]))));
            const glm::vec3 margin(.5f*width*modelScale);
            minimum-=margin;maximum+=margin;
            for(unsigned c=0;c<4;++c)for(unsigned corner=0;corner<8;++corner){
                const glm::vec3 p((corner&1)?maximum.x:minimum.x,(corner&2)?maximum.y:minimum.y,(corner&4)?maximum.z:minimum.z);
                const auto clip=vp[c]*glm::vec4(p,1);
                if(!std::isfinite(clip.z)||!std::isfinite(clip.w)||std::abs(clip.w)<1e-8f)throw std::invalid_argument("Invalid DOM projected groom bounds");
                const float depth=.5f*(clip.z/clip.w)+.5f;
                lo[c]=std::min(lo[c],depth);hi[c]=std::max(hi[c],depth);
            }
        }
        if(!any)return glm::vec4(1e-6f);
        glm::vec4 result;
        for(unsigned c=0;c<4;++c){const float safety=32.f*std::numeric_limits<float>::epsilon()*std::max(1.f,std::max(std::abs(lo[c]),std::abs(hi[c])));
            result[c]=std::max(hi[c]-lo[c]+safety,1e-6f);if(!std::isfinite(result[c]))throw std::invalid_argument("DOM depth span overflow");}
        return result;
    }
    void Target(RHIFrameEncoder& e,RenderResourceHandle<RenderTargetSpec> target,std::uint32_t size,std::uint8_t flags){
        RHICommand::SetRenderTarget t;t.target=target;t.width=t.height=size;t.clearFlags=flags;t.clearColor=glm::vec4(0);t.clearDepth=1;
        Require(e.SetScissor({false,0,0,size,size})&&e.SetRenderTarget(t)&&e.SetViewport({0,0,size,size}),"Cannot select DOM target");
    }
public:
    explicit StrandOpacityShadowPass(RHIDevice& device):device_(device){}
    StrandOpacityShadowResult Record(RHIFrameEncoder& encoder,std::span<const StrandRenderItem> items,
        const std::array<glm::mat4,4>& vp,glm::vec3 towardLight,std::uint32_t atlasResolution,
        RenderResourceHandle<RHITextureSpec> originalOpaqueDepth){
        if(atlasResolution<8||atlasResolution>8192||atlasResolution%4||!originalOpaqueDepth.IsValid())
            throw std::invalid_argument("DOM requires a valid opaque atlas and resolution divisible by four");
        for(const auto& p:vp)if(!Finite(p))throw std::invalid_argument("DOM projection must be finite");
        if(!std::isfinite(towardLight.x)||!std::isfinite(towardLight.y)||!std::isfinite(towardLight.z)||glm::dot(towardLight,towardLight)<1e-12f)
            throw std::invalid_argument("DOM light direction must be finite and nonzero");
        bool anyCurves=false;
        for(const auto& i:items)if(Caster(i)){
            if(!Finite(i.model)||!std::isfinite(i.strandCountFraction)||i.strandCountFraction<0||i.strandCountFraction>1||
               !std::isfinite(i.material.widthScale)||i.material.widthScale<=0||!std::isfinite(i.material.opacity)||i.material.opacity<0||i.material.opacity>1||
               !std::isfinite(i.material.minWidthMetres)||i.material.minWidthMetres<0||!std::isfinite(i.material.maxWidthMetres)||i.material.maxWidthMetres<0||
               (i.material.maxWidthMetres>0&&i.material.maxWidthMetres<i.material.minWidthMetres))
                throw std::invalid_argument("Invalid DOM strand model, width, fraction or opacity");
            anyCurves|=i.material.sourceCurveGeometry;
        }
        Initialize();Resize(atlasResolution);
        StrandOpacityShadowResult result;result.combinedAtlas=textures_.combinedAtlas;result.depthSpans=DepthSpans(items,vp);
        Require(encoder.KeepAlive(programsOwner_)&&encoder.KeepAlive(targetsOwner_),"Cannot retain DOM resource generation");
        // Slot12 may retain last frame's packed output. Detach it before that
        // texture becomes the pack target; no attachment is ever sampled.
        Require(encoder.BindTexture({},0)&&encoder.BindTexture({},1)&&encoder.BindTexture({},2)&&encoder.BindTexture({},3)&&encoder.BindTexture({},12),"Cannot reset DOM sampled attachments");
        const auto o=textures_.opacityResolution,tile=o/2;
        std::array<StrandCurvePass*,4> curvePasses{};
        Target(encoder,nearestTarget_,o,RHICommand::ClearDepth);
        const auto draw=[&](unsigned cascade,bool tau){
            const auto x=std::int32_t((cascade%2)*tile),y=std::int32_t((cascade/2)*tile);
            Require(encoder.SetViewport({x,y,tile,tile})&&encoder.SetScissor({true,x,y,tile,tile}),"Cannot select DOM cascade");
            View view;view.vp=vp[cascade];view.direction=glm::vec4(towardLight,0);view.viewport=glm::vec4(tile,tile,result.depthSpans[cascade],0);
            Require(encoder.UpdateUniformBufferBytes(view_,&view,sizeof(view),0)&&encoder.BindUniformBuffer(view_,0),"Cannot bind DOM view");
            auto* curves=curvePasses[cascade];
            if(!tau&&anyCurves){curves=&Curves(vp[cascade],tile);curvePasses[cascade]=curves;curves->Begin(items.size());}
            for(std::size_t index=0;index<items.size();++index){const auto& item=items[index];if(!Caster(item))continue;
                const float q=item.strandCountFraction>1e-5f?1.f/item.strandCountFraction:100000.f;const auto& m=item.material;
                const glm::vec4 widths(m.widthScale*q,m.minWidthMetres*q,m.maxWidthMetres*q,m.opacity);
                Require(encoder.KeepAlive(item.geometry)&&encoder.UpdateUniformBufferBytes(object_,&item.model,sizeof(item.model),0)&&encoder.BindUniformBuffer(object_,1)&&
                    encoder.UpdateUniformBufferBytes(material_,&widths,sizeof(widths),0)&&encoder.BindUniformBuffer(material_,2),"Cannot bind DOM strand");
                if(item.material.sourceCurveGeometry){
                    if(!tau){StrandPassView lightView;lightView.viewProjection=vp[cascade];curves->Prepare(index,item,lightView,tile,tile);}
                    Require(encoder.BindPipeline(tau?tauCurvePipeline_:nearestCurvePipeline_),"Cannot bind DOM curve pipeline");
                    curves->Bind(encoder,index);curves->Draw(encoder,index);
                }else Require(encoder.BindPipeline(tau?tauPipeline_:nearestPipeline_)&&encoder.BindMesh(item.geometry->Mesh())&&encoder.DrawIndexed({item.geometry->IndexCount()}),"Cannot draw DOM strand ribbons");
                if(tau)++result.tauDraws;else ++result.nearestDraws;
            }
            if(!tau&&curves){const auto s=curves->Statistics();result.curveStatistics.uploads+=s.uploads;result.curveStatistics.originalSegments+=s.originalSegments;
                result.curveStatistics.visibleSegments+=s.visibleSegments;result.curveStatistics.curvePieces+=s.curvePieces;}
        };
        for(unsigned c=0;c<4;++c)draw(c,false);
        // Switching to a color-only MRT detaches nearest depth before sampling.
        Target(encoder,tauTarget_,o,RHICommand::ClearColor);
        Require(encoder.BindTexture(textures_.nearest,0),"Cannot sample DOM nearest depth");
        for(unsigned c=0;c<4;++c)draw(c,true);
        Require(encoder.BindTexture({},0),"Cannot detach DOM nearest sampler");
        Target(encoder,combinedTarget_,atlasResolution,RHICommand::ClearColor);
        Require(encoder.BindTexture(originalOpaqueDepth,0)&&encoder.BindTexture(textures_.nearest,1)&&encoder.BindTexture(textures_.lowTau,2)&&encoder.BindTexture(textures_.highTau,3)&&
            encoder.BindPipeline(packPipeline_)&&encoder.BindMesh(fullscreen_)&&encoder.DrawIndexed({3}),"Cannot pack DOM with original opaque depth");
        ++result.packPasses;
        Require(encoder.BindTexture({},0)&&encoder.BindTexture({},1)&&encoder.BindTexture({},2)&&encoder.BindTexture({},3)&&
            encoder.SetViewport({0,0,atlasResolution,atlasResolution})&&encoder.SetScissor({false,0,0,atlasResolution,atlasResolution}),"Cannot restore DOM shadow viewport/samplers");
        return result;
    }
    StrandOpacityShadowDebugTextures DebugTextures()const{return textures_;}
    void Reset(){
        for(auto& cache:curves_){if(cache.pass)cache.pass->Shutdown();cache=CurveCache{};}
        curveUse_=0;bounds_.clear();textures_={};targetsOwner_.reset();programsOwner_.reset();
        nearestPipeline_={};tauPipeline_={};packPipeline_={};nearestCurvePipeline_={};tauCurvePipeline_={};
        nearestTarget_={};tauTarget_={};combinedTarget_={};view_={};object_={};material_={};fullscreen_={};
    }
};
} // namespace Render::PipelineDetail
