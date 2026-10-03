#pragma once

#include <chrono>
#include <stdexcept>
#include <type_traits>
#include "Render/Private/Material/rhi_material_resources.h"

namespace Render::PipelineDetail {
// Small synchronous bootstrap over the asynchronous RHI. Completion callbacks
// register ownership before waking the caller, including a late timeout result.
class ResourceBuilder {
    struct MeshOwner {
        RHIDevice& device;
        std::vector<RenderResourceHandle<VertexBufferSpec>> handles;
        ~MeshOwner() { for (auto h : handles) device.async_DeleteVertexBuffer(h); }
    };
    RHIDevice& device_;
    Material::OwnedMaterialResources owned_;
    std::shared_ptr<MeshOwner> meshes_;
    std::size_t pending_ = 0;
public:
    explicit ResourceBuilder(RHIDevice& device) : device_(device), meshes_(new MeshOwner{device, {}}) {
        if (!device_.IsRunning()) throw std::runtime_error("Start RHIDevice before initializing pipeline resources");
        owned_.dependencies.push_back(meshes_);
    }
    ~ResourceBuilder() {
        using namespace std::chrono_literals;
        while (pending_) {
            device_.returnSystem.DrainCallbacks();
            if (pending_) device_.returnSystem.WaitForCallbacks(2ms);
        }
        meshes_.reset();
        Material::RetainMaterialResources(device_, std::move(owned_)).reset();
    }
    template <class Spec, class Submit> RenderResourceHandle<Spec> Create(Submit submit) {
        if (!device_.IsRunning()) throw std::runtime_error("Cannot create pipeline resources on a stopped device");
        struct Result { RenderResourceHandle<Spec> handle; bool complete = false; };
        auto result = std::make_shared<Result>();
        ++pending_;
        try {
            submit([this, result](auto handle) {
                if (handle.IsValid()) {
                    if constexpr (std::is_same_v<Spec, ShaderSourceSpec>) owned_.sources.push_back(handle);
                    else if constexpr (std::is_same_v<Spec, ShaderProgramSpec>) owned_.programs.push_back(handle);
                    else if constexpr (std::is_same_v<Spec, PipelineSpec>) owned_.pipelines.push_back(handle);
                    else if constexpr (std::is_same_v<Spec, UniformBufferSpec>) owned_.uniformBuffers.push_back(handle);
                    else if constexpr (std::is_same_v<Spec, RHITextureSpec>) owned_.textures.push_back(handle);
                    else if constexpr (std::is_same_v<Spec, RenderTargetSpec>) owned_.renderTargets.push_back(handle);
                    else if constexpr (std::is_same_v<Spec, VertexBufferSpec>) meshes_->handles.push_back(handle);
                }
                result->handle = handle; result->complete = true; --pending_;
            });
        } catch (...) { --pending_; throw; }
        using namespace std::chrono_literals;
        auto deadline = std::chrono::steady_clock::now() + 10s;
        while (!result->complete) {
            device_.returnSystem.DrainCallbacks();
            if (result->complete) break;
            if (std::chrono::steady_clock::now() > deadline) throw std::runtime_error("Pipeline resource creation timed out");
            device_.returnSystem.WaitForCallbacks(2ms);
        }
        if (!result->handle.IsValid()) throw std::runtime_error("Pipeline resource creation failed");
        return result->handle;
    }
    auto Uniform(std::uint32_t bytes) {
        CreateUniformBufferDesc desc; desc.byteSize = bytes;
        return Create<UniformBufferSpec>([&](auto cb) { device_.async_CreateUniformBuffer(desc, cb); });
    }
    auto Texture(std::uint32_t w, std::uint32_t h, RHITextureFormat format, std::uint32_t samples = 1) {
        CreateRHITextureSpec desc;
        desc.width = w; desc.height = h;
        desc.textureDataStoreType = desc.textureUseType = format;
        desc.mipmaps = false;
        desc.samples = samples;
        desc.filterMode = format == RHITextureFormat::Depth32F ? RHIFilterMode::Nearest : RHIFilterMode::Linear;
        desc.addressMode = RHIAddressMode::ClampToEdge;
        return Create<RHITextureSpec>([&](auto cb) { device_.async_CreateTexture(desc, cb); });
    }
    auto Target(RenderResourceHandle<RHITextureSpec> color, RenderResourceHandle<RHITextureSpec> depth) {
        return Create<RenderTargetSpec>([&](auto cb) { device_.async_CreateRenderTarget({color, depth}, cb); });
    }
    auto Source(ShaderSourceType type, const std::string& source) {
        CreateShaderSourceDesc desc{type, source.data(), source.size()};
        return Create<ShaderSourceSpec>([&](auto cb) { device_.async_CreateShaderSource(desc, cb); });
    }
    auto FullscreenPipeline(const std::string& vertex, const std::string& fragment, bool depthCopy = false) {
        CreateGraphicShaderDesc desc;
        desc.vertexShaderSource = Source(ShaderSourceType::Vertex, vertex);
        desc.fragmentShaderSource = Source(ShaderSourceType::Fragment, fragment);
        auto program = Create<ShaderProgramSpec>([&](auto cb) { device_.async_CreateGraphicShader(desc, cb); });
        PipelineSpec spec;
        spec.shaderProgram = program;
        spec.expectVertexLayout = {{VertexFieldType::Vec2, VertexFieldType::Vec2}};
        spec.depthTest = spec.depthWrite = depthCopy;
        spec.depthCompare = CompareOp::Always;
        spec.cullMode = CullMode::None;
        return Create<PipelineSpec>([&](auto cb) { device_.async_CreatePipeline({spec}, cb); });
    }
    auto FullscreenMesh() {
        const std::array<float, 12> vertices{-1,-1,0,0, 3,-1,2,0, -1,3,0,2};
        const std::array<std::uint32_t, 3> indices{0,1,2};
        CreateMeshBufferDesc desc;
        desc.vertexLayout = {{VertexFieldType::Vec2, VertexFieldType::Vec2}};
        desc.vertexCount = 3; desc.vertexData = vertices.data(); desc.vertexByteSize = sizeof(vertices);
        desc.indexCount = 3; desc.indexData = indices.data(); desc.indexByteSize = sizeof(indices);
        return Create<VertexBufferSpec>([&](auto cb) { device_.async_CreateMeshBuffer(desc, cb); });
    }
    std::shared_ptr<const void> Finish() {
        return Material::RetainMaterialResources(device_, std::exchange(owned_, {}));
    }
};
} // namespace Render::PipelineDetail
