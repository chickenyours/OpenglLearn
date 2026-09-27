#include "IWanna/Public/game_config.h"
#include <fstream>
#include <cmath>
#include <stdexcept>
namespace IWanna {
GameConfig GameConfig::Load(const std::filesystem::path& path) {
    std::ifstream input(path);Json::Value root;Json::CharReaderBuilder builder;std::string errors;
    if(!input || !Json::parseFromStream(builder,input,&root,&errors)) throw std::runtime_error("Invalid gameplay JSON: "+path.string()+" "+errors);
    auto number=[](const Json::Value& object,const char* key,float lo,float hi) {
        auto& v=object[key];
        if(!v.isNumeric() || !std::isfinite(v.asDouble()) || v.asDouble()<lo || v.asDouble()>hi) throw std::runtime_error(std::string("Invalid gameplay setting: ")+key);
        return v.asFloat();
    };
    auto integer=[&](const Json::Value& object,const char* key,int lo,int hi) {
        float value=number(object,key,float(lo),float(hi));
        if(std::floor(value)!=value)throw std::runtime_error(std::string("Gameplay setting must be an integer: ")+key);
        return int(value);
    };
    GameConfig c;auto& p=root["physics"];
    c.physics.runSpeed=number(p,"runSpeed",0,100);c.physics.jumpSpeed=number(p,"jumpSpeed",1,100);
    c.physics.doubleJumpSpeed=number(p,"doubleJumpSpeed",1,100);c.physics.gravity=number(p,"gravity",1,500);
    c.physics.maxFallSpeed=number(p,"maxFallSpeed",1,200);c.physics.fixedStep=1/number(p,"fixedHz",30,480);
    c.physics.maxSubstepDistance=number(p,"maxSubstepDistance",.005f,.1f);
    c.alphaThreshold=integer(root["collision"],"alphaThreshold",1,255);
    auto& player=root["player"];c.playerSize={number(player,"width",.5f,10),number(player,"height",.5f,10)};
    if(!player["facesLeftInSource"].isBool()) throw std::runtime_error("facesLeftInSource must be boolean");
    c.facesLeft=player["facesLeftInSource"].asBool();
    // Optional for compatibility with previously saved gameplay configs.
    const auto& box=root["collision"]["playerBody"];
    if(!box.isNull()) {
        c.character.sizeRatio={number(box,"widthRatio",.05f,1),number(box,"heightRatio",.05f,1)};
        c.character.offsetRatio={number(box,"offsetXRatio",-.45f,.45f),number(box,"offsetYRatio",-.45f,.45f)};
        if(std::abs(c.character.offsetRatio.x)+c.character.sizeRatio.x*.5f>.5f ||
           std::abs(c.character.offsetRatio.y)+c.character.sizeRatio.y*.5f>.5f)
            throw std::runtime_error("playerBody must fit inside the player image");
    }
    const auto& contact=root["collision"]["contact"];
    if(!contact.isNull()) {
        c.character.skin=number(contact,"skin",.001f,.1f);
        c.character.stepHeight=number(contact,"stepHeight",0,.3f);
        c.character.groundSnap=number(contact,"groundSnap",0,.2f);
        c.character.recoveryDistance=number(contact,"recoveryDistance",.01f,1);
        if(contact.isMember("wallSlide"))c.character.wallSlide=number(contact,"wallSlide",0,.2f);
    }
    if(2*c.character.skin>=std::min(c.playerSize.x*c.character.sizeRatio.x,c.playerSize.y*c.character.sizeRatio.y))
        throw std::runtime_error("contact skin must be smaller than half the player body");
    auto animation=[&](const char* name) {
        const auto& v=player[name];Sprite s;
        if(!v["image"].isString()) throw std::runtime_error("Missing animation image");s.image=v["image"].asString();
        if(std::filesystem::path(s.image).filename().string()!=s.image || s.image.empty()) throw std::runtime_error("Animation image must be a filename");
        s.columns=integer(v,"columns",1,64);s.cellWidth=integer(v,"cellWidth",1,2048);s.cellHeight=integer(v,"cellHeight",1,2048);
        s.duration=number(v,"duration",.05f,10);s.frames.clear();for(int i=0;i<s.columns;++i) s.frames.push_back(i);return s;
    };
    c.idle=animation("idle");c.run=animation("run");c.jump=animation("jump");return c;
}
}
