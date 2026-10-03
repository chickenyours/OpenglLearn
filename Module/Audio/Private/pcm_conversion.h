#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Audio::Detail {
// Output is injectable, so native backends must also handle callbacks which
// bypass Mixer and supply out-of-range/non-finite samples.
inline int16_t ToPcm16(float sample) noexcept {
    if(!std::isfinite(sample)) return 0;
    return static_cast<int16_t>(std::lround(std::clamp(sample,-1.f,1.f)*32767.f));
}
}
