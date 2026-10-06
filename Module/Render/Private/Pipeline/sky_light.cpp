#include "Render/Public/Pipeline/sky_light.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Render {
namespace {
constexpr float Pi=3.14159265358979323846f;
bool ValidRadiance(glm::vec3 v) {
    return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&
           glm::all(glm::greaterThanEqual(v,glm::vec3(0)))&&glm::all(glm::lessThanEqual(v,glm::vec3(60000)));
}
float RadicalInverse(std::uint32_t v) {
    v=(v<<16)|(v>>16); v=((v&0x55555555u)<<1)|((v&0xaaaaaaaau)>>1);
    v=((v&0x33333333u)<<2)|((v&0xccccccccu)>>2);
    v=((v&0x0f0f0f0fu)<<4)|((v&0xf0f0f0f0u)>>4);
    v=((v&0x00ff00ffu)<<8)|((v&0xff00ff00u)>>8);
    return float(v)*2.3283064365386963e-10f;
}
glm::vec3 Direction(float u,float v) {
    const float phi=u*2*Pi,theta=v*Pi,s=std::sin(theta);
    return {s*std::cos(phi),std::cos(theta),s*std::sin(phi)};
}
std::array<float,9> Basis(glm::vec3 n) {
    return {.282095f,.488603f*n.y,.488603f*n.z,.488603f*n.x,1.092548f*n.x*n.y,
        1.092548f*n.y*n.z,.315392f*(3*n.z*n.z-1),1.092548f*n.x*n.z,.546274f*(n.x*n.x-n.y*n.y)};
}
glm::vec3 Sample(std::span<const glm::vec3> image,std::uint32_t w,std::uint32_t h,glm::vec3 d) {
    float u=std::atan2(d.z,d.x)/(2*Pi); u-=std::floor(u);
    const float x=u*w-.5f,y=std::acos(std::clamp(d.y,-1.0f,1.0f))/Pi*h-.5f;
    const int ix=int(std::floor(x)),iy=int(std::floor(y));
    const auto tap=[&](int a,int b) { return image[std::clamp(b,0,int(h)-1)*w+(a%int(w)+int(w))%int(w)]; };
    return glm::mix(glm::mix(tap(ix,iy),tap(ix+1,iy),x-ix),
                    glm::mix(tap(ix,iy+1),tap(ix+1,iy+1),x-ix),y-iy);
}
glm::vec3 GgxHalfVector(float u,float v,float roughness) {
    const float a=roughness*roughness,a2=a*a,phi=2*Pi*u;
    const float c=std::sqrt((1-v)/std::max(1+(a2-1)*v,1e-8f));
    const float s=std::sqrt(std::max(0.0f,1-c*c));
    return {s*std::cos(phi),s*std::sin(phi),c};
}
}
std::shared_ptr<const SkyEnvironment> BakeSkyEnvironment(std::uint32_t w,std::uint32_t h,std::span<const glm::vec3> image) {
    if(!w||!h||w>8192||h>4096||image.size()!=std::size_t(w)*h)
        throw std::invalid_argument("Invalid linear HDR sky dimensions/pixels");
    for(auto pixel:image) if(!ValidRadiance(pixel)) throw std::invalid_argument("Sky radiance must be finite, nonnegative, <=60000");
    auto result=std::make_shared<SkyEnvironment>();
    // Exact spherical row solid angle avoids pole overweighting and preserves a constant sky.
    std::array<glm::dvec3,9> sh{};
    for(std::uint32_t y=0;y<h;++y) {
        const double weight=2.0*Pi/w*(std::cos(double(y)*Pi/h)-std::cos(double(y+1)*Pi/h));
        for(std::uint32_t x=0;x<w;++x) {
            auto b=Basis(Direction((x+.5f)/w,(y+.5f)/h));
            for(int i=0;i<9;++i) sh[i]+=glm::dvec3(image[y*w+x])*double(b[i])*weight;
        }
    }
    for(int i=0;i<9;++i) result->diffuseSH_[i]=glm::vec4(glm::vec3(sh[i]*(i==0?1.0:i<4?2.0/3.0:0.25)),0);
    result->reflectionAtlas_.resize(SkyAtlasWidth*SkyAtlasHeight);
    constexpr std::uint32_t Samples=128;
    for(std::uint32_t level=0;level<SkyReflectionLevels;++level) {
        const float roughness=float(level)/(SkyReflectionLevels-1);
        std::array<glm::vec3,Samples> halves;
        for(std::uint32_t i=0;i<Samples;++i) halves[i]=GgxHalfVector(float(i)/Samples,RadicalInverse(i),roughness);
        for(std::uint32_t y=0;y<SkyReflectionHeight;++y) for(std::uint32_t x=0;x<SkyReflectionWidth;++x) {
            const auto N=Direction((x+.5f)/SkyReflectionWidth,(y+.5f)/SkyReflectionHeight);
            glm::vec3 color=Sample(image,w,h,N);
            if(level) {
                auto T=glm::normalize(glm::cross(std::abs(N.y)<.999f?glm::vec3(0,1,0):glm::vec3(1,0,0),N));
                auto B=glm::cross(N,T); glm::vec3 sum(0); float weight=0;
                for(auto local:halves) {
                    const auto H=T*local.x+B*local.y+N*local.z;
                    const auto L=2*glm::dot(N,H)*H-N;
                    const float NoL=glm::dot(N,L);
                    if(NoL>0) { sum+=Sample(image,w,h,L)*NoL; weight+=NoL; }
                }
                color=sum/std::max(weight,1e-6f);
            }
            result->reflectionAtlas_[(level*(SkyReflectionHeight+2)+y+1)*SkyAtlasWidth+x+1]=glm::vec4(color,1);
        }
        auto& a=result->reflectionAtlas_;
        for(std::uint32_t y=1;y<=SkyReflectionHeight;++y) {
            const auto row=(level*(SkyReflectionHeight+2)+y)*SkyAtlasWidth;
            a[row]=a[row+SkyReflectionWidth]; a[row+SkyReflectionWidth+1]=a[row+1];
        }
        for(std::uint32_t x=0;x<SkyAtlasWidth;++x) {
            const auto row=level*(SkyReflectionHeight+2)*SkyAtlasWidth;
            a[row+x]=a[row+SkyAtlasWidth+x];
            a[row+(SkyReflectionHeight+1)*SkyAtlasWidth+x]=a[row+SkyReflectionHeight*SkyAtlasWidth+x];
        }
    }
    return result;
}
std::shared_ptr<const SkyEnvironment> MakeProceduralSkyEnvironment(glm::vec3 zenith,glm::vec3 horizon,glm::vec3 ground) {
    if(!ValidRadiance(zenith)||!ValidRadiance(horizon)||!ValidRadiance(ground)) throw std::invalid_argument("Invalid procedural sky colors");
    constexpr std::uint32_t W=256,H=128;
    std::vector<glm::vec3> pixels(W*H);
    for(std::uint32_t y=0;y<H;++y) {
        const float elevation=std::cos((y+.5f)/H*Pi);
        auto color=elevation>=0?glm::mix(horizon,zenith,std::pow(elevation,.45f)):
                                glm::mix(horizon,ground,std::min(1.0f,-elevation*8));
        for(std::uint32_t x=0;x<W;++x) pixels[y*W+x]=color;
    }
    return BakeSkyEnvironment(W,H,pixels);
}
const std::vector<glm::vec4>& SkyBrdfIntegrationLut() {
    static const auto lut=[] {
        std::vector<glm::vec4> pixels(SkyBrdfLutSize*SkyBrdfLutSize);
        constexpr std::uint32_t Samples=256;
        for(std::uint32_t y=0;y<SkyBrdfLutSize;++y) for(std::uint32_t x=0;x<SkyBrdfLutSize;++x) {
            const float NoV=(x+.5f)/SkyBrdfLutSize,r=(y+.5f)/SkyBrdfLutSize,a2=r*r*r*r;
            const glm::vec3 V(std::sqrt(1-NoV*NoV),0,NoV);
            float A=0,B=0;
            for(std::uint32_t i=0;i<Samples;++i) {
                const auto H=GgxHalfVector(float(i)/Samples,RadicalInverse(i),r);
                const float VoH=std::max(glm::dot(V,H),0.0f);
                const auto L=2*VoH*H-V; const float NoL=L.z;
                if(NoL<=0) continue;
                const float denom=NoL*std::sqrt(a2+(1-a2)*NoV*NoV)+NoV*std::sqrt(a2+(1-a2)*NoL*NoL);
                const float GVis=2*NoL*VoH/std::max(denom*H.z,1e-6f);
                const float F=std::pow(1-VoH,5);
                A+=(1-F)*GVis; B+=F*GVis;
            }
            pixels[y*SkyBrdfLutSize+x]={A/Samples,B/Samples,0,1};
        }
        return pixels;
    }();
    return lut;
}
std::shared_ptr<const SkyEnvironment> DefaultSkyEnvironment() {
    static const auto daylight=MakeProceduralSkyEnvironment();
    return daylight;
}
bool ValidateSkyLightSettings(const SkyLightSettings& s,std::string* error) {
    const auto strength=[](float v) { return std::isfinite(v)&&v>=0&&v<=1000; };
    if(!strength(s.intensity)||!strength(s.diffuseStrength)||!strength(s.specularStrength)||
       !std::isfinite(s.rotation)||!std::isfinite(s.occlusionStrength)||s.occlusionStrength<0||s.occlusionStrength>1||
       (s.environment&&s.environment->ReflectionAtlas().size()!=SkyAtlasWidth*SkyAtlasHeight)) {
        if(error) *error="Invalid sky light intensity, rotation, occlusion or environment";
        return false;
    }
    return true;
}
SkyLightConstants MakeSkyLightConstants(const SkyLightSettings& s,const SkyEnvironment& environment) {
    SkyLightConstants result; result.diffuseSH=environment.DiffuseSH();
    result.options={s.enabled?1.0f:0.0f,s.intensity,s.diffuseStrength,s.specularStrength};
    result.transform={std::cos(s.rotation),std::sin(s.rotation),s.background?1.0f:0.0f,s.occlusionStrength};
    return result;
}
} // namespace Render
