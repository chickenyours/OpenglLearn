#include "Render/Public/Sprite/sprite_animation.h"
#include <array>
#include <iostream>
#include <limits>

using namespace Render::Animation;
namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Near(float actual, float expected) { Check(std::abs(actual - expected) < .0001f, "wrong sampled value"); }
template<class F> void Reject(F action) {
    bool rejected = false;
    try { action(); } catch (const std::invalid_argument&) { rejected = true; }
    Check(rejected, "invalid animation accepted");
}
}
int main() {
    try {
        const std::array keys{Key{0, 0, 0, 4}, Key{2, 2, -2, 0}};
        const std::array tracks{Track{0, Channel::PositionX, keys}};
        std::array<Pose, 2> poses{};
        poses[0].position.y = 17;
        Sample({2, false, tracks}, 1, poses);
        Near(poses[0].position.x, 2.5f); // Nonzero slopes and two-second segment.
        Near(poses[0].position.y, 17);
        Near(poses[1].scale.x, 1);
        Sample({2, false, tracks}, -2, poses); Near(poses[0].position.x, 0);
        Sample({2, false, tracks}, 20, poses); Near(poses[0].position.x, 2);
        Sample({2, true, tracks}, 3, poses); Near(poses[0].position.x, 2.5f);
        Sample({2, true, tracks}, -1, poses); Near(poses[0].position.x, 2.5f);
        Sample({2, true, tracks}, 2, poses); Near(poses[0].position.x, 0);
        const std::array rotation{Key{0, 180, 0, 0}};
        const std::array rotateTracks{Track{1, Channel::AngleDegrees, rotation}};
        Sample({0, true, rotateTracks}, 123, poses); Near(poses[1].angle, std::numbers::pi_v<float>);
        const std::array reset{Key{0, 1, 0, 0}, Key{1.0333333f, 1, 0, 0}};
        const std::array resetTracks{Track{0, Channel::ScaleX, reset}};
        Sample({1, false, resetTracks}, 1, poses); Near(poses[0].scale.x, 1);
        const auto before = poses;
        Sample({1, false, resetTracks}, 1, poses);
        Check(poses[0].position == before[0].position && poses[1].angle == before[1].angle,
              "sampling modified an unbound channel");
        const float nan = std::numeric_limits<float>::quiet_NaN();
        Reject([&] { Sample({-1, false, tracks}, 0, poses); });
        Reject([&] { Sample({nan, false, tracks}, 0, poses); });
        Reject([&] { Sample({1, false, tracks}, nan, poses); });
        Reject([&] { Sample({1, false, tracks}, 0, std::span<Pose>{}); });
        const std::array emptyTrack{Track{0, Channel::PositionX, {}}};
        Reject([&] { Sample({1, false, emptyTrack}, 0, poses); });
        for (const auto badKey : {Key{-1, 0, 0, 0}, Key{0, nan, 0, 0}, Key{0, 0, nan, 0}, Key{0, 0, 0, nan}}) {
            const std::array badKeys{badKey};
            const std::array badTracks{Track{0, Channel::ScaleX, badKeys}};
            Reject([&] { Sample({1, false, badTracks}, 0, poses); });
        }
        const std::array duplicate{Key{0, 1, 0, 0}, Key{0, 2, 0, 0}};
        const std::array duplicateTracks{Track{0, Channel::ScaleX, duplicate}};
        Reject([&] { Sample({1, false, duplicateTracks}, 0, poses); });
        std::cout << "Sprite animation: Hermite, boundaries, loop, channels, rotation and validation passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
