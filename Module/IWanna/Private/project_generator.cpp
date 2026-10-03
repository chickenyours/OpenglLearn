#include "IWanna/Public/project_generator.h"
#include "IWanna/Public/content_format.h"
#include "IWanna/Public/room_data.h"
#include <fstream>
#include <stdexcept>

namespace IWanna {
namespace {
void CopyFiles(const std::filesystem::path& source,const std::filesystem::path& destination){
    if(!std::filesystem::is_directory(source))throw std::runtime_error("Missing project template directory: "+source.string());
    std::filesystem::create_directories(destination);
    for(const auto& entry:std::filesystem::directory_iterator(source))if(entry.is_regular_file())
        std::filesystem::copy_file(entry.path(),destination/entry.path().filename());
}
void WriteText(const std::filesystem::path& path,const std::string& value){
    std::ofstream file(path,std::ios::binary);file<<value;file.flush();
    if(!file)throw std::runtime_error("Cannot write project file: "+path.string());
}
Json::Value BlankRoom(){
    Json::Value room;room["format"]="IWANNA_ROOM_2";room["title"]="Room 01";room["hint"]="Edit this room and scripts/room_01.lua";
    room["grid"]["width"]=24;room["grid"]["height"]=14;room["grid"]["tileSize"]=2.5;
    room["grid"]["origin"][0]=-30;room["grid"]["origin"][1]=-17.5;
    for(int edge=0;edge<16;++edge){
        const auto suffix=std::string(edge<10?"0":"")+std::to_string(edge)+".png";
        room["palette"][std::to_string(1+edge)]="terrain_moss_"+suffix;
        room["palette"][std::to_string(17+edge)]="terrain_moss_alt_"+suffix;
    }
    for(int y=0;y<14;++y)for(int x=0;x<24;++x){
        int gid=0;
        if(y>=11){
            const int edges=(y==11?1:0)|(x==23?2:0)|(y==13?4:0)|(x==0?8:0);
            const bool alternate=(x*17+y*37)%11<3;
            gid=1+edges+(alternate?16:0);
        }
        room["tileLayers"]["Terrain"][y].append(gid);
    }
    room["entities"]["start"]["kind"]="Spawn";room["entities"]["start"]["position"][0]=0;room["entities"]["start"]["position"][1]=5;
    room["entities"]["start"]["properties"]=Json::Value(Json::objectValue);
    room["boundaries"]=Json::Value(Json::objectValue);room["script"]="scripts/room_01.lua";
    room["camera"]["mode"]="fixed";room["camera"]["position"][0]=0;room["camera"]["position"][1]=0;
    return room;
}
}
std::filesystem::path CreateBlankProject(const std::filesystem::path& destination,
                                         const std::filesystem::path& templateAssets,
                                         const std::filesystem::path& runtimeDirectory){
    auto target=std::filesystem::absolute(destination).lexically_normal();
    auto assets=std::filesystem::absolute(templateAssets).lexically_normal();
    auto runtime=std::filesystem::absolute(runtimeDirectory).lexically_normal();
    if(target.filename().empty()||std::filesystem::exists(target))throw std::runtime_error("Destination must be a new directory: "+target.string());
    if(!std::filesystem::is_regular_file(assets/"gameplay.json"))throw std::runtime_error("Missing gameplay template");
#ifdef _WIN32
    if(!std::filesystem::is_regular_file(runtime/"iwanna_showcase.exe")||!std::filesystem::is_regular_file(runtime/"iwanna_editor.exe"))throw std::runtime_error("Missing built I Wanna runtime/editor in "+runtime.string());
#else
    if(!std::filesystem::is_regular_file(runtime/"iwanna_showcase")||!std::filesystem::is_regular_file(runtime/"iwanna_editor"))throw std::runtime_error("Missing built I Wanna runtime/editor in "+runtime.string());
#endif
    std::filesystem::create_directories(target.parent_path());
    auto stage=target;int suffix=0;do{stage=target.parent_path()/(target.filename().string()+".iwanna-create-"+std::to_string(++suffix));}while(std::filesystem::exists(stage));
    std::filesystem::create_directory(stage);
    // A failure leaves the uniquely named staging directory for inspection; it never overwrites user files.
    CopyFiles(assets/"images",stage/"images");CopyFiles(assets/"audio",stage/"audio");
    CopyFiles(assets/"Workshop/prefabs",stage/"prefabs");CopyFiles(assets/"Workshop/schemas",stage/"schemas");
    auto gameplay=Content::Read(assets/"gameplay.json");gameplay["description"]="Project-local gameplay and character settings. Restart the game after editing; no compilation is needed.";
    Content::Write(stage/"gameplay.json",gameplay);
    Json::Value world;world["format"]="IWANNA_WORLD_2";world["startRoom"]="room_01";world["startSpawn"]="start";world["roomDirectory"]="rooms";world["prefabDirectory"]="prefabs";
    Content::Write(stage/"world.json",world);std::filesystem::create_directory(stage/"rooms");std::filesystem::create_directory(stage/"scripts");
    Content::Write(stage/"rooms/room_01.room.json",BlankRoom());
    WriteText(stage/"scripts/room_01.lua","-- Room-local gameplay callbacks. Changes take effect on the next game launch.\nreturn {\n    on_enter = function(ctx, event)\n    end\n}\n");
#ifdef _WIN32
    auto bin=runtime.string();
    WriteText(stage/"run.bat","@echo off\r\nsetlocal\r\nif not defined IWANNA_RUNTIME_DIR set \"IWANNA_RUNTIME_DIR="+bin+"\"\r\n\"%IWANNA_RUNTIME_DIR%\\iwanna_showcase.exe\" --assets \"%~dp0.\" --config \"%~dp0gameplay.json\" --world \"%~dp0world.json\" %*\r\n");
    WriteText(stage/"edit.bat","@echo off\r\nsetlocal\r\nif not defined IWANNA_RUNTIME_DIR set \"IWANNA_RUNTIME_DIR="+bin+"\"\r\n\"%IWANNA_RUNTIME_DIR%\\iwanna_editor.exe\" --assets \"%~dp0.\" --world \"%~dp0world.json\" %*\r\n");
#else
    auto bin=runtime.string();
    WriteText(stage/"run.sh","#!/bin/sh\nPROJECT_DIR=$(CDPATH= cd -- \"$(dirname -- \"$0\")\" && pwd)\nRUNTIME_DIR=${IWANNA_RUNTIME_DIR:-\""+bin+"\"}\nexec \"$RUNTIME_DIR/iwanna_showcase\" --assets \"$PROJECT_DIR\" --config \"$PROJECT_DIR/gameplay.json\" --world \"$PROJECT_DIR/world.json\" \"$@\"\n");
    WriteText(stage/"edit.sh","#!/bin/sh\nPROJECT_DIR=$(CDPATH= cd -- \"$(dirname -- \"$0\")\" && pwd)\nRUNTIME_DIR=${IWANNA_RUNTIME_DIR:-\""+bin+"\"}\nexec \"$RUNTIME_DIR/iwanna_editor\" --assets \"$PROJECT_DIR\" --world \"$PROJECT_DIR/world.json\" \"$@\"\n");
    std::filesystem::permissions(stage/"run.sh",std::filesystem::perms::owner_exec|std::filesystem::perms::group_exec|std::filesystem::perms::others_exec,std::filesystem::perm_options::add);
    std::filesystem::permissions(stage/"edit.sh",std::filesystem::perms::owner_exec|std::filesystem::perms::group_exec|std::filesystem::perms::others_exec,std::filesystem::perm_options::add);
#endif
    WriteText(stage/"README.md","# I Wanna content project\n\nThis directory contains game content only. Run `run.bat` / `run.sh` to play and `edit.bat` / `edit.sh` to open the level editor. The launcher uses the runtime installed when this project was created; set `IWANNA_RUNTIME_DIR` if it moves. Edit Lua, rooms, prefabs and `gameplay.json`, then restart the game. No C++ rebuild is needed.\n\n- `world.json`: start room and asset directories.\n- `rooms/*.room.json`: independent room assets; copying a file creates another room after editor F5.\n- `scripts/*.lua`: gameplay callbacks, one file per room. A room's `script` path is relative to this project root.\n- `prefabs/*.prefab.json`: entity templates, discovered by the editor on F5.\n- `images/`, `audio/`: local art and sound assets.\n- `gameplay.json`: player, movement and collision tuning.\n- `schemas/`: JSON schemas for external tools and agents.\n\nIn the editor use F9 to play the current room. The initial room has one spawn and a safe floor. Missing boundary rules cause death; configure transfers, death or ignore in each room. Keep room and prefab IDs in filenames, entity IDs as keys, and Lua references stable.\n");
    RoomCatalog::Load(stage/"world.json");
    std::filesystem::rename(stage,target);
    return target;
}
}
