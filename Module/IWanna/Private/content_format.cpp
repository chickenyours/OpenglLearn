#include "IWanna/Public/content_format.h"
#include <fstream>
#include <cstring>
#include <vector>
#include <set>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace IWanna::Content {
Json::Value Read(const std::filesystem::path& path){std::ifstream f(path);Json::Value v;Json::CharReaderBuilder b;b["rejectDupKeys"]=true;b["failIfExtra"]=true;std::string error;if(!f||!Json::parseFromStream(b,f,&v,&error))throw std::runtime_error(path.string()+": "+error);return v;}
void Write(const std::filesystem::path& path,const Json::Value& value){std::ofstream f(path);Json::StreamWriterBuilder b;b["indentation"]="  ";f<<Json::writeString(b,value)<<'\n';f.flush();if(!f)throw std::runtime_error("Cannot write "+path.string());}
static Json::Value Properties(const Json::Value& o){Json::Value p(Json::objectValue);for(const auto& x:o["properties"])p[x["name"].asString()]=x["value"];return p;}
static void Set(Json::Value& o,const std::string& key,Json::Value value){Json::Value p;p["name"]=key;p["value"]=value;p["type"]=value.isBool()?"bool":value.isNumeric()?"float":"string";o["properties"].append(p);}
static std::filesystem::path Local(const std::filesystem::path& root,const std::string& name){auto p=Content::Path(name);auto rel=p.lexically_normal();if(p.is_absolute()||rel.empty()||*rel.begin()=="..")throw std::runtime_error("Invalid content path: "+name);return root/rel;}
Json::Value ExpandWorld(const std::filesystem::path& path){
    auto world=Read(path);if(world["format"]!="IWANNA_WORLD_2")return world;
    auto root=path.parent_path();world["format"]="IWANNA_WORLD_1";world["rooms"]=Json::Value(Json::arrayValue);world["prefabs"]=Json::Value(Json::objectValue);
    for(auto [field,suffix]:{std::pair{"roomDirectory",".room.json"},std::pair{"prefabDirectory",".prefab.json"}}){
        auto dir=Local(root,world[field].asString());std::vector<std::filesystem::path> paths;
        for(const auto& entry:std::filesystem::directory_iterator(dir))if(entry.is_regular_file()&&entry.path().filename().string().ends_with(suffix))paths.push_back(entry.path());std::sort(paths.begin(),paths.end());
        for(const auto& p:paths){auto data=Read(p);auto utf8=p.filename().u8string();std::string name(utf8.begin(),utf8.end());name.resize(name.size()-std::strlen(suffix));if(name.empty()||name=="$self")throw std::runtime_error("Invalid asset filename");
            if(std::string(field)=="prefabDirectory"){if(data["format"]!="IWANNA_PREFAB_1")throw std::runtime_error("Invalid prefab: "+p.string());world["prefabs"][name]=data;}
            else {if(data["format"]!="IWANNA_ROOM_2")throw std::runtime_error("Invalid room: "+p.string());if(data.isMember("script")&&data.isMember("scriptLua"))throw std::runtime_error("Room must use either script or scriptLua: "+p.string());Json::Value item;item["id"]=name;auto relative=p.lexically_relative(root).generic_u8string();item["map"]=std::string(relative.begin(),relative.end());item["title"]=data.get("title",name);item["hint"]=data.get("hint","EDIT THIS ROOM");if(data.isMember("script"))item["script"]=data["script"];else item["scriptLua"]=data.get("scriptLua","return {}");world["rooms"].append(item);}
        }
    }
    if(world["rooms"].empty())throw std::runtime_error("World needs at least one room");return world;
}
Json::Value ToTiled(const Json::Value& room){
    if(room["format"]!="IWANNA_ROOM_2")return room;
    const auto& g=room["grid"];int w=g["width"].asInt(),h=g["height"].asInt();float unit=g["tileSize"].asFloat();
    if(!g["width"].isInt()||!g["height"].isInt()||!g["tileSize"].isNumeric()||w<1||h<1||w>256||h>256||!std::isfinite(unit)||unit<=0||unit>10||g["origin"].size()!=2)throw std::runtime_error("Invalid room grid");
    Json::Value map;map["_native"]=room;map["width"]=w;map["height"]=h;map["tilewidth"]=32;map["tileheight"]=32;map["orientation"]="orthogonal";map["infinite"]=false;
    Set(map,"worldTileSize",unit);Set(map,"originX",g["origin"][0]);Set(map,"originY",g["origin"][1]);
    Json::Value set;set["firstgid"]=1;for(const auto& key:room["palette"].getMemberNames()){size_t n;int gid=std::stoi(key,&n);if(n!=key.size()||gid<1)throw std::runtime_error("Invalid palette GID");Json::Value t;t["id"]=gid-1;Set(t,"runtimeImage",room["palette"][key]);set["tiles"].append(t);}map["tilesets"].append(set);
    int layerID=1;for(const auto& name:room["tileLayers"].getMemberNames()){const auto& rows=room["tileLayers"][name];if(rows.size()!=h)throw std::runtime_error("Tile layer row count: "+name);Json::Value layer;layer["id"]=layerID++;layer["name"]=name;layer["type"]="tilelayer";for(const auto& row:rows){if(row.size()!=w)throw std::runtime_error("Tile layer column count: "+name);for(const auto& gid:row){if(!gid.isUInt())throw std::runtime_error("Tile GID must be unsigned integer");layer["data"].append(gid);}}map["layers"].append(layer);}
    if(layerID==1)throw std::runtime_error("Room requires a tile layer");
    Json::Value objects;objects["id"]=layerID;objects["name"]="Objects";objects["type"]="objectgroup";objects["objects"]=Json::Value(Json::arrayValue);int id=1;
    for(const auto& uid:room["entities"].getMemberNames()){
        const auto& e=room["entities"][uid];auto kind=e.get("kind","Entity").asString();if(kind!="Entity"&&kind!="Spawn"&&kind!="Label")throw std::runtime_error("Unknown entity kind: "+uid);
        if(e["position"].size()!=2)throw std::runtime_error("Entity position needs two numbers: "+uid);
        Json::Value o;o["_nativeEntity"]=e;o["id"]=id++;o["class"]=kind;o["name"]=uid;if(e.isMember("rotation"))o["rotation"]=e["rotation"];float sx=kind=="Entity"?e["size"][0].asFloat():0,sy=kind=="Entity"?e["size"][1].asFloat():0;
        o["width"]=sx/unit*32;o["height"]=sy/unit*32;o["x"]=(e["position"][0].asFloat()-sx*.5f-g["origin"][0].asFloat())/unit*32;o["y"]=(e["position"][1].asFloat()-sy*.5f-g["origin"][1].asFloat())/unit*32;
        auto properties=e.get("properties",Json::Value(Json::objectValue));if(!properties.isObject())throw std::runtime_error("Entity properties must be an object: "+uid);
        for(const auto& key:properties.getMemberNames()){const auto& v=properties[key];if(!v.isString()&&!v.isBool()&&!v.isNumeric())throw std::runtime_error("Entity properties must be scalar: "+uid+"/"+key);}
        properties["uid"]=uid;if(kind=="Entity")properties["prefab"]=e["prefab"];
        for(const auto& key:properties.getMemberNames())Set(o,key,properties[key]);objects["objects"].append(o);
    }
    map["layers"].append(objects);map["nextobjectid"]=id;return map;
}
Json::Value FromTiled(const Json::Value& map){
    auto room=map.get("_native",Json::Value(Json::objectValue));room["format"]="IWANNA_ROOM_2";auto p=Properties(map);float unit=p["worldTileSize"].asFloat(),scale=unit/map["tilewidth"].asFloat();int w=map["width"].asInt();
    room["grid"]["width"]=map["width"];room["grid"]["height"]=map["height"];room["grid"]["tileSize"]=unit;room["grid"]["origin"][0]=p["originX"];room["grid"]["origin"][1]=p["originY"];
    room["tileLayers"]=Json::Value(Json::objectValue);room["entities"]=Json::Value(Json::objectValue);
    for(const auto& l:map["layers"]){if(l["type"]=="tilelayer"){auto name=l.get("name","Terrain").asString();for(int i=0;i<int(l["data"].size());++i)room["tileLayers"][name][i/w][i%w]=l["data"][i];}
        else if(l["type"]=="objectgroup")for(const auto& o:l["objects"]){auto props=Properties(o);auto uid=props.get("uid","label_"+std::to_string(o["id"].asInt())).asString();if(room["entities"].isMember(uid))throw std::runtime_error("Duplicate entity ID: "+uid);auto e=o.get("_nativeEntity",Json::Value(Json::objectValue));e["kind"]=o.get("class",o.get("type","Entity"));
            e["position"][0]=p["originX"].asFloat()+(o["x"].asFloat()+o.get("width",0).asFloat()*.5f)*scale;e["position"][1]=p["originY"].asFloat()+(o["y"].asFloat()+o.get("height",0).asFloat()*.5f)*scale;
            if(e["kind"]=="Entity"){e["prefab"]=props["prefab"];e["size"][0]=o["width"].asFloat()*scale;e["size"][1]=o["height"].asFloat()*scale;if(o.isMember("rotation"))e["rotation"]=o["rotation"];}props.removeMember("uid");props.removeMember("prefab");e["properties"]=props;room["entities"][uid]=e;}}
    if(!room.isMember("connections"))room["connections"]=Json::Value(Json::objectValue);return room;
}
}
