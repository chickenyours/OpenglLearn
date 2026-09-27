#include "IWanna/Public/game_module.h"
#include "IWanna/Systems/game_systems.h"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace IWanna {
GameModule::GameModule(std::filesystem::path assets,std::filesystem::path config):assets_(std::move(assets)),configPath_(std::move(config)) {
    pipeline_.Add<AnimationSystem>();pipeline_.Add<InputSystem>();pipeline_.Add<PhysicsSystem>();
    pipeline_.RunBefore<AnimationSystem,InputSystem>();
}
bool GameModule::Startup() {
    if(started_) return true;
    try {
        RegisterComponents();
        config_=GameConfig::Load(configPath_.empty()?assets_/"gameplay.json":configPath_);rules=config_.physics;
        masks_.Attach(config_.idle,assets_/"images",config_.alphaThreshold);
        masks_.Attach(config_.run,assets_/"images",config_.alphaThreshold);
        masks_.Attach(config_.jump,assets_/"images",config_.alphaThreshold);
        scene_=std::make_unique<ECS::Core::Scene>();
        auto desc=scene_->CreateArchTypeDescription();
        desc->AddComponentArray<Transform>();desc->AddComponentArray<Motion>();desc->AddComponentArray<Sprite>();
        desc->AddComponentArray<Collider>();desc->AddComponentArray<Behavior>();desc->AddComponentArray<Player>();
        desc->AddComponentArray<TileCell>();
        archetype_=scene_->CreateArchType(desc,64);
        std::ifstream file(assets_/"level.txt"); std::string line;
        if(!std::getline(file,line) || line!="IWANNA_LEVEL_1") throw std::runtime_error("Cannot read IWANNA_LEVEL_1");
        const std::unordered_map<std::string,Role> roles={{"decoration",Role::Decoration},{"player",Role::Player},{"solid",Role::Solid},
            {"hazard",Role::Hazard},{"checkpoint",Role::Checkpoint},{"vanish",Role::Vanish},{"goal",Role::Goal},
            {"intro",Role::Intro},{"ending",Role::Ending},{"run_template",Role::RunTemplate},{"death_template",Role::DeathTemplate}};
        int sourceRecord=0;
        while(std::getline(file,line)) {
            if(line.empty()) continue;
            Transform t; Sprite s; Collider c; Behavior b; std::string role,frames; size_t count;
            std::istringstream in(line);
            if(!(in>>std::quoted(b.name)>>std::quoted(s.image)>>std::quoted(role)>>t.position.x>>t.position.y>>t.size.x>>t.size.y>>t.rotation
                >>s.layer>>s.flipX>>s.flipY>>c.enabled>>s.columns>>s.rows>>s.cellWidth>>s.cellHeight>>s.duration>>std::quoted(frames)>>count) || count<3 || count>64 ||
                t.size.x<=0 || t.size.y<=0 || s.duration<=0 || s.columns<1 || s.rows<1) throw std::runtime_error("Invalid level record: "+b.name);
            c.points.resize(count);for(auto& p:c.points) if(!(in>>p.x>>p.y)) throw std::runtime_error("Invalid collision polygon");
            if(!(in>>std::quoted(b.target)>>b.triggerVelocity.x>>b.triggerVelocity.y)) throw std::runtime_error("Invalid trigger record");
            s.frames.clear();std::istringstream animation(frames);int f;while(animation>>f) {if(f<0 || f>=s.columns*s.rows) throw std::runtime_error("Invalid animation frame");s.frames.push_back(f);}
            if(s.frames.empty()) s.frames.push_back(0);
            b.role=roles.at(role);t.spawn=t.position;
            const int record=sourceRecord++;
            if(b.role==Role::Solid)continue; // Terrain is now authored in tilemap.json.
            s.visible=b.role!=Role::RunTemplate && b.role!=Role::Ending && b.role!=Role::DeathTemplate;
            auto id=scene_->CreateEntity(archetype_).GetID();entities_.push_back(id);
            Get<Transform>(id)=t;Get<Sprite>(id)=s;Get<Collider>(id)=c;Get<Behavior>(id)=b;
            Get<TileCell>(id).sourceRecord=record;
            if(b.role==Role::Player) player_=id;
            if(b.role==Role::RunTemplate) run_=id;
            if(b.role==Role::DeathTemplate) deathTemplate_=id;
        }
        if(!player_ || !run_ || !deathTemplate_) throw std::runtime_error("Missing player/animation/death template");
        LoadTileMap();
        Get<Sprite>(player_)=config_.idle;Get<Sprite>(run_)=config_.run;Get<Sprite>(run_).visible=false;
        Get<Transform>(player_).size=config_.playerSize;Get<Transform>(run_).size=config_.playerSize;
        for(auto id:entities_) masks_.Attach(Get<Sprite>(id),assets_/"images",config_.alphaThreshold);
        // The last x template is also the original death-count icon.
        Get<Sprite>(deathTemplate_).visible=true;
        RebuildViews();
        context_.scene=scene_.get(); context_.SetService(this);
        started_=pipeline_.Start(context_); if(!started_) throw std::runtime_error("System pipeline failed");
        error_.clear(); return true;
    } catch(const std::exception& e) {error_=e.what();Shutdown();return false;}
}
void GameModule::Shutdown() {
    pipeline_.Stop(context_);views_.clear();entities_.clear();archetype_=nullptr;scene_.reset();masks_.Clear();context_.scene=nullptr;started_=false;
    player_=run_=deathTemplate_=0;state_=State::Playing;deaths_=0;accumulator_=0;pending_={};checkpoint_={-71.351f,23.764f};
}
void GameModule::Advance(double seconds,Input next) {
    if(!started_ || !std::isfinite(seconds) || seconds<0) return;
    if(!std::isfinite(rules.fixedStep) || rules.fixedStep<=0) throw std::invalid_argument("fixedStep must be finite and positive");
    pending_.left=next.left;pending_.right=next.right;pending_.jump|=next.jump;pending_.restart|=next.restart;pending_.any|=next.any;
    accumulator_+=std::min(seconds,.25);
    while(accumulator_>=rules.fixedStep) {FixedTick(pending_);pending_.jump=pending_.restart=pending_.any=false;accumulator_-=rules.fixedStep;}
}
void GameModule::FixedTick(Input next) {if(!started_) return;input=next;context_.deltaSeconds=rules.fixedStep;++context_.frameIndex;pipeline_.Tick(context_);}
ECS::EntityID GameModule::Find(const std::string& name) {for(auto id:entities_) if(Get<Behavior>(id).name==name) return id;return 0;}
void GameModule::Restart() {
    if(!player_) return;state_=State::Playing;
    Get<Transform>(player_).position=checkpoint_;Get<Motion>(player_).velocity={};Get<Player>(player_)={};Get<Sprite>(player_).visible=true;
    for(auto id:entities_) {
        auto& b=Get<Behavior>(id);
        if(b.role==Role::Vanish || b.role==Role::Hazard) {Get<Transform>(id).position=Get<Transform>(id).spawn;Get<Motion>(id).velocity={};Get<Sprite>(id).visible=true;Get<Collider>(id).enabled=true;}
        if(b.role==Role::Goal) {Get<Sprite>(id).visible=true;Get<Collider>(id).enabled=true;}
        if(b.role==Role::Corpse || b.role==Role::Ending || b.role==Role::Intro) Get<Sprite>(id).visible=false;
        if(b.role!=Role::Checkpoint) b.triggered=false;
    }
}
void GameModule::Kill() {
    if(state_!=State::Playing) return;state_=State::Dead;++deaths_;Sound("die");
    // Copy before allocating: ECS chunk growth can invalidate component addresses.
    auto t=Get<Transform>(deathTemplate_);t.position=Get<Transform>(player_).position;t.size={2.6f,3.23f};
    auto s=Get<Sprite>(deathTemplate_);s.visible=true;s.flipY=false;
    Get<Sprite>(player_).visible=false;Get<Motion>(player_).velocity={};
    auto id=scene_->CreateEntity(archetype_).GetID();entities_.push_back(id);Get<Transform>(id)=t;Get<Sprite>(id)=s;
    Get<Behavior>(id).role=Role::Corpse;Get<Behavior>(id).name="corpse";
    RebuildViews();
}
void GameModule::Touch(ECS::EntityID id) {
    auto& b=Get<Behavior>(id);
    if(b.role==Role::Checkpoint && !b.triggered) {
        checkpoint_=Get<Transform>(id).position;b.triggered=true;Get<Sprite>(id).visible=false;Get<Collider>(id).enabled=false;Sound("BLIP");
        for(auto other:entities_)if(Get<Sprite>(other).image=="checkpoint_active.png" && glm::distance(Get<Transform>(other).position,checkpoint_)<.1f)Get<Sprite>(other).visible=true;
    }
    if(b.role==Role::Vanish) {
        for(auto other:entities_) if(Get<Behavior>(other).role==Role::Vanish) {Get<Sprite>(other).visible=false;Get<Collider>(other).enabled=false;}
        Sound("apple");
    }
    if(b.role==Role::Goal && state_==State::Playing) {
        state_=State::Won;Sound("end");Get<Sprite>(player_).visible=false;Get<Motion>(player_).velocity={};Get<Sprite>(id).visible=false;Get<Collider>(id).enabled=false;
        for(auto other:entities_) if(Get<Behavior>(other).role==Role::Corpse) Get<Sprite>(other).visible=true;
    }
    if(!b.target.empty() && !b.triggered) {b.triggered=true;auto target=Find(b.target);if(target) Get<Motion>(target).velocity=b.triggerVelocity;Sound("apple");}
}
std::vector<DrawSprite> GameModule::Extract() {
    std::vector<DrawSprite> draws;
    draws.reserve(views_.size());
    for(const auto& view:views_) {
        if(!view.sprite->visible) continue;
        auto t=*view.transform;auto s=*view.sprite;
        draws.push_back({t,s});
    }
    std::stable_sort(draws.begin(),draws.end(),[](const auto& a,const auto& b){return a.sprite.layer>b.sprite.layer;});return draws;
}
void GameModule::SelectPlayerAnimation() {
    auto& s=Get<Sprite>(player_);auto& p=Get<Player>(player_);auto& velocity=Get<Motion>(player_).velocity;
    const Sprite& selected=!p.grounded?config_.jump:(std::abs(velocity.x)>.01f?config_.run:config_.idle);
    if(s.image!=selected.image){bool flip=s.flipX,visible=s.visible;s=selected;s.flipX=flip;s.visible=visible;}
}
void GameModule::RebuildViews() {
    views_.clear();views_.reserve(entities_.size());
    for(auto id:entities_) views_.push_back({id,&Get<Transform>(id),&Get<Motion>(id),&Get<Sprite>(id),&Get<Collider>(id),&Get<Behavior>(id),&Get<Player>(id)});
}
}
