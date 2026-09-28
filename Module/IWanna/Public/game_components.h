#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>
#include "engine/ECS/Component/component_loader_registry.h"

namespace IWanna {
struct AlphaMask;
template<class T> struct GameComponent : ECS::Component::Component<T> {
    // Level records are loaded by GameModule, not the engine JSON scene loader.
    bool LoadFromMetaDataImpl(const Json::Value&,Log::StackLogErrorHandle) {return false;}
};
enum class Role { Decoration, Player, Solid, Hazard, Checkpoint, Vanish, Goal, Intro, Ending, RunTemplate, DeathTemplate, Corpse, Trigger, Exit };
struct Transform : GameComponent<Transform> { glm::vec2 position{}, size{1}, spawn{}; float rotation=0; };
struct Motion : GameComponent<Motion> { glm::vec2 velocity{}; };
struct Sprite : GameComponent<Sprite> {
    bool pixelArt=false;
    int repeatX=1;
    std::string image;
    int layer=0, columns=1, rows=1, cellWidth=0, cellHeight=0;
    bool flipX=false, flipY=false, visible=true;
    float duration=1, elapsed=0;
    std::vector<int> frames{0};
    std::shared_ptr<const std::vector<AlphaMask>> masks;
};
struct Collider : GameComponent<Collider> {
    std::vector<glm::vec2> points; bool enabled=false;
    bool detectionBox=false; glm::vec2 detectionOffset{},detectionSize{1};
    // Derived cache: rebuilt only when source geometry or transform changes.
    std::vector<glm::vec2> cachedLocal,world;
    glm::vec2 cachedPosition{},cachedSize{},minimum{},maximum{};
    float cachedRotation=0;
    bool cacheValid=false;
};
struct Behavior : GameComponent<Behavior> {
    Role role=Role::Decoration;
    std::string name, target;
    std::string stableId,event;
    glm::vec2 triggerVelocity{};
    bool triggered=false;
};
struct Player : GameComponent<Player> { int jumps=0; bool grounded=false; };
struct TileCell : GameComponent<TileCell> {int column=0,row=0,material=0,sourceRecord=-1;};
inline void RegisterComponents() {
    REGISTER_COMPONENT("iwanna_transform",Transform);REGISTER_COMPONENT("iwanna_motion",Motion);
    REGISTER_COMPONENT("iwanna_sprite",Sprite);REGISTER_COMPONENT("iwanna_collider",Collider);
    REGISTER_COMPONENT("iwanna_behavior",Behavior);REGISTER_COMPONENT("iwanna_player",Player);
    REGISTER_COMPONENT("iwanna_tile_cell",TileCell);
}
struct Input { bool left=false,right=false,jump=false,restart=false,any=false; };
enum class State { Playing, Dead, Won };
struct Rules {
    float runSpeed=18,jumpSpeed=56,doubleJumpSpeed=52,gravity=100,maxFallSpeed=65;
    float fixedStep=1.f/120.f,maxSubstepDistance=.025f;
};
struct DrawSprite { Transform transform; Sprite sprite; };
// Borrowed ECS chunk addresses. GameModule rebuilds after structural changes.
struct EntityView {
    ECS::EntityID id;
    Transform* transform; Motion* motion; Sprite* sprite; Collider* collider; Behavior* behavior; Player* player;
};
}
