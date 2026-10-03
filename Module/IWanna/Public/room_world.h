#pragma once
#include "room_data.h"
#include "Scripting/Public/lua_module.h"
#include <memory>
#include <set>
#include <random>
#include <deque>
#include "engine/ECS/Scene/scene.h"
namespace IWanna {
class GameModule;
class RoomWorld {
public:
    explicit RoomWorld(GameModule& game):game_(game){}
    void Load(const std::filesystem::path& file);
    void BeginTick();
    void EndTick(double dt);
    void Touch(ECS::EntityID id);
    void Shoot(const Input& input,double dt);
    void Hit(ECS::EntityID target,ECS::EntityID projectile);
    void Retire(ECS::EntityID id);
    void Death();
    bool CrossBoundary(glm::vec2 position);
    void RequestReset(){reset_=true;}
    void RequestRoom(const std::string& room,const std::string& spawn,std::optional<glm::vec2> position={});
    ECS::EntityID Find(const std::string& id) const;
    const RoomDefinition& Current() const {return catalog_.rooms.at(current_);}
    const std::string& CurrentId() const {return current_;}
    const std::string& Message() const {return message_;}
    const std::string& Error() const {return error_.empty()?script_->Error():error_;}
    std::vector<Sprite> Sprites() const {return catalog_.Sprites();}
private:
    GameModule& game_;
    RoomCatalog catalog_;
    std::string current_,entry_,nextRoom_,nextSpawn_,message_,error_;
    std::optional<glm::vec2> nextPosition_;
    std::unique_ptr<Scripting::LuaModule> script_;
    std::map<std::string,ECS::EntityID> ids_;
    std::map<std::string,ECS::EntityHandle> handles_;
    std::map<std::string,RoomObject> definitions_;
    // Stable IDs only: callbacks may append commands which move ECS storage.
    // Remove erases pending entries before an ID can be reused.
    std::deque<std::string> pendingSpawns_;
    std::set<std::string> touched_,previous_,activated_;
    std::set<std::string> retired_;
    std::map<std::string,std::string> hits_;
    std::mt19937 random_{std::random_device{}()};
    uint64_t runtimeSerial_=0;
    struct Save {glm::vec2 position;std::string id;};
    std::map<std::string,Save> saves_;
    bool reset_=false,died_=false;
    void Enter(const std::string& room,const std::string& spawn,bool reset,std::optional<glm::vec2> position={});
    ECS::EntityID Create(const RoomObject& object);
    void Remove(const std::string& id);
    void SetEnabled(const std::string& id,bool enabled);
    void Apply();
};
}
