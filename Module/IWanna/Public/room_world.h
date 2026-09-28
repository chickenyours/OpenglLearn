#pragma once
#include "room_data.h"
#include "Scripting/Public/lua_module.h"
#include <memory>
#include <set>
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
    void Death();
    bool CrossBoundary(glm::vec2 position);
    void RequestReset(){reset_=true;}
    void RequestRoom(const std::string& room,const std::string& spawn);
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
    std::unique_ptr<Scripting::LuaModule> script_;
    std::map<std::string,ECS::EntityID> ids_;
    std::map<std::string,ECS::EntityHandle> handles_;
    std::map<std::string,RoomObject> definitions_;
    std::set<std::string> touched_,previous_,activated_;
    struct Save {glm::vec2 position;std::string id;};
    std::map<std::string,Save> saves_;
    bool reset_=false,died_=false;
    void Enter(const std::string& room,const std::string& spawn,bool reset);
    ECS::EntityID Create(const RoomObject& object);
    void Apply();
};
}
