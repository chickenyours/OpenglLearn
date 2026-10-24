#pragma once
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Private/Pipeline/strand_render_pass.h"
#include "Render/Private/Pipeline/strand_segment_source.h"
#include <glm/gtc/matrix_inverse.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>

namespace Render::PipelineDetail {
// Source target2/index136 coarseRasterMixSubDHardware count/predicate. Inputs
// use source NDC depth[0,1]; the caller performs the host canonical-depth map.
inline float StrandCurveRoundEven(float x){
    const float floor=std::floor(x),fraction=x-floor;
    if(fraction<.5f)return floor;if(fraction>.5f)return floor+1.f;
    return std::fmod(std::abs(floor),2.f)==0.f?floor:floor+1.f;
}
inline glm::vec2 StrandCurveSourceUV(glm::vec3 p){return {.5f*p.x+.5f,.5f-.5f*p.y};}
inline std::uint32_t StrandCurveNativeSubdivisionCount(glm::vec3 a,glm::vec3 b,glm::vec2 screen,float scale=1.f){
    const auto uv0=StrandCurveSourceUV(a),uv1=StrandCurveSourceUV(b),size=screen*scale;
    const float dx=std::abs((uv0.x-uv1.x)*size.x),dy=std::abs((uv0.y-uv1.y)*size.y);
    const float count=(dx+dy)*0.1111111119389534f;
    return std::isfinite(count)?static_cast<std::uint32_t>(std::floor(std::clamp(count,1.f,15.f))):1u;
}
inline bool StrandCurveNativeVisible(glm::vec3 a,glm::vec3 b,glm::vec2 screen,float scale=1.f){
    // Finite projected coordinates are a host guard; native's late check is
    // only a sum of the untransformed endpoints. No opaque depth is sampled.
    for(int c=0;c<3;++c)if(!std::isfinite(a[c])||!std::isfinite(b[c]))return false;
    const auto lo=glm::min(a,b),hi=glm::max(a,b);
    if(hi.x< -1.f||hi.y< -1.f||lo.x>1.f||lo.y>1.f||hi.z<0.f||lo.z>1.f||hi.z>1.f)return false;
    if((StrandCurveRoundEven(screen.x*lo.x)==StrandCurveRoundEven(screen.x*hi.x)&&
        StrandCurveRoundEven(screen.y*lo.y)==StrandCurveRoundEven(screen.y*hi.y))||lo.z<=0.f)return false;
    const auto size=screen*scale,p=StrandCurveSourceUV(a)*size,q=StrandCurveSourceUV(b)*size;
    return !(std::min(p.x,q.x)>=size.x||std::min(p.y,q.y)>=size.y||std::max(p.x,q.x)< -1.f||std::max(p.y,q.y)< -1.f);
}

// Quality-first CPU scheduling adapter over Render/RHI. It emits the native
// compact child records, never15 degenerate quads per input segment and never
// evaluates scattering or curve positions on the CPU. Fixed original streams
// remain authoritative for shade identities, simulation, and density.
class StrandCurvePass {
    struct alignas(16) Constants {
        glm::uvec4 atlas{0},offsets{0};
        glm::mat4 inverseRasterProjection{1};
    };
    static_assert(sizeof(Constants)==96);
    struct Entry {
        std::weak_ptr<const StrandSegmentSourceData> source;
        std::shared_ptr<const void> owner;
        RenderResourceHandle<RHITextureSpec> atlas;
        std::vector<glm::uvec4> pieces;
        std::vector<std::uint32_t> visibility;
        std::shared_ptr<const StrandGeometry> geometry;
        bool active=false;
        Constants constants;
    };
    RHIDevice& device_;
    std::shared_ptr<const void> common_;
    RenderResourceHandle<VertexBufferSpec> quad_;
    RenderResourceHandle<UniformBufferSpec> constants_;
    RenderResourceHandle<RHITextureSpec> fallback_;
    std::vector<Entry> entries_;
    StrandCurveStatistics statistics_{};
    StrandCurveDebugTextures debug_{};
    static void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
    void Initialize(){
        if(common_)return;
        ResourceBuilder builder(device_);
        struct Vertex {StrandVertex current;glm::vec2 coordinates{-1,-1};StrandVertex previous;};
        static_assert(sizeof(Vertex)==72);
        std::array<Vertex,4> vertices{};
        for(unsigned i=0;i<4;++i){vertices[i].current.positionRadius.w=i<2?-1.f:1.f;vertices[i].previous=vertices[i].current;}
        const std::array<std::uint32_t,6> indices{0,1,2,1,3,2};
        CreateMeshBufferDesc mesh;
        mesh.vertexLayout={{VertexFieldType::Vec4,VertexFieldType::Vec4,VertexFieldType::Vec2,VertexFieldType::Vec4,VertexFieldType::Vec4}};
        mesh.vertexCount=4;mesh.vertexData=vertices.data();mesh.vertexByteSize=sizeof(vertices);
        mesh.indexCount=6;mesh.indexData=indices.data();mesh.indexByteSize=sizeof(indices);
        quad_=builder.Create<VertexBufferSpec>([&](auto callback){device_.async_CreateMeshBuffer(mesh,callback);});
        constants_=builder.Uniform(sizeof(Constants));fallback_=builder.Texture(1,1,RHITextureFormat::RGBA32F);common_=builder.Finish();
    }
public:
    explicit StrandCurvePass(RHIDevice& device):device_(device){}
    void Begin(std::size_t count){entries_.resize(count);for(auto& entry:entries_)entry.active=false;statistics_={};debug_={};}
    void Prepare(std::size_t index,const StrandRenderItem& item,const StrandPassView& view,
                 std::uint32_t width,std::uint32_t height,bool applySourceVisibility=true,bool requireAtlas=false){
        auto& entry=entries_.at(index);entry.geometry=item.geometry;entry.active=item.material.sourceCurveGeometry||requireAtlas;
        if(!entry.active){entry.visibility.clear();return;}
        if(!width||!height||!item.geometry)throw std::invalid_argument("Curve raster requires geometry and a nonempty viewport");
        Initialize();const auto source=item.geometry->SegmentSource();
        if(!source||!source->directedChains||source->points.empty()||source->segments.empty()||
           source->radii.size()!=source->points.size()||source->previousRadii.size()!=source->points.size())
            throw std::invalid_argument("Source curves require directed chains and immutable current/previous physical radii");
        if(source->points.size()>=0x1000000u||source->segments.size()>=0x1000000u)
            throw std::invalid_argument("Curve atlas indices must fit the source24-bit ordinal contract");
        const bool prior=view.motionHistoryValid&&item.previousTransformValid&&item.geometry->PreviousInputMatched();
        const bool shutter=prior&&view.shutterSample<1.f;
        const float time=shutter?view.shutterSample:1.f;
        glm::mat4 projection=view.viewProjection;
        if(shutter){
            projection=view.previousUnjitteredViewProjection*(1.f-time)+view.currentUnjitteredViewProjection*time;
            const auto jitter=view.viewProjection*glm::inverse(view.currentUnjitteredViewProjection)*glm::vec4(0,0,0,1);
            if(!std::isfinite(jitter.w)||std::abs(jitter.w)<1e-8f)throw std::invalid_argument("Curve shutter jitter must be finite");
            for(int c=0;c<4;++c){projection[c].x+=jitter.x/jitter.w*projection[c].w;projection[c].y+=jitter.y/jitter.w*projection[c].w;}
        }
        const float determinant=glm::determinant(projection);
        if(!std::isfinite(determinant)||std::abs(determinant)<1e-20f)throw std::invalid_argument("Curve projection must be invertible");
        entry.constants.inverseRasterProjection=glm::inverse(projection);
        const auto position=[&](std::uint32_t point){
            const auto current=item.model*glm::vec4(glm::vec3(source->points[point].positionArc),1);
            if(!shutter)return glm::vec3(current);
            const auto old=item.previousModel*glm::vec4(glm::vec3(source->points[point].previousPositionArc),1);
            return glm::mix(glm::vec3(old),glm::vec3(current),time);
        };
        const auto project=[&](glm::vec3 p){
            const auto clip=projection*glm::vec4(p,1);
            if(!std::isfinite(clip.w)||std::abs(clip.w)<1e-12f)return glm::vec3(std::numeric_limits<float>::quiet_NaN());
            auto ndc=glm::vec3(clip)/clip.w;ndc.z=ndc.z*.5f+.5f;return ndc;
        };
        std::vector<glm::uvec4> pieces;pieces.reserve(source->segments.size());
        entry.visibility.assign((source->segments.size()+31u)/32u,0u);
        std::uint64_t visible=0;
        const glm::vec2 screen{float(width),float(height)};
        for(std::size_t s=0;s<source->segments.size();++s){
            const auto record=source->segments[s];
            const auto a=project(position(record.x)),b=project(position(record.y));
            const auto& rawA=source->points[record.x].positionArc;const auto& rawB=source->points[record.y].positionArc;
            const float rawSum=((((rawA.x+rawA.y)+rawA.z)+rawB.x)+rawB.y)+rawB.z;
            if(applySourceVisibility&&(!std::isfinite(rawSum)||!StrandCurveNativeVisible(a,b,screen)))continue;
            const auto count=item.material.sourceCurveGeometry?StrandCurveNativeSubdivisionCount(a,b,screen):1u;++visible;
            entry.visibility[s/32u]|=1u<<(s%32u);
            for(std::uint32_t child=0;child<count;++child)pieces.emplace_back(static_cast<std::uint32_t>(s),count,child,record.w);
        }
        statistics_.originalSegments+=source->segments.size();statistics_.visibleSegments+=visible;statistics_.curvePieces+=pieces.size();
        if(!entry.owner||entry.source.lock()!=source||entry.pieces!=pieces){
            const auto points=static_cast<std::uint32_t>(source->points.size()),segments=static_cast<std::uint32_t>(source->segments.size());
            Constants layout;layout.inverseRasterProjection=entry.constants.inverseRasterProjection;
            layout.offsets={points,points*2u,points*3u,points*3u+segments};
            const auto texelCount=std::size_t(layout.offsets.w)+pieces.size();
            const auto atlasWidth=static_cast<std::uint32_t>(std::min<std::size_t>(2048,std::max<std::size_t>(texelCount,1)));
            const auto atlasHeight=static_cast<std::uint32_t>((texelCount+atlasWidth-1)/atlasWidth);
            if(atlasHeight>16384)throw std::invalid_argument("Curve atlas exceeds bounded16K height");
            layout.atlas={atlasWidth,atlasHeight,points,segments};
            std::vector<glm::vec4> texels(std::size_t(atlasWidth)*atlasHeight,glm::vec4(0));
            std::vector<std::uint32_t> next(points,std::numeric_limits<std::uint32_t>::max());
            for(const auto& record:source->segments)next[record.x]=record.y;
            for(std::size_t p=0;p<points;++p){
                texels[p]=glm::vec4(glm::vec3(source->points[p].positionArc),source->radii[p]);
                texels[layout.offsets.x+p]=glm::vec4(glm::vec3(source->points[p].previousPositionArc),source->previousRadii[p]);
                texels[layout.offsets.y+p]=glm::vec4(source->points[p].positionArc.w,
                    source->points[p].colorCoordinates.x,source->points[p].colorCoordinates.y,0);
            }
            for(std::size_t s=0;s<segments;++s){const auto r=source->segments[s];
                const auto successor=next[r.y]==std::numeric_limits<std::uint32_t>::max()?r.y:next[r.y];
                texels[layout.offsets.z+s]=glm::vec4(r.w?r.x:r.z,r.x,r.y,successor);
            }
            for(std::size_t p=0;p<pieces.size();++p)texels[layout.offsets.w+p]=glm::vec4(pieces[p]);
            ResourceBuilder builder(device_);CreateRHITextureSpec texture;
            texture.width=atlasWidth;texture.height=atlasHeight;texture.data=texels.data();texture.mipmaps=false;
            texture.textureDataStoreType=texture.textureUseType=RHITextureFormat::RGBA32F;
            texture.filterMode=RHIFilterMode::Nearest;texture.addressMode=RHIAddressMode::ClampToEdge;
            const auto atlas=builder.Create<RHITextureSpec>([&](auto callback){device_.async_CreateTexture(texture,callback);});
            entry.atlas=atlas;entry.owner=builder.Finish();entry.source=source;entry.pieces=std::move(pieces);entry.constants=layout;
            ++statistics_.uploads;
        }
        if(!debug_.atlas.IsValid())debug_={entry.atlas,entry.constants.atlas,entry.constants.offsets,static_cast<std::uint32_t>(entry.pieces.size())};
    }
    void Bind(RHIFrameEncoder& encoder,std::size_t index){
        Initialize();
        const auto& entry=entries_.at(index);
        if(!entry.active){const Constants disabled{};
            Require(encoder.KeepAlive(common_)&&(!entry.geometry||encoder.KeepAlive(entry.geometry))&&
                encoder.UpdateUniformBufferBytes(constants_,&disabled,sizeof(disabled),0)&&encoder.BindUniformBuffer(constants_,13)&&
                encoder.BindTexture(fallback_,8),"Cannot restore legacy strand geometry");return;
        }
        Require(entry.owner&&encoder.KeepAlive(common_)&&encoder.KeepAlive(entry.owner)&&
            encoder.UpdateUniformBufferBytes(constants_,&entry.constants,sizeof(Constants),0)&&encoder.BindUniformBuffer(constants_,13)&&
            encoder.BindTexture(entry.atlas,8),"Cannot bind source curve geometry");
    }
    void Draw(RHIFrameEncoder& encoder,std::size_t index){
        const auto& entry=entries_.at(index);
        if(!entry.active){if(entry.geometry)Require(encoder.BindMesh(entry.geometry->Mesh())&&
            encoder.DrawIndexed({entry.geometry->IndexCount()}),"Cannot draw legacy strand geometry");return;}
        const auto count=entry.pieces.size();if(!count)return;
        Require(encoder.BindMesh(quad_)&&encoder.DrawIndexed({6,0,0,static_cast<std::uint32_t>(count),0}),"Cannot draw compact source curve children");
    }
    const StrandCurveStatistics& Statistics()const{return statistics_;}
    StrandCurveDebugTextures DebugTextures()const{return debug_;}
    StrandCurveDebugTextures Input(std::size_t index)const{const auto& e=entries_.at(index);return e.active?
        StrandCurveDebugTextures{e.atlas,e.constants.atlas,e.constants.offsets,static_cast<std::uint32_t>(e.pieces.size())}:StrandCurveDebugTextures{};}
    std::span<const std::uint32_t> VisibilityWords(std::size_t index)const{return entries_.at(index).visibility;}
    void Shutdown(){entries_.clear();common_.reset();quad_={};constants_={};fallback_={};statistics_={};debug_={};}
};
} // namespace Render::PipelineDetail
