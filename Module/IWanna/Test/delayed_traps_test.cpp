#include "IWanna/Public/game_module.h"
#include "IWanna/Public/content_format.h"
#include "Audio/Public/audio_module.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace IWanna;
namespace {
void Check(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
void Tick(GameModule& game,int frames=1){for(int i=0;i<frames;++i)game.FixedTick({});Check(game.World()->Error().empty(),game.World()->Error());}
void Place(GameModule& game,glm::vec2 position){auto id=game.PlayerEntity();game.Get<Transform>(id).position=position;game.Get<Motion>(id).velocity={};game.Get<Player>(id)={};}
std::vector<ECS::EntityID> Temporary(GameModule& game,const std::string& suffix){
    std::vector<ECS::EntityID> ids;
    for(const auto& v:game.Views())if(v.behavior->stableId.starts_with("delayed:")&&v.behavior->stableId.ends_with(suffix))ids.push_back(v.id);
    return ids;
}
Json::Value Entity(const char* prefab,glm::vec2 position,glm::vec2 size){
    Json::Value e;e["kind"]="Entity";e["prefab"]=prefab;e["position"][0]=position.x;e["position"][1]=position.y;
    e["size"][0]=size.x;e["size"][1]=size.y;e["properties"]=Json::Value(Json::objectValue);return e;
}
}
int main(){try{
    const auto assets=std::filesystem::path(IWANNA_ASSETS);
    auto root=std::filesystem::current_path()/"delayed_traps_test_generated";
    std::filesystem::create_directories(root/"prefabs");std::filesystem::create_directories(root/"rooms");
    for(const auto* name:{"explosion_effect","laser_effect","delayed_explosion_plate","delayed_laser_plate","effect_anchor","effect_warning"})
        Content::Write(root/"prefabs"/(std::string(name)+".prefab.json"),Content::Read(assets/"Workshop/prefabs"/(std::string(name)+".prefab.json")));
    Json::Value room;room["format"]="IWANNA_ROOM_2";room["title"]="Delayed trap fixture";room["hint"]="";
    room["grid"]["width"]=64;room["grid"]["height"]=20;room["grid"]["tileSize"]=2.5;
    room["grid"]["origin"][0]=-80;room["grid"]["origin"][1]=-25;room["palette"]["1"]="terrain_moss_00.png";
    for(int y=0;y<20;++y)for(int x=0;x<64;++x)room["tileLayers"]["Terrain"][y].append(y>=18?1:0);
    auto& e=room["entities"];
    e["start"]["kind"]="Spawn";e["start"]["position"][0]=-60;e["start"]["position"][1]=18.9;e["start"]["properties"]=Json::Value(Json::objectValue);
    e["blast_plate"]=Entity("delayed_explosion_plate",{-40,19.55},{2.5,.9});
    e["laser_plate"]=Entity("delayed_laser_plate",{-25,19.55},{2.5,.9});
    e["blast_center"]=Entity("effect_anchor",{0,10},{2,2});
    e["laser_center"]=Entity("effect_anchor",{30,10},{2,2});
    e["animation_probe"]=Entity("explosion_effect",{-70,-10},{8,8});
    for(const auto* id:{"blast_plate","laser_plate"}){
        auto& p=e[id]["properties"];p["effectAnchor"]=std::string(id)=="blast_plate"?"blast_center":"laser_center";
        p["delay"]=.30;p["damageStart"]=.10;p["damageDuration"]=.20;p["visualDuration"]=.75;p["cooldown"]=.20;
    }
    e["blast_plate"]["properties"]["effectWidth"]=12;e["blast_plate"]["properties"]["effectHeight"]=12;
    e["laser_plate"]["properties"]["effectWidth"]=3;e["laser_plate"]["properties"]["effectHeight"]=20;
    e["laser_plate"]["properties"]["effectRotation"]=90;
    std::ifstream source(assets/"Examples/delayed_traps.lua");std::ostringstream text;text<<source.rdbuf();Check(!text.str().empty(),"Lua trap source exists");
    room["scriptLua"]="local traps=(function()\n"+text.str()+R"(
end)()
local on_timer=traps.on_timer
traps.on_enter=function(ctx)
    ctx:set_visible('animation_probe',false)
    ctx:after(0.1,'probe_animation_reset')
end
traps.on_timer=function(ctx,event)
    if event.name=='probe_animation_reset' then
        ctx:restart_animation('animation_probe')
        ctx:set_visible('animation_probe',true)
    else on_timer(ctx,event) end
end
return traps
)";
    Json::Value world;world["format"]="IWANNA_WORLD_2";world["prefabDirectory"]="prefabs";world["roomDirectory"]="rooms";world["startRoom"]="arena";world["startSpawn"]="start";
    Content::Write(root/"world.json",world);Content::Write(root/"rooms/arena.room.json",room);
    auto other=room;other["entities"].removeMember("blast_plate");other["entities"].removeMember("laser_plate");Content::Write(root/"rooms/other.room.json",other);
    GameModule game(assets,{},root/"world.json");Check(game.Startup(),game.Error());
    std::vector<std::string> sounds;game.playSound=[&](const std::string& sound){sounds.push_back(sound);};
    auto reset=[&]{if(game.GetState()!=State::Playing){game.Restart();Tick(game);}game.World()->RequestRoom("arena","start");Tick(game);sounds.clear();};
    auto arm=[&](const char* id){auto p=game.Get<Transform>(game.Find(id)).position;p.y=18.9;Place(game,p);Tick(game);Place(game,{-60,18.9});};
    auto waitActive=[&]{for(int i=0;i<180;++i){Tick(game);auto effects=Temporary(game,":effect");for(auto id:effects)if(game.Get<Collider>(id).enabled)return id;}throw std::runtime_error("damage window never opened");};
    arm("blast_plate");Check(Temporary(game,":warning").size()==1&&Temporary(game,":effect").empty(),"trigger arms warning without immediate damage");
    // Exiting and touching the same plate while busy cannot stack another task.
    Tick(game,2);arm("blast_plate");Check(Temporary(game,":warning").size()==1,"busy pressure plate deduplicates reentry");
    reset();arm("blast_plate");
    int visual=-1,opening=-1,closing=-1,removed=-1;bool sawActive=false;
    for(int i=1;i<=170;++i){
        Tick(game);auto effects=Temporary(game,":effect");
        if(!effects.empty()){
            auto id=effects.front();auto& sprite=game.Get<Sprite>(id);
            Check(effects.size()==1&&game.Get<Behavior>(id).role==Role::Hazard,"effect is one temporary hazard");
            Check(sprite.frames.size()==24&&!sprite.loop&&sprite.masks->size()==24,"explosion has per-frame masks and plays once");
            if(visual<0)visual=i;
            if(game.Get<Collider>(id).enabled){if(opening<0)opening=i;sawActive=true;}
            else if(sawActive&&closing<0){closing=i;Check(sprite.visible,"visual tail remains after collider closes");}
        }else if(visual>=0&&removed<0)removed=i;
    }
    Check(visual>=35&&visual<=38,"visual begins after configured arming delay");
    Check(opening-visual>=11&&opening-visual<=14,"damageStart delays collision independently of visuals");
    Check(closing-opening>=23&&closing-opening<=26,"explosion damage is a finite 0.2-second window");
    Check(removed-visual>=89&&removed-visual<=93&&Temporary(game,":warning").empty(),"visual cleanup task runs independently of damage window");
    Check(sounds.size()==1&&sounds[0]=="trap_explosion","explosion emits the supplied sound once");
    auto probe=game.Find("animation_probe");auto& probeSprite=game.Get<Sprite>(probe);
    Check(probeSprite.elapsed==probeSprite.duration&&AnimationFrame(probeSprite)==23,"actual animation system clamps a one-shot animation to its last frame");
    Check(probeSprite.visible&&!game.Get<Collider>(probe).enabled,"Lua visibility change does not accidentally enable collision");
    arm("blast_plate");Check(Temporary(game,":warning").size()==1,"cooldown completes and plate can rearm");
    reset();arm("blast_plate");
    // Standing inside the effect before/after the damage window is harmless.
    for(int i=0;i<39;++i){Place(game,{0,10});Tick(game);Check(game.GetState()==State::Playing,"warning and startup are nonlethal");}
    Place(game,{-60,18.9});auto blast=waitActive();auto p=game.Get<Transform>(blast).position;
    Place(game,p);Tick(game);Check(game.GetState()==State::Dead,"current-frame explosion mask damages during active window");
    Check(Temporary(game,":effect").empty()&&Temporary(game,":warning").empty(),"death cancels tasks and removes damage/visual entities");
    game.Restart();Tick(game,180);Check(Temporary(game,":effect").empty()&&game.World()->Error().empty(),"restart has no stale timers or effects");
    reset();arm("blast_plate");waitActive();
    for(int i=0;i<40;++i)Tick(game);
    Check(!Temporary(game,":effect").empty(),"explosion tail is still present for harmless-tail test");
    Place(game,{0,10});Tick(game);Check(game.GetState()==State::Playing,"explosion tail cannot hurt after window closes");
    reset();arm("laser_plate");auto laser=waitActive();
    auto& beam=game.Get<Sprite>(laser);Check(beam.loop&&beam.frames.size()==8&&beam.masks->size()==8,"laser loops extracted frames with matching masks");
    Check(game.Get<Transform>(laser).size==glm::vec2(3,20)&&game.Get<Transform>(laser).rotation==90,"Lua config controls laser dimensions and orientation");
    Place(game,{38,10});Tick(game);Check(game.GetState()==State::Dead,"rotated laser uses transformed animated mask");
    reset();arm("blast_plate");arm("laser_plate");Check(Temporary(game,":warning").size()==2,"independent traps arm concurrently");
    Tick(game,40);Check(Temporary(game,":effect").size()==2,"asynchronous tasks progress without blocking each other");
    game.World()->RequestRoom("other","start");Tick(game,180);
    Check(Temporary(game,":effect").empty()&&Temporary(game,":warning").empty(),"changing rooms discards pending old-room tasks");
    // Audio files must decode in the existing cross-platform PCM runtime.
    Audio::Clip::LoadWav(assets/"audio/trap_explosion.wav");Audio::Clip::LoadWav(assets/"audio/trap_laser.wav");
    // A non-looping sprite holds its final mask; looping maps back to frame 0.
    Sprite sample;sample.frames={2,0,1};sample.duration=.3f;sample.elapsed=.9f;sample.loop=false;
    Check(AnimationFrame(sample)==1,"non-loop animation holds final frame");sample.loop=true;sample.elapsed=.3f;Check(AnimationFrame(sample)==2,"loop animation wraps through authored sequence");
    auto invalid=Content::Read(root/"prefabs/explosion_effect.prefab.json");invalid["animation"]["frames"][0]=99999;Content::Write(root/"prefabs/explosion_effect.prefab.json",invalid);
    bool rejected=false;try{RoomCatalog::Load(root/"world.json");}catch(...){rejected=true;}Check(rejected,"out-of-atlas animation frame is rejected");
    std::cout<<"Delayed traps PASS: warning, async phases, finite damage, dedup/rearm, animated masks, rotated laser, death/room cancellation, supplied audio\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
