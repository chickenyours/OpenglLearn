#pragma once
#include "Model/Public/character_material_profile.h"
#include "Render/Public/Pipeline/strand_direct_lighting.h"
#include <json/json.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <fstream>
#include <algorithm>

namespace Model::EditorPreview {
struct HairDirectLightingProfile {
    std::string label,parameterOrigin;
    glm::vec3 characterPosition{0},groomCenter{.005832567f,.827952564f,-.074002102f};
    glm::quat characterRotation{1,0,0,0};
    glm::vec3 sunTravelDirection{0,-1,0},sunColor{1};
    float sunIntensity=0,hostUnitEV=-7;
    bool sunEnabled=false;
    std::vector<Render::StrandSourcePunctualLight> lights;
    std::vector<int> sourceInstances;
    std::shared_ptr<const Render::StrandTextureTable> iesTable;
    Json::Value provenance;
    static HairDirectLightingProfile Load(const std::filesystem::path& path){
        const auto require=[](bool b,const char* m){if(!b)throw std::runtime_error(m);};
        std::ifstream input(path);require(bool(input),"Cannot open source direct-light profile");
        Json::Value j;input>>j;
        require(j["schema"].asString()=="model.hair-direct-lighting.v1","Unsupported source direct-light schema");
        const auto scalar=[&](const Json::Value& v){require(v.isNumeric(),"Expected numeric source light parameter");const float x=v.asFloat();require(std::isfinite(x),"Non-finite source light parameter");return x;};
        const auto v3=[&](const Json::Value& v){require(v.isArray()&&v.size()==3,"Expected source light vec3");return glm::vec3(scalar(v[0]),scalar(v[1]),scalar(v[2]));};
        HairDirectLightingProfile result;result.label=j["label"].asString();
        result.parameterOrigin=j.get("parameterOrigin","unverified adapter").asString();
        result.characterPosition=v3(j["sourceCharacterPosition"]);
        if(j.isMember("groomCenterLocal"))result.groomCenter=v3(j["groomCenterLocal"]);
        if(j.isMember("sourceCharacterRotation")){
            const auto& q=j["sourceCharacterRotation"];require(q.isArray()&&q.size()==4,"Invalid source character quaternion");
            result.characterRotation=glm::quat(scalar(q[3]),scalar(q[0]),scalar(q[1]),scalar(q[2]));
            require(glm::length(result.characterRotation)>.01f,"Zero source character quaternion");
            result.characterRotation=glm::normalize(result.characterRotation);
        }
        result.hostUnitEV=scalar(j["hostUnitEV"]);require(std::abs(result.hostUnitEV)<=24,"Source direct EV adapter exceeds range");
        const auto& sun=j["directional"];result.sunEnabled=sun.get("enabled",true).asBool();
        result.sunTravelDirection=v3(sun["direction"]);require(glm::length(result.sunTravelDirection)>.01f,"Zero source sun direction");
        result.sunTravelDirection=glm::normalize(result.sunTravelDirection);result.sunColor=v3(sun["color"]);
        result.sunIntensity=scalar(sun["intensity"]);require(result.sunIntensity>=0,"Negative source sun intensity");
        const auto& lights=j["lights"];require(lights.isArray()&&lights.size()<=Render::MaxStrandSourceLights,"Source light count exceeds supported bound");
        for(const auto& item:lights){
            Render::StrandSourcePunctualLight light;
            light.positionRange={v3(item["position"]),scalar(item["range"])};
            light.directionFalloff={v3(item["direction"]),scalar(item["falloff"])};
            light.attenuation={0,0,scalar(item["coneA"]),scalar(item["coneB"])};
            const auto& c=item["coefficients"];require(c.isArray()&&c.size()==3,"Source attenuation needs c0,c1,c2");
            light.colorTolerance={v3(item["color"]),Render::StrandPackSourceHalfPair(scalar(c[1]),scalar(c[2]))};
            light.extended={Render::StrandPackSourceHalfPair(scalar(item["exponent"]),scalar(c[0])),scalar(item["startDistance"]),0,0};
            light.shadowIes.y=item.get("iesRow",-1).asInt();
            if(item.isMember("toleranceBits"))light.colorTolerance.w=std::bit_cast<float>(item["toleranceBits"].asUInt());
            if(item.isMember("extendedBits"))light.extended.x=std::bit_cast<float>(item["extendedBits"].asUInt());
            result.lights.push_back(light);result.sourceInstances.push_back(item.get("sourceInstance",-1).asInt());
        }
        if(j.isMember("iesTable")){
            const auto text=j["iesTable"].asString();const auto rel=std::filesystem::path(std::u8string(text.begin(),text.end()));
            require(!rel.empty()&&rel.is_relative(),"Source IES table must be relative");
            for(const auto& part:rel)require(part!="..","Source IES path escapes profile directory");
            std::string error;result.iesTable=LoadStrandTextureTable(path.parent_path()/rel,&error);if(!result.iesTable)throw std::runtime_error(error);
        }
        Render::StrandSourceDirectSettings check;check.enabled=true;check.count=unsigned(result.lights.size());check.iesTable=result.iesTable;
        std::copy(result.lights.begin(),result.lights.end(),check.lights.begin());(void)Render::MakeStrandSourceDirectConstants(check);
        result.provenance=j.get("source",Json::Value{});return result;
    }
    struct Applied {
        Render::StrandSourceDirectSettings hair;
        glm::vec3 sunDirection{0,-1,0},sunColor{1};float sunIntensity=0;
        std::array<glm::vec4,4> pointPositions{},pointColors{};
        std::array<int,4> proxySourceInstances{-1,-1,-1,-1};
        double proxyLuminanceCoverage=0;
    };
    Applied Apply(const glm::dmat4& basePlacement)const{
        const float scale=float(glm::length(glm::dvec3(basePlacement[0])));
        if(!std::isfinite(scale)||scale<=1e-8f)throw std::runtime_error("Invalid direct-light preview placement scale");
        for(int i=1;i<3;++i)if(std::abs(glm::length(glm::dvec3(basePlacement[i]))-scale)>scale*.0001)
            throw std::runtime_error("Source direct-light adapter requires uniform preview scaling");
        const auto sourceCharacter=glm::translate(glm::dmat4(1),glm::dvec3(characterPosition))*glm::mat4_cast(glm::dquat(characterRotation));
        const auto mapping=basePlacement*glm::inverse(sourceCharacter);
        Applied result;result.hair.enabled=true;result.hair.count=unsigned(lights.size());result.hair.iesTable=iesTable;
        result.pointPositions.fill(glm::vec4(0,0,0,1));result.pointColors.fill(glm::vec4(0));
        result.hair.distanceToSourceMetres=1.f/scale;
        const float gain=std::exp2(hostUnitEV);
        result.sunDirection=glm::normalize(glm::vec3(mapping*glm::dvec4(sunTravelDirection,0)));
        result.sunColor=sunColor;result.sunIntensity=sunEnabled?sunIntensity*gain:0.f;
        const auto center=glm::vec3(basePlacement*glm::dvec4(groomCenter,1));
        std::vector<std::pair<double,unsigned>> ranked;double totalY=0;
        for(unsigned i=0;i<lights.size();++i){
            auto light=lights[i];light.positionRange=glm::vec4(glm::vec3(mapping*glm::dvec4(glm::vec3(light.positionRange),1)),light.positionRange.w);
            light.directionFalloff=glm::vec4(glm::normalize(glm::vec3(mapping*glm::dvec4(glm::vec3(light.directionFalloff),0))),light.directionFalloff.w);
            light.colorTolerance=glm::vec4(glm::vec3(light.colorTolerance)*gain,light.colorTolerance.w);result.hair.lights[i]=light;
            const auto to=glm::vec3(light.positionRange)-center;const float worldR=glm::length(to),r=worldR/scale,R=light.positionRange.w;
            float attenuation=0;
            if(r<=R&&worldR>1e-8f){
                const auto c12=Render::StrandUnpackSourceHalfPair(light.colorTolerance.w),ec0=Render::StrandUnpackSourceHalfPair(light.extended.x);
                const float x=std::max(r,light.extended.y)-light.extended.y;
                const float fade=glm::clamp(1.f-std::pow(x/(R-light.extended.y),ec0.x),0.f,1.f);
                const float t=glm::clamp(glm::clamp(glm::dot(-glm::vec3(light.directionFalloff),to/worldR)*light.attenuation.w+light.attenuation.z,0.f,1.f)/light.directionFalloff.w,0.f,1.f);
                attenuation=fade*fade*t*t*(3-2*t)/std::max(ec0.y+c12.x*x+c12.y*x*x,1e-8f);
            }
            const double y=glm::dot(glm::vec3(light.colorTolerance),glm::vec3(.2126f,.7152f,.0722f))*attenuation;
            totalY+=y;ranked.emplace_back(y,i);
        }
        std::stable_sort(ranked.begin(),ranked.end(),[](const auto& a,const auto& b){return a.first>b.first;});
        double selectedY=0;
        for(unsigned k=0;k<std::min<std::size_t>(4,ranked.size());++k){
            const auto [y,i]=ranked[k];selectedY+=y;const auto& light=result.hair.lights[i];
            const double baseY=glm::dot(glm::vec3(light.colorTolerance),glm::vec3(.2126f,.7152f,.0722f));
            const float factor=baseY>0?float(y/baseY):0.f;
            const float d2=glm::dot(glm::vec3(light.positionRange)-center,glm::vec3(light.positionRange)-center);
            result.pointPositions[k]=glm::vec4(glm::vec3(light.positionRange),1);
            result.pointColors[k]=glm::vec4(glm::vec3(light.colorTolerance)*factor*d2,.01f*scale);
            result.proxySourceInstances[k]=sourceInstances[i];
        }
        result.proxyLuminanceCoverage=totalY>0?selectedY/totalY:1;
        (void)Render::MakeStrandSourceDirectConstants(result.hair);return result;
    }
};
}
