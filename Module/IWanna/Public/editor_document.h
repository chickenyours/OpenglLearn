#pragma once
#include "room_data.h"
#include "content_format.h"
#include "transform_math.h"
#include <set>
#include <fstream>
#include <sstream>
#include <deque>
#include <algorithm>
#include <vector>
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
    std::string paintLayer="Terrain";
    std::vector<std::string> TileLayers()const {
        std::vector<std::string> names;for(const auto& layer:map["layers"])if(layer["type"]=="tilelayer")names.push_back(layer.get("name","Terrain").asString());return names;
    }
    std::string PaintLayer()const {
        auto names=TileLayers();if(std::find(names.begin(),names.end(),paintLayer)!=names.end())return paintLayer;
        if(std::find(names.begin(),names.end(),"Terrain")!=names.end())return "Terrain";
        return names.empty()?"":names.front();
    }
    void CycleTileLayer() {
        auto names=TileLayers();if(names.empty())return;auto current=std::find(names.begin(),names.end(),PaintLayer());
        paintLayer=names[(std::distance(names.begin(),current)+1)%names.size()];
    }
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
    std::string RoomId()const{return world["rooms"][roomIndex]["id"].asString();}
    std::vector<std::string> SpawnIds()const{
        std::vector<std::string> result;
        for(const auto& layer:map["layers"])if(layer["type"]=="objectgroup")for(const auto& object:layer["objects"])
            if(object.get("class",object.get("type","")).asString()=="Spawn")result.push_back(Props(object)["uid"].asString());
        std::sort(result.begin(),result.end());return result;
    }
    std::string DefaultSpawn()const{
        auto ids=SpawnIds();if(ids.empty())throw std::runtime_error("Room has no spawn points");
        auto preferred=world["startRoom"]==RoomId()?world["startSpawn"].asString():"left";
        return std::find(ids.begin(),ids.end(),preferred)!=ids.end()?preferred:ids.front();
    }
    Json::Value CameraConfig()const{
        auto origin=Origin();glm::vec2 extent{map["width"].asFloat()*Unit(),map["height"].asFloat()*Unit()};
        Json::Value result(Json::objectValue);result["mode"]="fixed";result["position"][0]=origin.x+extent.x*.5f;result["position"][1]=origin.y+extent.y*.5f;
        result["offset"][0]=0;result["offset"][1]=0;result["zoom"]=std::clamp(std::min(150.f/extent.x,84.375f/extent.y),.05f,8.f);result["followSpeed"]=8;result["clampToRoom"]=true;
        const auto& configured=NativeRoom()?map["_native"]["camera"]:map["camera"];
        for(const auto& name:configured.getMemberNames())result[name]=configured[name];return result;
    }
    void SetCameraField(const std::string& key,const std::string& value){
        auto numeric=[&](float min,float max){size_t n=0;float v=std::stof(value,&n);if(n!=value.size()||!std::isfinite(v)||v<min||v>max)throw std::runtime_error("Invalid camera value");return v;};
        auto current=CameraConfig();auto before=map;Begin();
        try{
            auto& camera=NativeRoom()?map["_native"]["camera"]:map["camera"];
            if(key=="mode"){if(value!="fixed"&&value!="follow")throw std::runtime_error("Use fixed or follow");camera["mode"]=value;}
            else if(key=="x"||key=="y"){camera["position"]=current["position"];camera["position"][key=="x"?0:1]=numeric(-1000,1000);}
            else if(key=="offsetX"||key=="offsetY"){camera["offset"]=current["offset"];camera["offset"][key=="offsetX"?0:1]=numeric(-1000,1000);}
            else if(key=="zoom")camera["zoom"]=numeric(.05f,8.f);
            else if(key=="followSpeed")camera["followSpeed"]=numeric(0,60);
            else if(key=="clampToRoom"){if(value!="true"&&value!="false")throw std::runtime_error("Use true or false");camera["clampToRoom"]=value=="true";}
            else throw std::runtime_error("Unknown camera property");
        }catch(...){map=before;throw;}
    }
    Json::Value& Objects(){for(auto& l:map["layers"])if(l["type"]=="objectgroup")return l["objects"];throw std::runtime_error("No object layer");}
    Json::Value* Selected(){auto& a=Objects();return selected>=0&&selected<int(a.size())?&a[selected]:nullptr;}
    Transform ObjectTransform(const Json::Value& o)const {
        Transform t;t.size={o.get("width",0).asFloat()*Scale(),o.get("height",0).asFloat()*Scale()};
        t.position=Origin()+glm::vec2(o["x"].asFloat(),o["y"].asFloat())*Scale()+t.size*.5f;
        t.rotation=o.get("rotation",Effective(o).get("rotation",0)).asFloat();return t;
    }
    Json::Value Effective(const Json::Value& o)const {
        auto p=Props(o);auto name=p.get("prefab","").asString();auto result=world["prefabs"].get(name,Json::Value(Json::objectValue));
        for(const auto& key:p.getMemberNames())result[key]=p[key];return result;
    }
    Transform Box(const Json::Value& o)const {
        auto t=ObjectTransform(o);auto p=Effective(o);
        if(p.get("detectionEnabled",false).asBool()){t.position+=RotateOffset({p.get("detectionX",0).asFloat(),p.get("detectionY",0).asFloat()},t.rotation);t.size={p.get("detectionW",t.size.x).asFloat(),p.get("detectionH",t.size.y).asFloat()};}return t;
    }
    void Move(Json::Value& o,glm::vec2 pos){auto t=ObjectTransform(o);auto px=(pos-t.size*.5f-Origin())/Scale();o["x"]=px.x;o["y"]=px.y;}
    void Resize(Json::Value& o,glm::vec2 size){auto pos=ObjectTransform(o).position;o["width"]=std::max(.1f,size.x)/Scale();o["height"]=std::max(.1f,size.y)/Scale();Move(o,pos);}
    void Paint(glm::vec2 pos,int gid){
        auto c=glm::ivec2(glm::floor((pos-Origin())/Unit()));
        int w=map["width"].asInt(),h=map["height"].asInt();
        if(c.x<0||c.y<0||c.x>=w||c.y>=h)return;
        const auto selectedLayer=PaintLayer();
        for(auto& layer:map["layers"])if(layer["type"]=="tilelayer"&&layer.get("name","Terrain").asString()==selectedLayer){
            auto& data=layer["data"];
            data[c.y*w+c.x]=gid;
            if(!NativeRoom())return;

            // Native terrain palettes can provide an image for each exposed
            // side mask. Retile the painted cell and its four neighbors while
            // retaining each cell's material and alternate visual pattern.
            std::map<int,std::string> images;
            std::map<std::string,int> gids;
            for(const auto& ref:map["tilesets"])for(const auto& tile:ref["tiles"]){
                int id=int(ref["firstgid"].asUInt()+tile["id"].asUInt());
                auto name=Props(tile)["runtimeImage"].asString();
                images[id]=name;gids[name]=id;
            }
            auto family=[](const std::string& name){
                const auto n=name.size();
                if(!name.starts_with("terrain_")||n<14||name[n-7]!='_'||
                   name[n-6]<'0'||name[n-6]>'9'||name[n-5]<'0'||name[n-5]>'9'||
                   name.compare(n-4,4,".png")!=0)return std::string{};
                return name.substr(0,n-6);
            };
            auto occupied=[&](int x,int y){return x>=0&&y>=0&&x<w&&y<h&&data[y*w+x].asInt()!=0;};
            auto retile=[&](int x,int y){
                if(!occupied(x,y))return;
                int old=data[y*w+x].asInt();auto found=images.find(old);
                if(found==images.end())return;
                auto prefix=family(found->second);if(prefix.empty())return;
                int edges=(!occupied(x,y-1)?1:0)|(!occupied(x+1,y)?2:0)|
                          (!occupied(x,y+1)?4:0)|(!occupied(x-1,y)?8:0);
                std::string filename=prefix+(edges<10?"0":"")+std::to_string(edges)+".png";
                if(auto next=gids.find(filename);next!=gids.end())data[y*w+x]=next->second;
            };
            retile(c.x,c.y);retile(c.x-1,c.y);retile(c.x+1,c.y);
            retile(c.x,c.y-1);retile(c.x,c.y+1);
            return;
        }
    }
    void Place(const std::string& prefab,glm::vec2 pos){
        Begin();auto& a=Objects();auto p=world["prefabs"][prefab];int id=std::max(1,map.get("nextobjectid",1).asInt());
        for(const auto& o:a)id=std::max(id,o["id"].asInt()+1);
        std::string uid;bool unique=false;while(!unique){uid=prefab+"_"+std::to_string(id++);unique=true;for(const auto& o:a)if(Props(o)["uid"]==uid)unique=false;}
        Json::Value o;o["id"]=id-1;o["name"]=uid;o["class"]="Entity";o["rotation"]=p.get("rotation",0);o["visible"]=true;o["x"]=0;o["y"]=0;o["width"]=p["width"].asFloat()/Scale();o["height"]=p["height"].asFloat()/Scale();
        Property(o,"uid",uid);Property(o,"prefab",prefab);if(p["role"]=="exit"){Property(o,"destinationRoom",world["startRoom"]);Property(o,"destinationSpawn",world["startSpawn"]);}
        Move(o,pos);a.append(o);selected=int(a.size())-1;map["nextobjectid"]=id;
    }
    int Hit(glm::vec2 pos){auto& a=Objects();for(int i=int(a.size())-1;i>=0;--i){auto t=ObjectTransform(a[i]);auto s=glm::max(t.size,glm::vec2(1));auto d=glm::abs(RotateOffset(pos-t.position,-t.rotation));if(d.x<=s.x*.5f&&d.y<=s.y*.5f)return i;}return -1;}
    void Erase(){if(!Selected())return;Begin();auto indices=selection;if(indices.empty())indices.insert(selected);for(auto i=indices.rbegin();i!=indices.rend();++i){Json::Value removed;Objects().removeIndex(*i,&removed);}selected=-1;selection.clear();}
    void EnableBox(){auto* o=Selected();if(!o)return;Begin();auto t=Box(*o);auto base=ObjectTransform(*o);auto offset=RotateOffset(t.position-base.position,-base.rotation);Property(*o,"detectionEnabled",true);Property(*o,"collide",true);Property(*o,"detectionX",offset.x);Property(*o,"detectionY",offset.y);Property(*o,"detectionW",t.size.x);Property(*o,"detectionH",t.size.y);}
    void SetField(const std::string& key,const std::string& value){
        auto* o=Selected();if(!o)return;Json::Value before=map;Begin();
        try{
            if(key=="x"||key=="y"||key=="width"||key=="height"||key=="rotation"){
                size_t n=0;float v=std::stof(value,&n);if(n!=value.size()||!std::isfinite(v))throw std::runtime_error("Enter a finite number");auto t=ObjectTransform(*o);
                if(key=="x"||key=="y"){t.position[key=="x"?0:1]=v;Move(*o,t.position);}else if(key=="rotation"){if(std::abs(v)>3600)throw std::runtime_error("Rotation must be within -3600..3600");(*o)["rotation"]=v;}
                else{if(v<=0)throw std::runtime_error("Size must be positive");t.size[key=="width"?0:1]=v;Resize(*o,t.size);}
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
    void SelectArea(glm::vec2 a,glm::vec2 b){selection.clear();auto lo=glm::min(a,b),hi=glm::max(a,b);for(int i=0;i<int(Objects().size());++i){bool inside=true;for(auto point:Corners(ObjectTransform(Objects()[i])))if(point.x<lo.x||point.y<lo.y||point.x>hi.x||point.y>hi.y)inside=false;if(inside)selection.insert(i);}selected=selection.empty()?-1:*selection.begin();}
    void MoveSelection(glm::vec2 pos){if(!Selected())return;auto delta=pos-ObjectTransform(*Selected()).position;if(selection.empty())selection.insert(selected);for(int i:selection)Move(Objects()[i],ObjectTransform(Objects()[i]).position+delta);}
    void Duplicate(glm::vec2 offset){
        if(!Selected())return;Begin();auto indices=selection;if(indices.empty())indices.insert(selected);std::set<int> copied;std::map<std::string,std::string> names;
        for(int i:indices){auto o=Objects()[i];auto props=Props(o);auto base=props.get("uid","object").asString();int n=1;std::string uid;bool exists;
            do{uid=base+"_copy"+std::to_string(n++);exists=false;for(const auto& x:Objects())if(Props(x)["uid"]==uid)exists=true;}while(exists);
            names[base]=uid;int id=map["nextobjectid"].asInt();for(const auto& x:Objects())id=std::max(id,x["id"].asInt()+1);o["id"]=id;map["nextobjectid"]=id+1;Property(o,"uid",uid);o["name"]=uid;Move(o,ObjectTransform(o).position+offset);Objects().append(o);copied.insert(int(Objects().size())-1);
        }
        for(int i:copied){auto p=Props(Objects()[i]);for(const auto& key:{"target","targetId","spawnAnchor","effectAnchor"})if(p[key].isString()&&names.contains(p[key].asString()))Property(Objects()[i],key,names.at(p[key].asString()));}
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
        const bool externalScript=data.isMember("script")&&!data.isMember("scriptLua");auto originalScript=externalScript?worldPath.parent_path()/Content::Path(data["script"].asString()):std::filesystem::path{};
        auto scriptPath=worldPath.parent_path()/"scripts"/(name+".lua");if(externalScript){if(std::filesystem::exists(scriptPath))throw std::runtime_error("Room script already exists");data["script"]="scripts/"+name+".lua";}
        if(!clone){data["hint"]="NEW ROOM";if(!externalScript)data["scriptLua"]="return {}";data["connections"]=Json::Value(Json::objectValue);data["boundaries"]=Json::Value(Json::objectValue);data["entities"]=Json::Value(Json::objectValue);int w=data["grid"]["width"].asInt(),h=data["grid"]["height"].asInt();data["tileLayers"]=Json::Value(Json::objectValue);for(int y=0;y<h;++y)for(int x=0;x<w;++x)data["tileLayers"]["Terrain"][y][x]=y==h-3?1:0;auto& spawn=data["entities"]["left"];spawn["kind"]="Spawn";spawn["position"][0]=Origin().x+Unit()*3;spawn["position"][1]=Origin().y+(h-5)*Unit();spawn["properties"]=Json::Value(Json::objectValue);}
        bool scriptCreated=false;try{
            if(externalScript){std::filesystem::create_directories(scriptPath.parent_path());if(clone)std::filesystem::copy_file(originalScript,scriptPath);else {std::ofstream script(scriptPath);script<<"return {}\n";if(!script)throw std::runtime_error("Cannot create room script");}scriptCreated=true;}
            Content::Write(path,data);Refresh();
        }catch(...){if(std::filesystem::exists(path))std::filesystem::remove(path);if(scriptCreated)std::filesystem::remove(scriptPath);throw;}
        for(int i=0;i<int(world["rooms"].size());++i)if(world["rooms"][i]["id"]==name)LoadRoom(i);
    }
    void DeleteRoom(){
        if(!nativeWorld||Dirty())throw std::runtime_error("Save a WORLD_2 room before deleting");if(world["rooms"].size()<2)throw std::runtime_error("Cannot delete the last room");auto id=world["rooms"][roomIndex]["id"].asString();
        for(const auto& [other,r]:catalog.rooms)if(other!=id){for(const auto& o:r.objects)if(o.role==Role::Exit&&o.destinationRoom==id)throw std::runtime_error("Room referenced by portal: "+other+"/"+o.id);for(const auto& [edge,c]:r.connections)if(c.room==id)throw std::runtime_error("Room referenced by boundary: "+other+"/"+edge);}
        auto config=Read(worldPath);auto original=config;if(config["startRoom"]==id){for(const auto& [other,r]:catalog.rooms)if(other!=id){config["startRoom"]=other;config["startSpawn"]=r.spawns.begin()->first;break;}}
        auto trash=mapPath.parent_path()/".trash";std::filesystem::create_directories(trash);auto dest=trash/mapPath.filename();int n=1;while(std::filesystem::exists(dest))dest=trash/(mapPath.filename().string()+"."+std::to_string(n++));auto old=mapPath;
        std::filesystem::rename(old,dest);try{Content::Write(worldPath,config);Refresh();}catch(...){std::filesystem::rename(dest,old);Content::Write(worldPath,original);throw;}
    }
    Json::Value BoundaryRules()const {
        auto rules=map["_native"]["boundaries"];
        const auto& old=map["_native"]["connections"];
        for(const auto& edge:old.getMemberNames())if(!rules.isMember(edge)){rules[edge]=old[edge];rules[edge]["action"]="transfer";}
        return rules;
    }
    void Boundary(const std::string& selector,const std::string& destination){
        if(!NativeRoom())throw std::runtime_error("Boundary links require native room assets");
        auto number=[](const std::string& s){size_t n=0;float v=std::stof(s,&n);if(n!=s.size()||!std::isfinite(v))throw std::runtime_error("Invalid boundary coordinate");return v;};
        const auto bracket=selector.find('[');const auto edge=selector.substr(0,bracket);std::optional<glm::vec2> range;
        if(edge!="left"&&edge!="right"&&edge!="top"&&edge!="bottom")throw std::runtime_error("Use left/right/top/bottom, optionally [min,max]");
        if(bracket!=std::string::npos){
            if(selector.back()!=']')throw std::runtime_error("Use edge[min,max]=rule");
            auto values=selector.substr(bracket+1,selector.size()-bracket-2);auto comma=values.find(',');
            if(comma==std::string::npos)throw std::runtime_error("Use edge[min,max]=rule");
            range=glm::vec2(number(values.substr(0,comma)),number(values.substr(comma+1)));
            int axis=edge=="left"||edge=="right"?1:0;
            float low=Origin()[axis],high=low+map[axis==0?"width":"height"].asFloat()*Unit();
            if(range->x>=range->y||range->x<low||range->y>high)throw std::runtime_error("Boundary range must be ordered and inside the room edge");
        }
        auto current=BoundaryRules()[edge];if(current.isNull())current["action"]="death";
        int segmentIndex=-1;
        if(range)for(int i=0;i<int(current["segments"].size());++i){const auto& old=current["segments"][i]["range"];
            if(old[0].asFloat()==range->x&&old[1].asFloat()==range->y)segmentIndex=i;
            else if(destination!="-"&&range->x<old[1].asFloat()&&range->y>old[0].asFloat())throw std::runtime_error("Boundary segments cannot overlap");
        }
        if(destination=="-"){
            if(range&&segmentIndex<0)throw std::runtime_error("No boundary segment has that exact range");
            Begin();map["_native"]["connections"].removeMember(edge);
            if(!range)map["_native"]["boundaries"].removeMember(edge);
            else {Json::Value removed;current["segments"].removeIndex(segmentIndex,&removed);if(current["segments"].empty())current.removeMember("segments");map["_native"]["boundaries"][edge]=current;}
            return;
        }
        Json::Value rule(Json::objectValue);
        if(destination=="death"||destination=="ignore")rule["action"]=destination;
        else {
        auto slash=destination.find('/');if(slash==std::string::npos)throw std::runtime_error("Use edge[min,max]=room/spawn@x,y, death, ignore or -");
        auto room=destination.substr(0,slash),target=destination.substr(slash+1),actual=room=="$self"?RoomId():room;
        auto at=target.find('@');auto spawn=target.substr(0,at);std::optional<glm::vec2> arrival;
        if(at!=std::string::npos){auto point=target.substr(at+1);auto comma=point.find(',');if(comma==std::string::npos)throw std::runtime_error("Use @x,y for arrival position");
            arrival=glm::vec2(number(point.substr(0,comma)),number(point.substr(comma+1)));}
        if(!catalog.rooms.contains(actual)||(!spawn.empty()&&!catalog.rooms.at(actual).spawns.contains(spawn)))throw std::runtime_error("Unknown destination room/spawn");
        if(spawn.empty()&&!arrival)throw std::runtime_error("Transfer needs spawn or position");
        if(arrival){const auto& targetRoom=catalog.rooms.at(actual);if(arrival->x<=targetRoom.origin.x||arrival->y<=targetRoom.origin.y||arrival->x>=targetRoom.origin.x+targetRoom.extent.x||arrival->y>=targetRoom.origin.y+targetRoom.extent.y)throw std::runtime_error("Arrival must be inside destination room");}
        rule["action"]="transfer";rule["room"]=room;if(!spawn.empty())rule["spawn"]=spawn;if(arrival){rule["position"][0]=arrival->x;rule["position"][1]=arrival->y;}
        }
        if(range){rule["range"][0]=range->x;rule["range"][1]=range->y;if(segmentIndex>=0)current["segments"][segmentIndex]=rule;else current["segments"].append(rule);}
        else {if(current.isMember("segments"))rule["segments"]=current["segments"];current=std::move(rule);}
        Begin();map["_native"]["connections"].removeMember(edge);map["_native"]["boundaries"][edge]=std::move(current);
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
