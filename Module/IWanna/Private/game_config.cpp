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
    if(p.isMember("jumpHoldSeconds"))c.physics.jumpHoldSeconds=number(p,"jumpHoldSeconds",0,.5f);
    if(p.isMember("jumpHoldGravityScale"))c.physics.jumpHoldGravityScale=number(p,"jumpHoldGravityScale",0,1);
    if(p.isMember("jumpReleaseMultiplier"))c.physics.jumpReleaseMultiplier=number(p,"jumpReleaseMultiplier",0,1);
    c.alphaThreshold=integer(root["collision"],"alphaThreshold",1,255);
    auto& player=root["player"];c.playerSize={number(player,"width",.5f,10),number(player,"height",.5f,10)};
    // Older projects without a scale get the same smaller default character.
    if(player.isMember("scale")) {
        const auto& scale=player["scale"];
        if(!scale.isObject())throw std::runtime_error("player.scale must be an object with x and y");
        c.playerScale={number(scale,"x",.1f,4),number(scale,"y",.1f,4)};
    }
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
    const auto scaled=c.ScaledPlayerSize();
    if(2*c.character.skin>=std::min(scaled.x*c.character.sizeRatio.x,scaled.y*c.character.sizeRatio.y))
        throw std::runtime_error("contact skin must be smaller than half the player body");
    auto animation=[&](const char* name) {
        const auto& v=player[name];Sprite s;
        if(!v["image"].isString()) throw std::runtime_error("Missing animation image");s.image=v["image"].asString();
        if(std::filesystem::path(s.image).filename().string()!=s.image || s.image.empty()) throw std::runtime_error("Animation image must be a filename");
        s.columns=integer(v,"columns",1,64);s.cellWidth=integer(v,"cellWidth",1,2048);s.cellHeight=integer(v,"cellHeight",1,2048);
        s.duration=number(v,"duration",.05f,10);s.frames.clear();for(int i=0;i<s.columns;++i) s.frames.push_back(i);return s;
    };
    c.idle=animation("idle");c.run=animation("run");c.jump=animation("jump");
    const auto& shot=player["shooting"];
    if(!shot.isNull()) {
        if(!shot.isObject())throw std::runtime_error("player.shooting must be an object");
        if(shot.isMember("enabled")) {
            if(!shot["enabled"].isBool())throw std::runtime_error("shooting.enabled must be boolean");
            c.shooting.enabled=shot["enabled"].asBool();
        }
        if(shot.isMember("image")) {
            c.shooting.sprite.image=shot["image"].asString();
            if(c.shooting.sprite.image.empty()||std::filesystem::path(c.shooting.sprite.image).filename().string()!=c.shooting.sprite.image)
                throw std::runtime_error("shooting.image must be a filename");
        }
        if(shot.isMember("width"))c.shooting.size.x=number(shot,"width",.05f,5);
        if(shot.isMember("height"))c.shooting.size.y=number(shot,"height",.05f,5);
        if(shot.isMember("speed"))c.shooting.speed=number(shot,"speed",1,500);
        if(shot.isMember("cooldown"))c.shooting.cooldown=number(shot,"cooldown",.03f,5);
        if(shot.isMember("lifetime"))c.shooting.lifetime=number(shot,"lifetime",.05f,10);
        if(shot.isMember("muzzleXRatio"))c.shooting.muzzleOffset.x=number(shot,"muzzleXRatio",0,.12f);
        if(shot.isMember("muzzleYRatio"))c.shooting.muzzleOffset.y=number(shot,"muzzleYRatio",-.3f,.3f);
    }
    return c;
}
}
