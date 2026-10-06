#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include "Render/Public/Pipeline/sky_light.h"

namespace {
void Check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
glm::vec3 Irradiance(const Render::SkyEnvironment& sky,glm::vec3 n) {
    const float b[9]={.282095f,.488603f*n.y,.488603f*n.z,.488603f*n.x,1.092548f*n.x*n.y,
        1.092548f*n.y*n.z,.315392f*(3*n.z*n.z-1),1.092548f*n.x*n.z,.546274f*(n.x*n.x-n.y*n.y)};
    glm::vec3 sum(0); for(int i=0;i<9;++i) sum+=glm::vec3(sky.DiffuseSH()[i])*b[i];
    return sum;
}
}
int main() {
    try {
        using namespace Render;
        constexpr int W=64,H=32;
        std::vector<glm::vec3> image(W*H,glm::vec3(.7f,.3f,.15f));
        const auto uniform=BakeSkyEnvironment(W,H,image);
        for(auto n:{glm::vec3(1,0,0),glm::vec3(0,1,0),glm::vec3(0,-1,0),glm::normalize(glm::vec3(1,2,3))})
            Check(glm::length(Irradiance(*uniform,n)-image[0])<.002,"SH constant sky energy/orientation wrong");
        for(auto pixel:uniform->ReflectionAtlas())
            Check(glm::length(glm::vec3(pixel)-image[0])<.00001,"GGX filtering changed a constant sky");
        // A l=1 signal has exact Lambert convolution L(n)=base + (2/3)*gradient.n.
        for(int y=0;y<H;++y) for(int x=0;x<W;++x) {
            const float theta=(y+.5f)/H*3.14159265359f,phi=(x+.5f)/W*6.28318530718f;
            image[y*W+x]=glm::vec3(.5f)+glm::vec3(.25f*std::sin(theta)*std::cos(phi),.2f*std::cos(theta),0);
        }
        const auto gradient=BakeSkyEnvironment(W,H,image);
        for(auto n:{glm::vec3(1,0,0),glm::vec3(-1,0,0),glm::vec3(0,1,0),glm::vec3(0,-1,0)}) {
            const auto expected=glm::vec3(.5f)+glm::vec3(.25f*n.x,.2f*n.y,0)*(2.0f/3);
            Check(glm::length(Irradiance(*gradient,n)-expected)<.002,"SH directional integral disagrees with analytic reference");
        }
        const auto& atlas=gradient->ReflectionAtlas();
        for(std::uint32_t level=0;level<SkyReflectionLevels;++level) {
            for(std::uint32_t y=1;y<=SkyReflectionHeight;++y) {
                const auto row=(level*(SkyReflectionHeight+2)+y)*SkyAtlasWidth;
                Check(atlas[row]==atlas[row+SkyReflectionWidth]&&atlas[row+SkyReflectionWidth+1]==atlas[row+1],
                      "Longitude gutters broke reflection seam");
            }
            for(std::uint32_t x=0;x<SkyAtlasWidth;++x) {
                const auto row=level*(SkyReflectionHeight+2)*SkyAtlasWidth;
                Check(atlas[row+x]==atlas[row+SkyAtlasWidth+x]&&
                      atlas[row+(SkyReflectionHeight+1)*SkyAtlasWidth+x]==atlas[row+SkyReflectionHeight*SkyAtlasWidth+x],
                      "Pole gutters bleed between roughness levels");
            }
        }
        const auto contrast=[&](int level) {
            float lo=100,hi=0;
            for(std::uint32_t y=1;y<=SkyReflectionHeight;++y) for(std::uint32_t x=1;x<=SkyReflectionWidth;++x) {
                const float value=atlas[(level*(SkyReflectionHeight+2)+y)*SkyAtlasWidth+x].r;
                lo=std::min(lo,value); hi=std::max(hi,value);
            }
            return hi-lo;
        };
        Check(contrast(5)<contrast(0)*.75f,"GGX roughness did not blur the environment");
        for(auto v:SkyBrdfIntegrationLut())
            Check(std::isfinite(v.x)&&std::isfinite(v.y)&&v.x>=0&&v.y>=0&&v.x+v.y<1.1f,"Invalid GGX DFG integral");
        SkyLightSettings s; Check(ValidateSkyLightSettings(s),"Valid defaults rejected");
        s.intensity=-1; Check(!ValidateSkyLightSettings(s),"Negative sky intensity accepted");
        s.intensity=1; s.rotation=std::numeric_limits<float>::quiet_NaN();
        Check(!ValidateSkyLightSettings(s),"NaN sky rotation accepted");
        bool rejected=false; image[0].x=-1;
        try { BakeSkyEnvironment(W,H,image); } catch(const std::invalid_argument&) { rejected=true; }
        Check(rejected,"Invalid HDR pixels accepted");
        std::cout << "Sky CPU test passed: analytic SH integral, constant HDR energy, GGX roughness, atlas seams, DFG LUT and validation\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
