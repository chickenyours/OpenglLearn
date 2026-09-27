#pragma once
#include "game_components.h"
#include <filesystem>
namespace IWanna {
struct CharacterCollision {
    glm::vec2 sizeRatio{.28f,.72f},offsetRatio{0,.04f};
    float skin=.02f,stepHeight=.12f,groundSnap=.06f,recoveryDistance=.35f,wallSlide=.1f;
};
struct GameConfig {
    Rules physics;
    Sprite idle,run,jump;
    glm::vec2 playerSize{4.8f,3.6f};
    int alphaThreshold=128;
    bool facesLeft=true;
    CharacterCollision character;
    static GameConfig Load(const std::filesystem::path& path);
};
}
