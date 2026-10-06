#include "Render/Public/Pipeline/diffuse_probe_volume.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace Render {
namespace {
constexpr float Pi=3.14159265358979323846f;
bool Finite(glm::vec3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
bool Bounded(glm::vec3 v) { return Finite(v)&&glm::all(glm::lessThanEqual(glm::abs(v),glm::vec3(1000000))); }
std::array<float,9> Basis(glm::vec3 n) {
    return {.282095f,.488603f*n.y,.488603f*n.z,.488603f*n.x,1.092548f*n.x*n.y,
        1.092548f*n.y*n.z,.315392f*(3*n.z*n.z-1),1.092548f*n.x*n.z,.546274f*(n.x*n.x-n.y*n.y)};
}
glm::vec2 Oct(glm::vec3 d) {
    d/=std::abs(d.x)+std::abs(d.y)+std::abs(d.z);
    glm::vec2 p(d);
    if(d.z<0) p=(glm::vec2(1)-glm::abs(glm::vec2(p.y,p.x)))*glm::vec2(p.x>=0?1:-1,p.y>=0?1:-1);
    return p*.5f+.5f;
}
glm::vec3 DecodeOct(glm::vec2 uv) {
    const auto p=uv*2.0f-1.0f; glm::vec3 d(p.x,p.y,1-std::abs(p.x)-std::abs(p.y));
    if(d.z<0) { d.x=(1-std::abs(p.y))*(p.x>=0?1:-1); d.y=(1-std::abs(p.x))*(p.y>=0?1:-1); }
    return glm::normalize(d);
}
glm::vec3 SkySample(const SkyEnvironment& sky,glm::vec3 d,float angle) {
    const float c=std::cos(angle),s=std::sin(angle);
    d={c*d.x+s*d.z,d.y,-s*d.x+c*d.z};
    float u=std::atan2(d.z,d.x)/(2*Pi); u-=std::floor(u);
    const float x=.5f+u*SkyReflectionWidth,y=.5f+std::acos(std::clamp(d.y,-1.0f,1.0f))/Pi*SkyReflectionHeight;
    const int ix=int(x),iy=int(y); const auto& atlas=sky.ReflectionAtlas();
    const auto tap=[&](int a,int b) { return glm::vec3(atlas[b*SkyAtlasWidth+a]); };
    return glm::mix(glm::mix(tap(ix,iy),tap(ix+1,iy),x-ix),glm::mix(tap(ix,iy+1),tap(ix+1,iy+1),x-ix),y-iy);
}
struct Hit { float distance; int triangle=-1; glm::vec3 normal{0}; bool front=true; };
class Bvh {
    struct Node { glm::vec3 lo,hi; std::uint32_t begin=0,count=0,left=0,right=0; };
    std::span<const ProbeTriangle> triangles_;
    std::vector<std::uint32_t> indices_;
    std::vector<Node> nodes_;
    std::uint32_t Build(std::uint32_t begin,std::uint32_t end) {
        Node n; n.lo=glm::vec3(1e30f); n.hi=glm::vec3(-1e30f);
        glm::vec3 centroidLo(1e30f),centroidHi(-1e30f);
        for(auto i=begin;i<end;++i) {
            const auto& t=triangles_[indices_[i]]; const auto center=(t.a+t.b+t.c)/3.0f;
            n.lo=glm::min(n.lo,glm::min(t.a,glm::min(t.b,t.c))); n.hi=glm::max(n.hi,glm::max(t.a,glm::max(t.b,t.c)));
            centroidLo=glm::min(centroidLo,center); centroidHi=glm::max(centroidHi,center);
        }
        const auto index=std::uint32_t(nodes_.size()); nodes_.push_back(n);
        if(end-begin<=8) { nodes_[index].begin=begin; nodes_[index].count=end-begin; return index; }
        const auto extent=centroidHi-centroidLo; const int axis=extent.x>extent.y?(extent.x>extent.z?0:2):(extent.y>extent.z?1:2);
        const auto middle=(begin+end)/2;
        std::nth_element(indices_.begin()+begin,indices_.begin()+middle,indices_.begin()+end,[&](auto a,auto b) {
            const auto& x=triangles_[a]; const auto& y=triangles_[b];
            return x.a[axis]+x.b[axis]+x.c[axis]<y.a[axis]+y.b[axis]+y.c[axis];
        });
        const auto left=Build(begin,middle),right=Build(middle,end); nodes_[index].left=left; nodes_[index].right=right;
        return index;
    }
    static bool Box(const Node& n,glm::vec3 p,glm::vec3 d,float maximum) {
        float lo=0,hi=maximum;
        for(int a=0;a<3;++a) {
            if(std::abs(d[a])<1e-9f) { if(p[a]<n.lo[a]||p[a]>n.hi[a]) return false; continue; }
            float x=(n.lo[a]-p[a])/d[a],y=(n.hi[a]-p[a])/d[a]; if(x>y) std::swap(x,y);
            lo=std::max(lo,x); hi=std::min(hi,y); if(lo>hi) return false;
        }
        return true;
    }
public:
    explicit Bvh(std::span<const ProbeTriangle> triangles):triangles_(triangles),indices_(triangles.size()) {
        std::iota(indices_.begin(),indices_.end(),0); if(!triangles.empty()) Build(0,std::uint32_t(triangles.size()));
    }
    Hit Trace(glm::vec3 p,glm::vec3 d,float maximum) const {
        Hit hit{maximum}; if(nodes_.empty()) return hit;
        std::array<std::uint32_t,64> stack{}; int size=1;
        while(size) {
            const auto& node=nodes_[stack[--size]]; if(!Box(node,p,d,hit.distance)) continue;
            if(!node.count) { stack[size++]=node.left; stack[size++]=node.right; continue; }
            for(auto i=node.begin;i<node.begin+node.count;++i) {
                const auto index=indices_[i]; const auto& t=triangles_[index];
                const auto ab=t.b-t.a,ac=t.c-t.a,q=glm::cross(d,ac); const float determinant=glm::dot(ab,q);
                if(std::abs(determinant)<1e-9f) continue;
                const float inverse=1/determinant; const auto relative=p-t.a;
                const float u=glm::dot(relative,q)*inverse; if(u<0||u>1) continue;
                const auto cross=glm::cross(relative,ab); const float v=glm::dot(d,cross)*inverse;
                if(v<0||u+v>1) continue;
                const float distance=glm::dot(ac,cross)*inverse;
                if(distance<1e-5f||distance>=hit.distance) continue;
                hit.distance=distance; hit.triangle=int(index); hit.normal=glm::normalize(glm::cross(ab,ac));
                hit.front=glm::dot(hit.normal,d)<0;
            }
        }
        return hit;
    }
};
glm::vec3 DirectIrradiance(const Bvh& bvh,glm::vec3 p,glm::vec3 N,const ProbeBakeLighting& lighting,
    const SpotLightConstants& spot,float bias,float maximum) {
    glm::vec3 result(0); const auto origin=p+N*bias;
    const auto visible=[&](glm::vec3 L,float distance) { return bvh.Trace(origin,L,std::max(0.0f,distance-bias*2)).triangle<0; };
    if(lighting.sunIntensity>0) {
        const auto L=-glm::normalize(lighting.sunDirection); const float cosine=std::max(glm::dot(N,L),0.0f);
        if(cosine>0&&visible(L,maximum)) result+=lighting.sunColor*lighting.sunIntensity*cosine;
    }
    for(int i=0;i<4;++i) {
        const auto color=glm::vec3(lighting.pointColors[i]); if(glm::all(glm::lessThanEqual(color,glm::vec3(0)))) continue;
        const auto delta=glm::vec3(lighting.pointPositions[i])-p; const float distance=glm::length(delta);
        if(distance<1e-5f) continue; const auto L=delta/distance; const float cosine=std::max(glm::dot(N,L),0.0f);
        const float sourceRadius=std::max(lighting.pointColors[i].w,0.f);
        if(cosine>0&&visible(L,distance-sourceRadius)) result+=color*cosine/std::max({distance*distance,sourceRadius*sourceRadius,.0001f});
    }
    if(lighting.spotLight.enabled) {
        const auto& light=lighting.spotLight;const auto delta=light.position-p;const float distance=glm::length(delta);
        if(distance>1e-5f) {
            const auto L=delta/distance;const float cosine=std::max(glm::dot(N,L),0.f);
            const float cone=SpotLightConeWeight(spot,-L);
            if(cosine>0&&cone>0&&visible(L,distance-light.sourceRadius))
                result+=light.intensity*(cosine*cone/std::max({distance*distance,light.sourceRadius*light.sourceRadius,.0001f}));
        }
    }
    for(std::uint32_t i=0;i<lighting.areaLights.count;++i) {
        const auto& light=lighting.areaLights.lights[i]; if(!light.enabled) continue;
        const auto cross=glm::cross(light.halfAxisU,light.halfAxisV); const float cellArea=glm::length(cross);
        const auto normal=cross/cellArea;
        for(float u:{-.5f,.5f}) for(float v:{-.5f,.5f}) {
            const auto delta=light.center+light.halfAxisU*u+light.halfAxisV*v-p; const float distance=glm::length(delta);
            if(distance<1e-5f) continue; const auto L=delta/distance;
            const float receiver=std::max(glm::dot(N,L),0.0f),face=glm::dot(normal,-L);
            const float emitter=light.twoSided?std::abs(face):std::max(face,0.0f);
            if(receiver>0&&emitter>0&&visible(L,distance)) result+=light.radiance*(receiver*emitter*cellArea/std::max(distance*distance,.0001f));
        }
    }
    return result;
}
float HashUnit(std::uint32_t x) {
    x^=x>>16; x*=0x7feb352du; x^=x>>15; x*=0x846ca68bu; x^=x>>16;
    return float(x>>8)*(1.0f/16777216);
}
glm::vec3 CosineDirection(glm::vec3 n,std::uint32_t seed) {
    const float u=HashUnit(seed),phi=2*Pi*HashUnit(seed+0x9e3779b9u),r=std::sqrt(u);
    const auto t=glm::normalize(glm::cross(std::abs(n.y)<.999f?glm::vec3(0,1,0):glm::vec3(1,0,0),n));
    return t*(r*std::cos(phi))+glm::cross(n,t)*(r*std::sin(phi))+n*std::sqrt(1-u);
}
}
std::shared_ptr<const DiffuseProbeVolume> BakeDiffuseProbeVolume(const ProbeBakeSettings& settings,
    std::span<const ProbeTriangle> triangles,const ProbeBakeLighting& lighting) {
    const auto start=std::chrono::steady_clock::now();
    const auto count=std::uint64_t(settings.counts.x)*settings.counts.y*settings.counts.z;
    if(!Bounded(settings.origin)||!Finite(settings.spacing)||glm::any(glm::lessThanEqual(settings.spacing,glm::vec3(.001f)))||
       glm::any(glm::greaterThan(settings.spacing,glm::vec3(10000)))||glm::any(glm::lessThan(settings.counts,glm::uvec3(2)))||
       glm::any(glm::greaterThan(settings.counts,glm::uvec3(MaxDiffuseProbes)))||
       count>MaxDiffuseProbes||settings.raysPerProbe<64||settings.raysPerProbe>4096||settings.diffuseBounces>4||
       !std::isfinite(settings.maxDistance)||settings.maxDistance<=0||settings.maxDistance>10000||
       !std::isfinite(settings.rayBias)||settings.rayBias<=0||settings.rayBias>.5f||triangles.size()>200000)
        throw std::invalid_argument("Invalid diffuse probe bake grid/budget");
    if(!ValidateSkyLightSettings(lighting.sky)||!ValidateAreaLightsSettings(lighting.areaLights)||!ValidateSpotLightSettings(lighting.spotLight)||
       !Bounded(lighting.sunDirection)||glm::length(lighting.sunDirection)<1e-6f||!Bounded(lighting.sunColor)||
       glm::any(glm::lessThan(lighting.sunColor,glm::vec3(0)))||!std::isfinite(lighting.sunIntensity)||lighting.sunIntensity<0||lighting.sunIntensity>1000000)
        throw std::invalid_argument("Invalid probe bake light");
    for(int i=0;i<4;++i) if(!Bounded(glm::vec3(lighting.pointPositions[i]))||!Finite(glm::vec3(lighting.pointColors[i]))||
       glm::any(glm::lessThan(glm::vec3(lighting.pointColors[i]),glm::vec3(0)))||
       glm::any(glm::greaterThan(glm::vec3(lighting.pointColors[i]),glm::vec3(1000000)))||
       !std::isfinite(lighting.pointColors[i].w)||lighting.pointColors[i].w<0||lighting.pointColors[i].w>10000)
        throw std::invalid_argument("Invalid probe point light");
    for(const auto& t:triangles) if(!Bounded(t.a)||!Bounded(t.b)||!Bounded(t.c)||!Finite(t.diffuseReflectance)||!Finite(t.emission)||
       glm::length(glm::cross(t.b-t.a,t.c-t.a))<1e-7f||
       glm::any(glm::lessThan(t.diffuseReflectance,glm::vec3(0)))||glm::any(glm::greaterThan(t.diffuseReflectance,glm::vec3(1)))||
       glm::any(glm::lessThan(t.emission,glm::vec3(0)))||glm::any(glm::greaterThan(t.emission,glm::vec3(60000))))
        throw std::invalid_argument("Invalid probe triangle/material");
    Bvh bvh(triangles);const auto spot=MakeSpotLightConstants(lighting.spotLight);
    std::shared_ptr<const SkyEnvironment> sky;
    if(lighting.sky.enabled) sky=lighting.sky.environment?lighting.sky.environment:DefaultSkyEnvironment();
    auto result=std::make_shared<DiffuseProbeVolume>(); auto& data=result->data_;
    data.origin=glm::vec4(settings.origin,0); data.spacing=glm::vec4(settings.spacing,0);
    data.counts=glm::vec4(glm::vec3(settings.counts),float(count)); data.limits={settings.maxDistance,0,0,0};
    for(std::uint32_t index=0;index<count;++index) {
        auto& probe=data.probes[index];
        const glm::uvec3 cell(index%settings.counts.x,(index/settings.counts.x)%settings.counts.y,index/(settings.counts.x*settings.counts.y));
        const auto position=settings.origin+settings.spacing*glm::vec3(cell); probe.positionValid=glm::vec4(position,1);
        for(auto& distances:probe.distances) distances=glm::vec4(settings.maxDistance);
        for(int bin=0;bin<16;++bin) {
            const auto d=DecodeOct({(bin%4+.5f)/4,(bin/4+.5f)/4}); const auto hit=bvh.Trace(position,d,settings.maxDistance);
            probe.distances[bin/4][bin%4]=hit.distance;
            if(hit.triangle>=0) {
                const auto N=hit.front?hit.normal:-hit.normal;
                result->visibility_.planes[index*16+bin]=glm::vec4(N,glm::dot(N,d*hit.distance));
            }
        }
        std::array<glm::dvec3,9> radianceSH{}; std::array<double,9> skySH{}; std::uint32_t backfaces=0;
        for(std::uint32_t ray=0;ray<settings.raysPerProbe;++ray) {
            const float z=1-2*(ray+.5f)/settings.raysPerProbe,phi=float(ray)*2.39996322973f;
            const auto direction=glm::vec3(std::sqrt(std::max(0.0f,1-z*z))*std::cos(phi),z,
                                          std::sqrt(std::max(0.0f,1-z*z))*std::sin(phi));
            const auto primary=bvh.Trace(position,direction,settings.maxDistance);
            if(primary.triangle>=0&&!primary.front) ++backfaces;
            glm::vec3 radiance(0),throughput(1),p=position,d=direction;
            for(std::uint32_t bounce=0;bounce<=settings.diffuseBounces;++bounce) {
                const auto hit=bounce==0?primary:bvh.Trace(p,d,settings.maxDistance);
                if(hit.triangle<0) { if(sky) radiance+=throughput*SkySample(*sky,d,lighting.sky.rotation)*lighting.sky.intensity*lighting.sky.diffuseStrength; break; }
                const auto& triangle=triangles[hit.triangle]; const auto point=p+d*hit.distance;
                if(!triangle.analyticEmission&&(hit.front||triangle.twoSidedEmission)) radiance+=throughput*triangle.emission;
                if(bounce==settings.diffuseBounces) break;
                const auto N=hit.front?hit.normal:-hit.normal;
                radiance+=throughput*triangle.diffuseReflectance*DirectIrradiance(bvh,point,N,lighting,spot,settings.rayBias,settings.maxDistance)/Pi;
                throughput*=triangle.diffuseReflectance;
                if(glm::all(glm::lessThan(throughput,glm::vec3(1e-5f)))) break;
                p=point+N*settings.rayBias; d=CosineDirection(N,ray*0x9e3779b9u+index*37+bounce*101);
            }
            auto basis=Basis(direction);
            for(int i=0;i<9;++i) {
                radianceSH[i]+=glm::dvec3(glm::clamp(radiance,glm::vec3(0),glm::vec3(60000)))*double(basis[i]);
                if(primary.triangle<0) skySH[i]+=basis[i];
            }
        }
        if(backfaces>settings.raysPerProbe*.6f) { probe.positionValid.w=0; continue; }
        ++result->validProbes_;
        for(int i=0;i<9;++i) {
            const double sphere=4.0*Pi/settings.raysPerProbe,convolution=i==0?1:i<4?2.0/3.0:.25;
            probe.sh[i]=glm::vec4(glm::vec3(radianceSH[i]*(sphere*convolution)),float(skySH[i]*sphere));
        }
    }
    result->bakeMilliseconds_=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    return result;
}
bool ValidateDiffuseProbeSettings(const DiffuseProbeSettings& s,std::string* error) {
    if(!std::isfinite(s.intensity)||s.intensity<0||s.intensity>1000||!std::isfinite(s.normalBias)||s.normalBias<0||s.normalBias>2||
       !std::isfinite(s.visibilityBias)||s.visibilityBias<0||s.visibilityBias>2||
       (s.enabled&&(!s.volume||s.volume->Constants().counts.w<8))) {
        if(error) *error="Invalid diffuse probe settings or missing volume"; return false;
    }
    return true;
}
ProbeLightingSample SampleDiffuseProbeVolume(const DiffuseProbeVolume& volume,glm::vec3 p,glm::vec3 N,glm::vec3 R,float roughness,float bias,float visibilityBias) {
    if(!Bounded(p)||!Bounded(N)||!Bounded(R)||glm::length(N)<1e-6f||glm::length(R)<1e-6f||
       !std::isfinite(roughness)||roughness<0||roughness>1||!std::isfinite(bias)||bias<0||bias>2||
       !std::isfinite(visibilityBias)||visibilityBias<0||visibilityBias>2)
        throw std::invalid_argument("Invalid diffuse probe diagnostic query");
    ProbeLightingSample result; const auto& data=volume.Constants();
    if(volume.ValidProbes()==0) return result;
    N=glm::normalize(N); R=glm::normalize(R); const auto coord=(p-glm::vec3(data.origin))/glm::vec3(data.spacing);
    const auto outside=glm::max(glm::max(-.5f-coord,coord-(glm::vec3(data.counts)-.5f)),glm::vec3(0));
    result.coverage=1-std::clamp(std::max({outside.x,outside.y,outside.z})*4,0.0f,1.0f); if(result.coverage<=0) return result;
    const auto query=p+N*bias,grid=glm::clamp((query-glm::vec3(data.origin))/glm::vec3(data.spacing),glm::vec3(0),glm::vec3(data.counts)-1.0f);
    const auto base=glm::min(glm::ivec3(glm::floor(grid)),glm::ivec3(data.counts)-2); const auto phase=grid-glm::vec3(base);
    const auto b=Basis(N),r=Basis(R); float total=0,visibility=0;
    for(int z=0;z<2;++z) for(int y=0;y<2;++y) for(int x=0;x<2;++x) {
        const auto cell=base+glm::ivec3(x,y,z); const int index=cell.x+int(data.counts.x)*(cell.y+int(data.counts.y)*cell.z);
        const auto& probe=data.probes[index]; if(probe.positionValid.w<.5f) continue;
        const auto delta=query-glm::vec3(probe.positionValid); const float distance=glm::length(delta); const auto direction=distance>1e-6f?delta/distance:N;
        const auto pixel=Oct(direction)*4.0f-.5f; const auto lo=glm::ivec2(glm::floor(pixel)); const auto f=glm::fract(pixel);
        float obstruction=visibilityBias,planeVisibility=0;
        for(int yy=0;yy<2;++yy) for(int xx=0;xx<2;++xx) {
            const auto texel=glm::clamp(lo+glm::ivec2(xx,yy),glm::ivec2(0),glm::ivec2(3)); const int bin=texel.y*4+texel.x;
            const float weight=(xx?f.x:1-f.x)*(yy?f.y:1-f.y);
            obstruction+=probe.distances[bin/4][bin%4]*weight;
            const auto plane=volume.Visibility().planes[index*16+bin];
            const float side=glm::dot(glm::vec3(plane),delta)-plane.w;
            planeVisibility+=(glm::dot(glm::vec3(plane),glm::vec3(plane))<.5f?1.0f:
                glm::smoothstep(-std::max(visibilityBias,.01f),0.0f,side))*weight;
        }
        const float visible=std::max(planeVisibility,1-glm::smoothstep(obstruction,obstruction+std::max(visibilityBias,.05f),distance));
        const float nw=std::pow(std::max(.05f,glm::dot(N,-direction)*.5f+.5f),2);
        const glm::vec3 factor(x?phase.x:1-phase.x,y?phase.y:1-phase.y,z?phase.z:1-phase.z);
        const float weight=factor.x*factor.y*factor.z*visible*nw; if(weight<=1e-8f) continue;
        glm::vec3 color(0); float sky=0;
        for(int i=0;i<9;++i) { color+=glm::vec3(probe.sh[i])*b[i]; sky+=probe.sh[i].w*r[i]*glm::mix(1.0f,i==0?1.0f:i<4?2.0f/3:.25f,roughness*roughness); }
        result.irradianceOverPi+=glm::max(color,glm::vec3(0))*weight; visibility+=std::clamp(sky,0.0f,1.0f)*weight; total+=weight;
    }
    if(total>1e-8f) { result.irradianceOverPi/=total; result.skyVisibility=visibility/total; } else result.skyVisibility=0;
    return result;
}
} // namespace Render
