#include "IWanna/Public/game_module.h"
#include "IWanna/Public/content_format.h"
#include <fstream>
#include <sstream>
#include <set>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace IWanna;
namespace {
void Check(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
void Tick(GameModule& game,int count=1,Input input={}) {
    for(int i=0;i<count;++i){game.FixedTick(input);input.shoot=false;}
    Check(game.World()->Error().empty(),game.World()->Error());
}
void Place(GameModule& game,glm::vec2 position) {
    const auto id=game.PlayerEntity();game.Get<Transform>(id).position=position;
    game.Get<Motion>(id).velocity={};game.Get<Player>(id)={};
}
std::vector<ECS::EntityID> Actors(GameModule& game) {
    std::vector<ECS::EntityID> result;
    for(const auto& v:game.Views())if(v.behavior->event=="apple_monster_actor")result.push_back(v.id);
    return result;
}
std::vector<ECS::EntityID> Shots(GameModule& game) {
    std::vector<ECS::EntityID> result;
    for(const auto& v:game.Views())if(v.behavior->stableId.starts_with("apple_monster:")&&
        v.behavior->stableId.find(":shot:")!=std::string::npos)result.push_back(v.id);
    return result;
}
size_t Bars(GameModule& game) {
    size_t n=0;for(const auto& v:game.Views())if(v.behavior->stableId.starts_with("apple_monster:")&&
        v.behavior->stableId.find(":bar:")!=std::string::npos)++n;return n;
}
float Fill(GameModule& game) {
    for(const auto& v:game.Views())if(v.sprite->image=="apple_health_yellow.png")return v.transform->size.x;
    return 0;
}
Json::Value Entity(const char* prefab,glm::vec2 position,glm::vec2 size) {
    Json::Value v;v["kind"]="Entity";v["prefab"]=prefab;
    v["position"][0]=position.x;v["position"][1]=position.y;
    v["size"][0]=size.x;v["size"][1]=size.y;
    v["properties"]=Json::Value(Json::objectValue);return v;
}
}
int main(){try{
    const auto assets=std::filesystem::path(IWANNA_ASSETS);
    const auto root=std::filesystem::current_path()/"apple_monster_test_generated";
    std::filesystem::create_directories(root/"rooms");std::filesystem::create_directories(root/"prefabs");
    for(const auto* name:{"apple_monster_trap","apple_monster_actor","apple_monster_shot",
        "apple_health_frame","apple_health_empty","apple_health_fill"})
        Content::Write(root/"prefabs"/(std::string(name)+".prefab.json"),Content::Read(assets/"Workshop/prefabs"/(std::string(name)+".prefab.json")));
    auto bullet=Content::Read(assets/"Workshop/prefabs/apple_monster_shot.prefab.json");
    bullet["role"]="projectile";bullet["image"]="demo_white.png";
    bullet["width"]=.7;bullet["height"]=.28;
    Content::Write(root/"prefabs/test_bullet.prefab.json",bullet);
    std::ifstream file(assets/"Examples/apple_monster.lua");std::ostringstream source;source<<file.rdbuf();
    Check(!source.str().empty(),"canonical apple monster Lua exists");
    Json::Value room;room["format"]="IWANNA_ROOM_2";room["title"]="Apple monster fixture";room["hint"]="";
    room["grid"]["width"]=64;room["grid"]["height"]=20;room["grid"]["tileSize"]=2.5;
    room["grid"]["origin"][0]=-80;room["grid"]["origin"][1]=-25;room["palette"]["1"]="terrain_moss_00.png";
    for(int y=0;y<20;++y)for(int x=0;x<64;++x)room["tileLayers"]["Terrain"][y].append(y>=18?1:0);
    auto& e=room["entities"];e["start"]["kind"]="Spawn";e["start"]["position"][0]=-70;e["start"]["position"][1]=18.9;
    e["plate"]=Entity("apple_monster_trap",{-45,19.55},{2.5,.9});
    auto& p=e["plate"]["properties"];p["arenaMinX"]=-70;p["arenaMaxX"]=60;p["arenaMinY"]=-15;p["arenaMaxY"]=19;
    p["wanderSpeed"]=0;p["wanderHeight"]=0;p["hoverOffset"]=0;p["firstShotDelay"]=10;p["seed"]=7;
    room["scriptLua"]=source.str();
    Json::Value world;world["format"]="IWANNA_WORLD_2";world["prefabDirectory"]="prefabs";world["roomDirectory"]="rooms";
    world["startRoom"]="damage";world["startSpawn"]="start";
    Content::Write(root/"world.json",world);Content::Write(root/"rooms/damage.room.json",room);
    auto aim=room;auto& ap=aim["entities"]["plate"]["properties"];
    ap["firstShotDelay"]=.25;ap["shotInterval"]=.5;ap["shotLifetime"]=.35;ap["shotSpeed"]=16;
    Content::Write(root/"rooms/aim.room.json",aim);
    auto wandering=room;auto& wp=wandering["entities"]["plate"]["properties"];
    wp["wanderSpeed"]=6;wp["retargetSeconds"]=.25;wp["wanderHeight"]=3;wp["hoverOffset"]=3;
    Content::Write(root/"rooms/wander.room.json",wandering);
    auto random=room;random["entities"]["plate"]["properties"]["seed"]=0;
    Content::Write(root/"rooms/random.room.json",random);
    auto empty=room;empty["entities"].removeMember("plate");Content::Write(root/"rooms/empty.room.json",empty);
    auto authored=room;
    authored["entities"]["authored_monster"]=Entity("apple_monster_actor",{20,8},{6,6});
    Content::Write(root/"rooms/authored.room.json",authored);
    auto same=room;same["scriptLua"]="local monster=(function()\n"+source.str()+R"(
end)()
local initialize=monster.on_entity_spawn
monster.on_entity_spawn=function(ctx,e)
    initialize(ctx,e)
    if e.name=='apple_monster_actor' then ctx:after(.05,'simultaneous_shots',e.id) end
end
local timer=monster.on_timer
monster.on_timer=function(ctx,e)
    if e.name=='simultaneous_shots' then
        local x,y=ctx:get_position(e.id)
        ctx:spawn('test_bullet','double:one',x-4,y)
        ctx:set_velocity('double:one',80,0)
        ctx:spawn('test_bullet','double:two',x-4,y+.05)
        ctx:set_velocity('double:two',80,0)
    else timer(ctx,e) end
end
return monster
)";Content::Write(root/"rooms/same_frame.room.json",same);
    auto budget=empty;budget["scriptLua"]="local monster=(function()\n"+source.str()+R"(
end)()
monster.on_enter=function(ctx)
    for i=1,40 do monster.on_trigger(ctx,{name='summon_apple_monster',phase='enter',id='budget:'..i,
        properties={firstShotDelay=10,wanderSpeed=0,hoverOffset=0,wanderHeight=0,
        arenaMinX=-78,arenaMaxX=70,arenaMinY=-15,arenaMaxY=19,seed=i}}) end
