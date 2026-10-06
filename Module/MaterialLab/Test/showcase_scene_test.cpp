#include <iostream>
#include <stdexcept>
#include "MaterialLab/Public/showcase_scene.h"
#include "MaterialLab/Public/material_lab_module.h"

namespace {
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}
int main() {
    using namespace MaterialLab;
    FrameSettings scaled;const auto aa=scaled.effects.temporal.antialiasing;
    SetShowcaseResolution(scaled,2560,1440,.625f);
    Check(scaled.width==1600&&scaled.height==900&&scaled.OutputExtent()==glm::uvec2(2560,1440),
          "Render scale failed to preserve 2K presentation");
    Check(std::abs(scaled.OutputAspect()-16.f/9)<1e-6f&&scaled.effects.temporal.antialiasing==aa,
          "Render scale changed camera aspect or antialiasing");
    SetShowcaseResolution(scaled,1919,1079,.625f);
    Check(scaled.width==1199&&scaled.height==674&&std::abs(scaled.OutputAspect()-1919.f/1079)<1e-6f,
          "Odd render-scale extent distorted the output projection");
    SetShowcaseResolution(scaled,2560,1440);
    Check(scaled.width==2560&&scaled.height==1440&&scaled.effects.outputSize==glm::uvec2(0),
          "Native render scale retained an obsolete output override");
    bool rejected=false;try{SetShowcaseResolution(scaled,2560,1440,.49f);}catch(const std::invalid_argument&){rejected=true;}
    Check(rejected,"Unbounded render scale was accepted");
    ShowcaseSettings state;
    for(std::size_t i=0;i<ShowcaseMovableCount;++i) {
        const auto& o=ShowcaseMovable(i);Check(o.movable==int(i),"Missing movable prop");
        state.offsets[i]={.5f,.2f,-.4f};state.rotations[i]=.3f;
        auto model=ShowcaseTransform(o,state);
        Check(glm::length(glm::vec3(model[3])-o.position-state.offsets[i])<1e-5f,"Prop translation was lost");
    }
    state={};
    Check(PickShowcaseObject({-3.8f,1.05f,6},{0,0,-1},state)==0,"Sphere picking failed");
    Check(PickShowcaseObject({-.9f,.7f,4},{0,0,-1},state)==1,"Box picking failed");
    Check(PickShowcaseObject({-1.9f,.8f,-11},{0,0,1},state)==-1,"Picking ignored the opaque back wall");
    state.offsets[0]={2,0,0};Check(PickShowcaseObject({-3.8f,1.05f,6},{0,0,-1},state)!=0,"Picking used the stale prop transform");
    Check(PickShowcaseObject({0,1,0},{0,0,0},state)==-1,"Zero ray accepted");
    for(unsigned i=0;i<3;++i) {
        auto c=ShowcaseCameraPreset(i,16.f/9);auto inv=glm::inverse(c.projection*c.view);
        auto farPoint=inv*glm::vec4(0,0,1,1);auto direction=glm::normalize(glm::vec3(farPoint)/farPoint.w-c.position);
        auto forward=-glm::vec3(glm::inverse(c.view)[2]);
        Check(glm::dot(direction,forward)>.999f,"Free camera and pick ray disagree");
    }
    Render::MaterialPipelineSettings effects;ConfigureShowcaseLighting(effects,state,0);
    Check(Render::ValidateAreaLightsSettings(effects.areaLights),"Showcase area lights are invalid");
    for(unsigned i=0;i<4;++i)Check(effects.lighting.lightColors[i].w>0&&effects.lighting.lightColors[i].w<.3f,
        "Showcase fixture lost its finite point-source radius");
    bool ceiling=false;for(const auto& object:ShowcaseObjects())if(object.name=="Ceiling area emitter")
        ceiling=object.material==ShowcaseMaterial::CeilingLamp;
    Check(ceiling,"Ceiling emitter shares two-sided path-lamp material");
    effects.realtimeGi.enabled=false;Check(Render::ValidateRealtimeGiSettings(effects.realtimeGi),"Showcase probe grid is invalid");
    auto before=effects.areaLights.lights[1].center;state.offsets[5]={1,0,0};ConfigureShowcaseLighting(effects,state,0);
    Check(glm::length(effects.areaLights.lights[1].center-before-glm::vec3(1,0,0))<1e-5f,"Panel and area light did not move together");
    state.sunlight=state.pointLights=state.areaLights=false;ConfigureShowcaseLighting(effects,state,0);
    Check(effects.shadows.sunIntensity==0&&effects.areaLights.count==0,"Light toggles were ignored");
    for(auto color:effects.lighting.lightColors)Check(glm::length(glm::vec3(color))==0,"Point lights stayed enabled");
    std::cout<<"Showcase camera, picking, props and light controls passed\n";
}
