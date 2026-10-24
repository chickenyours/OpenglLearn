#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include "pipeline_resource_builder.h"
#include "ue_lumen_high_short_range_ao_shaders.h"

namespace Render::PipelineDetail {
struct alignas(16) UeLumenHighSamplingConstants {
    glm::mat4 inverseView{1},inverseProjection{1},view{1};
    glm::ivec4 extent{0};
    glm::uvec4 frame{0};
};
static_assert(sizeof(UeLumenHighSamplingConstants)==224);
inline std::vector<std::uint8_t> LoadUeLumenHighBlueNoise(
    const std::filesystem::path& directory,const char* name,std::size_t bytes) {
    const auto path=directory/name;
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file||file.tellg()!=std::streamoff(bytes))
        throw std::runtime_error("UE High requires the extracted original UE BlueNoise asset: "+path.string());
    std::vector<std::uint8_t> data(bytes);file.seekg(0);
    if(!file.read(reinterpret_cast<char*>(data.data()),std::streamsize(bytes)))
        throw std::runtime_error("Cannot read original UE BlueNoise asset: "+path.string());
    return data;
}
// Add inside Impl: PipelineDetail::UeLumenHighShortRangeAOState<Target> ueHighAO;
// The root renderer commits the AO history only after the enclosing frame
// succeeds, alongside GI. Recording never advances a sampling/history sequence.
template<class Target> struct UeLumenHighShortRangeAOState {
    std::shared_ptr<const void> samplingOwner,programsOwner,targetsOwner;
    RenderResourceHandle<UniformBufferSpec> samplingBuffer;
    RenderResourceHandle<RHITextureSpec> blueNoiseScalar,blueNoiseVec2;
    RenderResourceHandle<PipelineSpec> furthestHzbProgram,rawAOProgram,temporalAOProgram,compositeProgram;
    Target furthestHzb,rawAO,compositeGI;
    std::array<Target,2> history;
    bool historyValid=false,pending=false;
    unsigned completedImage=0,pendingImage=0;
};
} // namespace Render::PipelineDetail