end
return monster
)";Content::Write(root/"rooms/budget.room.json",budget);
    auto bulletBudget=budget;
    auto budgetSource=budget["scriptLua"].asString();
    const auto delayPosition=budgetSource.find("firstShotDelay=10,wanderSpeed=0");
    Check(delayPosition!=std::string::npos,"bullet budget fixture has an encounter configuration");
    budgetSource.replace(delayPosition,std::string("firstShotDelay=10,wanderSpeed=0").size(),
        "firstShotDelay=.25,shotInterval=.4,shotLifetime=6,shotSpeed=1,wanderSpeed=0");
    bulletBudget["scriptLua"]=budgetSource;Content::Write(root/"rooms/bullet_budget.room.json",bulletBudget);
    auto authoredBudget=budget;
    for(int i=0;i<3;++i)authoredBudget["entities"]["placed:"+std::to_string(i)]=Entity("apple_monster_actor",{20.f+10*i,8},{6,6});
    authoredBudget["scriptLua"]="local monster=(function()\n"+source.str()+R"(
end)()
monster.on_enter=function(ctx) ctx:after(.1,'authored_budget','burst') end
local timer=monster.on_timer
monster.on_timer=function(ctx,e)
    if e.name=='authored_budget' then
        for i=1,40 do monster.on_trigger(ctx,{name='summon_apple_monster',phase='enter',id='mixed:'..i,
            properties={firstShotDelay=10,wanderSpeed=0,hoverOffset=0,wanderHeight=0,
            arenaMinX=-78,arenaMaxX=70,arenaMinY=-15,arenaMaxY=19,seed=i}}) end
    else timer(ctx,e) end
