#include "Render/Public/Pipeline/surface_cache.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <glm/gtc/matrix_inverse.hpp>

namespace Render {
namespace {
bool Finite(glm::vec3 v) {return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
std::size_t Padded(std::size_t n) {return std::max<std::size_t>(SurfaceCacheAtlasWidth,(n+SurfaceCacheAtlasWidth-1)/SurfaceCacheAtlasWidth*SurfaceCacheAtlasWidth);}
glm::vec3 Axis(int axis,float sign=1) {glm::vec3 result(0);result[axis]=sign;return result;}
bool BoxRay(glm::vec3 p,glm::vec3 d,glm::vec3 low,glm::vec3 high,float maximum) {
    float minimum=0;
    for(int a=0;a<3;++a) {
        if(std::abs(d[a])<1e-12f) {if(p[a]<low[a]||p[a]>high[a])return false;continue;}
        const float x=(low[a]-p[a])/d[a],y=(high[a]-p[a])/d[a];
        minimum=std::max(minimum,std::min(x,y));maximum=std::min(maximum,std::max(x,y));
        if(minimum>maximum)return false;
    }
    return true;
}
// Consume the same immutable local BVH as Mesh SDF generation. No separate
// acceleration build and no O(triangles * texels) capture loop are needed.
bool TraceLocal(const RealtimeGiScene& scene,glm::vec3 origin,glm::vec3 direction,float maximum,
    glm::vec3& position,glm::vec3& normal) {
    if(!scene.NodeCount())return false;
    const auto* data=scene.Pixels().data();const auto triangleStart=scene.NodeCount()*2;
    std::array<std::uint32_t,64> stack{};std::size_t size=1;bool found=false;
    while(size) {
        const auto node=stack[--size];const auto low=data[node*2],high=data[node*2+1];
        if(!BoxRay(origin,direction,glm::vec3(low),glm::vec3(high),maximum))continue;
        if(high.w>=0) {
            if(size+2>stack.size())throw std::runtime_error("Surface cache BVH stack overflow");
            stack[size++]=std::uint32_t(low.w);stack[size++]=std::uint32_t(high.w);continue;
        }
        for(auto triangle=std::uint32_t(low.w);triangle<std::uint32_t(low.w-high.w);++triangle) {
            const auto at=triangleStart+triangle*5;
            const glm::vec3 a(data[at]),ab=glm::vec3(data[at+1])-a,ac=glm::vec3(data[at+2])-a;
            const auto cross=glm::cross(direction,ac);const float determinant=glm::dot(ab,cross);
            if(std::abs(determinant)<1e-10f)continue;
            const auto relative=origin-a;const float u=glm::dot(relative,cross)/determinant;
            if(u< -1e-6f||u>1.000001f)continue;
            const auto q=glm::cross(relative,ab);const float v=glm::dot(direction,q)/determinant;
            if(v< -1e-6f||u+v>1.000001f)continue;
            const float distance=glm::dot(ac,q)/determinant;
            if(distance<0||distance>=maximum)continue;
            maximum=distance;position=origin+direction*distance;normal=glm::normalize(glm::cross(ab,ac));found=true;
        }
    }
    return found;
}
void ValidateTransform(const glm::mat4& transform) {
    for(int i=0;i<4;++i)for(int j=0;j<4;++j)
        if(!std::isfinite(transform[i][j])||std::abs(transform[i][j])>100000)
            throw std::invalid_argument("Invalid surface cache transform");
    if(glm::any(glm::greaterThan(glm::abs(glm::vec4(transform[0].w,transform[1].w,transform[2].w,transform[3].w)-glm::vec4(0,0,0,1)),glm::vec4(1e-6f)))||
        std::abs(glm::determinant(glm::mat3(transform)))<1e-10f)
        throw std::invalid_argument("Surface cache requires invertible affine transforms");
}
}
struct SurfaceCacheScene::LocalCapture {
    std::shared_ptr<const ProbeMeshSdf> mesh;
    std::vector<glm::vec4> positions,normals;
    std::uint64_t validTexels=0;
    LocalCapture(std::shared_ptr<const ProbeMeshSdf> source,std::uint32_t resolution):mesh(std::move(source)) {
        const auto count=std::size_t(resolution)*resolution*6;positions.resize(count);normals.resize(count);
        const auto low=mesh->BoundsMin(),high=mesh->BoundsMax(),extent=high-low;
        const float margin=std::max(.001f,glm::length(extent)*.001f);
        for(int direction=0;direction<6;++direction) {
            const int axis=direction/2,u=(axis+1)%3,v=(axis+2)%3;const float sign=direction%2?-1.f:1.f;
            const auto outward=Axis(axis,sign);
            // A card with no projected area has no meaningful capture.
            if(extent[u]<1e-7f||extent[v]<1e-7f)continue;
            for(std::uint32_t y=0;y<resolution;++y)for(std::uint32_t x=0;x<resolution;++x) {
                auto origin=low;origin[u]+=(float(x)+.5f)/resolution*extent[u];origin[v]+=(float(y)+.5f)/resolution*extent[v];
                origin[axis]=(sign>0?high[axis]+margin:low[axis]-margin);
                glm::vec3 position,normal;
                if(!TraceLocal(*mesh->Geometry(),origin,-outward,extent[axis]+margin*2,position,normal)||glm::dot(normal,outward)<=.05f)continue;
                const auto index=(std::size_t(direction)*resolution+y)*resolution+x;
                positions[index]=glm::vec4(position,1);normals[index]=glm::vec4(normal,1);++validTexels;
            }
        }
    }
};

SurfaceCacheScene::SurfaceCacheScene(const SurfaceCacheSettings& settings):settings_(settings) {
    if(settings.pageResolution<16||settings.pageResolution>128||(settings.pageResolution&(settings.pageResolution-1)))
        throw std::invalid_argument("Surface cache page resolution must be 16, 32, 64 or 128");
    attributes_.resize(planeTexels_*3);metadata_.resize(SurfaceCacheAtlasWidth);
}
bool SurfaceCacheScene::Update(const ProbeSdfScene& scene,std::span<const SurfaceCacheMaterial> materials) {
    const auto begin=std::chrono::steady_clock::now();const auto& instances=scene.Instances();
    if(!materials.empty()&&materials.size()!=instances.size())throw std::invalid_argument("Surface cache material count must match instances");
    const std::uint64_t texels=std::uint64_t(instances.size())*6*settings_.pageResolution*settings_.pageResolution;
    if(texels>4194304||instances.size()>512)throw std::invalid_argument("Surface cache fixed atlas capacity exceeded");
    for(std::size_t i=0;i<instances.size();++i) {
        if(!instances[i].mesh)throw std::invalid_argument("Surface cache instance has no mesh");
        ValidateTransform(instances[i].localToWorld);const auto material=materials.empty()?SurfaceCacheMaterial{}:materials[i];
        if(!Finite(material.albedo)||!Finite(material.emission)||glm::any(glm::lessThan(material.albedo,glm::vec3(0)))||
            glm::any(glm::greaterThan(material.albedo,glm::vec3(1)))||glm::any(glm::lessThan(material.emission,glm::vec3(0)))||
            glm::any(glm::greaterThan(material.emission,glm::vec3(60000))))throw std::invalid_argument("Invalid surface cache material");
    }
    statistics_.updatedCards=0;statistics_.updatedTexels=0;statistics_.captureRays=0;dirtyCards_.clear();
    const auto& geometry=scene.PackedGeometry();
    const bool ownersChanged=!ownerGeometry_||ownerGeometry_->TopologyKey()!=geometry->TopologyKey();
    const auto metadataSize=Padded(instances.size()*6*SurfaceCacheCardTexels+(std::size_t(geometry->TriangleCount())+3)/4);
    const bool resize=states_.size()!=instances.size()||generation_==0;
    if(resize) {
        states_.resize(instances.size());planeTexels_=std::uint32_t(Padded(std::size_t(texels)));
        attributes_.assign(std::size_t(planeTexels_)*3,glm::vec4(0));
        metadata_.assign(metadataSize,glm::vec4(0));
    }
    if(metadata_.size()!=metadataSize)metadata_.resize(metadataSize,glm::vec4(0));
    bool changed=resize||ownersChanged;const auto resolution=settings_.pageResolution,pageTexels=resolution*resolution;
    for(std::size_t object=0;object<instances.size();++object) {
        const auto& instance=instances[object];const auto mesh=instance.geometryMesh?instance.geometryMesh:instance.mesh;
        const auto material=materials.empty()?SurfaceCacheMaterial{}:materials[object];auto& previous=states_[object];
        const bool geometryChanged=previous.mesh!=mesh,transformChanged=previous.transform!=instance.localToWorld;
        const bool materialChanged=previous.material!=material;
        if(!resize&&!geometryChanged&&!transformChanged&&!materialChanged)continue;
        changed=true;
        std::shared_ptr<const LocalCapture> capture=previous.capture;
        if(!capture||geometryChanged) {
            const auto existing=captures_.find(mesh.get());
            if(existing!=captures_.end())capture=existing->second;
            else {capture=std::make_shared<LocalCapture>(mesh,resolution);captures_.emplace(mesh.get(),capture);statistics_.captureRays+=std::uint64_t(pageTexels)*6;}
        }
        const auto inverse=glm::inverse(instance.localToWorld);const auto normalMatrix=glm::transpose(glm::mat3(inverse));
        const auto low=mesh->BoundsMin(),high=mesh->BoundsMax(),extent=high-low;
        glm::vec3 worldLow(1e30f),worldHigh(-1e30f);
        for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
            const auto point=glm::vec3(instance.localToWorld*glm::vec4(x?high.x:low.x,y?high.y:low.y,z?high.z:low.z,1));
            worldLow=glm::min(worldLow,point);worldHigh=glm::max(worldHigh,point);
        }
        for(std::uint32_t direction=0;direction<6;++direction) {
            const auto card=std::uint32_t(object)*6+direction,first=card*pageTexels,meta=card*SurfaceCacheCardTexels;
            dirtyCards_.push_back(card);++statistics_.updatedCards;statistics_.updatedTexels+=pageTexels;
            const int axis=int(direction)/2,u=(axis+1)%3,v=(axis+2)%3;
            const auto worldNormal=glm::normalize(normalMatrix*Axis(axis,direction%2?-1.f:1.f));
            const float cellU=glm::length(glm::vec3(instance.localToWorld[u]))*extent[u]/resolution;
            const float cellV=glm::length(glm::vec3(instance.localToWorld[v]))*extent[v]/resolution;
            const float tolerance=std::max(.002f,.75f*std::sqrt(cellU*cellU+cellV*cellV));
            for(int column=0;column<4;++column)metadata_[meta+column]=inverse[column];
            metadata_[meta+4]=glm::vec4(low,float(direction));metadata_[meta+5]=glm::vec4(high,float(first));
            metadata_[meta+6]=glm::vec4(worldNormal,tolerance);metadata_[meta+7]=glm::vec4(material.albedo,float(object));
            metadata_[meta+8]=glm::vec4(material.emission,float(generation_+1));
            metadata_[meta+9]=glm::vec4(worldLow,float(resolution));metadata_[meta+10]=glm::vec4(worldHigh,0);
            for(std::uint32_t texel=0;texel<pageTexels;++texel) {
                const auto local=direction*pageTexels+texel,index=first+texel;
                if(capture->positions[local].w<.5f) {
                    attributes_[index]=attributes_[planeTexels_+index]=attributes_[planeTexels_*2+index]=glm::vec4(0);continue;
                }
                if(resize||geometryChanged||transformChanged||!previous.capture) {
                    attributes_[index]=glm::vec4(glm::vec3(instance.localToWorld*capture->positions[local]),1);
                    attributes_[planeTexels_+index]=glm::vec4(glm::normalize(normalMatrix*glm::vec3(capture->normals[local])),float(card+1));
                }
                attributes_[planeTexels_*2+index]=glm::vec4(material.albedo,1);
            }
        }
        previous={mesh,std::move(capture),instance.localToWorld,material};
    }
    if(resize||ownersChanged) {
        std::vector<std::uint32_t> inputOwners;inputOwners.reserve(geometry->TriangleCount());
        for(std::uint32_t object=0;object<instances.size();++object) {
            const auto& instance=instances[object];const auto mesh=instance.geometryMesh?instance.geometryMesh:instance.mesh;
            inputOwners.insert(inputOwners.end(),mesh->Triangles().size(),object);
        }
        if(inputOwners.size()!=geometry->TriangleCount())throw std::runtime_error("Surface cache triangle ownership mismatch");
        const auto offset=TriangleOwnerOffset();
        std::fill(metadata_.begin()+offset,metadata_.end(),glm::vec4(-1));
        const auto& order=geometry->InputTriangleIndices();
        for(std::uint32_t triangle=0;triangle<geometry->TriangleCount();++triangle)
            metadata_[offset+triangle/4][triangle%4]=float(inputOwners.at(order.at(triangle)));
    }
    ownerGeometry_=geometry;
    if(changed)++generation_;
    statistics_.cards=CardCount();statistics_.localCaptures=std::uint32_t(captures_.size());statistics_.validTexels=0;
    for(const auto& state:states_)if(state.capture)statistics_.validTexels+=state.capture->validTexels;
    statistics_.updateMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    return changed;
}
SurfaceCacheSample SurfaceCacheScene::Lookup(glm::vec3 world,glm::vec3 normal,std::uint32_t object) const {
    SurfaceCacheSample result;if(!Finite(world)||!Finite(normal)||glm::dot(normal,normal)<1e-12f)return result;
    normal=glm::normalize(normal);const int resolution=int(settings_.pageResolution);
    const auto first=object==std::numeric_limits<std::uint32_t>::max()?0u:object*6;
    const auto end=object==std::numeric_limits<std::uint32_t>::max()?CardCount():std::min(CardCount(),first+6);
    if(object!=std::numeric_limits<std::uint32_t>::max()&&object>=states_.size())return result;
    for(std::uint32_t group=first;group<end;group+=6) {
        const auto groupMeta=group*SurfaceCacheCardTexels;
        const auto padding=metadata_[groupMeta+6].w;
        if(glm::any(glm::lessThan(world,glm::vec3(metadata_[groupMeta+9])-glm::vec3(padding)))||
           glm::any(glm::greaterThan(world,glm::vec3(metadata_[groupMeta+10])+glm::vec3(padding))))continue;
        std::array<std::uint32_t,3> sides{0,2,4};std::array<float,3> facing{};
        for(int axis=0;axis<3;++axis) {
            const float alignment=glm::dot(glm::vec3(metadata_[(group+axis*2)*SurfaceCacheCardTexels+6]),normal);
            facing[axis]=std::abs(alignment);if(alignment<0)++sides[axis];
        }
        const auto swap=[&](int a,int b){if(facing[a]<facing[b]){std::swap(facing[a],facing[b]);std::swap(sides[a],sides[b]);}};
        swap(0,1);swap(1,2);swap(0,1);
        const glm::mat4 inverse(metadata_[groupMeta],metadata_[groupMeta+1],metadata_[groupMeta+2],metadata_[groupMeta+3]);
        const auto local=glm::vec3(inverse*glm::vec4(world,1)),low=glm::vec3(metadata_[groupMeta+4]),extent=glm::vec3(metadata_[groupMeta+5])-low;
        for(int rank=0;rank<3;++rank) {
            if(facing[rank]<=.05f)break;
            const auto card=group+sides[rank],meta=card*SurfaceCacheCardTexels;
            const auto cardNormal=glm::vec3(metadata_[meta+6]);const int axis=int(sides[rank])/2,u=(axis+1)%3,v=(axis+2)%3;
            if(extent[u]<1e-7f||extent[v]<1e-7f)continue;
            const glm::vec2 uv((local[u]-low[u])/extent[u],(local[v]-low[v])/extent[v]);
            if(glm::any(glm::lessThan(uv,glm::vec2(0)))||glm::any(glm::greaterThan(uv,glm::vec2(1))))continue;
            const auto grid=uv*float(resolution)-.5f;const auto base=glm::ivec2(glm::floor(grid));float best=1e30f;
            for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
                const auto tap=glm::clamp(base+glm::ivec2(x,y),glm::ivec2(0),glm::ivec2(resolution-1));
                const auto index=std::uint32_t(metadata_[meta+5].w)+std::uint32_t(tap.y*resolution+tap.x);
                const auto p=attributes_[index],n=attributes_[planeTexels_+index];if(p.w<.5f||glm::dot(glm::vec3(n),normal)<.6f)continue;
                const auto delta=world-glm::vec3(p);const float tolerance=metadata_[meta+6].w,score=glm::dot(delta,delta);
                if(std::abs(glm::dot(delta,cardNormal))>tolerance||score>tolerance*tolerance*6.25f||score>=best)continue;
                best=score;
                result={true,index,card,std::uint32_t(metadata_[meta+7].w),glm::vec3(p),glm::vec3(n),glm::vec3(attributes_[planeTexels_*2+index]),glm::vec3(metadata_[meta+8])};
            }
            if(result.valid)return result;
        }
    }
    return result;
}
std::shared_ptr<SurfaceCacheScene> BuildSurfaceCache(const ProbeSdfScene& scene,std::span<const SurfaceCacheMaterial> materials,const SurfaceCacheSettings& settings) {
    auto result=std::make_shared<SurfaceCacheScene>(settings);result->Update(scene,materials);return result;
}
} // namespace Render
