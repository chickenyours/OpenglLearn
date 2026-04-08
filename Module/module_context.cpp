#include "module_context.h"

ModuleContext::ModuleContext() noexcept {
    Reset();
}

void ModuleContext::Reset() noexcept {
    for (std::size_t i = 0; i < ModuleIncludeGen::kModuleCount; ++i) {
        slots_[i] = nullptr;
    }
}

bool ModuleContext::IsBound(std::size_t index) const noexcept {
    if (index >= ModuleIncludeGen::kModuleCount) {
        return false;
    }
    return slots_[index] != nullptr;
}

void ModuleContext::SetSlot(std::size_t index, void* ptr) noexcept {
    if (index < ModuleIncludeGen::kModuleCount) {
        slots_[index] = ptr;
    }
}

void* ModuleContext::GetRaw(std::size_t index) noexcept {
    if (index >= ModuleIncludeGen::kModuleCount) {
        return nullptr;
    }
    return slots_[index];
}

const void* ModuleContext::GetRaw(std::size_t index) const noexcept {
    if (index >= ModuleIncludeGen::kModuleCount) {
        return nullptr;
    }
    return slots_[index];
}
