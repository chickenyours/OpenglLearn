#include "IWanna/Public/room_data.h"
#include "IWanna/Public/content_format.h"
#include "Scripting/Public/lua_module.h"
#include <iostream>
int main(int argc,char** argv){Json::Value report;try{
    if(argc!=3||(std::string(argv[1])!="validate"&&std::string(argv[1])!="inspect"))throw std::runtime_error("Usage: iwanna_content validate|inspect path/to/world.json");
    auto catalog=IWanna::RoomCatalog::Load(std::filesystem::absolute(argv[2]));report["ok"]=true;report["startRoom"]=catalog.startRoom;report["startSpawn"]=catalog.startSpawn;report["rooms"]=Json::Value(Json::objectValue);
    for(const auto& [id,r]:catalog.rooms){Scripting::LuaModule script;if(!(r.inlineScript?script.LoadSource(r.scriptLua,id):script.Load(r.script)))throw std::runtime_error(id+": "+script.Error());auto& node=report["rooms"][id];node["objectCount"]=int(r.objects.size());node["spawns"]=Json::Value(Json::arrayValue);for(const auto& [s,_]:r.spawns)node["spawns"].append(s);node["links"]=Json::Value(Json::arrayValue);
        auto appendBoundary=[&](const std::string& edge,const auto& rule,const Json::Value& range=Json::Value{}){Json::Value link;link["kind"]="boundary";link["source"]=edge;if(!range.isNull())link["range"]=range;
            if(rule.action==IWanna::RoomDefinition::BoundaryRule::Action::Transfer){link["action"]="transfer";link["room"]=rule.room;link["spawn"]=rule.spawn;if(rule.position){link["position"][0]=rule.position->x;link["position"][1]=rule.position->y;}}
            else link["action"]=rule.action==IWanna::RoomDefinition::BoundaryRule::Action::Death?"death":"ignore";node["links"].append(link);};
        for(const auto& [edge,rule]:r.boundaries){appendBoundary(edge,rule);for(const auto& segment:rule.segments){Json::Value range;range[0]=segment.range.x;range[1]=segment.range.y;appendBoundary(edge,segment,range);}}
        for(const auto& o:r.objects)if(o.role==IWanna::Role::Exit){Json::Value link;link["kind"]="portal";link["source"]=o.id;link["room"]=o.destinationRoom;link["spawn"]=o.destinationSpawn;node["links"].append(link);}
    }
    report["prefabs"]=Json::Value(Json::arrayValue);for(const auto& [name,_]:catalog.prefabs)report["prefabs"].append(name);
    std::cout<<report<<'\n';return 0;
}catch(const std::exception& e){report=Json::Value(Json::objectValue);report["ok"]=false;report["error"]=e.what();std::cout<<report<<'\n';return 1;}}
