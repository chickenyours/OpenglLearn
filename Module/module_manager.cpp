#include "module_manager.h"

namespace ModuleManagerDetail {

template<typename T>
bool OnPreModuleStartup(ModuleManager&, T&) noexcept {
    return true;
}

template<>
bool OnPreModuleStartup<Render::RenderModule>(ModuleManager& manager,
                                              Render::RenderModule& module) noexcept {
    auto* app = manager.GetModule<Application::ApplicationModule>();
    if (app == nullptr) {
        return false;
    }
    return module.InitializeFromApplication(*app);
}

} // namespace ModuleManagerDetail

ModuleManager& ModuleManager::Instance() noexcept {
    static ModuleManager instance;
    return instance;
}

ModuleManager::ModuleManager() noexcept {
    hostAPI_.context = this;
    hostAPI_.query_by_index = &ModuleManager::QueryByIndexThunk;
    BuildModuleTable();
}

bool ModuleManager::Startup() {
    if (started_) {
        return true;
    }

    BuildModuleTable();

#define MODULE_MANAGER_START_ONE(index, type, member)                                \
    do {                                                                              \
        if (!ModuleManagerDetail::OnPreModuleStartup(*this, member)) {                \
            Shutdown();                                                                \
            return false;                                                              \
        }                                                                             \
        if (!member.Startup()) {                                                      \
            Shutdown();                                                                \
            return false;                                                              \
        }                                                                             \
    } while (false)

    MODULE_INCLUDE_GEN_MODULES(MODULE_MANAGER_START_ONE)
#undef MODULE_MANAGER_START_ONE

    started_ = true;
    return true;
}

void ModuleManager::Shutdown() noexcept {
#define MODULE_MANAGER_SHUTDOWN_ONE(index, type, member) \
    do {                                                  \
        if (member.IsStarted()) {                         \
            member.Shutdown();                            \
        }                                                 \
    } while (false)

    MODULE_INCLUDE_GEN_MODULES_REVERSE(MODULE_MANAGER_SHUTDOWN_ONE)
#undef MODULE_MANAGER_SHUTDOWN_ONE

    started_ = false;
}

bool ModuleManager::IsStarted() const noexcept {
    return started_;
}

void* ModuleManager::QueryRaw(std::size_t index) noexcept {
    if (index >= ModuleIncludeGen::kModuleCount) {
        return nullptr;
    }
    return moduleTable_[index];
}

const void* ModuleManager::QueryRaw(std::size_t index) const noexcept {
    if (index >= ModuleIncludeGen::kModuleCount) {
        return nullptr;
    }
    return moduleTable_[index];
}

const ModuleHostAPI& ModuleManager::GetHostAPI() const noexcept {
    return hostAPI_;
}

void ModuleManager::BuildModuleTable() noexcept {
#define MODULE_MANAGER_BIND_SLOT(index, type, member) \
    moduleTable_[index] = static_cast<IModule*>(&member);

    MODULE_INCLUDE_GEN_MODULES(MODULE_MANAGER_BIND_SLOT)
#undef MODULE_MANAGER_BIND_SLOT
}

void* ModuleManager::QueryByIndexThunk(void* context, std::size_t index) noexcept {
    if (context == nullptr) {
        return nullptr;
    }
    return static_cast<ModuleManager*>(context)->QueryRaw(index);
}
