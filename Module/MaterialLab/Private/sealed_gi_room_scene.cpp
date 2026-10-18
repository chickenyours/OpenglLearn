#include "MaterialLab/Public/sealed_gi_room_scene.h"
#include <array>
#include <cmath>
#include <stdexcept>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace MaterialLab {
const std::vector<ProbePlacementObject>& SealedGiRoomObjects() {
    using Mesh=ShowcaseMesh;
    using M=ProbePlacementMaterial;
    static const std::vector<ProbePlacementObject> objects{
        {"Ivory floor",Mesh::Box,M::Floor,{0,-.12f,0},{4.24f,.12f,3.74f}},
        {"Coral red left wall",Mesh::Box,M::Wall,{-4.12f,2.25f,0},{.12f,2.25f,3.74f}},
        {"Jade right wall",Mesh::Box,M::Wall,{4.12f,2.25f,0},{.12f,2.25f,3.74f}},
        {"Cobalt back wall",Mesh::Box,M::Wall,{0,2.25f,-3.62f},{4.24f,2.25f,.12f}},
        {"Ochre front wall",Mesh::Box,M::Wall,{0,2.25f,3.62f},{4.24f,2.25f,.12f}},
        // Four solid slabs leave a real 2 x 2 opening in the ceiling.
        {"Ceiling left slab",Mesh::Box,M::Wall,{-2.62f,4.62f,0},{1.62f,.12f,3.74f}},
        {"Ceiling right slab",Mesh::Box,M::Wall,{2.62f,4.62f,0},{1.62f,.12f,3.74f}},
        {"Ceiling back slab",Mesh::Box,M::Wall,{0,4.62f,-2.37f},{1,.12f,1.37f}},
        {"Ceiling front slab",Mesh::Box,M::Wall,{0,4.62f,2.37f},{1,.12f,1.37f}},
        {"Recess closed backing",Mesh::Box,M::Block,{0,4.78f,0},{1,.06f,1}},
        {"Recess left lining",Mesh::Box,M::Block,{-.98f,4.62f,0},{.02f,.10f,1}},
        {"Recess right lining",Mesh::Box,M::Block,{.98f,4.62f,0},{.02f,.10f,1}},
        {"Recess back lining",Mesh::Box,M::Block,{0,4.62f,-.98f},{.96f,.10f,.02f}},
        {"Recess front lining",Mesh::Box,M::Block,{0,4.62f,.98f},{.96f,.10f,.02f}},
        {"Lamp left black trim",Mesh::Box,M::Block,{-1.015f,4.495f,0},{.085f,.025f,1.10f}},
        {"Lamp right black trim",Mesh::Box,M::Block,{1.015f,4.495f,0},{.085f,.025f,1.10f}},
        {"Lamp back black trim",Mesh::Box,M::Block,{0,4.495f,-1.015f},{.93f,.025f,.085f}},
        {"Lamp front black trim",Mesh::Box,M::Block,{0,4.495f,1.015f},{.93f,.025f,.085f}},
        // Its geometric normal is down, matching cross(U,V) of the area light.
        {"Recessed square diffuser",Mesh::Plane,M::Block,SealedGiRoomEmitterCenter,{.9f,1,.9f},0,glm::pi<float>()},
        {"Porcelain large sphere",Mesh::Sphere,M::Sphere,{-1.45f,.88f,-.65f},{.88f,.88f,.88f}},
        {"Porcelain upright oval",Mesh::Sphere,M::Sphere,{1.28f,1.10f,-1.15f},{.65f,1.10f,.65f}},
        {"Porcelain movable cube",Mesh::Box,M::Block,{-.75f,.48f,1.15f},{.62f,.48f,.62f},.25f,0,{1,0,0},true},
        {"Porcelain tall prism",Mesh::Box,M::Block,{1.65f,.80f,-.25f},{.45f,.80f,.45f},-.30f},
        {"Porcelain small sphere",Mesh::Sphere,M::Sphere,{.05f,.46f,-1.90f},{.46f,.46f,.46f}},
        {"Porcelain low oval",Mesh::Sphere,M::Sphere,{-2.42f,.32f,1.20f},{.65f,.32f,.45f}}
    };
    return objects;
}

