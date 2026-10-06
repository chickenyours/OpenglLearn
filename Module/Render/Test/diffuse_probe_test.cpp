#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
#include "Render/Public/Pipeline/diffuse_probe_volume.h"
#include "Render/Public/Pipeline/realtime_gi.h"

namespace {
using namespace Render;
void Check(bool x,const char* message) { if(!x) throw std::runtime_error(message); }
void Quad(std::vector<ProbeTriangle>& scene,glm::vec3 a,glm::vec3 b,glm::vec3 c,glm::vec3 d,glm::vec3 normal,
          glm::vec3 diffuse=glm::vec3(0),glm::vec3 emission=glm::vec3(0)) {
    if(glm::dot(glm::cross(b-a,c-a),normal)<0) std::swap(b,d);
    scene.push_back({a,b,c,diffuse,emission}); scene.push_back({a,c,d,diffuse,emission});
}
std::vector<ProbeTriangle> Room(float size=2,bool inward=true) {
    std::vector<ProbeTriangle> s; const float r=inward?1:-1;
    Quad(s,{-size,-size,-size},{-size,-size,size},{-size,size,size},{-size,size,-size},{r,0,0});
    Quad(s,{size,-size,-size},{size,-size,size},{size,size,size},{size,size,-size},{-r,0,0});
    Quad(s,{-size,-size,-size},{size,-size,-size},{size,-size,size},{-size,-size,size},{0,r,0});
    Quad(s,{-size,size,-size},{size,size,-size},{size,size,size},{-size,size,size},{0,-r,0});
    Quad(s,{-size,-size,-size},{size,-size,-size},{size,size,-size},{-size,size,-size},{0,0,r});
    Quad(s,{-size,-size,size},{size,-size,size},{size,size,size},{-size,size,size},{0,0,-r});
    return s;
}
}
int main() {
    try {
        {
            SpotLightSettings spot;Check(!spot.enabled&&ValidateSpotLightSettings(spot),"Optional spot light changed the default light set");
            spot.enabled=true;spot.direction={0,0,-3};spot.intensity={24,12,6};
            const auto packed=MakeSpotLightConstants(spot);
            Check(glm::vec3(packed.directionOuter)==glm::vec3(0,0,-1)&&
                glm::vec3(packed.intensityInner)==spot.intensity,"Spot light packing lost normalization or RGB intensity");
            for(double angle:{0.0,.1,.174532925,.24,.30,.34906585,.5,3.141592653589793}) {
                const glm::vec3 direction(float(std::sin(angle)),0,-float(std::cos(angle)));
                const double phase=std::clamp((std::cos(angle)-std::cos(double(spot.outerAngle)))/
                    (std::cos(double(spot.innerAngle))-std::cos(double(spot.outerAngle))),0.0,1.0);
                const double reference=phase*phase*(3-2*phase);
                Check(std::abs(SpotLightConeWeight(packed,direction)-reference)<3e-6,"Spot cone disagrees with double-precision angular reference");
            }
            spot.direction=glm::vec3(0);Check(!ValidateSpotLightSettings(spot),"Zero spot direction accepted");
            spot.direction={0,0,-1};spot.outerAngle=spot.innerAngle;Check(!ValidateSpotLightSettings(spot),"Reversed or zero-width spot cone accepted");
            spot.outerAngle=.4f;spot.sourceRadius=-1;Check(!ValidateSpotLightSettings(spot),"Negative spot radius accepted");
            spot.enabled=false;const auto disabled=MakeSpotLightConstants(spot);
            Check(disabled.positionRadius==glm::vec4(0)&&disabled.directionOuter==glm::vec4(0)&&
                disabled.intensityInner==glm::vec4(0),"Disabled spot retained transport or hash state");
        }
        ProbeBakeSettings grid; grid.origin={-1.5f,-1.5f,-1.5f}; grid.spacing=glm::vec3(1); grid.counts=glm::uvec3(4);
        ProbeBakeLighting lighting; lighting.sky.enabled=true;
        lighting.sky.environment=MakeProceduralSkyEnvironment(glm::vec3(1),glm::vec3(1),glm::vec3(1));
        const auto open=BakeDiffuseProbeVolume(grid,{},lighting);
        Check(open->ValidProbes()==64,"Empty volume lost probes");
        for(auto N:{glm::vec3(1,0,0),glm::vec3(0,1,0),glm::vec3(0,0,1),glm::normalize(glm::vec3(1,2,3))}) {
            const auto sample=SampleDiffuseProbeVolume(*open,glm::vec3(0),N,N,.2f);
            Check(sample.coverage==1&&glm::length(sample.irradianceOverPi-glm::vec3(1))<.006&&sample.skyVisibility>.99,
                  "Unoccluded uniform sky integration/interpolation lost energy");
        }
        Check(SampleDiffuseProbeVolume(*open,{100,0,0},{0,1,0},{0,1,0}).coverage==0,"Outside volume did not fall back");
        auto room=Room(); const auto sealed=BakeDiffuseProbeVolume(grid,room,lighting);
        for(auto p:{glm::vec3(0),glm::vec3(.2f,-1.8f,.5f),glm::vec3(1.8f,0,0)}) {
            const auto sample=SampleDiffuseProbeVolume(*sealed,p,{0,1,0},{1,0,0});
            Check(sample.coverage==1&&glm::length(sample.irradianceOverPi)==0&&sample.skyVisibility==0,
                  "Fully enclosed room leaked global sky");
        }
        lighting.sky.enabled=false;
        room[0].emission=room[1].emission={5,.1f,0};
        Quad(room,{0,-2,-2},{0,-2,2},{0,2,2},{0,2,-2},{1,0,0});
        const auto divided=BakeDiffuseProbeVolume(grid,room,lighting);
        const auto lit=SampleDiffuseProbeVolume(*divided,{-1,0,0},{-1,0,0},{-1,0,0});
        const auto dark=SampleDiffuseProbeVolume(*divided,{.25f,0,0},{1,0,0},{1,0,0});
        Check(lit.irradianceOverPi.r>.1f&&dark.irradianceOverPi.r<.01f,"Visibility interpolation leaked through a thin opaque wall");
        // A fixture paired with an analytic light retains visible emission in
        // extraction, but cannot contribute that direct light a second time.
        room[0].analyticEmission=room[1].analyticEmission=true;
        const auto owned=BakeDiffuseProbeVolume(grid,room,lighting);
        Check(glm::length(SampleDiffuseProbeVolume(*owned,{-1,0,0},{-1,0,0},{-1,0,0}).irradianceOverPi)==0,
              "Analytic fixture emission was counted twice by diffuse transport");
        auto emitter=room[0];auto packedEmitter=BuildRealtimeGiScene(std::span(&emitter,1));
        const auto emissionOffset=packedEmitter->NodeCount()*2+4;
        Check(glm::vec3(packedEmitter->Pixels()[emissionOffset])==emitter.emission&&packedEmitter->Pixels()[emissionOffset].w==2,
              "Analytic emitter packing lost visible radiance or ownership");
        emitter.twoSidedEmission=true;packedEmitter=RefitRealtimeGiScene(*packedEmitter,std::span(&emitter,1));
        Check(packedEmitter->Pixels()[emissionOffset].w==3,"Refit conflated emitter ownership with two-sided emission");
        room[0].analyticEmission=room[1].analyticEmission=false;
        auto solid=Room(.25f,false); for(auto& t:solid) { t.a+=glm::vec3(.5f); t.b+=glm::vec3(.5f); t.c+=glm::vec3(.5f); }
        const auto occupied=BakeDiffuseProbeVolume(grid,solid,lighting);
        Check(occupied->ValidProbes()==63,"Probe inside a closed solid was not rejected");
        std::vector<ProbeTriangle> floor;
        Quad(floor,{-20,-2,-20},{20,-2,-20},{20,-2,20},{-20,-2,20},{0,1,0},{.8f,.1f,.05f});
        lighting.pointPositions[0]={0,3,0,1}; lighting.pointColors[0]={30,30,30,1};
        const auto bounce=BakeDiffuseProbeVolume(grid,floor,lighting);
        const auto bounced=SampleDiffuseProbeVolume(*bounce,{0,0,0},{0,-1,0},{0,-1,0});
        Check(bounced.irradianceOverPi.r>.01f&&bounced.irradianceOverPi.r>bounced.irradianceOverPi.g*6,
              "World-space point-lit diffuse color bounce missing");
        {
            // All receiver points lie inside both represented sources. Their
            // radiance must follow I/R^2, rather than a singular point I/d^2.
            ProbeBakeSettings nearGrid;nearGrid.origin={-.04f,.06f,-.04f};nearGrid.spacing=glm::vec3(.08f);nearGrid.counts={2,2,2};nearGrid.diffuseBounces=1;
            ProbeBakeLighting nearLight;nearLight.pointPositions[0]={0,.1f,0,1};nearLight.pointColors[0]={1,1,1,.5f};
            std::vector<ProbeTriangle> nearFloor;
            Quad(nearFloor,{-.1f,0,-.1f},{.1f,0,-.1f},{.1f,0,.1f},{-.1f,0,.1f},{0,1,0},glm::vec3(.8f));
            const auto halfSource=BakeDiffuseProbeVolume(nearGrid,nearFloor,nearLight);
            nearLight.pointColors[0].w=1;const auto unitSource=BakeDiffuseProbeVolume(nearGrid,nearFloor,nearLight);
            double energy=0;
            for(unsigned i=0;i<8;++i)for(unsigned j=0;j<9;++j) {
                const auto a=glm::vec3(halfSource->Constants().probes[i].sh[j]),b=glm::vec3(unitSource->Constants().probes[i].sh[j]);
                energy+=glm::length(a);
                Check(glm::length(a-b*4.f)<.00005f,"CPU diffuse bake ignored the finite point source's energy bound");
            }
            Check(energy>.1,"Finite point-source near-field regression has no transported light");
            nearLight.pointColors={};nearLight.spotLight.enabled=true;
            nearLight.spotLight.position={0,.1f,0};nearLight.spotLight.direction={0,-2,0};
            nearLight.spotLight.intensity=glm::vec3(1);nearLight.spotLight.innerAngle=1;
            nearLight.spotLight.outerAngle=1.2f;nearLight.spotLight.sourceRadius=.5f;
            const auto spotHalf=BakeDiffuseProbeVolume(nearGrid,nearFloor,nearLight);
            nearLight.spotLight.sourceRadius=1;const auto spotUnit=BakeDiffuseProbeVolume(nearGrid,nearFloor,nearLight);
            nearLight.spotLight.direction={0,1,0};const auto away=BakeDiffuseProbeVolume(nearGrid,nearFloor,nearLight);
            for(unsigned i=0;i<8;++i)for(unsigned j=0;j<9;++j) {
                const auto a=glm::vec3(spotHalf->Constants().probes[i].sh[j]),b=glm::vec3(spotUnit->Constants().probes[i].sh[j]);
                const auto point=glm::vec3(halfSource->Constants().probes[i].sh[j]);
                Check(glm::length(a-point)<.00005f&&glm::length(a-b*4.f)<.00005f,
                    "Spot GI transport lost on-axis intensity or finite-source energy cap");
                Check(glm::length(glm::vec3(away->Constants().probes[i].sh[j]))==0,
                    "Spot GI transport illuminated a surface behind the beam");
            }
        }
        grid.diffuseBounces=0; const auto zeroBounce=BakeDiffuseProbeVolume(grid,floor,lighting);
        Check(glm::length(SampleDiffuseProbeVolume(*zeroBounce,{0,0,0},{0,-1,0},{0,-1,0}).irradianceOverPi)==0,
              "Zero bounce retained indirect reflection");
        DiffuseProbeSettings settings; settings.enabled=true; Check(!ValidateDiffuseProbeSettings(settings),"Missing probe volume accepted");
        settings.volume=sealed; Check(ValidateDiffuseProbeSettings(settings),"Valid volume rejected");
        settings.visibilityBias=-1; Check(!ValidateDiffuseProbeSettings(settings),"Negative visibility bias accepted");
        bool rejected=false; grid.counts=glm::uvec3(100);
        try { BakeDiffuseProbeVolume(grid,{},lighting); } catch(const std::invalid_argument&) { rejected=true; }
        Check(rejected,"Probe budget exceeded 64");
        rejected=false;
        try { SampleDiffuseProbeVolume(*open,{0,0,0},{0,0,0},{0,1,0}); } catch(const std::invalid_argument&) { rejected=true; }
        Check(rejected,"Zero diagnostic normal accepted");
        grid.counts={4,4,4}; grid.origin=glm::vec3(1e30f); rejected=false;
        try { BakeDiffuseProbeVolume(grid,{},lighting); } catch(const std::invalid_argument&) { rejected=true; }
        Check(rejected,"Unbounded bake position accepted");
        grid.origin={-1.5f,-1.5f,-1.5f};lighting.pointColors[0].w=-.1f;rejected=false;
        try { BakeDiffuseProbeVolume(grid,{},lighting); } catch(const std::invalid_argument&) { rejected=true; }
        Check(rejected,"Negative finite point-source radius accepted");lighting.pointColors[0].w=0;
        RealtimeGiSettings rt;rt.enabled=true;Check(!ValidateRealtimeGiSettings(rt),"Missing realtime scene accepted");
        rt.scene=BuildRealtimeGiScene({});Check(ValidateRealtimeGiSettings(rt)&&rt.scene->NodeCount()==0&&rt.scene->Height()==1,"Empty realtime scene packing rejected");
        rt.scene=BuildRealtimeGiScene(room);Check(rt.scene->TriangleCount()==room.size()&&rt.scene->NodeCount()>0&&rt.scene->HasEmission(),"Realtime BVH or emission metadata missing");
        rt.probesPerFrame=0;Check(!ValidateRealtimeGiSettings(rt),"Zero realtime budget accepted");rt.probesPerFrame=16;
        rt.bounceFeedback=1;Check(!ValidateRealtimeGiSettings(rt),"Unbounded realtime feedback accepted");
        rejected=false;auto invalidGeometry=room;invalidGeometry[0].emission.x=std::numeric_limits<float>::infinity();
        try{BuildRealtimeGiScene(invalidGeometry);}catch(const std::invalid_argument&){rejected=true;}
        Check(rejected,"Nonfinite realtime source accepted");
        std::cout<<"World probe CPU test passed: constant sky, enclosed sky occlusion, thin-wall leak="<<dark.irradianceOverPi.r
                 <<", solid rejection, camera-independent point/color bounce, zero bounce and validation\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
