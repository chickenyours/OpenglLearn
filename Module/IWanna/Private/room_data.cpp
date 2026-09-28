#include "IWanna/Public/room_data.h"
#include "IWanna/Public/content_format.h"
#include <json/json.h>
#include <fstream>
#include <set>
#include <stdexcept>
#include <cmath>
namespace IWanna {
namespace {
Json::Value Read(const std::filesystem::path& path) {
    std::ifstream input(path);Json::Value value;Json::CharReaderBuilder reader;std::string error;
    if(!input||!Json::parseFromStream(reader,input,&value,&error))throw std::runtime_error("Cannot read room data: "+path.string()+" "+error);return value;
}
std::string String(const Json::Value& v,const char* name) {
    if(!v[name].isString()||v[name].asString().empty())throw std::runtime_error(std::string("Missing room string: ")+name);return v[name].asString();
}
float Number(const Json::Value& v,const char* name) {
    if(!v[name].isNumeric()||!std::isfinite(v[name].asFloat()))throw std::runtime_error(std::string("Invalid room number: ")+name);return v[name].asFloat();
}
std::filesystem::path Local(const std::filesystem::path& root,const std::string& path) {
    auto relative=Content::Path(path);
    if(relative.is_absolute())throw std::runtime_error("Room paths must be relative");
    auto result=(root/relative).lexically_normal();auto rel=result.lexically_relative(root.lexically_normal());
    if(rel.empty()||*rel.begin()=="..")throw std::runtime_error("Room path escapes content directory");return result;
}
Json::Value Properties(const Json::Value& object) {
    Json::Value result(Json::objectValue);
    for(const auto& p:object["properties"])result[String(p,"name")]=p["value"];return result;
}
void Detection(RoomObject& o,const Json::Value& p) {
    o.properties=p;o.collide=p.get("collide",o.collide).asBool();
    o.detectionBox=p.get("detectionEnabled",o.detectionBox).asBool();
    o.detectionOffset={p.get("detectionX",0).asFloat(),p.get("detectionY",0).asFloat()};
    o.detectionSize={p.get("detectionW",o.transform.size.x).asFloat(),p.get("detectionH",o.transform.size.y).asFloat()};
    if(!std::isfinite(o.detectionOffset.x)||!std::isfinite(o.detectionOffset.y)||!std::isfinite(o.detectionSize.x)||!std::isfinite(o.detectionSize.y)||o.detectionSize.x<=0||o.detectionSize.y<=0)throw std::runtime_error("Invalid detection rectangle");
}
Role ParseRole(const std::string& role) {
    static const std::map<std::string,Role> names={{"solid",Role::Solid},{"hazard",Role::Hazard},{"checkpoint",Role::Checkpoint},
        {"trigger",Role::Trigger},{"exit",Role::Exit},{"decoration",Role::Decoration}};
    auto found=names.find(role);if(found==names.end())throw std::runtime_error("Unknown room role: "+role);return found->second;
}
std::string ImageName(const Json::Value& v,const char* key) {
    auto value=String(v,key);if(std::filesystem::path(value).filename().string()!=value)throw std::runtime_error("Runtime images must be filenames");return value;
}
RoomDefinition ImportTiled(const std::filesystem::path& path,const std::map<std::string,RoomObject>& prefabs) {
    auto source=Content::Read(path);auto map=Content::ToTiled(source);RoomDefinition room;
    for(const auto& edge:source["connections"].getMemberNames()){
        if(edge!="left"&&edge!="right"&&edge!="top"&&edge!="bottom")throw std::runtime_error("Invalid boundary: "+edge);
        const auto& c=source["connections"][edge];room.connections[edge]={String(c,"room"),String(c,"spawn")};
    }
    if(map.get("orientation","")!="orthogonal"||map.get("infinite",false).asBool())throw std::runtime_error("Only finite orthogonal Tiled maps are supported");
    int width=int(Number(map,"width")),height=int(Number(map,"height"));float tw=Number(map,"tilewidth"),th=Number(map,"tileheight");
    if(width<1||height<1||width>256||height>256||tw<=0||tw!=th)throw std::runtime_error("Invalid Tiled grid");
    auto properties=Properties(map);float unit=Number(properties,"worldTileSize");
    if(unit<=0||unit>10)throw std::runtime_error("Invalid worldTileSize");
    glm::vec2 origin{Number(properties,"originX"),Number(properties,"originY")};const float scale=unit/tw;room.origin=origin;room.extent={width*unit,height*unit};
    std::map<unsigned,Sprite> tiles;
    for(const auto& reference:map["tilesets"]) {
        unsigned first=reference["firstgid"].asUInt();auto set=reference.isMember("source")?Read(Local(path.parent_path(),String(reference,"source"))):reference;
        for(const auto& tile:set["tiles"]) {
            auto p=Properties(tile);Sprite sprite;sprite.image=ImageName(p,"runtimeImage");sprite.pixelArt=true;
            if(!tiles.emplace(first+tile["id"].asUInt(),sprite).second)throw std::runtime_error("Duplicate Tiled GID");
        }
    }
    std::set<std::string> ids;
    auto add=[&](RoomObject object){if(object.id.empty()||object.id=="player"||!ids.insert(object.id).second)throw std::runtime_error("Duplicate/reserved room object ID: "+object.id);room.objects.push_back(std::move(object));};
    for(const auto& layer:map["layers"]) {
        if(layer.get("type","")=="tilelayer") {
            if(layer.get("x",0).asInt()!=0||layer.get("y",0).asInt()!=0||layer.get("offsetx",0).asFloat()!=0||layer.get("offsety",0).asFloat()!=0)
                throw std::runtime_error("Tile layer offsets are not supported");
            const auto& data=layer["data"];if(!data.isArray()||data.size()!=width*height)throw std::runtime_error("Tiled terrain must use an uncompressed numeric JSON array");
            for(int i=0;i<width*height;++i) {
                unsigned gid=data[i].asUInt();if(!gid)continue;
                if(!tiles.contains(gid))throw std::runtime_error("Unknown/flipped Tiled GID");
                RoomObject o;o.id="tile:"+std::to_string(layer["id"].asInt())+":"+std::to_string(i);o.role=Role::Solid;o.collide=true;o.sprite=tiles.at(gid);
                o.cell.column=i%width;o.cell.row=i/width;o.cell.material=int(gid);
                o.transform.position=origin+glm::vec2(i%width+.5f,i/width+.5f)*unit;o.transform.size=glm::vec2(unit);add(o);
            }
        } else if(layer.get("type","")=="objectgroup") {
            for(const auto& object:layer["objects"]) {
                auto p=Properties(object);auto kind=object.get("class",object.get("type","")).asString();
                glm::vec2 pos=origin+glm::vec2(Number(object,"x"),Number(object,"y"))*scale;
                if(kind=="Label") {room.labels.push_back({String(p,"text"),pos,p.get("textScale",.35).asFloat()});continue;}
                auto id=String(p,"uid");
                if(kind=="Spawn") {if(!ids.insert(id).second||!room.spawns.emplace(id,pos).second)throw std::runtime_error("Duplicate spawn ID: "+id);continue;}
                auto prefab=String(p,"prefab");if(!prefabs.contains(prefab))throw std::runtime_error("Unknown prefab: "+prefab);
                auto o=prefabs.at(prefab);o.id=id;o.prefab=prefab;
                glm::vec2 pixels{Number(object,"width"),Number(object,"height")};if(pixels.x<=0||pixels.y<=0)throw std::runtime_error("Object rectangles must have positive size");
                o.transform.size=pixels*scale;o.transform.position=pos+o.transform.size*.5f;o.transform.rotation=object.get("rotation",0).asFloat();
                if(o.role==Role::Solid && o.sprite.pixelArt && std::abs(o.transform.size.y-unit)<.001f)
                    o.sprite.repeatX=std::max(1,int(std::round(o.transform.size.x/unit)));
                if(o.transform.rotation!=0)throw std::runtime_error("Room object rotation is not supported yet");
                auto merged=o.properties;for(const auto& key:p.getMemberNames())merged[key]=p[key];
                o.event=merged.get("event","").asString();o.destinationRoom=merged.get("destinationRoom","").asString();o.destinationSpawn=merged.get("destinationSpawn","").asString();
                o.sprite.visible=merged.get("visible",o.sprite.visible).asBool();Detection(o,merged);add(o);
            }
        } else throw std::runtime_error("Unsupported Tiled layer type");
    }
    if(room.spawns.empty())throw std::runtime_error("Room has no spawn points");return room;
}
}
RoomDefinition RoomCatalog::ReadRoom(const std::filesystem::path& path,const std::map<std::string,RoomObject>& prefabs) {return ImportTiled(path,prefabs);}
RoomCatalog RoomCatalog::Load(const std::filesystem::path& world) {
    auto root=Content::ExpandWorld(world);if(root["format"]!="IWANNA_WORLD_1")throw std::runtime_error("Invalid world format");
    RoomCatalog catalog;catalog.startRoom=String(root,"startRoom");catalog.startSpawn=String(root,"startSpawn");
    for(const auto& name:root["prefabs"].getMemberNames()) {
        auto& p=root["prefabs"][name];RoomObject o;o.prefab=name;o.role=ParseRole(String(p,"role"));o.sprite.image=ImageName(p,"image");
        o.sprite.pixelArt=p.get("pixelArt",true).asBool();o.sprite.visible=p.get("visible",true).asBool();o.collide=p.get("collide",o.role!=Role::Decoration).asBool();
        o.transform.size={Number(p,"width"),Number(p,"height")};if(o.transform.size.x<=0||o.transform.size.y<=0)throw std::runtime_error("Invalid prefab size");
        Detection(o,p);catalog.prefabs[name]=o;
    }
    for(const auto& item:root["rooms"]) {
        auto id=String(item,"id");auto room=ImportTiled(Local(world.parent_path(),String(item,"map")),catalog.prefabs);
        room.id=id;room.title=String(item,"title");room.hint=String(item,"hint");room.inlineScript=item.isMember("scriptLua");if(room.inlineScript)room.scriptLua=item["scriptLua"].asString();else room.script=Local(world.parent_path(),String(item,"script"));
        if(!room.inlineScript&&!std::filesystem::exists(room.script))throw std::runtime_error("Missing room script: "+room.script.string());
        for(auto& o:room.objects)if(o.destinationRoom=="$self")o.destinationRoom=id;
        for(auto& [_,c]:room.connections)if(c.room=="$self")c.room=id;
        if(!catalog.rooms.emplace(id,std::move(room)).second)throw std::runtime_error("Duplicate room ID: "+id);
    }
    if(!catalog.rooms.contains(catalog.startRoom)||!catalog.rooms.at(catalog.startRoom).spawns.contains(catalog.startSpawn))throw std::runtime_error("Invalid initial room/spawn");
    for(const auto& [_,room]:catalog.rooms)for(const auto& o:room.objects)if(o.role==Role::Exit) {
        if(!catalog.rooms.contains(o.destinationRoom)||!catalog.rooms.at(o.destinationRoom).spawns.contains(o.destinationSpawn))throw std::runtime_error("Broken room connection: "+room.id+"/"+o.id);
    }
    for(const auto& [id,room]:catalog.rooms)for(const auto& [edge,c]:room.connections) {
        if(!catalog.rooms.contains(c.room)||!catalog.rooms.at(c.room).spawns.contains(c.spawn))throw std::runtime_error("Broken boundary connection: "+id+"/"+edge);
        auto pos=catalog.rooms.at(c.room).spawns.at(c.spawn);const auto& dest=catalog.rooms.at(c.room);
        if(pos.x<=dest.origin.x||pos.y<=dest.origin.y||pos.x>=dest.origin.x+dest.extent.x||pos.y>=dest.origin.y+dest.extent.y)throw std::runtime_error("Boundary arrival spawn must be inside room: "+c.room+"/"+c.spawn);
    }
    return catalog;
}
std::vector<Sprite> RoomCatalog::Sprites() const {
    std::vector<Sprite> result;
    for(const auto& [_,p]:prefabs)result.push_back(p.sprite);
    for(const auto& [_,room]:rooms)for(const auto& o:room.objects)result.push_back(o.sprite);
    Sprite active;active.image="checkpoint_active.png";active.pixelArt=true;result.push_back(active);return result;
}
}
