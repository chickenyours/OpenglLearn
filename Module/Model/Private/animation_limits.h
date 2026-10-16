#pragma once
#include <cstddef>

namespace Model::AnimationLimits {
// Sampling work and retained data are different budgets: a large target can
// contain many untouched bones/expressions without needing dense output keys.
inline constexpr std::size_t MaximumSamples = 250000;
inline constexpr std::size_t MaximumSampledChannels = 16000000;
inline constexpr std::size_t MaximumStoredKeys = 2000000;
} // namespace Model::AnimationLimits
