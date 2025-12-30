#pragma once
#include <cstdint>

namespace Render{

enum class RenderPassFlag : uint64_t {
    None                        = 0,
    
    // 基础渲染
    BasePass                    = 1ULL << 0,
    GBufferPass                 = 1ULL << 1,
    ForwardTransparentPass      = 1ULL << 2,
    
    // 阴影 Pass
    ShadowPass                  = 1ULL << 3,
    DirectionalShadowPass       = 1ULL << 4,
    PointShadowPass             = 1ULL << 5,
    SpotShadowPass              = 1ULL << 6,

    // 特效/后处理
    SkyboxPass                  = 1ULL << 7,
    OutlinePass                 = 1ULL << 8,
    ParticlesPass               = 1ULL << 9,
    BloomPass                   = 1ULL << 10,
    SSRPass                     = 1ULL << 11,
    TAAPass                     = 1ULL << 12,

    // 调试
    DebugNormalsPass            = 1ULL << 20,
    DebugDepthPass              = 1ULL << 21,
    WireframePass               = 1ULL << 22,

    // 自定义扩展保留
    UserPass0                   = 1ULL << 56,
    UserPass1                   = 1ULL << 57,
    UserPass2                   = 1ULL << 58,
    UserPass3                   = 1ULL << 59,
    UserPass4                   = 1ULL << 60,
    UserPass5                   = 1ULL << 61,
    UserPass6                   = 1ULL << 62,
    UserPass7                   = 1ULL << 63
};

}