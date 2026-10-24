#pragma once
#include <bit>
#include <cstdint>

namespace Render {
// Native material writer140352f93..140353079 maps the authored longitudinal
// parameter to hm_v. Constants are the original f32 bits at149055cac/b0/b4.
// Each volatile store is an intentional f32 rounding boundary: do not fold
// this into pow(), reassociate the polynomial or contract its multiply/adds.
// Input validation belongs to the material recorder; no extra clamp is native.
inline float StrandSourceLongitudinalVariance(float authoredRoughness) {
    constexpr float quadraticCoefficient=std::bit_cast<float>(std::uint32_t{0x3f4fdf3b});
    constexpr float linearCoefficient=std::bit_cast<float>(std::uint32_t{0x3f39db23});
    constexpr float tailCoefficient=std::bit_cast<float>(std::uint32_t{0x406ccccd});
    volatile float r=authoredRoughness;
    volatile float r2=r*r;
    volatile float quadratic=quadraticCoefficient*r2;
    volatile float linear=linearCoefficient*r;
    volatile float sum=quadratic+linear;
    volatile float r4=r2*r2;
    volatile float r5=r4*r;
    volatile float r10=r5*r5;
    volatile float r20=r10*r10;
    volatile float tail=tailCoefficient*r20;
    volatile float polynomial=sum+tail;
    volatile float variance=polynomial*polynomial;
    return variance;
}
} // namespace Render
