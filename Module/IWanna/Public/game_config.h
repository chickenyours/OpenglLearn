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
    // Width and height in gameplay.json are the unscaled art size. The scale
    // affects both the rendered transform and its derived collision body.
    glm::vec2 playerSize{4.8f,3.6f},playerScale{.75f,.75f};
    glm::vec2 ScaledPlayerSize() const {return playerSize*playerScale;}
    int alphaThreshold=128;
    bool facesLeft=true;
    CharacterCollision character;
    struct Shooting {
        bool enabled=true;
        Sprite sprite=[] {Sprite s;s.image="demo_white.png";s.pixelArt=true;return s;}();
        glm::vec2 size{.7f,.28f},muzzleOffset{.08f,-.05f}; // offset as player size ratios
        float speed=80,cooldown=.15f,lifetime=1.8f;
    } shooting;
    static GameConfig Load(const std::filesystem::path& path);
};
}
