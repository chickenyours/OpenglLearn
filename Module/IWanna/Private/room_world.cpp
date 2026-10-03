#include "IWanna/Public/game_module.h"
#include "IWanna/Public/collision.h"
#include "IWanna/Public/transform_math.h"
#include <algorithm>
#include <stdexcept>
#include <string_view>
namespace IWanna {
namespace {
Scripting::Event ObjectEvent(const char* callback,const RoomObject& object,const char* phase,const Transform* live=nullptr) {
    Scripting::Event event{callback,object.event,object.id,phase};
    for(const auto& key:object.properties.getMemberNames()) {
        const auto& v=object.properties[key];
        if(v.isBool())event.properties[key]=v.asBool();else if(v.isNumeric())event.properties[key]=v.asDouble();else if(v.isString())event.properties[key]=v.asString();
    }
    const auto& t=live?*live:object.transform;
    event.properties["positionX"]=double(t.position.x);event.properties["positionY"]=double(t.position.y);
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
    b.group=object.properties.get("group","").asString();
    game_.Get<ShotReceiver>(id).enabled=object.properties.get("receivesShots",false).asBool();
    const auto& lifetime=object.properties["despawnAfter"];
    if(!lifetime.isNull()) {
        if(!lifetime.isNumeric()||!std::isfinite(lifetime.asDouble())||lifetime.asDouble()<=0||lifetime.asDouble()>60)
            throw std::runtime_error("Invalid lifetime: "+object.id);
        game_.Get<Lifetime>(id).remaining=lifetime.asFloat();
    }
    auto& c=game_.Get<Collider>(id);c.enabled=object.collide;c.detectionBox=object.detectionBox;c.detectionOffset=object.detectionOffset;c.detectionSize=object.detectionSize;c.points={{-1,-1},{1,-1},{1,1},{-1,1}};
    return id;
}
void RoomWorld::Enter(const std::string& roomId,const std::string& spawn,bool reset,std::optional<glm::vec2> arrival) {
    const auto& room=catalog_.rooms.at(roomId);auto fresh=std::make_unique<Scripting::LuaModule>();
    if(!(room.inlineScript?fresh->LoadSource(room.scriptLua,room.id):fresh->Load(room.script)))throw std::runtime_error(room.id+": "+fresh->Error());
    // Script validation precedes scene replacement; a bad destination script
    // keeps the old room alive. No scene mutation occurs within Pipeline::Tick.
    game_.views_.clear();game_.entities_.clear();game_.archetype_=nullptr;game_.scene_=std::make_unique<ECS::Core::Scene>();
    auto desc=game_.scene_->CreateArchTypeDescription();
    desc->AddComponentArray<Transform>();desc->AddComponentArray<Motion>();desc->AddComponentArray<Sprite>();
    desc->AddComponentArray<Collider>();desc->AddComponentArray<Behavior>();desc->AddComponentArray<Player>();desc->AddComponentArray<TileCell>();desc->AddComponentArray<Camera>();
    desc->AddComponentArray<ShotReceiver>();desc->AddComponentArray<Lifetime>();
    game_.archetype_=game_.scene_->CreateArchType(desc,64);game_.context_.scene=game_.scene_.get();
    current_=roomId;entry_=spawn;script_=std::move(fresh);ids_.clear();handles_.clear();definitions_.clear();touched_.clear();previous_.clear();
    hits_.clear();retired_.clear();runtimeSerial_=0;
    message_.clear();error_.clear();nextRoom_.clear();nextSpawn_.clear();nextPosition_.reset();reset_=died_=false;
    game_.state_=State::Playing;game_.pending_={};
    for(const auto& object:room.objects) {
        auto id=Create(object);
        if(object.role==Role::Checkpoint && activated_.contains(roomId+"/"+object.id)) {
            auto& s=game_.Get<Sprite>(id);s.image="checkpoint_active.png";game_.masks_.Attach(s,game_.assets_/"images",game_.config_.alphaThreshold);
            game_.Get<Collider>(id).enabled=false;
        }
    }
    RoomObject player;player.id="player";player.role=Role::Player;player.sprite=game_.config_.idle;player.transform.size=game_.config_.ScaledPlayerSize();
    player.transform.position=arrival.value_or(room.spawns.at(spawn));
    if(reset && saves_.contains(roomId))player.transform.position=saves_.at(roomId).position;
    game_.player_=Create(player);game_.run_=game_.deathTemplate_=0;
    game_.Get<Sprite>(game_.player_).flipX=game_.config_.facesLeft; // start facing right
    game_.Get<Camera>(game_.player_)=room.camera;game_.UpdateCamera(true);
    game_.checkpoint_=saves_.contains(roomId)?saves_.at(roomId).position:player.transform.position;
    game_.RebuildViews();
    script_->queryPosition=[this](const std::string& id)->std::optional<Scripting::LuaModule::Position> {
        const auto entity=Find(id);if(!entity)return std::nullopt;
        const auto position=game_.Get<Transform>(entity).position;
        return Scripting::LuaModule::Position{position.x,position.y};
    };
    script_->Call({"on_enter",roomId,spawn,""});if(reset)script_->Call({"on_reset",roomId,"",""});
    Apply();
}
void RoomWorld::RequestRoom(const std::string& room,const std::string& spawn,std::optional<glm::vec2> position) {
    if(!catalog_.rooms.contains(room)||!catalog_.rooms.at(room).spawns.contains(spawn))throw std::runtime_error("Unknown room/spawn: "+room+"/"+spawn);
    nextRoom_=room;nextSpawn_=spawn;nextPosition_=position;
}
void RoomWorld::BeginTick() {
    if(reset_) {
        try{Enter(current_,entry_,true);}catch(const std::exception& e){error_=e.what();reset_=false;}
    }
    touched_.clear();
    hits_.clear();retired_.clear();
}
void RoomWorld::Shoot(const Input& input,double dt) {
    if(game_.state_!=State::Playing||!error_.empty())return;
    auto& p=game_.Get<Player>(game_.player_);
    if(input.left!=input.right)p.facingLeft=input.left;
    p.shotCooldown=std::max(0.f,p.shotCooldown-float(dt));
    const auto& config=game_.config_.shooting;
    if(!config.enabled||!(input.shoot||input.shootHeld)||p.shotCooldown>1e-5f||ids_.size()>=4096)return;
    p.shotCooldown=config.cooldown;
    const float direction=p.facingLeft?-1.f:1.f;
    const auto t=game_.Get<Transform>(game_.player_); // copy before allocating
    RoomObject shot;shot.id="@shot:"+std::to_string(++runtimeSerial_);shot.role=Role::Projectile;
    shot.collide=true;shot.sprite=config.sprite;shot.sprite.layer=-2;
    shot.transform.size=config.size;
    shot.transform.position=t.position+t.size*glm::vec2(direction*config.muzzleOffset.x,config.muzzleOffset.y);
    shot.properties["despawnAfter"]=config.lifetime;
    try {
        const auto id=Create(shot);game_.Get<Motion>(id).velocity={direction*config.speed,0};game_.RebuildViews();
    }catch(const std::exception& e){error_=CurrentId()+": "+e.what();game_.RebuildViews();}
}
void RoomWorld::Hit(ECS::EntityID target,ECS::EntityID projectile) {
    hits_.try_emplace(game_.Get<Behavior>(target).stableId,game_.Get<Behavior>(projectile).stableId);
}
void RoomWorld::Retire(ECS::EntityID id) {
    const auto& name=game_.Get<Behavior>(id).stableId;
    if(name.empty()||name=="player")return;
    retired_.insert(name);game_.Get<Collider>(id).enabled=false;game_.Get<Sprite>(id).visible=false;
}
void RoomWorld::SetEnabled(const std::string& name,bool enabled) {
    const auto id=Find(name);
    game_.Get<Sprite>(id).visible=enabled;
    game_.Get<Collider>(id).enabled=enabled&&definitions_.at(name).collide;
}
void RoomWorld::Remove(const std::string& name) {
    const auto id=Find(name);if(!id||name=="player")return;
    game_.scene_->DeleteEntity(handles_.at(name));std::erase(game_.entities_,id);
    ids_.erase(name);handles_.erase(name);definitions_.erase(name);
}
void RoomWorld::Touch(ECS::EntityID entity) {
    const auto& b=game_.Get<Behavior>(entity);if(!b.stableId.empty())touched_.insert(b.stableId);
}
bool RoomWorld::CrossBoundary(glm::vec2 p) {
    const auto& r=Current();
    for(const auto* edge:{"left","right","top","bottom"}){
        const bool crossed=(std::string_view(edge)=="left"&&p.x<r.origin.x)||(std::string_view(edge)=="right"&&p.x>r.origin.x+r.extent.x)||
            (std::string_view(edge)=="top"&&p.y<r.origin.y)||(std::string_view(edge)=="bottom"&&p.y>r.origin.y+r.extent.y);
        if(!crossed)continue;
        auto found=r.boundaries.find(edge);if(found==r.boundaries.end()){game_.Kill();return true;}
        const RoomDefinition::BoundaryTarget* rule=&found->second;
        const float along=(std::string_view(edge)=="left"||std::string_view(edge)=="right")?p.y:p.x;
        for(const auto& segment:found->second.segments)if(along>=segment.range.x&&along<segment.range.y){rule=&segment;break;}
        if(rule->action==RoomDefinition::BoundaryRule::Action::Ignore)continue;
        if(rule->action==RoomDefinition::BoundaryRule::Action::Death)game_.Kill();
        else RequestRoom(rule->room,rule->spawn,rule->position);
        return true;
    }
    return false;
}
void RoomWorld::Death(){died_=true;nextRoom_.clear();nextSpawn_.clear();nextPosition_.reset();}
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
            if(!object.event.empty())script_->Call(ObjectEvent("on_trigger",object,"enter",&game_.Get<Transform>(Find(id))));
        }
        for(const auto& id:previous_)if(!touched_.contains(id)&&definitions_.contains(id)&&!definitions_.at(id).event.empty())
            script_->Call(ObjectEvent("on_trigger",definitions_.at(id),"exit"));
        for(const auto& [id,projectile]:hits_)if(definitions_.contains(id)&&!definitions_.at(id).event.empty()) {
            auto event=ObjectEvent("on_hit",definitions_.at(id),"hit",&game_.Get<Transform>(Find(id)));
            event.properties["projectileId"]=projectile;script_->Call(event);
        }
    }
    previous_=touched_;Apply();
    if(!retired_.empty()) {for(const auto& id:retired_)Remove(id);retired_.clear();game_.RebuildViews();}
    if(game_.state_==State::Playing && !nextRoom_.empty() && error_.empty()) {
        auto room=nextRoom_,spawn=nextSpawn_;auto arrival=nextPosition_;nextRoom_.clear();nextSpawn_.clear();nextPosition_.reset();
        try{Enter(room,spawn,false,arrival);}catch(const std::exception& e){error_=e.what();}
    }
}
void RoomWorld::Apply() {
    auto commands=script_->TakeCommands();if(!error_.empty())return;
    if(!script_->Error().empty()){error_=CurrentId()+": "+script_->Error();return;}
    bool structural=false;
    try {
        for(const auto& c:commands) {
            if(c.operation=="message"){message_=c.id;continue;}
            if(c.operation=="sound"){game_.Sound(c.id);continue;}
            if(c.operation=="change_room"){RequestRoom(c.id,c.text);continue;}
            if(c.operation=="complete"){game_.state_=State::Won;game_.Sound("end");continue;}
            if(c.operation=="set_group_enabled") {
                bool found=false;
                for(const auto& [name,id]:ids_)if(name!="player"&&game_.Get<Behavior>(id).group==c.id){SetEnabled(name,c.enabled);found=true;}
                if(!found)throw std::runtime_error("Unknown entity/tile group: "+c.id);
                continue;
            }
            if(c.operation=="spawn_radial"||c.operation=="spawn_radial_at") {
                if(!catalog_.prefabs.contains(c.id)||ids_.size()+c.maxCount>4096)throw std::runtime_error("Invalid/excessive radial prefab: "+c.id);
                glm::vec2 center{c.x,c.y};
                if(!c.anchor.empty()) {
                    const auto anchor=Find(c.anchor);if(!anchor)throw std::runtime_error("Unknown radial anchor: "+c.anchor);
                    center=game_.Get<Transform>(anchor).position;
                }
                const int count=std::uniform_int_distribution<int>(c.minCount,c.maxCount)(random_);
                const auto burst=++runtimeSerial_;
                for(int i=0;i<count;++i) {
                    const float angle=float(c.phase*.01745329252+6.28318530718*i/count);
                    const glm::vec2 direction{std::cos(angle),std::sin(angle)};
                    auto object=catalog_.prefabs.at(c.id);
                    object.id=c.text+":"+std::to_string(burst)+":"+std::to_string(i);
                    object.transform.position=center+direction*float(c.radius);object.properties["despawnAfter"]=c.lifetime;
                    Create(object);structural=true;
                    game_.Get<Motion>(Find(object.id)).velocity=direction*float(c.speed);
                }
                continue;
            }
            if(c.operation=="camera_fixed"||c.operation=="camera_follow") {
                auto& camera=game_.Get<Camera>(game_.PlayerEntity());camera.zoom=float(c.zoom);
                if(c.operation=="camera_fixed"){camera.mode=Camera::Mode::Fixed;camera.position={c.x,c.y};}
                else {camera.mode=Camera::Mode::FollowPlayer;camera.offset={c.x,c.y};if(c.followSpeed>=0)camera.followSpeed=float(c.followSpeed);}
                game_.UpdateCamera(true);continue;
            }
            if(c.operation=="set_player_scale"||c.operation=="set_player_size") {
                const auto size=c.operation=="set_player_scale"
                    ?game_.config_.playerSize*glm::vec2(float(c.x),float(c.y))
                    :glm::vec2(float(c.x),float(c.y));
                const auto& collision=game_.config_.character;
                if(2*collision.skin>=std::min(size.x*collision.sizeRatio.x,size.y*collision.sizeRatio.y))
                    throw std::runtime_error("Player size is too small for its collision skin");
                auto& t=game_.Get<Transform>(game_.player_);
                if(t.size==size)continue;
                // Anchor the physical feet so a live resize on a floor does not
                // suddenly push the body into the platform or make it hover.
                const float footRatio=collision.offsetRatio.y+collision.sizeRatio.y*.5f;
                auto candidate=t;candidate.position.y+=(t.size.y-size.y)*footRatio;candidate.size=size;
                auto body=candidate;body.position+=body.size*collision.offsetRatio;
                body.size=body.size*collision.sizeRatio-glm::vec2(2*collision.skin);body.rotation=0;
                for(const auto& view:game_.Views()) {
                    if(view.behavior->role!=Role::Solid||!view.collider->enabled)continue;
                    auto solid=*view.transform;
                    if(view.collider->detectionBox){solid.position+=RotateOffset(view.collider->detectionOffset,solid.rotation);solid.size=view.collider->detectionSize;}
                    if(solid.rotation==0 && (std::abs(body.position.x-solid.position.x)>=(body.size.x+solid.size.x)*.5f ||
                        std::abs(body.position.y-solid.position.y)>=(body.size.y+solid.size.y)*.5f))continue;
                    if(MaskOverlap(body,RectangleMask(),solid,view.collider->detectionBox?RectangleMask():*view.sprite))
                        throw std::runtime_error("Player size would intersect solid: "+view.behavior->stableId);
                }
                t=candidate;
                definitions_.at("player").transform.size=size;
                continue;
            }
            if(c.operation=="spawn") {
                if(!catalog_.prefabs.contains(c.id)||ids_.size()>4096)throw std::runtime_error("Invalid/excessive spawn prefab: "+c.id);
                auto object=catalog_.prefabs.at(c.id);object.id=c.text;object.transform.position={c.x,c.y};Create(object);structural=true;continue;
            }
            auto id=Find(c.id);if(!id||c.id=="player")throw std::runtime_error("Invalid script entity target: "+c.id);
            if(c.operation=="set_velocity")game_.Get<Motion>(id).velocity={c.x,c.y};
            else if(c.operation=="set_rotation") {game_.Get<Transform>(id).rotation=float(c.x);definitions_.at(c.id).transform.rotation=float(c.x);}
            else if(c.operation=="set_enabled")SetEnabled(c.id,c.enabled);
            else if(c.operation=="set_collision") {
                auto& collider=game_.Get<Collider>(id);collider.enabled=c.enabled;collider.cacheValid=false;
            }
            else if(c.operation=="set_visible")game_.Get<Sprite>(id).visible=c.enabled;
            else if(c.operation=="set_opacity")game_.Get<Sprite>(id).opacity=float(c.x);
            else if(c.operation=="restart_animation")game_.Get<Sprite>(id).elapsed=0;
            else if(c.operation=="set_size") {
                auto& transform=game_.Get<Transform>(id);transform.size={c.x,c.y};
                definitions_.at(c.id).transform.size=transform.size;
                // Detection boxes use world units; only mask-based collision
                // follows the sprite's new dimensions automatically.
                game_.Get<Collider>(id).cacheValid=false;
            }
            else if(c.operation=="destroy") {
                Remove(c.id);structural=true;
            } else throw std::runtime_error("Unknown script command");
        }
    }catch(const std::exception& e){error_=CurrentId()+": "+e.what();}
    if(structural)game_.RebuildViews();
}
}
