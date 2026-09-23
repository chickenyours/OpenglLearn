#pragma once

#include <cstdint>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "engine/ECS/Profiling/profiler.h"

namespace ECS::Core {
class Scene;
}

namespace ECS::System {

enum class Phase : std::uint8_t {
    PreUpdate,
    Update,
    PostUpdate,
    RenderExtract,
    RenderSubmit,
};
enum class AccessMode : std::uint8_t {
    Read,
    Write,
};

struct ComponentAccess {
    std::type_index type{typeid(void)};
    AccessMode mode = AccessMode::Read;
};

// Per-frame data and explicitly injected engine services.  Systems depend on
// public services instead of reaching into global renderer/game singletons.
class Context {
public:
    Core::Scene* scene = nullptr;
    double deltaSeconds = 0.0;
    std::uint64_t frameIndex = 0;

    template <typename T>
    void SetService(T* service) {
        services_[std::type_index(typeid(T))] = service;
    }

    template <typename T>
    T* GetService() const {
        const auto found = services_.find(std::type_index(typeid(T)));
        return found == services_.end() ? nullptr : static_cast<T*>(found->second);
    }

private:
    std::unordered_map<std::type_index, void*> services_;
};

// Main-thread lifecycle interface.  Execute() is retained so existing systems
// can be migrated without a flag day; new systems normally override OnTick().
class System {
public:
    explicit System(std::string_view name = "System", Phase phase = Phase::Update)
        : name_(name), phase_(phase) {}

    virtual ~System() = default;

    System(const System&) = delete;
    System& operator=(const System&) = delete;
    System(System&&) = delete;
    System& operator=(System&&) = delete;

    virtual bool OnStart() { return true; }
    virtual void OnTick() { Execute(); }
    virtual void Execute() {}
    virtual void OnEnd() {}

    std::string_view GetName() const noexcept { return name_; }
    Phase GetPhase() const noexcept { return phase_; }
    bool IsEnabled() const noexcept { return enabled_; }
    bool IsStarted() const noexcept { return started_; }
    const std::vector<ComponentAccess>& GetComponentAccesses() const noexcept {
        return componentAccesses_;
    }

    void SetEnabled(bool enabled) noexcept { enabled_ = enabled; }

protected:
    Context* GetContext() const noexcept { return context_; }

    template <typename ComponentT>
    void Reads() {
        DeclareAccess<ComponentT>(AccessMode::Read);
    }

    template <typename ComponentT>
    void Writes() {
        DeclareAccess<ComponentT>(AccessMode::Write);
    }

private:
    friend class Pipeline;

    template <typename ComponentT>
    void DeclareAccess(AccessMode mode) {
        const std::type_index type(typeid(ComponentT));
        for (ComponentAccess& access : componentAccesses_) {
            if (access.type == type) {
                if (mode == AccessMode::Write) access.mode = mode;
                return;
            }
        }
        componentAccesses_.push_back(ComponentAccess{type, mode});
    }

    bool StartInternal(Context& context) {
        if (started_) return true;
        context_ = &context;
        started_ = OnStart();
        context_ = nullptr;
        return started_;
    }

    void TickInternal(Context& context) {
        if (!started_ || !enabled_) return;
        context_ = &context;
#if ECS_PROFILING_ENABLED
        ECS::Profiling::ScopedTimer profileTimer(name_);
#endif
        OnTick();
        context_ = nullptr;
    }

    void EndInternal(Context& context) {
        if (!started_) return;
        context_ = &context;
        OnEnd();
        context_ = nullptr;
        started_ = false;
    }

    std::string_view name_;
    Phase phase_ = Phase::Update;
    bool enabled_ = true;
    bool started_ = false;
    Context* context_ = nullptr;
    std::vector<ComponentAccess> componentAccesses_;
};

} // namespace ECS::System
