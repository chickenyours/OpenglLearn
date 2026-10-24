#pragma once

#include "module_base.h"
#include "Model/Public/static_model.h"
#include "Render/Public/Pipeline/render_world.h"
#include <cstdint>

namespace Render { class RHIDevice; class RHIFrameEncoder; }

namespace Model {

// The render device must outlive this resource, its material snapshots, and
// submitted frames. Application-thread API, matching MaterialService and RHI.
class ModelResource final {
public:
    ~ModelResource();
    ModelResource(const ModelResource&) = delete;
    ModelResource& operator=(const ModelResource&) = delete;
    const StaticModel& Source() const;
    // Uploaded packed vertex contents changed, or a failed batch may have
    // partially changed them. Identical successful uploads retain this value.
    std::uint64_t GeometryRevision() const noexcept;
    Render::Material::MaterialService& Materials();
    const std::vector<Render::Material::MaterialHandle>& MaterialHandles() const;
    // Subtract the double render origin BEFORE converting to GPU float matrices.
    // Camera/view and other world objects must use the same render origin.
    bool AppendTo(Render::RenderFrame& frame, const glm::dmat4& placement = glm::dmat4(1),
        const glm::dvec3& renderOrigin = glm::dvec3(0), const glm::mat4& view = glm::mat4(1),
        std::string* error = nullptr) const;
    bool KeepAlive(Render::RHIFrameEncoder& encoder) const;
    // Application-thread preview/deformation path. Vertex topology is fixed;
    // updates complete before return so the next recorded frame sees the pose.
    // A backend failure is not transactional; GeometryRevision invalidates
    // dependent caches if writes may already have reached any mesh.
    bool UpdateGeometry(const StaticModel& deformed, std::string* error = nullptr);
private:
    struct Impl;
    explicit ModelResource(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
    friend class ModelModule;
};

// Borrows an already running Render device; owns no window or render loop.
struct ModelUploadOptions {
    // PBR eye materials need the original spherical sclera coordinates.
    // Opt-in keeps the legacy 48-byte vertex layout for existing callers.
    bool includeSecondUV = false;
};
class ModelModule final : public IModule {
public:
    explicit ModelModule(Render::RHIDevice& device);
    ~ModelModule() override;
    const char* GetName() const noexcept override { return "Model"; }
    bool Startup() override;
    void Shutdown() override;
    bool IsStarted() const noexcept override;
    std::shared_ptr<ModelResource> Load(const std::filesystem::path& path,
        const LoadOptions& options = {});
    std::shared_ptr<ModelResource> Upload(std::shared_ptr<const StaticModel> model,
        const ModelUploadOptions& options = {});
    const std::string& LastError() const noexcept;
private:
    Render::RHIDevice& device_;
    bool started_ = false;
    std::string error_;
};

} // namespace Model
