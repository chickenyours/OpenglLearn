#include "IWanna/Public/game_module.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace IWanna;
static void Check(bool yes,const char* text){if(!yes)throw std::runtime_error(text);}
int main(){try {
    const auto assets=std::filesystem::path(IWANNA_ASSETS);auto catalog=RoomCatalog::Load(assets/"Showcase/world.json");
    Check(catalog.rooms.size()==3,"three Tiled rooms imported");
    GameModule game(assets,{},assets/"Showcase/world.json");Check(game.Startup(),game.Error().c_str());
    auto tick=[&](int n){for(int i=0;i<n;++i)game.FixedTick({});Check(game.World()->Error().empty(),game.World()->Error().c_str());};
    auto place=[&](float x,float y){game.Get<Transform>(game.PlayerEntity()).position={x,y};game.Get<Motion>(game.PlayerEntity()).velocity={};game.Get<Player>(game.PlayerEntity())={};};
    auto enter=[&](const char* room){game.World()->RequestRoom(room,"left");tick(1);Check(game.World()->CurrentId()==room,"queued room transition");};
    Check(game.World()->CurrentId()=="movement","initial room");tick(30);
    // Play through room 1 with actual input/physics, not teleportation.
    for(int n=0;n<1800 && game.World()->CurrentId()=="movement";++n){int phase=n%100;game.FixedTick({false,true,phase==0||phase==40,false,false,phase<17||(phase>=40&&phase<57)});}
    Check(game.World()->CurrentId()=="traps","movement room is traversable through its exit");
    place(-68,20);tick(1);Check(game.World()->CurrentId()=="movement","return connection");tick(3);
    Check(game.World()->CurrentId()=="movement","arrival does not bounce between doors");
    enter("traps");place(-42,20.8f);tick(1);
    auto apple=game.Find("apple_01");Check(apple && game.Get<Motion>(apple).velocity.y==32,"Lua trigger starts apple");
    place(-51,20.8f);tick(190);Check(!game.Get<Collider>(apple).enabled,"Lua timer disables apple");
    place(-15,20.8f);tick(5);auto save=game.Checkpoint();
    Check(game.Get<Sprite>(game.Find("checkpoint")).image=="checkpoint_active.png","checkpoint persists outside script VM");
    game.Kill();tick(1);game.Restart();tick(1);
    Check(game.GetState()==State::Playing && std::abs(game.Get<Transform>(game.PlayerEntity()).position.x-save.x)<.01f,"respawn uses room checkpoint");
    apple=game.Find("apple_01");Check(game.Get<Collider>(apple).enabled && game.Get<Motion>(apple).velocity==glm::vec2(0),"reset restores trap and clears timer");
    // The solid platform reports contact to Lua and remains collidable until due.
    place(5,20.8f);tick(20);Check(game.Get<Collider>(game.Find("bridge")).enabled,"bridge delay preserves contact");
    tick(70);Check(!game.Get<Collider>(game.Find("bridge")).enabled,"Lua timer removes bridge collision");
    tick(100);Check(game.GetState()==State::Dead,"pit spike kills after bridge collapse");
    game.Restart();tick(1);Check(game.Get<Collider>(game.Find("bridge")).enabled,"reset restores bridge");
    enter("gallery");size_t count=game.Entities().size();place(-15,20.8f);tick(1);
    Check(game.Find("demo_orb") && game.Entities().size()==count+1,"Lua spawn adds an ECS entity after the tick");
    tick(245);Check(!game.Find("demo_orb") && game.Entities().size()==count,"Lua destroy removes ECS entity safely");
    for(const auto& view:game.Views())Check(view.transform==&game.Get<Transform>(view.id),"views refreshed after deletion");
    place(-30,20.8f);tick(1);place(-15,20.8f);tick(1);Check(!game.Find("demo_orb"),"once flag survives repeated trigger entry");
    game.Restart();tick(1);place(-15,20.8f);tick(1);Check(game.Find("demo_orb"),"reset clears once flags");
    enter("traps");tick(250);Check(!game.Find("demo_orb"),"old-room timer cannot affect new-room entities");
    for(int i=0;i<12;++i){enter("gallery");enter("traps");}
    Check(game.Get<Sprite>(game.Find("checkpoint")).image=="checkpoint_active.png","checkpoint retained on room return");
    game.Shutdown();Check(game.Startup(),"room module lifecycle restart");
    // Protected script failures discard commands and cannot hang the game.
    auto fixtures=std::filesystem::current_path()/"room_test_generated";std::filesystem::create_directories(fixtures);
    {std::ofstream out(fixtures/"loop.lua");out<<"while true do end";}
    Scripting::LuaModule lua;Check(!lua.Load(fixtures/"loop.lua") && lua.Error().find("budget")!=std::string::npos,"infinite Lua loop bounded");
    {std::ofstream out(fixtures/"error.lua");out<<"return { on_enter=function(ctx) ctx:message('not committed'); error('test failure') end }";}
    Check(lua.Load(fixtures/"error.lua"),"load failure fixture");Check(!lua.Call({"on_enter","","",""}) && lua.TakeCommands().empty(),"Lua failure discards pending commands");
    {std::ofstream out(fixtures/"memory.lua");out<<"return { on_enter=function(ctx) retained={} while true do retained[#retained+1]=string.rep('x',65536) end end }";}
    Check(lua.Load(fixtures/"memory.lua"),"load memory fixture");Check(!lua.Call({"on_enter","","",""}) && !lua.Error().empty(),"Lua allocation failure is protected");
    auto copy=fixtures/"catalog";std::filesystem::copy(assets/"Showcase",copy,std::filesystem::copy_options::recursive|std::filesystem::copy_options::overwrite_existing);
    auto mapFile=copy/"rooms/movement.tmj";
    auto read=[](const std::filesystem::path& path){std::ifstream in(path);Json::Value v;Json::CharReaderBuilder b;std::string e;Check(Json::parseFromStream(b,in,&v,&e),"fixture JSON");return v;};
    auto write=[](const std::filesystem::path& path,const Json::Value& v){std::ofstream out(path);Json::StreamWriterBuilder b;out<<Json::writeString(b,v);};
    auto original=read(mapFile),changed=original;
    auto& objects=changed["layers"][1]["objects"];
    for(Json::ArrayIndex i=0;i<objects.size()/2;++i)std::swap(objects[i],objects[objects.size()-1-i]);
    for(auto& o:objects)o["id"]=o["id"].asInt()+1000;
    write(mapFile,changed);auto reordered=RoomCatalog::Load(copy/"world.json");
    bool foundExit=false;for(const auto& o:reordered.rooms.at("movement").objects)if(o.id=="to_traps")foundExit=o.destinationRoom=="traps";
    Check(foundExit,"stable IDs survive Tiled reorder and numeric ID changes");
    changed=original;
    for(auto& o:changed["layers"][1]["objects"])for(auto& p:o["properties"])if(p["name"]=="destinationRoom")p["value"]="missing_room";
    write(mapFile,changed);bool rejected=false;try{RoomCatalog::Load(copy/"world.json");}catch(const std::exception&){rejected=true;}
    Check(rejected,"broken exits rejected before simulation");write(mapFile,original);
    {std::ofstream out(copy/"scripts/traps.lua");out<<"this is not valid lua !!!";}
    GameModule badDestination(assets,{},copy/"world.json");Check(badDestination.Startup(),badDestination.Error().c_str());
    size_t before=badDestination.Entities().size();badDestination.World()->RequestRoom("traps","left");badDestination.FixedTick({});
    Check(badDestination.World()->CurrentId()=="movement" && badDestination.Entities().size()==before && !badDestination.World()->Error().empty(),"bad destination script preserves current room");
    std::cout<<"Rooms: Tiled import, playable connection, Lua traps/timers, checkpoints, reset, spawn/destroy, isolation PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
