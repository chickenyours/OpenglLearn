#include "IWanna/Public/room_data.h"
#include "IWanna/Public/content_format.h"
#include "Scripting/Public/lua_module.h"
#include <iostream>
int main(int argc,char** argv){Json::Value report;try{
    if(argc!=3||(std::string(argv[1])!="validate"&&std::string(argv[1])!="inspect"))throw std::runtime_error("Usage: iwanna_content validate|inspect path/to/world.json");
    auto catalog=IWanna::RoomCatalog::Load(std::filesystem::absolute(argv[2]));report["ok"]=true;report["startRoom"]=catalog.startRoom;report["startSpawn"]=catalog.startSpawn;report["rooms"]=Json::Value(Json::objectValue);
    for(const auto& [id,r]:catalog.rooms){Scripting::LuaModule script;if(!(r.inlineScript?script.LoadSource(r.scriptLua,id):script.Load(r.script)))throw std::runtime_error(id+": "+script.Error());auto& node=report["rooms"][id];node["objectCount"]=int(r.objects.size());node["spawns"]=Json::Value(Json::arrayValue);for(const auto& [s,_]:r.spawns)node["spawns"].append(s);node["links"]=Json::Value(Json::arrayValue);
        for(const auto& [edge,c]:r.connections){Json::Value link;link["kind"]="boundary";link["source"]=edge;link["room"]=c.room;link["spawn"]=c.spawn;node["links"].append(link);}for(const auto& o:r.objects)if(o.role==IWanna::Role::Exit){Json::Value link;link["kind"]="portal";link["source"]=o.id;link["room"]=o.destinationRoom;link["spawn"]=o.destinationSpawn;node["links"].append(link);}
    }
    report["prefabs"]=Json::Value(Json::arrayValue);for(const auto& [name,_]:catalog.prefabs)report["prefabs"].append(name);
    std::cout<<report<<'\n';return 0;
}catch(const std::exception& e){report=Json::Value(Json::objectValue);report["ok"]=false;report["error"]=e.what();std::cout<<report<<'\n';return 1;}}
