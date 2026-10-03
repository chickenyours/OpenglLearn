#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Render/Public/Material/material.h"
#include "Render/Public/RHIResourceType/rhi_resource_type.h"

namespace Render::Material {

struct TextureBinding {
    std::uint32_t slot = 0;
    RenderResourceHandle<RHITextureSpec> texture;
};

// Supplied only after the RHI creation callbacks have completed. The lifetime
// token must own these resources (including textures), not merely their handles.
struct MaterialPassResources {
    // Declaration used to compile this pipeline. Equal byte size alone does not
    // establish that two templates have compatible names/types/offsets.
    std::shared_ptr<const MaterialTemplate> expectedTemplate;
    RenderResourceHandle<PipelineSpec> pipeline;
    RenderResourceHandle<UniformBufferSpec> parameterBuffer;
    std::uint32_t parameterBufferBytes = 0;
    std::uint32_t parameterBinding = MaterialParameterBinding;
    std::vector<TextureBinding> textures;
    std::shared_ptr<const void> lifetime;
    // Optional depth/shadow variant using the same template, textures and UBO.
    // The lifetime token must also retain this pipeline and its shader program.
    RenderResourceHandle<PipelineSpec> shadowPipeline;
};

class MaterialSnapshot final {
public:
    static std::shared_ptr<const MaterialSnapshot> Create(
        const MaterialInstance& instance, const MaterialPassResources& resources,
        const std::vector<ParameterOverride>& overrides = {}, std::string* error = nullptr) {
        ParameterBlock parameters;
        if(!instance.BuildDrawParameters(overrides, parameters, error)) return {};
        if(resources.expectedTemplate != parameters.GetTemplate())
            return Fail(error, "pipeline was not declared for this material template");
        if(resources.parameterBinding != MaterialParameterBinding)
            return Fail(error, "material parameters must use binding 2");
        if(!resources.pipeline.IsValid() || !resources.lifetime)
            return Fail(error, "material requires a ready pipeline and an owning lifetime token");
        const auto byteSize = parameters.Bytes().size();
        if(byteSize && (!resources.parameterBuffer.IsValid() ||
            resources.parameterBufferBytes != byteSize))
            return Fail(error, "material UBO must match the schema size");

        auto result = std::shared_ptr<MaterialSnapshot>(new MaterialSnapshot());
        result->parameters_ = std::move(parameters);
        result->resources_ = resources;
        result->revision_ = instance.Revision();
        auto& bindings = result->resources_.textures;
        const auto& slots = result->parameters_.GetTemplate()->Desc().textures;
        for(std::size_t i = 0; i < bindings.size(); ++i) {
            if(std::none_of(slots.begin(), slots.end(), [&](const auto& slot) {
                return slot.slot == bindings[i].slot;
            })) return Fail(error, "texture binding is not declared by the material template");
            for(std::size_t j = 0; j < i; ++j)
                if(bindings[i].slot == bindings[j].slot)
                    return Fail(error, "duplicate material texture binding");
        }
        for(const auto& slot : slots) {
            const auto found = std::find_if(bindings.begin(), bindings.end(), [&](const auto& binding) {
                return binding.slot == slot.slot;
            });
            if(found == bindings.end()) {
                if(slot.required) return Fail(error, "missing required texture: " + slot.name);
                bindings.push_back({slot.slot, {}}); // explicit unbind, never inherit previous draw
            } else if(slot.required && !found->texture.IsValid()) {
                return Fail(error, "invalid required texture: " + slot.name);
            }
        }
        std::sort(bindings.begin(), bindings.end(), [](const auto& a, const auto& b) {
            return a.slot < b.slot;
        });
        if(error) error->clear();
        return result;
    }

