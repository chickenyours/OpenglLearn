#include "IWanna/Public/room_data.h"
#include "IWanna/Public/content_format.h"
#include <json/json.h>
#include <fstream>
#include <set>
#include <stdexcept>
#include <cmath>
#include <algorithm>
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
    if(p.isMember("opacity")) {
        const auto& opacity=p["opacity"];
        if(!opacity.isNumeric()||!std::isfinite(opacity.asDouble())||opacity.asDouble()<0||opacity.asDouble()>1)
            throw std::runtime_error("Prefab/instance opacity must be finite and in [0, 1]");
        o.sprite.opacity=opacity.asFloat();
    }
    if(p.isMember("receivesShots")&&!p["receivesShots"].isBool())throw std::runtime_error("receivesShots must be boolean");
    if(p.isMember("group")&&!p["group"].isString())throw std::runtime_error("group must be string");
    if(p.isMember("despawnAfter")&&(!p["despawnAfter"].isNumeric()||!std::isfinite(p["despawnAfter"].asDouble())||p["despawnAfter"].asDouble()<=0||p["despawnAfter"].asDouble()>60))throw std::runtime_error("Invalid prefab despawnAfter");
    o.properties=p;o.collide=p.get("collide",o.collide).asBool();
    o.detectionBox=p.get("detectionEnabled",o.detectionBox).asBool();
    o.detectionOffset={p.get("detectionX",0).asFloat(),p.get("detectionY",0).asFloat()};
    o.detectionSize={p.get("detectionW",o.transform.size.x).asFloat(),p.get("detectionH",o.transform.size.y).asFloat()};
    if(!std::isfinite(o.detectionOffset.x)||!std::isfinite(o.detectionOffset.y)||!std::isfinite(o.detectionSize.x)||!std::isfinite(o.detectionSize.y)||o.detectionSize.x<=0||o.detectionSize.y<=0)throw std::runtime_error("Invalid detection rectangle");
}
Role ParseRole(const std::string& role) {
    static const std::map<std::string,Role> names={{"solid",Role::Solid},{"hazard",Role::Hazard},{"checkpoint",Role::Checkpoint},
        {"trigger",Role::Trigger},{"exit",Role::Exit},{"decoration",Role::Decoration},{"projectile",Role::Projectile}};
    auto found=names.find(role);if(found==names.end())throw std::runtime_error("Unknown room role: "+role);return found->second;
}
std::string ImageName(const Json::Value& v,const char* key) {
    auto value=String(v,key);if(std::filesystem::path(value).filename().string()!=value)throw std::runtime_error("Runtime images must be filenames");return value;
}
void Animation(Sprite& sprite,const Json::Value& value) {
    if(value.isNull())return;
    if(!value.isObject())throw std::runtime_error("Prefab animation must be an object");
    for(const auto& key:value.getMemberNames())
        if(key!="columns"&&key!="rows"&&key!="cellWidth"&&key!="cellHeight"&&key!="frames"&&key!="duration"&&key!="loop")
            throw std::runtime_error("Unknown prefab animation field: "+key);
    auto integer=[&](const char* key,int fallback,int maximum) {
        if(!value.isMember(key))return fallback;
        const auto& number=value[key];
        if(!number.isInt()||number.asInt()<1||number.asInt()>maximum)
            throw std::runtime_error(std::string("Invalid prefab animation ")+key);
        return number.asInt();
    };
    sprite.columns=integer("columns",1,64);sprite.rows=integer("rows",1,64);
    sprite.cellWidth=integer("cellWidth",0,4096);sprite.cellHeight=integer("cellHeight",0,4096);
    const int count=sprite.columns*sprite.rows;
    if(count>1024)throw std::runtime_error("Prefab animation has too many cells");
    sprite.duration=value.isMember("duration")?Number(value,"duration"):1.f;
    if(sprite.duration<=0||sprite.duration>60)throw std::runtime_error("Invalid prefab animation duration");
    if(value.isMember("loop")&&!value["loop"].isBool())throw std::runtime_error("Prefab animation loop must be boolean");
    sprite.loop=value.get("loop",true).asBool();sprite.elapsed=0;sprite.frames.clear();
    if(value.isMember("frames")) {
        const auto& frames=value["frames"];
        if(!frames.isArray()||frames.empty()||frames.size()>1024)throw std::runtime_error("Invalid prefab animation frame sequence");
        for(const auto& frame:frames) {
            if(!frame.isInt()||frame.asInt()<0||frame.asInt()>=count)throw std::runtime_error("Prefab animation frame is outside its sheet");
            sprite.frames.push_back(frame.asInt());
        }
    }else for(int i=0;i<count;++i)sprite.frames.push_back(i);
}
RoomDefinition::BoundaryTarget BoundaryTarget(const Json::Value& c,const std::string& where,const char* extraField) {
    if(!c.isObject())throw std::runtime_error("Boundary rule must be an object: "+where);
    RoomDefinition::BoundaryTarget rule;auto action=String(c,"action");
    if(action=="transfer"){
        rule.action=RoomDefinition::BoundaryRule::Action::Transfer;rule.room=String(c,"room");
        if(c.isMember("spawn"))rule.spawn=String(c,"spawn");
        if(c.isMember("position")){
            const auto& p=c["position"];
            if(!p.isArray()||p.size()!=2||!p[0].isNumeric()||!p[1].isNumeric()||!std::isfinite(p[0].asDouble())||!std::isfinite(p[1].asDouble()))throw std::runtime_error("Invalid boundary arrival position: "+where);
            rule.position=glm::vec2(p[0].asFloat(),p[1].asFloat());
            if(!std::isfinite(rule.position->x)||!std::isfinite(rule.position->y))throw std::runtime_error("Boundary arrival position is out of range: "+where);
        }
        if(rule.spawn.empty()&&!rule.position)throw std::runtime_error("Boundary transfer needs a spawn or position: "+where);
    }else if(action=="ignore")rule.action=RoomDefinition::BoundaryRule::Action::Ignore;
    else if(action!="death")throw std::runtime_error("Unknown boundary action: "+action);
    for(const auto& key:c.getMemberNames())if(key!="action"&&key!="room"&&key!="spawn"&&key!="position"&&key!=extraField)throw std::runtime_error("Unknown boundary field: "+where+"/"+key);
    if(rule.action!=RoomDefinition::BoundaryRule::Action::Transfer&&(c.isMember("room")||c.isMember("spawn")||c.isMember("position")))throw std::runtime_error("Only transfer boundaries have targets: "+where);
    return rule;
}
RoomDefinition ImportTiled(const std::filesystem::path& path,const std::map<std::string,RoomObject>& prefabs) {
    auto source=Content::Read(path);auto map=Content::ToTiled(source);RoomDefinition room;
    for(const auto& edge:source["connections"].getMemberNames()){
        if(edge!="left"&&edge!="right"&&edge!="top"&&edge!="bottom")throw std::runtime_error("Invalid boundary: "+edge);
        const auto& c=source["connections"][edge];RoomDefinition::BoundaryRule rule;rule.action=RoomDefinition::BoundaryRule::Action::Transfer;
        rule.room=String(c,"room");rule.spawn=String(c,"spawn");room.boundaries[edge]=rule;
    }
    for(const auto& edge:source["boundaries"].getMemberNames()){
        if(edge!="left"&&edge!="right"&&edge!="top"&&edge!="bottom")throw std::runtime_error("Invalid boundary: "+edge);
        const auto& c=source["boundaries"][edge];RoomDefinition::BoundaryRule rule;
        static_cast<RoomDefinition::BoundaryTarget&>(rule)=BoundaryTarget(c,edge,"segments");
        if(c.isMember("segments")){
            if(!c["segments"].isArray())throw std::runtime_error("Boundary segments must be an array: "+edge);
            for(Json::ArrayIndex i=0;i<c["segments"].size();++i){
                const auto& s=c["segments"][i];const auto where=edge+"["+std::to_string(i)+"]";
                RoomDefinition::BoundarySegment segment;
                static_cast<RoomDefinition::BoundaryTarget&>(segment)=BoundaryTarget(s,where,"range");
                const auto& range=s["range"];
                if(!range.isArray()||range.size()!=2||!range[0].isNumeric()||!range[1].isNumeric()||!std::isfinite(range[0].asDouble())||!std::isfinite(range[1].asDouble()))throw std::runtime_error("Invalid boundary segment range: "+where);
                segment.range={range[0].asFloat(),range[1].asFloat()};
                if(!std::isfinite(segment.range.x)||!std::isfinite(segment.range.y)||segment.range.x>=segment.range.y)throw std::runtime_error("Boundary segment needs finite min < max: "+where);
                rule.segments.push_back(segment);
            }
        }
        room.boundaries[edge]=rule;
    }
    if(map.get("orientation","")!="orthogonal"||map.get("infinite",false).asBool())throw std::runtime_error("Only finite orthogonal Tiled maps are supported");
    int width=int(Number(map,"width")),height=int(Number(map,"height"));float tw=Number(map,"tilewidth"),th=Number(map,"tileheight");
    if(width<1||height<1||width>256||height>256||tw<=0||tw!=th)throw std::runtime_error("Invalid Tiled grid");
    auto properties=Properties(map);float unit=Number(properties,"worldTileSize");
    if(unit<=0||unit>10)throw std::runtime_error("Invalid worldTileSize");
    glm::vec2 origin{Number(properties,"originX"),Number(properties,"originY")};const float scale=unit/tw;room.origin=origin;room.extent={width*unit,height*unit};
    for(const auto& [edge,rule]:room.boundaries){
        const bool vertical=edge=="left"||edge=="right";
        const float begin=vertical?room.origin.y:room.origin.x,end=begin+(vertical?room.extent.y:room.extent.x);
        std::vector<glm::vec2> ranges;
        for(const auto& segment:rule.segments){
            if(segment.range.x<begin||segment.range.y>end)throw std::runtime_error("Boundary segment range must be inside room edge: "+edge);
            ranges.push_back(segment.range);
        }
        std::sort(ranges.begin(),ranges.end(),[](glm::vec2 a,glm::vec2 b){return a.x<b.x;});
        for(size_t i=1;i<ranges.size();++i)if(ranges[i].x<ranges[i-1].y)throw std::runtime_error("Overlapping boundary segments: "+edge);
    }
    auto& camera=room.camera;camera.position=origin+room.extent*.5f;
    camera.zoom=std::clamp(std::min(150.f/room.extent.x,84.375f/room.extent.y),.05f,8.f);
    const auto& config=source["camera"];
    if(!config.isNull()) {
        if(!config.isObject())throw std::runtime_error("Camera must be an object");
        auto mode=config.get("mode","fixed");
        if(mode=="follow")camera.mode=Camera::Mode::FollowPlayer;
        else if(mode!="fixed")throw std::runtime_error("Camera mode must be fixed or follow");
        auto point=[&](const char* name,glm::vec2 fallback){const auto& v=config[name];if(v.isNull())return fallback;
            if(!v.isArray()||v.size()!=2||!v[0].isNumeric()||!v[1].isNumeric())throw std::runtime_error(std::string("Invalid camera ")+name);
            glm::vec2 result{v[0].asFloat(),v[1].asFloat()};if(!std::isfinite(result.x)||!std::isfinite(result.y))throw std::runtime_error(std::string("Invalid camera ")+name);return result;};
        camera.position=point("position",camera.position);camera.offset=point("offset",{});
        if(config.isMember("zoom"))camera.zoom=Number(config,"zoom");
        if(config.isMember("followSpeed"))camera.followSpeed=Number(config,"followSpeed");
        if(config.isMember("clampToRoom")){if(!config["clampToRoom"].isBool())throw std::runtime_error("Invalid camera clampToRoom");camera.clampToRoom=config["clampToRoom"].asBool();}
    }
    if(camera.zoom<.05f||camera.zoom>8.f||camera.followSpeed<0||camera.followSpeed>60.f)throw std::runtime_error("Invalid camera zoom or followSpeed");
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
                o.properties["group"]="tiles:"+layer.get("name","Terrain").asString();
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
                o.transform.size=pixels*scale;o.transform.position=pos+o.transform.size*.5f;
                if(object.isMember("rotation"))o.transform.rotation=Number(object,"rotation");
                if(o.role==Role::Solid && o.sprite.pixelArt && std::abs(o.transform.size.y-unit)<.001f)
                    o.sprite.repeatX=std::max(1,int(std::round(o.transform.size.x/unit)));
                if(std::abs(o.transform.rotation)>3600)throw std::runtime_error("Room rotation is out of range");
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
        Animation(o.sprite,p["animation"]);
        o.transform.size={Number(p,"width"),Number(p,"height")};if(o.transform.size.x<=0||o.transform.size.y<=0)throw std::runtime_error("Invalid prefab size");
        if(p.isMember("rotation")){o.transform.rotation=Number(p,"rotation");if(std::abs(o.transform.rotation)>3600)throw std::runtime_error("Prefab rotation is out of range");}
        Detection(o,p);o.event=p.get("event","").asString();
        o.destinationRoom=p.get("destinationRoom","").asString();o.destinationSpawn=p.get("destinationSpawn","").asString();
        catalog.prefabs[name]=o;
    }
    for(const auto& item:root["rooms"]) {
        auto id=String(item,"id");auto room=ImportTiled(Local(world.parent_path(),String(item,"map")),catalog.prefabs);
        room.id=id;room.title=String(item,"title");
        // Room copy is creator-owned and is no longer drawn by the fixed HUD.
        // An intentionally empty hint is valid for a stage built entirely in art.
        if(!item["hint"].isString())throw std::runtime_error("Room hint must be a string");
        room.hint=item["hint"].asString();
        room.inlineScript=item.isMember("scriptLua");if(room.inlineScript)room.scriptLua=item["scriptLua"].asString();else room.script=Local(world.parent_path(),String(item,"script"));
        if(!room.inlineScript&&!std::filesystem::exists(room.script))throw std::runtime_error("Missing room script: "+room.script.string());
        for(auto& o:room.objects)if(o.destinationRoom=="$self")o.destinationRoom=id;
        for(auto& [_,rule]:room.boundaries){
            if(rule.action==RoomDefinition::BoundaryRule::Action::Transfer&&rule.room=="$self")rule.room=id;
            for(auto& segment:rule.segments)if(segment.action==RoomDefinition::BoundaryRule::Action::Transfer&&segment.room=="$self")segment.room=id;
        }
        if(!catalog.rooms.emplace(id,std::move(room)).second)throw std::runtime_error("Duplicate room ID: "+id);
    }
    if(!catalog.rooms.contains(catalog.startRoom)||!catalog.rooms.at(catalog.startRoom).spawns.contains(catalog.startSpawn))throw std::runtime_error("Invalid initial room/spawn");
    for(const auto& [_,room]:catalog.rooms)for(const auto& o:room.objects)if(o.role==Role::Exit) {
        if(!catalog.rooms.contains(o.destinationRoom)||!catalog.rooms.at(o.destinationRoom).spawns.contains(o.destinationSpawn))throw std::runtime_error("Broken room connection: "+room.id+"/"+o.id);
    }
    for(auto& [id,room]:catalog.rooms)for(auto& [edge,rule]:room.boundaries) {
        auto connect=[&](RoomDefinition::BoundaryTarget& target,const std::string& key){
            if(target.action!=RoomDefinition::BoundaryRule::Action::Transfer)return;
            if(!catalog.rooms.contains(target.room))throw std::runtime_error("Broken boundary destination: "+id+"/"+key);
            const auto& dest=catalog.rooms.at(target.room);
            if(target.spawn.empty())target.spawn=dest.spawns.begin()->first;
            if(!dest.spawns.contains(target.spawn))throw std::runtime_error("Broken boundary spawn: "+id+"/"+key);
            auto pos=target.position.value_or(dest.spawns.at(target.spawn));
            if(pos.x<=dest.origin.x||pos.y<=dest.origin.y||pos.x>=dest.origin.x+dest.extent.x||pos.y>=dest.origin.y+dest.extent.y)throw std::runtime_error("Boundary arrival must be inside room: "+target.room+"/"+target.spawn);
            room.connections[key]={target.room,target.spawn,target.position};
        };
        connect(rule,edge);
        for(size_t i=0;i<rule.segments.size();++i)connect(rule.segments[i],edge+"["+std::to_string(i)+"]");
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
