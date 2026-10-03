#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <span>
#include <stdexcept>

namespace Render::Animation {

enum class Channel { PositionX, PositionY, ScaleX, ScaleY, AngleDegrees, Opacity };
struct Key { float time, value, inTangent, outTangent; };
struct Track { std::size_t node; Channel channel; std::span<const Key> keys; };
struct Clip { float duration; bool loop; std::span<const Track> tracks; };
struct Pose {
    glm::vec2 position{0}, scale{1};
    float angle = 0, opacity = 1;
};

// Tangents are derivatives per second, not normalized segment handles. Clips
// may contain a last key beyond their stop time (as authored by Unity).
inline void Validate(const Clip& clip, std::size_t nodes) {
    if (!std::isfinite(clip.duration) || clip.duration < 0)
        throw std::invalid_argument("Invalid animation duration");
    for (const auto& track : clip.tracks) {
        if (track.node >= nodes || track.keys.empty() ||
            static_cast<unsigned>(track.channel) > static_cast<unsigned>(Channel::Opacity))
            throw std::invalid_argument("Invalid animation track");
        float previous = -1;
        for (const auto& key : track.keys) {
            if (!std::isfinite(key.time) || key.time < 0 || key.time <= previous ||
                !std::isfinite(key.value) || !std::isfinite(key.inTangent) || !std::isfinite(key.outTangent))
                throw std::invalid_argument("Invalid animation key");
            previous = key.time;
        }
    }
}

inline float SampleTrack(std::span<const Key> keys, double time) {
    if (time <= keys.front().time) return keys.front().value;
    if (time >= keys.back().time) return keys.back().value;
    const auto next = std::upper_bound(keys.begin(), keys.end(), time,
        [](double value, const Key& key) { return value < key.time; });
    const auto& a = *(next - 1);
    const auto& b = *next;
    const double duration = double(b.time) - a.time;
    const double u = (time - a.time) / duration, u2 = u * u, u3 = u2 * u;
    const float value = static_cast<float>((2 * u3 - 3 * u2 + 1) * a.value +
        (u3 - 2 * u2 + u) * duration * a.outTangent +
        (-2 * u3 + 3 * u2) * b.value + (u3 - u2) * duration * b.inTangent);
    if (!std::isfinite(value)) throw std::invalid_argument("Animation sample overflow");
    return value;
}

// Pure sampling: caller owns the clock and resets base poses when switching
// clips. Channels absent from a clip preserve their supplied base values.
inline void Sample(const Clip& clip, double time, std::span<Pose> poses) {
    if (!std::isfinite(time)) throw std::invalid_argument("Invalid animation time");
    Validate(clip, poses.size());
    if (clip.loop && clip.duration > 0) {
        time = std::fmod(time, double(clip.duration));
        if (time < 0) time += clip.duration;
    } else time = std::clamp(time, 0.0, double(clip.duration));
    for (const auto& track : clip.tracks) {
        const float value = SampleTrack(track.keys, time);
        auto& pose = poses[track.node];
        switch (track.channel) {
        case Channel::PositionX: pose.position.x = value; break;
        case Channel::PositionY: pose.position.y = value; break;
        case Channel::ScaleX: pose.scale.x = value; break;
        case Channel::ScaleY: pose.scale.y = value; break;
        case Channel::AngleDegrees: pose.angle = value * (std::numbers::pi_v<float> / 180.f); break;
        case Channel::Opacity: pose.opacity = value; break;
        }
    }
}

} // namespace Render::Animation
