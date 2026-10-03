#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "module_base.h"
#include "Render/Public/Material/material.h"
#include "Render/Public/render_backend_context.h"
#include "Render/Public/Pipeline/material_render_pipeline.h"

namespace MaterialLab {

struct FrameSettings {
    std::uint32_t width = 1280, height = 800;
    float lightTime = 0.0f;
    float exposure = 1.0f;
    float cameraYaw = 0.0f, cameraPitch = 0.08f, cameraDistance = 18.0f;
    // -1 draws the gallery. An index isolates a material for editing/testing.
    int isolatedMaterial = -1;
    bool present = true;
    Render::MaterialPipelineSettings effects;
};

struct MaterialInfo {
    std::string name;
    float metallic = 0.0f, roughness = 0.5f;
    bool normalMap = false, metallicMap = false, roughnessMap = false, aoMap = false;
};

struct FramePixels {
    std::uint32_t width = 0, height = 0;
    // RGBA8, first row at the bottom, as returned by OpenGL diagnostics.
    std::vector<std::uint8_t> rgba;
};

// Standalone material test module. The caller owns the window/context and must
// keep it alive until Shutdown. All public calls run on the application's thread.
class MaterialLabModule final : public IModule {
public:
    explicit MaterialLabModule(Render::OpenglBackendContext context,
                               std::string legacyTextureDirectory = {});
    ~MaterialLabModule() override;
    MaterialLabModule(const MaterialLabModule&) = delete;
    MaterialLabModule& operator=(const MaterialLabModule&) = delete;
    const char* GetName() const noexcept override { return "MaterialLab"; }
    bool Startup() override;
    void Shutdown() override;
    bool IsStarted() const noexcept override;
    const std::string& LastError() const noexcept { return error_; }

    std::size_t MaterialCount() const;
    MaterialInfo InspectMaterial(std::size_t index) const;
    bool SetMaterialParameter(std::size_t index, std::string_view name,
                              const Render::Material::ParameterValue& value);
    bool ResetMaterial(std::size_t index);
    // Reliable, completed submission: deliberately bounded to one frame in flight
    // for this small laboratory. No rendering work bypasses the RHI encoder.
    bool Render(const FrameSettings& settings);
    // Test-only backend diagnostic, called after Render(present=false).
    FramePixels Readback();
    Render::PipelineStatistics PipelineStatistics() const;
    // Test diagnostics: minimum CSM depth, AO minimum and maximum HDR channel.
    glm::vec3 InspectPipelineBuffers();

private:
    struct Impl;
    Render::OpenglBackendContext context_;
    std::string legacyTextureDirectory_;
    std::unique_ptr<Impl> impl_;
    std::string error_;
};

} // namespace MaterialLab
