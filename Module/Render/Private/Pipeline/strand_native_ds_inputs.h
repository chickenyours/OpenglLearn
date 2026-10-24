#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace Render::PipelineDetail::NativeDs {
// CPU writer 0x144af1510 stores resourceR128 and TraceLimit32 together at
// 0x144af1616..1620. Resource resolution stays a separate host budget choice.
inline constexpr std::uint32_t SourceTraceLimit=32u;

struct InjectionInputs {
    std::uint32_t count=0;
    float fraction=0;
    float rasterScale=0;
};

namespace InjectionInputDetail {
// Match the scalar vmulss/vdivss boundaries even when the caller is compiled
// with an optimizer that would otherwise propagate extra precision.
inline float Multiply(float a,float b){volatile float q=a*b;return q;}
inline float Divide(float a,float b){volatile float q=a/b;return q;}
}

// Recovered CPU selection/compensation, not a guessed uniform density gain.
// incomingCount is the separately supplied bound-list prefix budget. It must
// not be replaced by totalSegments or by a count derived from visible curves.
// Evidence: native_ds_prepare_144af1510.asm.txt, 0x144af15a6..1602 and
// 0x144af1672..16a2: max(.01,rate)*total, truncate, min(incoming), quality/flag
// select, selected/total, then authoredScale*(fraction>1e-5?1/fraction:100000).
inline InjectionInputs ResolveInjectionInputs(std::uint32_t totalSegments,
    std::uint32_t incomingCount,float rate,float authoredRasterScale,
    bool minLodFactor,std::uint32_t managerQuality){
    if(incomingCount>totalSegments)
        throw std::invalid_argument("Native DS injection prefix cannot exceed the original segment count");
    if(!std::isfinite(rate)||rate<0||!std::isfinite(authoredRasterScale)||authoredRasterScale<0)
        throw std::invalid_argument("Native DS injection rate and authored raster scale must be finite and nonnegative");
    InjectionInputs result;
    if(totalSegments!=0){
        const float product=InjectionInputDetail::Multiply(std::max(.01f,rate),float(totalSegments));
        // Compare in the integer domain before converting. In particular,
        // float(UINT32_MAX) rounds to2^32; casting that float back to uint32
        // would be undefined. The clamp also handles finite-rate overflow.
        const auto candidate=double(product)>=double(incomingCount)?incomingCount:
            static_cast<std::uint32_t>(product);
        result.count=(minLodFactor||managerQuality<2u)?candidate:incomingCount;
        result.fraction=InjectionInputDetail::Divide(float(result.count),float(totalSegments));
    }
    // total=0 is an explicit host empty-input policy. The original writer's
    // 0/0 domain is not asserted to have this result. Empty input records no
    // injection work, and carries the same finite small-fraction fallback.
    const float compensation=result.fraction>1e-5f?
        InjectionInputDetail::Divide(1.f,result.fraction):100000.f;
    result.rasterScale=InjectionInputDetail::Multiply(authoredRasterScale,compensation);
    if(!std::isfinite(result.rasterScale))
        throw std::overflow_error("Native DS compensated raster scale exceeds the finite float domain");
    return result;
}
} // namespace Render::PipelineDetail::NativeDs
