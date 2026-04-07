#pragma once

#include <cstddef>
#include <type_traits>

#include "module.h"
#include "module_include_gen.h"

struct ModuleHostAPI {
    void* context = nullptr;
    void* (*query_by_index)(void* context, std::size_t index) noexcept = nullptr;
};

class ModuleManager final {
public:
    static ModuleManager& Instance() noexcept;

    ModuleManager(const ModuleManager&) = delete;
    ModuleManager& operator=(const ModuleManager&) = delete;

    bool Startup();
    void Shutdown() noexcept;
    bool IsStarted() const noexcept;

    template<typename T>
    T* GetModule() noexcept {
        static_assert(ModuleIncludeGen::HasModuleSlot<T>::value,
                      "T is not registered in module_include_gen.h");
        return static_cast<T*>(QueryRaw(ModuleIncludeGen::ModuleSlot<T>::kIndex));
    }

    template<typename T>
    const T* GetModule() const noexcept {
        static_assert(ModuleIncludeGen::HasModuleSlot<T>::value,
                      "T is not registered in module_include_gen.h");
        return static_cast<const T*>(QueryRaw(ModuleIncludeGen::ModuleSlot<T>::kIndex));
    }

    void* QueryRaw(std::size_t index) noexcept;
    const void* QueryRaw(std::size_t index) const noexcept;

    const ModuleHostAPI& GetHostAPI() const noexcept;

private:
    ModuleManager() noexcept;
    ~ModuleManager() = default;

    void BuildModuleTable() noexcept;
    static void* QueryByIndexThunk(void* context, std::size_t index) noexcept;

private:
#define MODULE_MANAGER_DECLARE_MEMBER(index, type, member) type member{};
    MODULE_INCLUDE_GEN_MODULES(MODULE_MANAGER_DECLARE_MEMBER)
#undef MODULE_MANAGER_DECLARE_MEMBER

    IModule* moduleTable_[ModuleIncludeGen::kModuleCount] = {};
    bool started_ = false;
    ModuleHostAPI hostAPI_{};
};

template<typename T>
T* QueryModuleFromHostAPI(const ModuleHostAPI& api) noexcept {
    static_assert(ModuleIncludeGen::HasModuleSlot<T>::value,
                  "T is not registered in module_include_gen.h");

    if (api.query_by_index == nullptr) {
        return nullptr;
    }

    return static_cast<T*>(
        api.query_by_index(api.context, ModuleIncludeGen::ModuleSlot<T>::kIndex)
    );
}
