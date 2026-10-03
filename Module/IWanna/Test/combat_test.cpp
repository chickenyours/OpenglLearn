#include "IWanna/Public/game_module.h"
#include "IWanna/Public/content_format.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <set>
#include <fstream>
#include <sstream>
using namespace IWanna;
static void Check(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
static std::vector<ECS::EntityID> Burst(GameModule& game) {
    std::vector<ECS::EntityID> result;
    for(const auto& v:game.Views())if(v.behavior->stableId.starts_with("burst:"))result.push_back(v.id);
    return result;
}
static void Tick(GameModule& game,int count,Input input={}) {
    for(int i=0;i<count;++i){game.FixedTick(input);input.shoot=false;}
    Check(game.World()->Error().empty(),game.World()->Error());
}
static Json::Value Entity(const char* prefab,float x,float y,float w,float h) {
    Json::Value e;e["kind"]="Entity";e["prefab"]=prefab;e["position"][0]=x;e["position"][1]=y;e["size"][0]=w;e["size"][1]=h;e["properties"]=Json::Value(Json::objectValue);return e;
}
static void Place(GameModule& game,float x,float y) {
    const auto id=game.PlayerEntity();game.Get<Transform>(id).position={x,y};game.Get<Motion>(id).velocity={};game.Get<Player>(id)={};
}
int main(int argc,char** argv){try {
    const auto assets=std::filesystem::path(IWANNA_ASSETS);
    const auto root=std::filesystem::current_path()/"combat_test_generated";
    std::filesystem::create_directories(root/"prefabs");std::filesystem::create_directories(root/"rooms");
    for(const auto* name:{"shot_switch","ring_plate","ring_anchor","ring_apple","platform"})
        Content::Write(root/"prefabs"/(std::string(name)+".prefab.json"),Content::Read(assets/"Workshop/prefabs"/(std::string(name)+".prefab.json")));
    Json::Value room;room["format"]="IWANNA_ROOM_2";room["title"]="Combat fixture";room["hint"]="";
    room["grid"]["width"]=24;room["grid"]["height"]=12;room["grid"]["tileSize"]=2.5;room["grid"]["origin"][0]=-30;room["grid"]["origin"][1]=-15;
    room["palette"]["1"]="terrain_moss_00.png";
    for(int y=0;y<12;++y)for(int x=0;x<24;++x) {
        room["tileLayers"]["Terrain"][y].append(y>=10||y==0?1:0);
        room["tileLayers"]["ShotGate"][y].append(x==14&&y>0&&y<10?1:0);
    }
    auto& entities=room["entities"];
    entities["start"]["kind"]="Spawn";entities["start"]["position"][0]=-22;entities["start"]["position"][1]=8.9;entities["start"]["properties"]=Json::Value(Json::objectValue);
    entities["switch"]=Entity("shot_switch",0,8.8,2,2);
    entities["plate"]=Entity("ring_plate",-15,9.55,2.5,.9);
    entities["plate"]["properties"]["spawnAnchor"]="center";
    entities["plate"]["properties"]["radius"]=2;entities["plate"]["properties"]["speed"]=10;entities["plate"]["properties"]["lifetime"]=.35;
    entities["center"]=Entity("ring_anchor",18,-3,2,2);
    std::ifstream exampleFile(assets/"Examples/apple_ring.lua");std::ostringstream exampleText;exampleText<<exampleFile.rdbuf();
    Check(exampleFile.good()&&!exampleText.str().empty(),"canonical Lua ring example exists");
    room["scriptLua"]="local ring = (function()\n"+exampleText.str()+"\nend)()\n"+R"(return {
      on_hit=function(ctx,e) if e.name=='open_gate' and ctx:once(e.id) then
        ctx:set_group_enabled(e.properties.wallGroup,false);ctx:set_enabled(e.id,false)
      end end,
      on_trigger=function(ctx,e)
        ring.on_trigger(ctx,e)
        if e.name=='apple_ring' and e.phase=='enter' and ctx:once('early:'..e.id) then
          ctx:after(.05,'remove_early','burst:'..e.id..':1:0')
        end
      end,
      on_timer=function(ctx,e)
        if e.name=='remove_early' then ctx:destroy(e.id) else ring.on_timer(ctx,e) end
      end
    })";
    Json::Value world;world["format"]="IWANNA_WORLD_2";world["prefabDirectory"]="prefabs";world["roomDirectory"]="rooms";world["startRoom"]="arena";world["startSpawn"]="start";
    Content::Write(root/"world.json",world);Content::Write(root/"rooms/arena.room.json",room);
    GameModule game(assets,{},root/"world.json");Check(game.Startup(),game.Error());
    auto gateEnabled=[&]{for(const auto& v:game.Views())if(v.behavior->group=="tiles:ShotGate")return v.collider->enabled;return false;};
    Place(game,0,8.9);Tick(game,2);Check(gateEnabled(),"touching a receiver must not remove the wall");
    game.Restart();Tick(game,25);
    Input shoot;shoot.shoot=true;Tick(game,80,shoot);
    Check(!gateEnabled()&&!game.Get<Collider>(game.Find("switch")).enabled,"actual projectile hit opens tile gate");
    game.Restart();Tick(game,25);Check(gateEnabled(),"reset restores gate and receiver");
    shoot.shootHeld=true;Tick(game,61,shoot);int shots=0;
    for(const auto& v:game.Views())shots+=v.behavior->role==Role::Projectile;
    Check(shots>0&&shots<=5,"held fire respects cooldown");
    shoot={};shoot.left=true;shoot.shoot=true;Tick(game,1,shoot);
    bool leftShot=false;for(const auto& v:game.Views())if(v.behavior->role==Role::Projectile&&v.motion->velocity.x<0)leftShot=true;
    // A previous held shot may still be cooling down; keep holding left/fire.
    shoot.shootHeld=true;Tick(game,20,shoot);
    for(const auto& v:game.Views())if(v.behavior->role==Role::Projectile&&v.motion->velocity.x<0)leftShot=true;
    Check(leftShot,"shot direction follows the current facing direction");
    Place(game,-24,8.9);Tick(game,250);for(const auto& v:game.Views())Check(v.behavior->role!=Role::Projectile,"projectile lifetime cleans up missed shots");
    for(int attempt=0;attempt<30;++attempt) {
        game.Restart();Tick(game,1);
        const glm::vec2 expectedCenter{18.f+attempt*.2f,-3.f};
        game.Get<Transform>(game.Find("center")).position=expectedCenter;
        if(attempt==0) {Tick(game,360);Check(game.Find("plate")&&game.Get<Collider>(game.Find("plate")).enabled,"untriggered plate survives longer than its burst lifetime");}
        Place(game,-15,8.9);Tick(game,1);
        const auto apples=Burst(game);Check(apples.size()>=12&&apples.size()<=24,"random ring has 12-24 apples inclusive");
        const auto center=game.Get<Transform>(game.Find("center")).position;
        Check(center==expectedCenter,"Lua reads the live anchor transform, not authored coordinates");
        std::set<int> angles;
        for(auto id:apples) {
            Check(game.Get<Behavior>(id).role==Role::Hazard,"ring creates apple hazards, never copies emitting pressure plate");
            Check(game.Get<Sprite>(id).image=="demo_apple.png","ring uses apple artwork");
            Check(game.Get<Behavior>(id).event.empty(),"generated apples cannot recursively emit rings");
            auto delta=game.Get<Transform>(id).position-center,velocity=game.Get<Motion>(id).velocity;
            Check(std::abs(glm::length(delta)-2)<.001f&&std::abs(glm::length(velocity)-10)<.001f,"configured radial radius and speed");
            Check(glm::dot(glm::normalize(delta),glm::normalize(velocity))>.999f,"apple moves away from the anchor");
            angles.insert(int(std::round(std::atan2(delta.y,delta.x)*10000)));
        }
        Check(angles.size()==apples.size(),"Lua ring directions are distinct");
        for(size_t i=0;i<apples.size();++i) {
            const auto a=glm::normalize(game.Get<Motion>(apples[i]).velocity);
            const auto b=glm::normalize(game.Get<Motion>(apples[(i+1)%apples.size()]).velocity);
            Check(std::abs(glm::dot(a,b)-std::cos(6.28318530718/apples.size()))<.0001,"Lua ring directions have equal angular spacing");
        }
        // Another Lua timer removes one apple early; the ring cleanup must tolerate it.
        Tick(game,10);Check(Burst(game).size()==apples.size()-1,"plate triggers once; unrelated Lua rule can remove an apple early");
        for(const auto id:Burst(game))Check(glm::length(game.Get<Transform>(id).position-center)>2.7f,"Lua-set velocity expands ring during physics ticks");
        Tick(game,40);Check(Burst(game).empty(),"apple lifetime deletes entities and refreshes ECS views");
    }
    game.Restart();Tick(game,1);Place(game,-15,8.9);Tick(game,1);
    const auto applePosition=game.Get<Transform>(Burst(game).front()).position;
    Place(game,applePosition.x,applePosition.y);Tick(game,1);
    Check(game.GetState()==State::Dead,"Lua-generated apples retain lethal hazard collision");
    // A thin blocker must prevent hitting a receiver behind it, even at 500 units/s.
    room["entities"]["blocker"]=Entity("platform",-10,8.8,.05,3);
    Content::Write(root/"rooms/arena.room.json",room);
    auto config=Content::Read(assets/"gameplay.json");config["player"]["shooting"]["speed"]=500;
    Content::Write(root/"gameplay.json",config);
    GameModule blocked(assets,root/"gameplay.json",root/"world.json");Check(blocked.Startup(),blocked.Error());
    Tick(blocked,25);shoot={};shoot.shoot=true;Tick(blocked,30,shoot);
    Check(blocked.Get<Collider>(blocked.Find("switch")).enabled,"high-speed projectile cannot tunnel through a thin wall");
    // New Lua arguments report a protected failure without committing bad commands.
    Scripting::LuaModule lua;
    Check(lua.LoadSource("return {on_enter=function(c) c:spawn_radial('ring_apple','bad',0,0,{minCount=24,maxCount=12}) end}","invalid radial"),"invalid fixture loads");
    lua.Call({"on_enter","","",""});Check(!lua.Error().empty(),"invalid radial count is rejected");
    if(argc==2) {
        const auto project=std::filesystem::path(argv[1]);GameModule demo(project,{},project/"world.json");Check(demo.Startup(),demo.Error());
        Tick(demo,20);Place(demo,9,18.9);Tick(demo,20);shoot={};shoot.shoot=true;Tick(demo,30,shoot);
        Check(!demo.Get<Collider>(demo.Find("shot_switch_01")).enabled,"MyIwana switch can be hit from the safe approach");
        for(const auto& v:demo.Views())if(v.behavior->group=="tiles:ShotGate")Check(!v.collider->enabled,"all actual gate tiles disappear");
        // Walk across the opened wall to prove the change affects physics.
        shoot={};shoot.right=true;Tick(demo,30,shoot);
        Check(demo.GetState()==State::Playing&&demo.Get<Transform>(demo.PlayerEntity()).position.x>12.5f,"opened MyIwana wall is traversable");
        demo.Restart();Tick(demo,360);Place(demo,206.25f,18.9f);Tick(demo,1);
        Check(Burst(demo).size()>=12&&Burst(demo).size()<=24,"authored MyIwana plate immediately spawns a ring");
        for(auto id:Burst(demo)) {
            Check(demo.Get<Behavior>(id).role==Role::Hazard&&demo.Get<Sprite>(id).image=="demo_apple.png","MyIwana ring contains real apple hazards");
            Check(demo.Get<Behavior>(id).event.empty(),"MyIwana apples do not inherit emitter events or anchor references");
        }
        // Retreat out of the ring's reach, wait for cleanup, and continue.
        shoot={};shoot.left=true;Tick(demo,25,shoot);Tick(demo,290);
        Check(demo.GetState()==State::Playing&&Burst(demo).empty(),"MyIwana apple ring has a survivable retreat and clears");
        demo.Restart();Tick(demo,1);Place(demo,206.25f,18.9f);Tick(demo,1);
        const auto actualApplePosition=demo.Get<Transform>(Burst(demo).front()).position;
        Place(demo,actualApplePosition.x,actualApplePosition.y);Tick(demo,1);
        Check(demo.GetState()==State::Dead,"MyIwana ring is made of lethal apples and cannot recurse into pressure-plate events");
        std::cout<<"MyIwana gate opening/crossing and radial-trap escape PASS\n";
    }
    std::cout<<"Combat: hit-only receivers, tile groups, cooldown/facing, occlusion, random rings, motion, expiry/reset PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
