#include "MaterialLab/Public/showcase_scene.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

namespace MaterialLab {
const std::vector<ShowcaseObject>& ShowcaseObjects() {
    static const auto objects=[] {
        std::vector<ShowcaseObject> v;
        using M=ShowcaseMaterial; using G=ShowcaseMesh;
        const auto box=[&](const char* name,M m,glm::vec3 p,glm::vec3 s,int movable=-1) {
            v.push_back({name,G::Box,m,p,s,0,movable});
        };
        const auto sphere=[&](const char* name,M m,glm::vec3 p,glm::vec3 s,int movable=-1) {
            v.push_back({name,G::Sphere,m,p,s,0,movable});
        };
        const auto plane=[&](const char* name,M m,glm::vec3 p,glm::vec3 s,float tilt=0,glm::vec3 axis=glm::vec3(1,0,0),int movable=-1) {
            v.push_back({name,G::Plane,m,p,s,0,movable,false,axis,tilt});
        };
        box("Courtyard",M::Stone,{0,-.16f,-1},{12,.16f,11});
        box("Gallery floor",M::Tiles,{0,.025f,-5.5f},{5,.025f,3.5f});
        box("Back wall",M::Plaster,{0,2.2f,-9},{5,2.2f,.15f});
        box("Red bounce wall",M::Terracotta,{-5,2.2f,-5.5f},{.15f,2.2f,3.5f});
        // Right wall has a real opening, so skylight can enter from the pool.
        box("Window sill",M::Plaster,{5,.55f,-5.5f},{.15f,.55f,3.5f});
        box("Window lintel",M::Plaster,{5,3.85f,-5.5f},{.15f,.55f,3.5f});
        for(float z:{-8.3f,-2.7f})box("Window pier",M::Plaster,{5,2.2f,z},{.15f,1.1f,.7f});
        for(float x:{-4.8f,-1.5f,1.5f,4.8f})box("Entrance pier",M::Plaster,{x,2.2f,-2},{.16f,2.2f,.18f});
        box("Entrance beam",M::Wood,{0,4.15f,-2},{5,.25f,.25f});
        // Four roof pieces leave a skylight above the interior display.
        for(float x:{-3.0f,3.0f})box("Roof",M::Plaster,{x,4.4f,-5.5f},{2,.13f,3.5f});
        box("Roof front",M::Plaster,{0,4.4f,-3.5f},{1,.13f,1.5f});
        box("Roof back",M::Plaster,{0,4.4f,-8},{1,.13f,1});
        box("Exhibition plinth",M::Stone,{1.5f,.6f,-5.8f},{1.0f,.6f,1.0f});
        sphere("Courtyard gold sphere",M::Gold,{-3.8f,1.05f,2.4f},{1.05f,1.05f,1.05f},0);
        box("Movable ceramic cube",M::Ceramic,{-.9f,.7f,1.3f},{.7f,.7f,.7f},1);
        sphere("Interior chrome sphere",M::Chrome,{1.5f,2.05f,-5.8f},{.85f,.85f,.85f},2);
        box("Interior red block",M::Terracotta,{-1.9f,.8f,-5.7f},{.65f,.8f,.65f},3);
        sphere("Tinted glass sphere",M::Glass,{3.7f,1.1f,-3.7f},{.9f,.9f,.9f},4);
        plane("Movable emissive panel",M::EmissivePanel,{-4.72f,2,-6},{.65f,1,.8f},glm::radians(-90.f),{0,0,1},5);
        box("Display bench",M::Wood,{0,.46f,-8.3f},{2.5f,.12f,.45f});
        for(float x:{-2.0f,2.0f})box("Bench leg",M::Chrome,{x,.2f,-8.3f},{.08f,.2f,.3f});
        plane("Ceiling area emitter",M::CeilingLamp,{0,4.1f,-6},{1.2f,1,.65f},glm::radians(180.f));
        box("Interior warm fixture",M::WarmLamp,{-3.7f,3.25f,-8.7f},{.22f,.14f,.12f});
        box("Interior blue fixture",M::CoolLamp,{4.7f,2.8f,-7.8f},{.12f,.15f,.22f});
        // Shallow reflective pool and stone coping beside the open window.
        box("Pool bed",M::Ceramic,{7,.025f,1},{2,.025f,2.8f});
        plane("Flowing water",M::Water,{7,.12f,1},{1.9f,1,2.7f});
        for(float x:{4.9f,9.1f})box("Pool edge",M::Stone,{x,.14f,1},{.12f,.14f,2.9f});
        for(float z:{-1.9f,3.9f})box("Pool edge",M::Stone,{7,.14f,z},{2.2f,.14f,.12f});
        // Pergola casts small, repeated shadows and includes grazing receivers.
        for(float x:{-9.f,-5.6f})for(float z:{-.8f,4.2f})box("Pergola column",M::Wood,{x,1.7f,z},{.11f,1.7f,.11f});
        for(float x:{-9.f,-5.6f})box("Pergola beam",M::Wood,{x,3.4f,1.7f},{.14f,.14f,2.65f});
        for(int i=0;i<8;++i)box("Pergola slat",M::Wood,{-7.3f,3.58f,-.8f+i*.72f},{1.9f,.07f,.08f});
        box("Garden bench",M::Wood,{-7.3f,.5f,2.4f},{1.35f,.12f,.45f});
        for(float x:{-8.3f,-6.3f})box("Garden bench leg",M::Chrome,{x,.22f,2.4f},{.06f,.22f,.32f});
        for(float x:{-7.5f,7.5f}) {
            box("Planter",M::Terracotta,{x,.35f,6.5f},{.65f,.35f,.65f});
            sphere("Topiary",M::Leaves,{x,1.45f,6.5f},{.85f,.9f,.85f});
            box("Path lamp",M::Chrome,{x,.75f,4.9f},{.08f,.75f,.08f});
            sphere("Path lamp diffuser",M::WarmLamp,{x,1.55f,4.9f},{.18f,.18f,.18f});
        }
        return v;
    }();
    return objects;
}
const ShowcaseObject& ShowcaseMovable(std::size_t index) {
    for(const auto& o:ShowcaseObjects())if(o.movable==int(index))return o;
    throw std::out_of_range("Invalid showcase prop");
}
glm::mat4 ShowcaseTransform(const ShowcaseObject& o,const ShowcaseSettings& s) {
    auto p=o.position;float yaw=o.yaw;
    if(o.movable>=0){p+=s.offsets.at(o.movable);yaw+=s.rotations.at(o.movable);}
    return glm::translate(glm::mat4(1),p)*glm::rotate(glm::mat4(1),yaw,glm::vec3(0,1,0))*
        glm::rotate(glm::mat4(1),o.tilt,o.rotationAxis)*glm::scale(glm::mat4(1),o.scale);
}
glm::vec3 ShowcaseForward(float yaw,float pitch) {
    return {std::sin(yaw)*std::cos(pitch),std::sin(pitch),-std::cos(yaw)*std::cos(pitch)};
}
Render::PipelineCamera ShowcaseCamera(glm::vec3 p,float yaw,float pitch,float aspect) {
    Render::PipelineCamera c;c.position=p;c.nearPlane=.08f;c.farPlane=100;
    c.view=glm::lookAt(p,p+ShowcaseForward(yaw,pitch),glm::vec3(0,1,0));
    c.projection=glm::perspective(glm::radians(65.f),aspect,c.nearPlane,c.farPlane);return c;
}
Render::PipelineCamera ShowcaseCameraPreset(unsigned preset,float aspect) {
    const std::array<glm::vec3,3> eyes{{{11,7.8f,15},{0,1.7f,-2.8f},{8.8f,2.2f,5.2f}}};
    const std::array<glm::vec3,3> targets{{{0,1,-3},{-.6f,1.7f,-6.7f},{4.2f,1.1f,-2.5f}}};
    preset%=3;const auto f=glm::normalize(targets[preset]-eyes[preset]);
    return ShowcaseCamera(eyes[preset],std::atan2(f.x,-f.z),std::asin(f.y),aspect);
}
int PickShowcaseObject(glm::vec3 origin,glm::vec3 direction,const ShowcaseSettings& s) {
    if(glm::dot(direction,direction)<1e-12f)return -1;
    float nearest=std::numeric_limits<float>::max();int selected=-1;
    for(const auto& o:ShowcaseObjects()) {
        // The water is not pickable; the editable glass sphere is a prop itself.
        const auto inv=glm::inverse(ShowcaseTransform(o,s));
        const auto p=glm::vec3(inv*glm::vec4(origin,1)),d=glm::vec3(inv*glm::vec4(direction,0));
        float t=nearest;
        if(o.mesh==ShowcaseMesh::Sphere) {
            float a=glm::dot(d,d),b=glm::dot(p,d),c=glm::dot(p,p)-1,disc=b*b-a*c;
            if(disc<0)continue;t=(-b-std::sqrt(disc))/a;if(t<0)t=(-b+std::sqrt(disc))/a;
        } else if(o.mesh==ShowcaseMesh::Plane) {
            if(std::abs(d.y)<1e-8f)continue;t=-p.y/d.y;auto hit=p+d*t;
            if(std::abs(hit.x)>1||std::abs(hit.z)>1)continue;
        } else {
            float lo=0,hi=nearest;
            for(int axis=0;axis<3;++axis) {
                if(std::abs(d[axis])<1e-8f){if(std::abs(p[axis])>1)hi=-1;continue;}
                float a=(-1-p[axis])/d[axis],b=(1-p[axis])/d[axis];
                if(a>b)std::swap(a,b);lo=std::max(lo,a);hi=std::min(hi,b);
            }
            if(lo>hi)continue;t=lo;
        }
        if(t<0||t>=nearest)continue;
        if(o.material==ShowcaseMaterial::Water||(o.material==ShowcaseMaterial::Glass&&o.movable<0))continue;
        nearest=t;selected=o.movable;
    }
    return selected;
}
void ConfigureShowcaseLighting(Render::MaterialPipelineSettings& e,const ShowcaseSettings& s,float time) {
    e.shadows.sunDirection={-.55f,-1,-.35f};e.shadows.sunColor={1,.91f,.75f};
    e.shadows.sunIntensity=s.sunlight?2.4f:0;
    e.lighting.ambientAndExposure={0,0,0,1};
    const std::array<glm::vec3,4> positions{{{-3.6f,3.15f,-8.5f},{4.55f,2.8f,-7.8f},{-7.5f,1.55f,4.9f},{7.5f,1.55f,4.9f}}};
    const std::array<glm::vec3,4> colors{{{28,13,4},{4,12,30},{10,6,2},{10,6,2}}};
    const std::array<float,4> sourceRadii{{.14f,.15f,.18f,.18f}};
    for(int i=0;i<4;++i) {
        auto p=positions[i];if(s.animateLights&&i==1){p.x=2+2*std::sin(time*.7f);p.z=-6.2f+1.4f*std::cos(time*.7f);}
        e.lighting.lightPositions[i]=glm::vec4(p,1);
        e.lighting.lightColors[i]=glm::vec4(s.pointLights?colors[i]:glm::vec3(0),sourceRadii[i]);
    }
    e.areaLights.count=s.areaLights?2:0;e.areaLights.samplesPerAxis=4;
    e.areaLights.lights[0]={{0,4.08f,-6},{1.2f,0,0},{0,0,.65f},{5,3.5f,1.8f}};
    const auto model=ShowcaseTransform(ShowcaseMovable(5),s);
    e.areaLights.lights[1]={glm::vec3(model[3]),glm::vec3(model*glm::vec4(1,0,0,0)),
        glm::vec3(model*glm::vec4(0,0,-1,0)),{4,1.1f,.28f}};
    // Coverage spans indoor and outdoor receivers, not the old six-meter room.
    e.lumenGi.origin={-9.5f,.35f,-8.6f};e.lumenGi.spacing={6.3f,1.4f,3.8f};e.lumenGi.counts={4,3,5};
    e.lumenGi.maxDistance=40;e.lumenGi.surfaceUpdatesPerFrame=1024;
    e.realtimeGi.origin=e.lumenGi.origin;e.realtimeGi.spacing=e.lumenGi.spacing;e.realtimeGi.counts=e.lumenGi.counts;
    e.realtimeGi.maxDistance=40;
}
} // namespace MaterialLab