end
return monster
)";Content::Write(root/"rooms/authored_budget.room.json",authoredBudget);
    GameModule game(assets,{},root/"world.json");Check(game.Startup(),game.Error());
    auto enter=[&](const char* name){if(game.GetState()!=State::Playing){game.Restart();Tick(game);}
        game.World()->RequestRoom(name,"start");Tick(game);};
    auto summon=[&]{Place(game,{-45,18.9});Tick(game);Place(game,{-70,18.9});};
    Check(Actors(game).empty()&&Bars(game)==0,"trap is dormant until player detection fires");
    summon();Check(Actors(game).size()==1&&Bars(game)==3&&std::abs(Fill(game)-8)<.001,"one large apple begins with a full five-point bar");
    auto actor=Actors(game).front();const auto position=game.Get<Transform>(actor).position;
    Check(game.Get<Sprite>(actor).image=="demo_apple.png"&&game.Get<Transform>(actor).size==glm::vec2(6),"large monster uses actual apple artwork");
    summon();Check(Actors(game).size()==1,"reentering a spent trigger cannot summon duplicate monsters");
    Place(game,position);Tick(game);Check(game.GetState()==State::Playing,"touching the large apple alone is harmless");
    for(int hit=1;hit<=5;++hit) {
        Place(game,{position.x-10,18.9});Input input;input.shoot=true;Tick(game,20,input);
        if(hit<5)Check(Actors(game).size()==1&&std::abs(Fill(game)-8*(5-hit)/5.f)<.01,"each actual player bullet removes one fifth of health");
        else Check(Actors(game).empty()&&Bars(game)==0&&Shots(game).empty(),"fifth hit removes enemy, bar and owned projectiles");
    }
    summon();Check(Actors(game).empty(),"defeated trap remains spent until room reset");
    enter("same_frame");summon();Tick(game,30);
    Check(Actors(game).size()==1&&std::abs(Fill(game)-4.8)<.01,"two distinct projectiles hitting in one tick both deal damage");
    enter("wander");summon();actor=Actors(game).front();const auto before=game.Get<Transform>(actor).position;
    Tick(game,120);actor=Actors(game).front();const auto after=game.Get<Transform>(actor).position;
    Check(glm::length(after-before)>1,"monster wanders rather than remaining at the summon point");
    Check(after.x>=-70&&after.x<=60&&after.y>=-15&&after.y<=19,"wander respects authored arena bounds");
    for(const auto& v:game.Views())if(v.sprite->image=="apple_health_white.png")
        Check(glm::length(v.transform->position-(after-glm::vec2(0,4.5)))<.3f,"pixel health bar follows wandering enemy");
    enter("aim");summon();actor=Actors(game).front();game.Get<Transform>(actor).position={-20,8};Place(game,{-60,18.9});
    for(int i=0;i<80&&Shots(game).empty();++i)Tick(game);
    Check(Shots(game).size()==1,"monster launches a small apple after its firing interval");
    const auto shot=Shots(game).front();const auto shotName=game.Get<Behavior>(shot).stableId;
    const auto start=game.Get<Transform>(shot).position,velocity=game.Get<Motion>(shot).velocity;
    Check(game.Get<Sprite>(shot).image=="demo_apple.png"&&game.Get<Behavior>(shot).role==Role::Hazard,"enemy bullets are small lethal apples");
    Check(std::abs(glm::length(velocity)-16)<.001&&glm::dot(glm::normalize(velocity),glm::normalize(glm::vec2(-60,18.9)-start))>.999,
        "shot aims at player at firing time and has the configured speed");
    Place(game,{-70,18.9});Tick(game,12);
    Check(game.Find(shotName)&&game.Get<Motion>(game.Find(shotName)).velocity==velocity,"fired apple continues straight after player changes position");
    Tick(game,40);Check(!game.Find(shotName),"shot lifespan cleans expired apples");
    for(int i=0;i<80&&Shots(game).empty();++i)Tick(game);
    Check(!Shots(game).empty(),"monster continues shooting periodically");
    const auto lethal=Shots(game).front();Place(game,game.Get<Transform>(lethal).position+game.Get<Motion>(lethal).velocity*(1.f/120));Tick(game);
    Check(game.GetState()==State::Dead&&Actors(game).empty()&&Bars(game)==0&&Shots(game).empty(),"touching a small apple kills player and clears the encounter");
    game.Restart();Tick(game,5);summon();Check(Actors(game).size()==1&&std::abs(Fill(game)-8)<.001,"respawn starts a new encounter with five health");
    enter("empty");Tick(game,240);Check(Actors(game).empty()&&Bars(game)==0&&Shots(game).empty(),"room switch discards old brain timers and owned entities");
    std::set<int> spawnPositions;
    for(int i=0;i<12;++i){enter("random");summon();Check(Actors(game).size()==1,"random summon creates exactly one enemy");
        const auto point=game.Get<Transform>(Actors(game).front()).position;spawnPositions.insert(int(point.x*100));
        Check(glm::distance(point,glm::vec2(-45,18.9))>=6,"random summon does not overlap player");}
    Check(spawnPositions.size()>1,"summon location varies across unseeded encounters");
    enter("authored");Tick(game);Check(Actors(game).size()==1&&Bars(game)==3,"authored monster initializes without consuming a reserved summon slot");
    summon();Check(Actors(game).size()==2&&Bars(game)==6,"authored and triggered monsters can coexist");
    enter("authored_budget");Tick(game,20);
    Check(Actors(game).size()==4&&Bars(game)==12,"authored actors count toward the shared four-monster limit");
    enter("budget");Check(Actors(game).size()==4&&Bars(game)==12,"same-callback trigger burst reserves at most four enemy slots");
    Tick(game,360);Check(Actors(game).size()==4&&Shots(game).empty(),"bounded enemy timers remain stable under repeated updates");
    game.Kill();Tick(game);Check(Actors(game).empty()&&Bars(game)==0,"death clears all four active encounters");
    enter("bullet_budget");Place(game,{60,18.9});Tick(game,480);
    Check(Shots(game).size()==32,"all four enemies share a maximum of 32 active small apples");
    Tick(game,840);Check(Shots(game).size()<=32&&Shots(game).size()>=24,"expired apples free shot slots without unbounded accumulation");
    game.Kill();Tick(game);Check(Actors(game).empty()&&Bars(game)==0&&Shots(game).empty(),"full projectile budget is released on death");
    for(const auto* bad:{"health","shotInterval","spawnRadiusMax","arenaMaxX"}){
        Scripting::LuaModule lua;Check(lua.LoadSource(source.str(),"apple_monster_validation"),lua.Error());
        lua.queryPosition=[](const std::string&)->std::optional<Scripting::LuaModule::Position>{return {{0,0}};};
        Scripting::Event event{"on_trigger","summon_apple_monster","plate","enter"};
        event.properties[bad]=std::string(bad)=="arenaMaxX"?-980.:0.;
        Check(!lua.Call(event)&&lua.TakeCommands().empty(),"invalid enemy configuration fails before mutating the world");
    }
    std::cout<<"Apple monster PASS: random summon, roaming, aimed straight shots, lethal apples, five actual hits, same-tick damage, following health bar, budgets and lifecycle cleanup\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
