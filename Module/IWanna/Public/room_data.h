#pragma once
#include "game_components.h"
#include <filesystem>
#include <map>
namespace IWanna {
struct RoomObject {
    std::string id,prefab,event,destinationRoom,destinationSpawn;
    Transform transform;
    Sprite sprite;
    TileCell cell;
    Role role=Role::Decoration;
    bool collide=false;
    Json::Value properties;
    bool detectionBox=false;glm::vec2 detectionOffset{},detectionSize{1};
};
struct RoomLabel {std::string text;glm::vec2 position;float scale=.35f;};
struct RoomDefinition {
    std::string id,title,hint;
    std::filesystem::path script;
    std::string scriptLua;bool inlineScript=false;
    glm::vec2 origin{},extent{};
    struct Connection {std::string room,spawn;};
    std::map<std::string,Connection> connections;
    std::vector<RoomObject> objects;
    std::vector<RoomLabel> labels;
    std::map<std::string,glm::vec2> spawns;
};
struct RoomCatalog {
    std::string startRoom,startSpawn;
    std::map<std::string,RoomDefinition> rooms;
    std::map<std::string,RoomObject> prefabs;
    static RoomDefinition ReadRoom(const std::filesystem::path& path,const std::map<std::string,RoomObject>& prefabs);
    static RoomCatalog Load(const std::filesystem::path& world);
    std::vector<Sprite> Sprites() const;
};
}
