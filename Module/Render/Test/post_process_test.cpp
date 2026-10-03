#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

#include "Render/Public/Pipeline/post_process.h"

namespace {
using namespace Render;

void TestValidation() {
    PostProcessSettings settings;
    std::string error = "previous error";
    assert(ValidatePostProcessSettings(settings, &error) && error.empty());
    for (const auto levels : {1u, MaxBloomLevels}) {
        settings.bloomLevels = levels;
        assert(ValidatePostProcessSettings(settings));
    }
    for (const auto levels : {0u, MaxBloomLevels + 1}) {
        settings.bloomLevels = levels;
        assert(!ValidatePostProcessSettings(settings, &error) && !error.empty());
    }
    settings = {};
    settings.toneMap = static_cast<ToneMapMode>(99);
    assert(!ValidatePostProcessSettings(settings));
    settings = {};
    settings.filter = static_cast<PostFilter>(99);
    assert(!ValidatePostProcessSettings(settings));
    for (const auto mode : {ToneMapMode::Reinhard, ToneMapMode::ACES, ToneMapMode::None}) {
        settings = {};
        settings.toneMap = mode;
        assert(ValidatePostProcessSettings(settings));
    }
    for (std::uint32_t filter = 0; filter <= static_cast<std::uint32_t>(PostFilter::Ripple); ++filter) {
        settings = {};
        settings.filter = static_cast<PostFilter>(filter);
        assert(ValidatePostProcessSettings(settings));
    }
    float PostProcessSettings::* fields[] = {
        &PostProcessSettings::bloomThreshold, &PostProcessSettings::bloomStrength,
        &PostProcessSettings::ssaoRadius, &PostProcessSettings::ssaoBias,
        &PostProcessSettings::ssaoPower, &PostProcessSettings::exposure,
        &PostProcessSettings::gamma, &PostProcessSettings::saturation,
        &PostProcessSettings::contrast, &PostProcessSettings::vignette,
        &PostProcessSettings::sharpenStrength
    };
    for (const auto field : fields) {
        for (const float value : {std::numeric_limits<float>::quiet_NaN(),
                                  std::numeric_limits<float>::infinity(), -1.0f}) {
            settings = {};
            settings.*field = value;
            assert(!ValidatePostProcessSettings(settings, &error) && !error.empty());
        }
    }
    settings = {};
    settings.ssaoBias = settings.ssaoRadius + 0.1f;
    assert(!ValidatePostProcessSettings(settings));
    settings = {};
    settings.vignette = 1.01f;
    assert(!ValidatePostProcessSettings(settings));
    settings = {};
    settings.exposure = settings.gamma = 0.0f;
    assert(!ValidatePostProcessSettings(settings));
    settings = {};
    settings.bloomThreshold = settings.bloomStrength = settings.ssaoBias = settings.saturation =
        settings.contrast = settings.vignette = settings.sharpenStrength = 0.0f;
    assert(ValidatePostProcessSettings(settings, &error) && error.empty());
}

float Read(const PostProcessConstants& constants, std::size_t byteOffset) {
    float value = 0;
    std::memcpy(&value, reinterpret_cast<const std::byte*>(&constants) + byteOffset, sizeof(value));
    return value;
}

void TestPacking() {
    static_assert(sizeof(PostProcessConstants) == 224 && sizeof(PostProcessConstants) <= 256);
    PostProcessSettings settings;
    settings.bloomEnabled = false;
    settings.bloomThreshold = 1.25f;
    settings.bloomStrength = 0.3f;
    settings.ssaoEnabled = false;
    settings.ssaoRadius = 0.7f;
    settings.ssaoBias = 0.01f;
    settings.ssaoPower = 2.0f;
    settings.exposure = 1.8f;
    settings.gamma = 2.4f;
    settings.toneMap = ToneMapMode::ACES;
    settings.filter = PostFilter::Ripple;
    settings.saturation = 0.8f;
    settings.contrast = 1.1f;
    settings.vignette = 0.25f;
    settings.sharpenStrength = 0.5f;
    assert(ValidatePostProcessSettings(settings));
    const auto block = MakePostProcessConstants(settings);
    assert(Read(block, 128) == 1.25f && Read(block, 132) == 0.3f);
    assert(Read(block, 144) == 0.7f && Read(block, 148) == 0.01f);
    assert(Read(block, 152) == 2.0f && Read(block, 156) == 0.0f);
    assert(Read(block, 160) == 1.8f && Read(block, 164) == 2.4f);
    assert(Read(block, 168) == 1.0f && Read(block, 172) == 7.0f);
    assert(Read(block, 188) == 0.0f);
    assert(Read(block, 192) == 0.8f && Read(block, 196) == 1.1f);
    assert(Read(block, 200) == 0.25f && Read(block, 204) == 0.5f);
    assert(Read(block, 220) == 0.0f);
    const auto defaults = MakePostProcessConstants({});
    assert(defaults.ssao.w == 1.0f && defaults.misc.w == 1.0f && defaults.screen.w == 1.0f);
}
}

int main() {
    TestValidation();
    TestPacking();
    std::cout << "Post-process settings and std140 ABI passed.\n";
}
