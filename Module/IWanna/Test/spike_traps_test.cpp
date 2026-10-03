#include "IWanna/Public/game_module.h"
#include "IWanna/Public/content_format.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
using namespace IWanna;
namespace {
void Check(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
void Tick(GameModule& game,int frames=1) {
    for(int i=0;i<frames;++i)game.FixedTick({});
    Check(game.World()->Error().empty(),game.World()->Error());
}
void Place(GameModule& game,glm::vec2 point) {
    const auto id=game.PlayerEntity();game.Get<Transform>(id).position=point;
    game.Get<Motion>(id).velocity={};game.Get<Player>(id)={};
}
Json::Value Entity(const char* prefab,glm::vec2 position,glm::vec2 size) {
    Json::Value value;value["kind"]="Entity";value["prefab"]=prefab;
    value["position"][0]=position.x;value["position"][1]=position.y;
    value["size"][0]=size.x;value["size"][1]=size.y;value["properties"]=Json::Value(Json::objectValue);return value;
}
std::vector<ECS::EntityID> Temporary(GameModule& game,bool hazards) {
    std::vector<ECS::EntityID> ids;
    for(const auto& view:game.Views())if(view.behavior->stableId.starts_with("spike:")&&
        (view.behavior->role==Role::Hazard)==hazards)ids.push_back(view.id);
    return ids;
}
glm::vec2 Direction(const Transform& t) {
    const float angle=t.rotation*.01745329252f;return {std::sin(angle),-std::cos(angle)};
}
}
int main(){try{
    const auto assets=std::filesystem::path(IWANNA_ASSETS);
    const auto root=std::filesystem::current_path()/"spike_traps_test_generated";
    std::filesystem::create_directories(root/"prefabs");std::filesystem::create_directories(root/"rooms");
    for(const auto* name:{"floor_spike_trap","left_spike_trap","flying_spike_trap","spike_trap_effect","spike_trap_warning","effect_anchor"})
        Content::Write(root/"prefabs"/(std::string(name)+".prefab.json"),Content::Read(assets/"Workshop/prefabs"/(std::string(name)+".prefab.json")));
    Json::Value room;room["format"]="IWANNA_ROOM_2";room["title"]="Lua spike fixture";room["hint"]="";
    room["grid"]["width"]=64;room["grid"]["height"]=20;room["grid"]["tileSize"]=2.5;
    room["grid"]["origin"][0]=-80;room["grid"]["origin"][1]=-25;room["palette"]["1"]="terrain_moss_00.png";
    for(int y=0;y<20;++y)for(int x=0;x<64;++x)room["tileLayers"]["Terrain"][y].append(y>=18?1:0);
    auto& e=room["entities"];
    e["start"]["kind"]="Spawn";e["start"]["position"][0]=-60;e["start"]["position"][1]=18.9;
    e["floor_plate"]=Entity("floor_spike_trap",{-45,18.75},{2,2.5});
    e["left_plate"]=Entity("left_spike_trap",{-35,18.75},{2,2.5});
    e["fly_plate"]=Entity("flying_spike_trap",{-25,18.75},{2,2.5});
    e["floor_root"]=Entity("effect_anchor",{0,12},{1,1});
    e["left_root"]=Entity("effect_anchor",{30,8},{1,1});
    e["fly_root"]=Entity("effect_anchor",{55,-2},{1,1});
    for(const auto* id:{"floor_plate","left_plate","fly_plate"}) {
        auto& p=e[id]["properties"];p["detectionEnabled"]=true;p["detectionX"]=0;p["detectionY"]=0;p["detectionW"]=2;p["detectionH"]=2.5;
    }
    for(const auto* id:{"floor_plate","left_plate"}) {
        auto& p=e[id]["properties"];const bool floor=std::string(id)=="floor_plate";
        p["effectAnchor"]=floor?"floor_root":"left_root";p["orientation"]=floor?"up":"left";
        p["restLength"]=2;p["length"]=floor?12:18;p["thickness"]=2.5;
        p["extendSeconds"]=.25;p["holdSeconds"]=.15;p["retractSeconds"]=.25;p["delay"]=.15;p["cooldown"]=.2;
    }
    auto& flight=e["fly_plate"]["properties"];flight["effectAnchor"]="fly_root";flight["orientation"]="left";
    flight["laneCount"]=3;flight["laneSpacing"]=3;flight["count"]=1;flight["speed"]=30;flight["warningSeconds"]=.25;
    flight["lifetime"]=.65;flight["flightDistance"]=12;flight["cooldown"]=.2;
    std::ifstream source(assets/"Examples/spike_traps.lua");std::ostringstream luaSource;luaSource<<source.rdbuf();
    Check(!luaSource.str().empty(),"canonical spike Lua source exists");room["scriptLua"]=luaSource.str();
    Json::Value world;world["format"]="IWANNA_WORLD_2";world["prefabDirectory"]="prefabs";world["roomDirectory"]="rooms";
    world["startRoom"]="arena";world["startSpawn"]="start";
    Content::Write(root/"world.json",world);Content::Write(root/"rooms/arena.room.json",room);
    auto other=room;other["entities"].removeMember("floor_plate");other["entities"].removeMember("left_plate");other["entities"].removeMember("fly_plate");
    Content::Write(root/"rooms/other.room.json",other);
    auto budget=other;budget["scriptLua"]="local traps=(function()\n"+luaSource.str()+R"(
end)()
traps.on_enter=function(ctx)
    for i=1,40 do
        traps.on_trigger(ctx,{name='telescopic_spike',id='budget:'..i,phase='enter',properties={
            effectAnchor='floor_root',orientation='up',restLength=0.25,length=2,thickness=0.5,
            delay=0.1,extendSeconds=0.1,holdSeconds=0.1,retractSeconds=0.1,cooldown=0.1}})
    end
end
return traps
)";
    Content::Write(root/"rooms/budget.room.json",budget);
    GameModule game(assets,{},root/"world.json");Check(game.Startup(),game.Error());
    auto reset=[&] {
        if(game.GetState()!=State::Playing){game.Restart();Tick(game);}
        game.World()->RequestRoom("arena","start");Tick(game);
    };
    auto arm=[&](const char* id) {
        const auto point=game.Get<Transform>(game.Find(id)).position;
        Place(game,{point.x,18.9f});Tick(game);Place(game,{-60,18.9});
    };
    auto waitHazard=[&] {
        for(int i=0;i<180;++i){const auto ids=Temporary(game,true);if(!ids.empty())return ids.front();Tick(game);}
        throw std::runtime_error("spike task did not create a hazard");
    };
    auto checkExtension=[&](const char* plate,glm::vec2 rootPoint,glm::vec2 direction,float length) {
        reset();arm(plate);Check(Temporary(game,true).empty(),"telescopic spike waits before creating damage");
        Check(!Temporary(game,false).empty(),"telescopic spike gives a visible warning");
        Tick(game,1);arm(plate);
        Check(Temporary(game,false).size()==1,"busy telescopic trigger deduplicates repeated entry");
        float peak=0,minimumAfterPeak=length;bool retracted=false,seen=false;int first=-1,removed=-1;
        for(int i=1;i<=180;++i) {
            Tick(game);const auto ids=Temporary(game,true);
            Check(ids.size()<=1,"one extending spike per active trap task");
            if(ids.empty()){if(seen&&removed<0)removed=i;continue;}
            const auto id=ids.front();const auto& t=game.Get<Transform>(id);seen=true;if(first<0)first=i;
            Check(game.Get<Collider>(id).enabled,"extension and retraction use a live precise hazard mask");
            Check(glm::length(Direction(t)-direction)<.001f,"orientation sets the correct outward spike direction");
            Check(std::abs(t.size.x-2.5f)<.001f&&t.size.y>=1.99f&&t.size.y<=length+.001f,"extension respects configured thickness and length limits");
            const auto base=t.position-direction*(t.size.y*.5f);
            // Lua compensates the half-length offset using velocity on the
            // following physics step; the root may lag by one half-increment.
            Check(glm::length(base-rootPoint)<.5f,"telescopic root remains within one bounded Lua step of its anchor");
            peak=std::max(peak,t.size.y);
            if(peak>=length-.01f){minimumAfterPeak=std::min(minimumAfterPeak,t.size.y);if(t.size.y<length-1)retracted=true;}
        }
        Check(first>=13&&first<=18,"telescopic activation respects the configured warning delay");
        Check(peak>=length-.01f&&retracted&&minimumAfterPeak<=3,"spike reaches full length and visibly retracts");
        Check(removed>=first+70&&removed<=first+85&&Temporary(game,false).empty(),"retracted spike and warning are cleaned up after the phase sequence");
        arm(plate);Check(!Temporary(game,false).empty(),"cooldown permits a new independent trigger cycle");
    };
    checkExtension("floor_plate",{0,12},{0,-1},12);
    checkExtension("left_plate",{30,8},{-1,0},18);
    reset();arm("floor_plate");
    // The future tip region remains safe during the warning, then becomes
    // lethal once the real alpha mask extends across it.
    Place(game,{0,5});Tick(game,8);Check(game.GetState()==State::Playing,"warning phase cannot harm the player");Place(game,{-60,18.9});
    auto hazard=waitHazard();
    const auto effectName=game.Get<Behavior>(hazard).stableId;
    for(int i=0;i<60&&game.Get<Transform>(hazard).size.y<11.9f;++i){Tick(game);hazard=game.Find(effectName);Check(hazard!=0,"extension keeps an effect instance until retraction finishes");}
    Place(game,game.Get<Transform>(hazard).position);Tick(game);Check(game.GetState()==State::Dead,"fully extended spike uses lethal collision");
    Check(Temporary(game,true).empty()&&Temporary(game,false).empty(),"death cancels spikes and warning entities");
    game.Restart();Tick(game,180);Check(Temporary(game,true).empty()&&Temporary(game,false).empty(),"reset does not resume old spike timers");
    reset();arm("floor_plate");arm("left_plate");Tick(game,20);
    Check(Temporary(game,true).size()==2,"independent spike tasks can extend concurrently");
    game.World()->RequestRoom("other","start");Tick(game,180);
    Check(Temporary(game,true).empty()&&Temporary(game,false).empty(),"room changes cancel pending spike tasks");
    reset();std::set<int> lanes;
    for(int cycle=0;cycle<15;++cycle) {
        arm("fly_plate");const auto warnings=Temporary(game,false);Check(warnings.size()==1&&Temporary(game,true).empty(),"flying spike reserves one warning before launch");
        const auto warningPosition=game.Get<Transform>(warnings.front()).position;
        const float lane=(warningPosition.y+2)/3;Check(std::abs(lane-std::round(lane))<.001f&&lane>=-.001f&&lane<=2.001f,"random warning uses only authored corridor lanes");lanes.insert(int(std::round(lane)));
        Tick(game,12);Check(Temporary(game,true).empty(),"flying spike provides its full warning interval");
        hazard=waitHazard();const auto launched=game.Get<Transform>(hazard).position;const auto velocity=game.Get<Motion>(hazard).velocity;
        Check(std::abs(launched.y-warningPosition.y)<.001f,"flying spike follows the lane that was warned");
        Check(std::abs(velocity.x+30)<.001f&&std::abs(velocity.y)<.001f,"flying spike moves horizontally at the configured speed");
        Check(game.Get<Collider>(hazard).enabled,"launched flying spike is harmful");
        const auto shotName=game.Get<Behavior>(hazard).stableId;Tick(game,12);hazard=game.Find(shotName);
        Check(hazard&&game.Get<Transform>(hazard).position.x<launched.x-2.9f,"flying spike advances through the corridor");
        for(int i=0;i<125;++i) {
            Tick(game);hazard=game.Find(shotName);
            if(hazard)Check(game.Get<Transform>(hazard).position.x>=launched.x-12.5f,"flight distance bounds the horizontal travel");
        }
        Check(Temporary(game,true).empty()&&Temporary(game,false).empty(),"flight lifespan and cooldown finish without accumulating entities");
    }
    Check(lanes.size()>1,"random flight task varies its lane over repeated activations");
    arm("fly_plate");auto warnings=Temporary(game,false);Check(warnings.size()==1,"near-spawn safety fixture has a warning");
    auto sourcePoint=game.Get<Transform>(game.Find("fly_root")).position;sourcePoint.y=game.Get<Transform>(warnings.front()).position.y;
    for(int i=0;i<45;++i){Place(game,sourcePoint);Tick(game);Check(game.GetState()==State::Playing&&Temporary(game,true).empty(),"launch is skipped when it would appear directly on the player");}
    Place(game,{-60,18.9});Tick(game,180);arm("fly_plate");hazard=waitHazard();
    Place(game,game.Get<Transform>(hazard).position);Tick(game);
    Check(game.GetState()==State::Dead&&Temporary(game,true).empty()&&Temporary(game,false).empty(),"flying spike is lethal in flight and death cancels its task");
    reset();game.World()->RequestRoom("budget","start");Tick(game);
    Check(Temporary(game,false).size()==8,"burst requests stop at the shared eight-task budget");
    for(int i=0;i<180;++i) {
        Tick(game);Check(Temporary(game,true).size()<=8&&Temporary(game,false).size()<=8,"task cap bounds temporary entity growth");
    }
    Check(Temporary(game,true).empty()&&Temporary(game,false).empty(),"budget stress completes without leaking timers or entities");
    for(const auto* invalid:{"all_lanes","no_gap","short_warning","vertical_flight"}) {
        Scripting::LuaModule lua;Check(lua.LoadSource(luaSource.str(),"spike_validation"),lua.Error());
        lua.queryPosition=[](const std::string&)->std::optional<Scripting::LuaModule::Position>{return Scripting::LuaModule::Position{0,0};};
        Scripting::Event event{"on_trigger","flying_spikes","sensor","enter"};
        event.properties["length"]=2.8;event.properties["orientation"]=std::string("left");
        event.properties["laneCount"]=3.0;event.properties["count"]=1.0;
        if(std::string(invalid)=="all_lanes")event.properties["count"]=3.0;
        else if(std::string(invalid)=="no_gap"){event.properties["thickness"]=2.5;event.properties["laneSpacing"]=3.0;}
        else if(std::string(invalid)=="short_warning")event.properties["warningSeconds"]=.1;
        else event.properties["orientation"]=std::string("up");
        Check(!lua.Call(event)&&lua.TakeCommands().empty(),"unsafe flying spike settings are rejected before a spawn command is committed");
    }
    std::cout<<"Spike traps PASS: warnings, Lua extend/retract root tracking, lethal masks, dedup/cooldown, reset isolation, random warned flight lanes and bounded cleanup\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