    RenderResourceHandle<PipelineSpec> Pipeline() const { return resources_.pipeline; }
    Domain GetDomain() const { return parameters_.GetTemplate()->Desc().domain; }
    const ParameterBlock& Parameters() const { return parameters_; }
    const MaterialPassResources& Resources() const { return resources_; }
    // A source instance revision is diagnostic data, not a snapshot/cache key:
    // two draw overrides or replaced instances may have the same revision.
    std::uint64_t SourceInstanceRevision() const { return revision_; }

private:
    static std::shared_ptr<const MaterialSnapshot> Fail(std::string* error, std::string message) {
        if(error) *error = std::move(message);
        return {};
    }
    ParameterBlock parameters_;
    MaterialPassResources resources_;
    std::uint64_t revision_ = 0;
};

struct MaterialHandle {
    std::uint32_t index = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t generation = 0;
    bool IsValid() const { return index != std::numeric_limits<std::uint32_t>::max(); }
    bool operator==(const MaterialHandle&) const = default;
};

// Main-thread service suitable for ECS::System::Context::SetService. It owns
// instances; ECS stores small generation-checked handles. Capture returns data
// independent of subsequent edits, replacement, or removal of the instance.
class MaterialService final {
public:
    MaterialHandle Register(std::shared_ptr<const MaterialAsset> asset,
        const MaterialPassResources& resources, std::string* error = nullptr) {
        auto instance = std::make_unique<MaterialInstance>(std::move(asset));
        auto snapshot = MaterialSnapshot::Create(*instance, resources, {}, error);
        if(!snapshot) return {};
        std::uint32_t index = 0;
        for(; index < entries_.size(); ++index) if(!entries_[index].instance) break;
        if(index == entries_.size()) entries_.emplace_back();
        auto& entry = entries_[index];
        entry.instance = std::move(instance);
        entry.resources = resources;
        entry.snapshot = std::move(snapshot);
        return {index, entry.generation};
    }

    bool SetParameter(MaterialHandle handle, std::string_view name,
        const ParameterValue& value, std::string* error = nullptr) {
        auto* entry = Find(handle);
        if(!entry) return Invalid(error);
        if(!entry->instance->Set(name, value, error)) return false;
        entry->snapshot.reset();
        return true;
    }

    std::shared_ptr<const MaterialSnapshot> Capture(MaterialHandle handle,
        const std::vector<ParameterOverride>& overrides = {}, std::string* error = nullptr) {
        auto* entry = Find(handle);
        if(!entry) { Invalid(error); return {}; }
        if(!overrides.empty())
            return MaterialSnapshot::Create(*entry->instance, entry->resources, overrides, error);
        if(!entry->snapshot)
            entry->snapshot = MaterialSnapshot::Create(*entry->instance, entry->resources, {}, error);
        else if(error) error->clear();
        return entry->snapshot;
    }

    // Transactional publication: a failed candidate leaves the last ready
    // material untouched. Shader compilation/loading happens before this call.
    bool Replace(MaterialHandle handle, std::shared_ptr<const MaterialAsset> asset,
        const MaterialPassResources& resources, std::string* error = nullptr) {
        auto* entry = Find(handle);
        if(!entry) return Invalid(error);
        auto instance = std::make_unique<MaterialInstance>(std::move(asset));
        auto snapshot = MaterialSnapshot::Create(*instance, resources, {}, error);
        if(!snapshot) return false;
        entry->instance = std::move(instance);
        entry->resources = resources;
        entry->snapshot = std::move(snapshot);
        return true;
    }

    bool Remove(MaterialHandle handle) {
        auto* entry = Find(handle);
        if(!entry) return false;
        entry->snapshot.reset();
        entry->instance.reset();
        entry->resources = {};
        ++entry->generation;
        return true;
    }

    void Clear() {
        // Keep generation counters so handles cannot alias resources after clear.
        for(std::uint32_t i = 0; i < entries_.size(); ++i)
            if(entries_[i].instance) Remove({i, entries_[i].generation});
    }

private:
    struct Entry {
        std::uint32_t generation = 0;
        std::unique_ptr<MaterialInstance> instance;
        MaterialPassResources resources;
        std::shared_ptr<const MaterialSnapshot> snapshot;
    };
    Entry* Find(MaterialHandle handle) {
        if(handle.index >= entries_.size()) return nullptr;
        auto& entry = entries_[handle.index];
        return entry.instance && entry.generation == handle.generation ? &entry : nullptr;
    }
    static bool Invalid(std::string* error) {
        if(error) *error = "invalid or retired material handle";
        return false;
    }
    std::vector<Entry> entries_;
};

} // namespace Render::Material
