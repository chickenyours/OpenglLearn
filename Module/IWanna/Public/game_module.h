#pragma once
#include "module_base.h"
#include "game_components.h"
#include "game_config.h"
#include "alpha_mask.h"
#include "room_world.h"
#include "engine/ECS/Scene/scene.h"
#include "engine/ECS/System/system_pipeline.h"
#include <filesystem>
#include <functional>
#include <memory>

namespace IWanna {
class GameModule final : public IModule {
public:
    explicit GameModule(std::filesystem::path assets,std::filesystem::path config = {},std::filesystem::path world = {});
    ~GameModule() override { Shutdown(); }
    const char* GetName() const noexcept override { return "IWannaModule"; }
    bool Startup() override;
    void Shutdown() override;
    bool IsStarted() const noexcept override { return started_; }
    void Advance(double seconds,Input input);
    void FixedTick(Input input);
    std::vector<DrawSprite> Extract();
    void Restart();
    void Kill();
    void Touch(ECS::EntityID entity);
    ECS::EntityID Find(const std::string& name);
    template<class T> T& Get(ECS::EntityID id) {return *scene_->GetActiveComponent<T>(id).Get();}
    ECS::EntityID PlayerEntity() const {return player_;}
    const std::vector<ECS::EntityID>& Entities() const {return entities_;}
    const std::vector<EntityView>& Views() const {return views_;}
    State GetState() const {return state_;}
    int Deaths() const {return deaths_;}
    glm::vec2 Checkpoint() const {return checkpoint_;}
    const std::string& Error() const {return error_;}
    bool FacesLeft() const {return config_.facesLeft;}
    const CharacterCollision& CollisionSettings() const {return config_.character;}
    Transform PlayerBody() {
        auto body=Get<Transform>(player_);
        body.position+=body.size*config_.character.offsetRatio;
        body.size*=config_.character.sizeRatio;body.rotation=0;
        return body;
    }
    void SelectPlayerAnimation();
    std::vector<Sprite> AnimationAssets() const {
        std::vector<Sprite> result{config_.idle,config_.run,config_.jump};
        if(world_){auto sprites=world_->Sprites();result.insert(result.end(),sprites.begin(),sprites.end());}return result;
    }
    bool IsRoomGame() const {return world_!=nullptr;}
    RoomWorld* World() {return world_.get();}
    void ExportMasks(const std::filesystem::path& directory) const {masks_.Export(directory);}
    Rules rules;
    Input input;
    std::function<void(const std::string&)> playSound;
    void Sound(const std::string& name) {if(playSound) playSound(name);}
private:
    friend class RoomWorld;
    std::filesystem::path assets_;
    std::filesystem::path configPath_;
    GameConfig config_;
    std::filesystem::path worldPath_;
    std::unique_ptr<RoomWorld> world_;
    MaskLibrary masks_;
    std::unique_ptr<ECS::Core::Scene> scene_;
    ObjectWeakPtr<ECS::Core::ArchType> archetype_;
    ECS::System::Pipeline pipeline_;
    ECS::System::Context context_;
    std::vector<ECS::EntityID> entities_;
    std::vector<EntityView> views_;
    void RebuildViews();
    void LoadTileMap();
    ECS::EntityID player_=0,run_=0,deathTemplate_=0;
    State state_=State::Playing;
    glm::vec2 checkpoint_{-71.351f,23.764f};
    int deaths_=0;
    double accumulator_=0;
    bool started_=false;
    Input pending_;
    std::string error_;
};
}
