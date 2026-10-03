#include "IWanna/Public/game_module.h"
#include "IWanna/Public/content_format.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace IWanna;
namespace {
void Check(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
Json::Value Entity(const char* prefab,float x,float y) {
    Json::Value value;value["kind"]="Entity";value["prefab"]=prefab;
    value["position"][0]=x;value["position"][1]=y;
    value["size"][0]=2.5;value["size"][1]=3;return value;
}
Json::Value Room(const std::string& script) {
    Json::Value room;room["format"]="IWANNA_ROOM_2";room["title"]="Entity lifecycle fixture";
    room["grid"]["width"]=64;room["grid"]["height"]=20;room["grid"]["tileSize"]=2.5;
    room["grid"]["origin"][0]=-80;room["grid"]["origin"][1]=-25;
    room["palette"]["1"]="terrain_moss_00.png";
    for(int y=0;y<20;++y)for(int x=0;x<64;++x)room["tileLayers"]["Terrain"][y].append(y>=18?1:0);
    room["entities"]["start"]["kind"]="Spawn";
    room["entities"]["start"]["position"][0]=-60;room["entities"]["start"]["position"][1]=18.9;
    room["scriptLua"]=script;return room;
}
void Tick(GameModule& game,int frames=1) {
    for(int i=0;i<frames;++i)game.FixedTick({});
    Check(game.World()->Error().empty(),game.World()->Error());
}
void CheckViews(GameModule& game) {
    for(const auto& v:game.Views())Check(v.transform==&game.Get<Transform>(v.id),"structural initializer refreshes borrowed views");
}
}
int main(){try{
    const auto assets=std::filesystem::path(IWANNA_ASSETS);
    const auto root=std::filesystem::current_path()/"entity_lifecycle_test_generated";
    std::filesystem::create_directories(root/"prefabs");std::filesystem::create_directories(root/"rooms");
    Json::Value prefab;prefab["format"]="IWANNA_PREFAB_1";prefab["role"]="trigger";
    prefab["image"]="demo_spike.png";prefab["width"]=2.5;prefab["height"]=3;
    prefab["collide"]=false;prefab["event"]="initialize";prefab["power"]=3;prefab["flag"]=true;prefab["label"]="template";
    // Reserved live-transform properties must override authored scalar values.
    prefab["positionX"]=999;prefab["sizeX"]=999;
    Content::Write(root/"prefabs/controller.prefab.json",prefab);
    auto effect=prefab;effect["role"]="hazard";effect["event"]="";effect["collide"]=true;
    Content::Write(root/"prefabs/effect.prefab.json",effect);
    auto quiet=prefab;quiet["event"]="";Content::Write(root/"prefabs/quiet.prefab.json",quiet);
    auto room=Room(R"(
local ready=false
local seen={}
local children={}
return {
  on_enter=function(ctx)
    ready=true
    assert(ctx:get_position('player') and ctx:get_position('authored'))
    ctx:set_size('authored',4,6)
    ctx:set_rotation('authored',90)
    ctx:destroy('removed')
    ctx:spawn('controller','dynamic',20,5)
    ctx:set_size('dynamic',7,9)
    ctx:set_rotation('dynamic',-90)
    ctx:spawn('controller','reused',1,2)
    ctx:destroy('reused')
    ctx:spawn('controller','reused',3,4)
    ctx:after(0.05,'late')
  end,
  on_entity_spawn=function(ctx,e)
    assert(ready and e.phase=='spawn' and e.name=='initialize')
    assert(e.id~='removed' and e.id~='quiet' and not seen[e.id])
    seen[e.id]=true
    local p=e.properties
    local x,y=ctx:get_position(e.id)
    assert(x==p.positionX and y==p.positionY and p.flag and p.label=='template')
    if e.id=='authored' then
      assert(p.power==7 and p.sizeX==4 and p.sizeY==6 and p.rotation==90)
      assert(ctx:get_position('dynamic'))
    elseif e.id=='dynamic' then
      assert(p.power==3 and x==20 and y==5 and p.sizeX==7 and p.sizeY==9 and p.rotation==-90)
    elseif e.id=='reused' then
      assert(x==3 and y==4 and p.sizeX==2.5 and p.sizeY==3 and p.rotation==0)
    elseif e.id=='late' then
      assert(x==30 and y==6 and p.sizeX==5 and p.sizeY==2 and p.rotation==180)
    else error('unexpected initialized ID: '..e.id) end
    local child=e.id..':child'
    children[#children+1]=child
    ctx:spawn('effect',child,x,y)
    ctx:set_size(child,p.sizeX,p.sizeY)
    ctx:set_rotation(child,p.rotation)
    ctx:set_visible(e.id,false)
  end,
  on_timer=function(ctx,e)
    if e.name=='late' then
      ctx:spawn('controller','late',30,6)
      ctx:set_size('late',5,2)
      ctx:set_rotation('late',180)
    end
  end,
  on_death=function(ctx)
    for _,id in ipairs(children) do ctx:destroy(id) end
    ctx:message('cleaned')
  end
}
)");
    room["entities"]["authored"]=Entity("controller",10,4);
    room["entities"]["authored"]["properties"]["power"]=7;
    room["entities"]["removed"]=Entity("controller",12,4);
    room["entities"]["quiet"]=Entity("quiet",14,4);
    Content::Write(root/"rooms/main.room.json",room);
    auto legacy=Room(R"(return {on_enter=function(ctx)
        ctx:spawn('controller','legacy_dynamic',20,5)
        ctx:set_size('legacy_dynamic',5,6)
        ctx:message('legacy intact')
    end})");
    legacy["entities"]["legacy_authored"]=Entity("controller",10,4);
    Content::Write(root/"rooms/legacy.room.json",legacy);
    auto recursive=Room(R"(local serial=0
        return {on_entity_spawn=function(ctx,e)
            serial=serial+1
            ctx:spawn('controller','recursive:'..serial,20,5)
        end})");
    recursive["entities"]["root"]=Entity("controller",10,4);
    Content::Write(root/"rooms/recursive.room.json",recursive);
    auto failure=Room(R"(return {on_entity_spawn=function(ctx,e)
        ctx:spawn('effect','uncommitted',20,5)
        error('initializer failure')
    end})");
    failure["entities"]["root"]=Entity("controller",10,4);
    Content::Write(root/"rooms/failure.room.json",failure);
    // A large initializer expansion must not read stale ECS views when a later
    // initialization command resizes the player after the archetype grows.
    auto expansion=Room(R"(return {on_entity_spawn=function(ctx,e)
        if e.id=='root' then
            for i=1,70 do ctx:spawn('effect','expansion:'..i,20,5) end
            ctx:set_player_scale(0.7,0.7)
            ctx:spawn('controller','resize',25,5)
        elseif e.id=='resize' then ctx:set_player_scale(0.8,0.8) end
    end})");
    expansion["entities"]["root"]=Entity("controller",10,4);
    Content::Write(root/"rooms/expansion.room.json",expansion);
    Json::Value world;world["format"]="IWANNA_WORLD_2";world["prefabDirectory"]="prefabs";world["roomDirectory"]="rooms";
    world["startRoom"]="main";world["startSpawn"]="start";Content::Write(root/"world.json",world);
    GameModule game(assets,{},root/"world.json");Check(game.Startup(),game.Error());
    Check(game.World()->Error().empty(),game.World()->Error());
    Check(game.Find("authored:child")&&game.Find("dynamic:child")&&game.Find("reused:child"),"authored and dynamic controllers initialize in the same Apply");
    Check(!game.Find("removed")&&!game.Find("removed:child")&&!game.Find("quiet:child"),"deleted and eventless instances do not initialize");
    Check(!game.Get<Sprite>(game.Find("authored")).visible,"initializer commands commit before first render");
    Check(game.Get<Behavior>(game.Find("authored:child")).role==Role::Hazard&&game.Get<Collider>(game.Find("authored:child")).enabled,"trigger controller can initialize a separate ordinary hazard");
    Check(game.Get<Transform>(game.Find("dynamic:child")).size==glm::vec2(7,9),"live transforms drive generated child geometry");
    CheckViews(game);Tick(game,12);Check(game.Find("late:child"),"timer-spawned instance also receives initialization");
    game.Kill();Tick(game);Check(game.World()->Message()=="cleaned"&&!game.Find("authored:child")&&!game.Find("late:child"),"death cleanup does not reinitialize removed children");
    game.Restart();Tick(game);Check(game.Find("authored:child")&&game.Find("dynamic:child")&&!game.Find("late:child"),"reset creates fresh VM and reinitializes authored instances once");
    game.World()->RequestRoom("legacy","start");Tick(game);
    Check(game.World()->Message()=="legacy intact"&&game.Find("legacy_authored")&&game.Find("legacy_dynamic")&&!game.Find("authored:child"),"legacy callback tables remain valid and old room entities are gone");
    Check(game.Get<Transform>(game.Find("legacy_dynamic")).size==glm::vec2(5,6),"legacy spawn command ordering is unchanged");
    game.World()->RequestRoom("expansion","start");Tick(game);Check(game.Find("expansion:70"),"initializer-generated command batches are not lost");CheckViews(game);
    game.World()->RequestRoom("recursive","start");game.FixedTick({});
    Check(game.World()->Error().find("initialization batch budget exceeded")!=std::string::npos,"recursive prefab initialization is bounded with a clear error");
    Check(game.Entities().size()<500,"recursive callback cannot grow unbounded entities");CheckViews(game);
    game.Restart();game.FixedTick({});Check(game.World()->Error().find("initialization batch budget exceeded")!=std::string::npos,"reset safely clears pending initialization after a budget failure");
    world["startRoom"]="failure";Content::Write(root/"world.json",world);
    GameModule failed(assets,{},root/"world.json");Check(failed.Startup(),failed.Error());
    Check(failed.World()->Error().find("initializer failure")!=std::string::npos&&!failed.Find("uncommitted"),"failed initializer discards its queued commands");
    std::cout<<"Entity lifecycle: authored/dynamic initialization, live transforms, ID reuse, child commands, legacy compatibility, death/reset and recursion budget PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
