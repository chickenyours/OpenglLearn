#pragma once

#include "engine/ECS/Scene/scene.h"

#include <cstddef>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ECS::Core {

// Main-thread structural changes for one Scene, which must outlive this buffer.
// Record full handles and initializer values, never borrowed component pointers.
// Playback is a boundary outside all active query iterations: all destroys run
// first, then creates retain their recording order. This is not a transaction:
// successfully applied earlier commands remain applied if an initializer throws.
class CommandBuffer {
public:
    using Initializer = std::function<void(Scene&, EntityHandle)>;
    struct Result {
        std::size_t created = 0;
        std::size_t destroyed = 0;
        std::size_t skipped = 0;
    };

    explicit CommandBuffer(Scene& scene) : scene_(scene) {}
    CommandBuffer(const CommandBuffer&) = delete;
    CommandBuffer& operator=(const CommandBuffer&) = delete;
    CommandBuffer(CommandBuffer&&) = delete;
    CommandBuffer& operator=(CommandBuffer&&) = delete;

    // Handles belong to the bound Scene; ID-only handles have no valid generation
    // and are skipped. Duplicate and expired handles cannot delete a replacement.
    void Destroy(EntityHandle entity) { destroys_.push_back(entity); }

    void Create(ObjectWeakPtr<ArchType> archetype, Initializer initialize = {}) {
        creates_.push_back({std::move(archetype), std::move(initialize)});
    }

    bool Empty() const noexcept { return destroys_.empty() && creates_.empty(); }

    // During playback this clears only the next batch, not the active batch.
    void Clear() noexcept { destroys_.clear(); creates_.clear(); }

    Result Playback() {
        if (playing_) throw std::logic_error("CommandBuffer::Playback cannot be recursive");
        playing_ = true;
        struct Guard {
            bool& flag;
            ~Guard() { flag = false; }
        } guard{playing_};

        // Callbacks may record into the now-empty next batch. Recursive playback
        // is explicitly rejected. On failure both batches are discarded, so a
        // later call cannot replay any partially applied command.
        std::vector<EntityHandle> destroys;
        std::vector<CreateCommand> creates;
        destroys.swap(destroys_);
        creates.swap(creates_);
        Result result;
        try {
            for (const auto entity : destroys) {
                if (!scene_.IsAlive(entity)) { ++result.skipped; continue; }
                scene_.DeleteEntity(entity);
                ++result.destroyed;
            }
            for (auto& command : creates) {
                if (!command.archetype.Get()) { ++result.skipped; continue; }
                const auto entity = scene_.CreateEntity(command.archetype);
                if (!scene_.IsAlive(entity)) { ++result.skipped; continue; }
                try {
                    if (command.initialize) command.initialize(scene_, entity);
                } catch (...) {
                    // An initializer may already have removed the new entity;
                    // its old generation must never retire a replacement.
                    if (scene_.IsAlive(entity)) scene_.DeleteEntity(entity);
                    throw;
                }
                ++result.created;
            }
        } catch (...) {
            Clear();
            throw;
        }
        return result;
    }

private:
    struct CreateCommand {
        ObjectWeakPtr<ArchType> archetype;
        Initializer initialize;
    };
    Scene& scene_;
    std::vector<EntityHandle> destroys_;
    std::vector<CreateCommand> creates_;
    bool playing_ = false;
};

} // namespace ECS::Core
