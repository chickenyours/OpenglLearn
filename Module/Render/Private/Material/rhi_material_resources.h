#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "Render/Private/rhi_device.h"

namespace Render::Material {

// Ownership transfer. Each GPU handle must have exactly one owner; share the
// returned lease across materials instead of adopting the same handle twice.
struct OwnedMaterialResources {
    std::vector<RenderResourceHandle<PipelineSpec>> pipelines;
    std::vector<RenderResourceHandle<UniformBufferSpec>> uniformBuffers;
    std::vector<RenderResourceHandle<RHITextureSpec>> textures;
    std::vector<RenderResourceHandle<ShaderProgramSpec>> programs;
    std::vector<RenderResourceHandle<ShaderSourceSpec>> sources;
    std::vector<std::shared_ptr<const void>> dependencies;
    std::vector<RenderResourceHandle<RenderTargetSpec>> renderTargets;
};

namespace Detail {
struct RHIResourceLease final {
    RHIDevice& device;
    OwnedMaterialResources resources;
    ~RHIResourceLease() {
        for(auto handle : resources.renderTargets) if(handle.IsValid()) device.async_DeleteRenderTarget(handle);
        for(auto handle : resources.pipelines) if(handle.IsValid()) device.async_DeletePipeline(handle);
        for(auto handle : resources.uniformBuffers) if(handle.IsValid()) device.async_DeleteUniformBuffer(handle);
        for(auto handle : resources.textures) if(handle.IsValid()) device.async_DeleteTexture(handle);
        for(auto handle : resources.programs) if(handle.IsValid()) device.async_DeleteShaderProgram(handle);
        for(auto handle : resources.sources) if(handle.IsValid()) device.async_DeleteShaderSource(handle);
    }
};
}

// Device/context must outlive all leases. Release application/service snapshots
// before StopAndRelease; submitted command buffers release their leases during
// execution or replacement, and shutdown drains the resulting delete commands.
inline std::shared_ptr<const void> RetainMaterialResources(
    RHIDevice& device, OwnedMaterialResources resources) {
    return std::shared_ptr<const Detail::RHIResourceLease>(
        new Detail::RHIResourceLease{device, std::move(resources)});
}

} // namespace Render::Material
