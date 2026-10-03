#include "IWanna/Public/project_generator.h"
#include "IWanna/Public/editor_document.h"
#include "IWanna/Public/game_module.h"
#include <fstream>
#include <iostream>

using namespace IWanna;
static void Check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
int main(){try{
    auto root=std::filesystem::current_path()/"project_test_generated";int n=0;
    while(std::filesystem::exists(root))root=std::filesystem::current_path()/("project_test_generated_"+std::to_string(++n));
    auto project=CreateBlankProject(root,IWANNA_ASSETS,IWANNA_RUNTIME);
    const auto starter=Content::Read(project/"rooms/room_01.room.json");
    Check(starter["script"]=="scripts/room_01.lua","external room Lua reference");
    Check(starter["palette"].size()==32 && starter["palette"]["1"]=="terrain_moss_00.png",
          "new projects expose themed edge and alternate tiles");
    auto catalog=RoomCatalog::Load(project/"world.json");Check(catalog.rooms.size()==1&&catalog.startSpawn=="start","generated world loads");
    {std::ofstream script(project/"scripts/room_01.lua");script<<"return { on_enter=function(ctx) ctx:message('LUA_ONE') end }\n";}
    GameModule game(project,{},project/"world.json");Check(game.Startup(),game.Error().c_str());game.FixedTick({});Check(game.GetState()==State::Playing&&game.World()->Message()=="LUA_ONE","generated external Lua runs");game.Shutdown();
    {std::ofstream script(project/"scripts/room_01.lua");script<<"return { on_enter=function(ctx) ctx:message('LUA_TWO') end }\n";}
    Check(game.Startup(),game.Error().c_str());Check(game.World()->Message()=="LUA_TWO","editing Lua takes effect without recompilation");game.Shutdown();
    EditorDocument editor;editor.Open(project/"world.json");editor.NewRoom("room_02",false);
    Check(std::filesystem::exists(project/"scripts/room_02.lua")&&Content::Read(project/"rooms/room_02.room.json")["script"]=="scripts/room_02.lua","editor new room gets independent Lua");
    editor.NewRoom("room_03",true);
    Check(std::filesystem::exists(project/"scripts/room_03.lua")&&Content::Read(project/"rooms/room_03.room.json")["script"]=="scripts/room_03.lua","editor clone gets independent Lua");
    bool rejected=false;try{CreateBlankProject(root,IWANNA_ASSETS,IWANNA_RUNTIME);}catch(...){rejected=true;}
    Check(rejected,"generator refuses existing directory");
    std::cout<<"Blank project generation, external Lua, editor room copies PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