SealedGiRoomMaterial SealedGiRoomObjectMaterial(unsigned object) {
    if(object>=SealedGiRoomObjects().size())throw std::out_of_range("Invalid sealed GI room object");
    if(object==0)return {{.72f,.71f,.68f},.64f,{0,0,0}};
    if(object==1)return {{.66f,.075f,.045f},.82f,{0,0,0}};
    if(object==2)return {{.045f,.55f,.30f},.82f,{0,0,0}};
    if(object==3)return {{.055f,.16f,.64f},.82f,{0,0,0}};
    if(object==4)return {{.68f,.44f,.075f},.82f,{0,0,0}};
    if(object<=8)return {{.78f,.78f,.76f},.78f,{0,0,0}};
    if(object<SealedGiRoomLampIndex)return {{.025f,.027f,.032f},.30f,{0,0,0}};
    // Analytic rectangle transport is the sole light source. The visible lamp
    // is emissive, while its card emission is suppressed by scene integration.
    if(object==SealedGiRoomLampIndex)return {{0,0,0},.30f,SealedGiRoomEmitterRadiance};
    static constexpr std::array<glm::vec3,6> colors{{
        {.86f,.84f,.80f},{.82f,.85f,.86f},{.85f,.85f,.83f},
        {.80f,.82f,.86f},{.86f,.82f,.80f},{.83f,.86f,.82f}}};
    static constexpr std::array<float,6> roughness{.18f,.20f,.22f,.16f,.20f,.24f};
    const auto index=object-SealedGiRoomLampIndex-1;
    return {colors[index],roughness[index],{0,0,0}};
}

Render::PipelineCamera SealedGiRoomCameraPreset(unsigned preset,float aspect) {
    if(!std::isfinite(aspect)||aspect<=0)throw std::invalid_argument("Sealed GI room camera requires a positive aspect");
    const std::array<glm::vec3,3> eyes{{{0,2.05f,3.10f},{3.30f,2.45f,3.10f},{0,1.50f,2.85f}}};
    const std::array<glm::vec3,3> targets{{{0,1.75f,-.80f},{-.15f,2.30f,-.40f},{0,1,-.70f}}};
    Render::PipelineCamera camera;camera.position=eyes[preset%eyes.size()];
    camera.nearPlane=.04f;camera.farPlane=24;
    camera.view=glm::lookAt(camera.position,targets[preset%targets.size()],glm::vec3(0,1,0));
    camera.projection=glm::perspective(glm::radians(55.f),aspect,camera.nearPlane,camera.farPlane);
    return camera;
}

void ConfigureSealedGiRoomLighting(Render::MaterialPipelineSettings& effects) {
    effects.shadows.enabled=false;effects.shadows.sunIntensity=0;
    effects.sky.enabled=false;effects.sky.background=false;
    effects.reflection.enabled=false;effects.spotLight.enabled=false;
    effects.indirect.enabled=false;effects.probes.enabled=false;
    effects.realtimeGi.enabled=false;effects.lumenGi.enabled=false;
    effects.lighting.ambientAndExposure={0,0,0,1};
    for(unsigned index=0;index<effects.lighting.lightColors.size();++index) {
        effects.lighting.lightPositions[index]={0,0,0,1};
        effects.lighting.lightColors[index]={0,0,0,0};
    }
    effects.areaLights={};effects.areaLights.count=1;effects.areaLights.samplesPerAxis=8;
    auto& light=effects.areaLights.lights[0];
    light.center=SealedGiRoomEmitterCenter;
    light.halfAxisU=SealedGiRoomEmitterHalfAxisU;light.halfAxisV=SealedGiRoomEmitterHalfAxisV;
    light.radiance=SealedGiRoomEmitterRadiance;light.enabled=true;light.twoSided=false;
}
} // namespace MaterialLab
