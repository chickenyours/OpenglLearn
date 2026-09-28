#pragma once
#include "room_data.h"
#include "content_format.h"
#include <set>
#include <fstream>
#include <sstream>
#include <deque>
namespace IWanna {
// Lossless Tiled document: editing preserves unknown metadata and stable IDs.
class EditorDocument {
    std::deque<Json::Value> undo_,redo_;
    Json::Value saved_;
public:
    Json::Value world,map;
    bool nativeWorld=false;std::set<int> selection;
    bool NativeRoom()const{return map.isMember("_native");}
    RoomCatalog catalog;
    std::filesystem::path worldPath,mapPath;
    int roomIndex=0,selected=-1;
    static Json::Value Read(const std::filesystem::path& p) {
        std::ifstream f(p);Json::Value v;Json::CharReaderBuilder b;std::string e;
        if(!f||!Json::parseFromStream(b,f,&v,&e))throw std::runtime_error("Cannot read "+p.string()+e);return v;
    }
    static Json::Value Props(const Json::Value& o) {Json::Value p(Json::objectValue);for(const auto& v:o["properties"])p[v["name"].asString()]=v["value"];return p;}
    static void Property(Json::Value& o,const std::string& key,const Json::Value& v) {
        auto& list=o["properties"];for(auto& p:list)if(p["name"]==key){p["value"]=v;p["type"]=v.isBool()?"bool":v.isNumeric()?"float":"string";return;}
        Json::Value p;p["name"]=key;p["value"]=v;p["type"]=v.isBool()?"bool":v.isNumeric()?"float":"string";list.append(p);
    }
    void Open(const std::filesystem::path& p){worldPath=std::filesystem::absolute(p);nativeWorld=Read(worldPath)["format"]=="IWANNA_WORLD_2";world=Content::ExpandWorld(worldPath);catalog=RoomCatalog::Load(worldPath);LoadRoom(0);}
    void LoadRoom(int index) {
        if(index<0||index>=int(world["rooms"].size()))return;
        roomIndex=index;mapPath=worldPath.parent_path()/Content::Path(world["rooms"][index]["map"].asString());map=Content::ToTiled(Read(mapPath));saved_=map;selected=-1;selection.clear();undo_.clear();redo_.clear();
    }
    bool Dirty()const{return map!=saved_;}
    void Begin(){if(undo_.empty()||undo_.back()!=map){undo_.push_back(map);if(undo_.size()>128)undo_.pop_front();}redo_.clear();}
    void Undo(){if(undo_.empty())return;redo_.push_back(map);map=undo_.back();undo_.pop_back();selected=-1;selection.clear();}
    void Redo(){if(redo_.empty())return;undo_.push_back(map);map=redo_.back();redo_.pop_back();selected=-1;selection.clear();}
    float Unit()const{return Props(map)["worldTileSize"].asFloat();}
    float Scale()const{return Unit()/map["tilewidth"].asFloat();}
    glm::vec2 Origin()const{auto p=Props(map);return {p["originX"].asFloat(),p["originY"].asFloat()};}
    Json::Value& Objects(){for(auto& l:map["layers"])if(l["type"]=="objectgroup")return l["objects"];throw std::runtime_error("No object layer");}
    Json::Value* Selected(){auto& a=Objects();return selected>=0&&selected<int(a.size())?&a[selected]:nullptr;}
    Transform ObjectTransform(const Json::Value& o)const {
        Transform t;t.size={o.get("width",0).asFloat()*Scale(),o.get("height",0).asFloat()*Scale()};
        t.position=Origin()+glm::vec2(o["x"].asFloat(),o["y"].asFloat())*Scale()+t.size*.5f;return t;
    }
    Json::Value Effective(const Json::Value& o)const {
        auto p=Props(o);auto name=p.get("prefab","").asString();auto result=world["prefabs"].get(name,Json::Value(Json::objectValue));
        for(const auto& key:p.getMemberNames())result[key]=p[key];return result;
    }
    Transform Box(const Json::Value& o)const {
        auto t=ObjectTransform(o);auto p=Effective(o);
        if(p.get("detectionEnabled",false).asBool()){t.position+=glm::vec2(p.get("detectionX",0).asFloat(),p.get("detectionY",0).asFloat());t.size={p.get("detectionW",t.size.x).asFloat(),p.get("detectionH",t.size.y).asFloat()};}return t;
    }
    void Move(Json::Value& o,glm::vec2 pos){auto t=ObjectTransform(o);auto px=(pos-t.size*.5f-Origin())/Scale();o["x"]=px.x;o["y"]=px.y;}
    void Resize(Json::Value& o,glm::vec2 size){auto pos=ObjectTransform(o).position;o["width"]=std::max(.1f,size.x)/Scale();o["height"]=std::max(.1f,size.y)/Scale();Move(o,pos);}
    void Paint(glm::vec2 pos,int gid){auto c=glm::ivec2(glm::floor((pos-Origin())/Unit()));int w=map["width"].asInt(),h=map["height"].asInt();if(c.x<0||c.y<0||c.x>=w||c.y>=h)return;for(auto& l:map["layers"])if(l["type"]=="tilelayer"){l["data"][c.y*w+c.x]=gid;return;}}
    void Place(const std::string& prefab,glm::vec2 pos){
        Begin();auto& a=Objects();auto p=world["prefabs"][prefab];int id=std::max(1,map.get("nextobjectid",1).asInt());
        for(const auto& o:a)id=std::max(id,o["id"].asInt()+1);
        std::string uid;bool unique=false;while(!unique){uid=prefab+"_"+std::to_string(id++);unique=true;for(const auto& o:a)if(Props(o)["uid"]==uid)unique=false;}
        Json::Value o;o["id"]=id-1;o["name"]=uid;o["class"]="Entity";o["rotation"]=0;o["visible"]=true;o["x"]=0;o["y"]=0;o["width"]=p["width"].asFloat()/Scale();o["height"]=p["height"].asFloat()/Scale();
        Property(o,"uid",uid);Property(o,"prefab",prefab);if(p["role"]=="exit"){Property(o,"destinationRoom",world["startRoom"]);Property(o,"destinationSpawn",world["startSpawn"]);}
        Move(o,pos);a.append(o);selected=int(a.size())-1;map["nextobjectid"]=id;
    }
    int Hit(glm::vec2 pos){auto& a=Objects();for(int i=int(a.size())-1;i>=0;--i){auto t=ObjectTransform(a[i]);auto s=glm::max(t.size,glm::vec2(1));auto d=glm::abs(pos-t.position);if(d.x<=s.x*.5f&&d.y<=s.y*.5f)return i;}return -1;}
    void Erase(){if(!Selected())return;Begin();auto indices=selection;if(indices.empty())indices.insert(selected);for(auto i=indices.rbegin();i!=indices.rend();++i){Json::Value removed;Objects().removeIndex(*i,&removed);}selected=-1;selection.clear();}
    void EnableBox(){auto* o=Selected();if(!o)return;Begin();auto t=Box(*o);auto base=ObjectTransform(*o);Property(*o,"detectionEnabled",true);Property(*o,"collide",true);Property(*o,"detectionX",t.position.x-base.position.x);Property(*o,"detectionY",t.position.y-base.position.y);Property(*o,"detectionW",t.size.x);Property(*o,"detectionH",t.size.y);}
    void SetField(const std::string& key,const std::string& value){
        auto* o=Selected();if(!o)return;Json::Value before=map;Begin();
        try{
            if(key=="x"||key=="y"||key=="width"||key=="height"){
                size_t n=0;float v=std::stof(value,&n);if(n!=value.size()||!std::isfinite(v))throw std::runtime_error("Enter a finite number");auto t=ObjectTransform(*o);
                if(key=="x"||key=="y"){t.position[key=="x"?0:1]=v;Move(*o,t.position);}else{if(v<=0)throw std::runtime_error("Size must be positive");t.size[key=="width"?0:1]=v;Resize(*o,t.size);}
            }else{
                auto current=Props(*o);Json::Value v;std::string error;Json::CharReaderBuilder b;b["failIfExtra"]=true;std::istringstream in(value);
                if(!Json::parseFromStream(b,in,&v,&error))v=value;
                if(v.isArray()||v.isObject()||v.isNull())throw std::runtime_error("Use string, number or boolean properties");
                if(key=="uid"||key=="prefab"||key=="event"||key=="destinationRoom"||key=="destinationSpawn"||key=="text")v=value;
                if(key=="role"||key=="image"||key=="pixelArt")throw std::runtime_error("Template field: edit world.json instead");
                if(key=="prefab"&&!catalog.prefabs.contains(v.asString()))throw std::runtime_error("Unknown prefab");
                if(key=="uid"){if(value.empty()||value=="player")throw std::runtime_error("Invalid stable ID");for(int i=0;i<int(Objects().size());++i)if(i!=selected&&Props(Objects()[i])["uid"]==value)throw std::runtime_error("Duplicate stable ID");}
                if(key=="visible"||key=="collide"||key=="detectionEnabled")if(!v.isBool())throw std::runtime_error("Enter true or false");
                if(key.starts_with("detection")&&key!="detectionEnabled")if(!v.isNumeric()||!std::isfinite(v.asFloat())||((key=="detectionW"||key=="detectionH")&&v.asFloat()<=0))throw std::runtime_error("Invalid box number");
                Property(*o,key,v);
            }
        }catch(...){map=before;throw;}
    }
    void Refresh(){
        if(Dirty())throw std::runtime_error("Save or undo edits before refreshing");
        auto oldId=world["rooms"][roomIndex]["id"].asString();auto candidate=Content::ExpandWorld(worldPath);auto validated=RoomCatalog::Load(worldPath);
        world=std::move(candidate);catalog=std::move(validated);int index=0;for(int i=0;i<int(world["rooms"].size());++i)if(world["rooms"][i]["id"]==oldId)index=i;LoadRoom(index);
    }
    void SelectArea(glm::vec2 a,glm::vec2 b){selection.clear();auto lo=glm::min(a,b),hi=glm::max(a,b);for(int i=0;i<int(Objects().size());++i){auto t=ObjectTransform(Objects()[i]);if(t.position.x-t.size.x*.5f>=lo.x&&t.position.y-t.size.y*.5f>=lo.y&&t.position.x+t.size.x*.5f<=hi.x&&t.position.y+t.size.y*.5f<=hi.y)selection.insert(i);}selected=selection.empty()?-1:*selection.begin();}
    void MoveSelection(glm::vec2 pos){if(!Selected())return;auto delta=pos-ObjectTransform(*Selected()).position;if(selection.empty())selection.insert(selected);for(int i:selection)Move(Objects()[i],ObjectTransform(Objects()[i]).position+delta);}
    void Duplicate(glm::vec2 offset){
        if(!Selected())return;Begin();auto indices=selection;if(indices.empty())indices.insert(selected);std::set<int> copied;std::map<std::string,std::string> names;
        for(int i:indices){auto o=Objects()[i];auto props=Props(o);auto base=props.get("uid","object").asString();int n=1;std::string uid;bool exists;
            do{uid=base+"_copy"+std::to_string(n++);exists=false;for(const auto& x:Objects())if(Props(x)["uid"]==uid)exists=true;}while(exists);
            names[base]=uid;int id=map["nextobjectid"].asInt();for(const auto& x:Objects())id=std::max(id,x["id"].asInt()+1);o["id"]=id;map["nextobjectid"]=id+1;Property(o,"uid",uid);o["name"]=uid;Move(o,ObjectTransform(o).position+offset);Objects().append(o);copied.insert(int(Objects().size())-1);
        }
        for(int i:copied){auto p=Props(Objects()[i]);for(const auto& key:{"target","targetId"})if(p[key].isString()&&names.contains(p[key].asString()))Property(Objects()[i],key,names.at(p[key].asString()));}
        selection=copied;selected=*copied.begin();
    }
    glm::vec2 Snap(glm::vec2 position,float tolerance) {
        if(!Selected())return position;auto t=ObjectTransform(*Selected());glm::vec2 best(tolerance),correction{};
        auto consider=[&](int axis,float source,float target){float d=target-source;if(std::abs(d)<best[axis]){best[axis]=std::abs(d);correction[axis]=d;}};
        for(int axis=0;axis<2;++axis)for(float anchor:{-.5f,0.f,.5f}){float v=position[axis]+t.size[axis]*anchor;consider(axis,v,Origin()[axis]+std::round((v-Origin()[axis])/Unit())*Unit());}
        for(int i=0;i<int(Objects().size());++i)if(i!=selected&&!selection.contains(i)){auto other=ObjectTransform(Objects()[i]);if(glm::length(other.position-position)>std::max(t.size.x+other.size.x,t.size.y+other.size.y)+Unit()*2)continue;for(int axis=0;axis<2;++axis)for(float a:{-.5f,0.f,.5f})for(float b:{-.5f,0.f,.5f})consider(axis,position[axis]+t.size[axis]*a,other.position[axis]+other.size[axis]*b);}
        return position+correction;
    }
    void NewRoom(const std::string& name,bool clone){
        if(!nativeWorld)throw std::runtime_error("Room assets require a WORLD_2 project");if(Dirty())throw std::runtime_error("Save before creating rooms");
        if(name.empty()||name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw std::runtime_error("Use letters, numbers, underscore or dash");
        auto config=Read(worldPath);auto path=worldPath.parent_path()/config["roomDirectory"].asString()/(name+".room.json");if(std::filesystem::exists(path))throw std::runtime_error("Room already exists");auto data=Content::FromTiled(map);data["title"]=name;
        if(!clone){data["hint"]="NEW ROOM";data["scriptLua"]="return {}";data["connections"]=Json::Value(Json::objectValue);data["entities"]=Json::Value(Json::objectValue);int w=data["grid"]["width"].asInt(),h=data["grid"]["height"].asInt();data["tileLayers"]=Json::Value(Json::objectValue);for(int y=0;y<h;++y)for(int x=0;x<w;++x)data["tileLayers"]["Terrain"][y][x]=y==h-3?1:0;auto& spawn=data["entities"]["left"];spawn["kind"]="Spawn";spawn["position"][0]=Origin().x+Unit()*3;spawn["position"][1]=Origin().y+(h-5)*Unit();spawn["properties"]=Json::Value(Json::objectValue);}
        Content::Write(path,data);try{Refresh();}catch(...){std::filesystem::remove(path);throw;}for(int i=0;i<int(world["rooms"].size());++i)if(world["rooms"][i]["id"]==name)LoadRoom(i);
    }
    void DeleteRoom(){
        if(!nativeWorld||Dirty())throw std::runtime_error("Save a WORLD_2 room before deleting");if(world["rooms"].size()<2)throw std::runtime_error("Cannot delete the last room");auto id=world["rooms"][roomIndex]["id"].asString();
        for(const auto& [other,r]:catalog.rooms)if(other!=id){for(const auto& o:r.objects)if(o.role==Role::Exit&&o.destinationRoom==id)throw std::runtime_error("Room referenced by portal: "+other+"/"+o.id);for(const auto& [edge,c]:r.connections)if(c.room==id)throw std::runtime_error("Room referenced by boundary: "+other+"/"+edge);}
        auto config=Read(worldPath);auto original=config;if(config["startRoom"]==id){for(const auto& [other,r]:catalog.rooms)if(other!=id){config["startRoom"]=other;config["startSpawn"]=r.spawns.begin()->first;break;}}
        auto trash=mapPath.parent_path()/".trash";std::filesystem::create_directories(trash);auto dest=trash/mapPath.filename();int n=1;while(std::filesystem::exists(dest))dest=trash/(mapPath.filename().string()+"."+std::to_string(n++));auto old=mapPath;
        std::filesystem::rename(old,dest);try{Content::Write(worldPath,config);Refresh();}catch(...){std::filesystem::rename(dest,old);Content::Write(worldPath,original);throw;}
    }
    void Boundary(const std::string& edge,const std::string& destination){
        if(!NativeRoom())throw std::runtime_error("Boundary links require native room assets");if(edge!="left"&&edge!="right"&&edge!="top"&&edge!="bottom")throw std::runtime_error("Use left/right/top/bottom");
        if(destination=="-"){Begin();map["_native"]["connections"].removeMember(edge);return;}
        auto slash=destination.find('/');if(slash==std::string::npos)throw std::runtime_error("Use edge=room/spawn or edge=-");auto room=destination.substr(0,slash),spawn=destination.substr(slash+1);auto actual=room=="$self"?world["rooms"][roomIndex]["id"].asString():room;
        if(!catalog.rooms.contains(actual)||!catalog.rooms.at(actual).spawns.contains(spawn))throw std::runtime_error("Unknown destination room/spawn");Begin();auto& c=map["_native"]["connections"][edge];c["room"]=room;c["spawn"]=spawn;
    }
    void Save(){
        auto temp=mapPath;temp+=".editor.tmp";auto backup=mapPath;backup+=".editor.bak";
        Content::Write(temp,NativeRoom()?Content::FromTiled(map):map);
        auto validationPath=worldPath;validationPath+=".editor.tmp";
        RoomCatalog validated;
        try {
            auto candidate=world;auto relative=temp.lexically_relative(worldPath.parent_path()).generic_u8string();candidate["rooms"][roomIndex]["map"]=std::string(relative.begin(),relative.end());
            {std::ofstream f(validationPath);f<<candidate;if(!f)throw std::runtime_error("Cannot write validation world");}
            validated=RoomCatalog::Load(validationPath);
            std::filesystem::remove(validationPath);
        }catch(...){std::filesystem::remove(validationPath);std::filesystem::remove(temp);throw;}
        std::filesystem::copy_file(mapPath,backup,std::filesystem::copy_options::overwrite_existing);
        // Both renames stay on the same volume; restore the original on failure.
        auto old=mapPath;old+=".editor.old";if(std::filesystem::exists(old))throw std::runtime_error("Recovery file exists: "+old.string());
        std::filesystem::rename(mapPath,old);try{std::filesystem::rename(temp,mapPath);}catch(...){std::filesystem::rename(old,mapPath);throw;}
        std::filesystem::remove(old);saved_=map;catalog=std::move(validated);
    }
};
}
