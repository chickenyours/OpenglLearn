#include "IWanna/Public/game_module.h"
#include <algorithm>
#include <stdexcept>
namespace IWanna {
namespace {
Scripting::Event ObjectEvent(const char* callback,const RoomObject& object,const char* phase) {
    Scripting::Event event{callback,object.event,object.id,phase};
    for(const auto& key:object.properties.getMemberNames()) {
        const auto& v=object.properties[key];
        if(v.isBool())event.properties[key]=v.asBool();else if(v.isNumeric())event.properties[key]=v.asDouble();else if(v.isString())event.properties[key]=v.asString();
    }
    return event;
}
}
void RoomWorld::Load(const std::filesystem::path& file) {
    catalog_=RoomCatalog::Load(std::filesystem::absolute(file));
    // Load every declared image now so room transitions never surprise the atlas.
    for(auto sprite:catalog_.Sprites())game_.masks_.Attach(sprite,game_.assets_/"images",game_.config_.alphaThreshold);
    Enter(catalog_.startRoom,catalog_.startSpawn,false);
}
ECS::EntityID RoomWorld::Find(const std::string& id) const {auto found=ids_.find(id);return found==ids_.end()?0:found->second;}
ECS::EntityID RoomWorld::Create(const RoomObject& object) {
    if(ids_.contains(object.id))throw std::runtime_error("Duplicate live entity ID: "+object.id);
    auto handle=game_.scene_->CreateEntity(game_.archetype_);auto id=handle.GetID();game_.entities_.push_back(id);
    ids_[object.id]=id;handles_.emplace(object.id,handle);definitions_[object.id]=object;
    auto t=object.transform;t.spawn=t.position;game_.Get<Transform>(id)=t;
    game_.Get<TileCell>(id)=object.cell;
    auto s=object.sprite;game_.masks_.Attach(s,game_.assets_/"images",game_.config_.alphaThreshold);game_.Get<Sprite>(id)=s;
    auto& b=game_.Get<Behavior>(id);b.name=b.stableId=object.id;b.event=object.event;b.role=object.role;
    auto& c=game_.Get<Collider>(id);c.enabled=object.collide;c.detectionBox=object.detectionBox;c.detectionOffset=object.detectionOffset;c.detectionSize=object.detectionSize;c.points={{-1,-1},{1,-1},{1,1},{-1,1}};
    return id;
}
void RoomWorld::Enter(const std::string& roomId,const std::string& spawn,bool reset) {
    const auto& room=catalog_.rooms.at(roomId);auto fresh=std::make_unique<Scripting::LuaModule>();
    if(!(room.inlineScript?fresh->LoadSource(room.scriptLua,room.id):fresh->Load(room.script)))throw std::runtime_error(room.id+": "+fresh->Error());
    // Script validation precedes scene replacement; a bad destination script
    // keeps the old room alive. No scene mutation occurs within Pipeline::Tick.
    game_.views_.clear();game_.entities_.clear();game_.archetype_=nullptr;game_.scene_=std::make_unique<ECS::Core::Scene>();
    auto desc=game_.scene_->CreateArchTypeDescription();
    desc->AddComponentArray<Transform>();desc->AddComponentArray<Motion>();desc->AddComponentArray<Sprite>();
    desc->AddComponentArray<Collider>();desc->AddComponentArray<Behavior>();desc->AddComponentArray<Player>();desc->AddComponentArray<TileCell>();
    game_.archetype_=game_.scene_->CreateArchType(desc,64);game_.context_.scene=game_.scene_.get();
    current_=roomId;entry_=spawn;script_=std::move(fresh);ids_.clear();handles_.clear();definitions_.clear();touched_.clear();previous_.clear();
    message_.clear();error_.clear();nextRoom_.clear();nextSpawn_.clear();reset_=died_=false;
    game_.state_=State::Playing;game_.pending_={};
    for(const auto& object:room.objects) {
        auto id=Create(object);
        if(object.role==Role::Checkpoint && activated_.contains(roomId+"/"+object.id)) {
            auto& s=game_.Get<Sprite>(id);s.image="checkpoint_active.png";game_.masks_.Attach(s,game_.assets_/"images",game_.config_.alphaThreshold);
            game_.Get<Collider>(id).enabled=false;
        }
    }
    RoomObject player;player.id="player";player.role=Role::Player;player.sprite=game_.config_.idle;player.transform.size=game_.config_.playerSize;
    player.transform.position=room.spawns.at(spawn);
    if(reset && saves_.contains(roomId))player.transform.position=saves_.at(roomId).position;
    game_.player_=Create(player);game_.run_=game_.deathTemplate_=0;
    game_.checkpoint_=saves_.contains(roomId)?saves_.at(roomId).position:player.transform.position;
    game_.RebuildViews();
    script_->Call({"on_enter",roomId,spawn,""});if(reset)script_->Call({"on_reset",roomId,"",""});
    Apply();
}
void RoomWorld::RequestRoom(const std::string& room,const std::string& spawn) {
    if(!catalog_.rooms.contains(room)||!catalog_.rooms.at(room).spawns.contains(spawn))throw std::runtime_error("Unknown room/spawn: "+room+"/"+spawn);
    nextRoom_=room;nextSpawn_=spawn;
}
void RoomWorld::BeginTick() {
    if(reset_) {
        try{Enter(current_,entry_,true);}catch(const std::exception& e){error_=e.what();reset_=false;}
    }
    touched_.clear();
}
void RoomWorld::Touch(ECS::EntityID entity) {
    const auto& b=game_.Get<Behavior>(entity);if(!b.stableId.empty())touched_.insert(b.stableId);
}
bool RoomWorld::CrossBoundary(glm::vec2 p) {
    const auto& r=Current();
    for(const auto& [edge,c]:r.connections)if((edge=="left"&&p.x<r.origin.x)||(edge=="right"&&p.x>r.origin.x+r.extent.x)||(edge=="top"&&p.y<r.origin.y)||(edge=="bottom"&&p.y>r.origin.y+r.extent.y)) {RequestRoom(c.room,c.spawn);return true;}
    return false;
}
void RoomWorld::Death(){died_=true;nextRoom_.clear();nextSpawn_.clear();}
void RoomWorld::EndTick(double dt) {
    if(died_){script_->Call({"on_death",current_,"player",""});died_=false;}
    if(game_.state_==State::Playing && error_.empty()) {
        script_->Tick(dt);
        for(const auto& id:touched_) {
            if(previous_.contains(id)||!definitions_.contains(id))continue;
            const auto& object=definitions_.at(id);
            if(object.role==Role::Exit)RequestRoom(object.destinationRoom,object.destinationSpawn);
            if(object.role==Role::Checkpoint && !activated_.contains(current_+"/"+id)) {
                activated_.insert(current_+"/"+id);auto entity=Find(id);
                // Save the player's safe position, not the beacon's sprite center.
                game_.checkpoint_=game_.Get<Transform>(game_.player_).position;saves_[current_]={game_.checkpoint_,id};
                auto& s=game_.Get<Sprite>(entity);s.image="checkpoint_active.png";game_.masks_.Attach(s,game_.assets_/"images",game_.config_.alphaThreshold);
                game_.Get<Collider>(entity).enabled=false;game_.Sound("BLIP");script_->Call({"on_checkpoint",id,id,"enter"});
            }
            if(!object.event.empty())script_->Call(ObjectEvent("on_trigger",object,"enter"));
        }
        for(const auto& id:previous_)if(!touched_.contains(id)&&definitions_.contains(id)&&!definitions_.at(id).event.empty())
            script_->Call(ObjectEvent("on_trigger",definitions_.at(id),"exit"));
    }
    previous_=touched_;Apply();
    if(game_.state_==State::Playing && !nextRoom_.empty() && error_.empty()) {
        auto room=nextRoom_,spawn=nextSpawn_;nextRoom_.clear();nextSpawn_.clear();
        try{Enter(room,spawn,false);}catch(const std::exception& e){error_=e.what();}
    }
}
void RoomWorld::Apply() {
    auto commands=script_->TakeCommands();if(!error_.empty()||!script_->Error().empty())return;
    bool structural=false;
    try {
        for(const auto& c:commands) {
            if(c.operation=="message"){message_=c.id;continue;}
            if(c.operation=="sound"){game_.Sound(c.id);continue;}
            if(c.operation=="change_room"){RequestRoom(c.id,c.text);continue;}
            if(c.operation=="complete"){game_.state_=State::Won;game_.Sound("end");continue;}
            if(c.operation=="spawn") {
                if(!catalog_.prefabs.contains(c.id)||ids_.size()>4096)throw std::runtime_error("Invalid/excessive spawn prefab: "+c.id);
                auto object=catalog_.prefabs.at(c.id);object.id=c.text;object.transform.position={c.x,c.y};Create(object);structural=true;continue;
            }
            auto id=Find(c.id);if(!id||c.id=="player")throw std::runtime_error("Invalid script entity target: "+c.id);
            if(c.operation=="set_velocity")game_.Get<Motion>(id).velocity={c.x,c.y};
            else if(c.operation=="set_enabled") {game_.Get<Sprite>(id).visible=c.enabled;game_.Get<Collider>(id).enabled=c.enabled && definitions_.at(c.id).collide;}
            else if(c.operation=="destroy") {
                game_.scene_->DeleteEntity(handles_.at(c.id));std::erase(game_.entities_,id);ids_.erase(c.id);handles_.erase(c.id);definitions_.erase(c.id);structural=true;
            } else throw std::runtime_error("Unknown script command");
        }
    }catch(const std::exception& e){error_=CurrentId()+": "+e.what();}
    if(structural)game_.RebuildViews();
}
}
