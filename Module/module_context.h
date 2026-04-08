#pragma once

#include <cstddef>
#include <type_traits>

#include "module_include_gen.h"
#include "module_manager.h"

/**
 * @brief 非模板基类，提供统一的模块上下文访问接口
 * 
 * 内部保存按全局槽位编号存储的 void* 数组
 * 所有模块访问最终都归一到同一个结构上
 */
class ModuleContext {
public:
    ModuleContext() noexcept;

    /**
     * @brief 重置所有槽位为空指针
     */
    void Reset() noexcept;

    /**
     * @brief 检查指定槽位是否已绑定
     * @param index 槽位索引
     * @return 如果槽位已绑定则返回 true
     */
    bool IsBound(std::size_t index) const noexcept;

    /**
     * @brief 按类型获取模块指针
     * @tparam T 模块类型
     * @return 模块指针，如果未绑定则返回 nullptr
     */
    template<typename T>
    T* Get() noexcept {
        static_assert(ModuleIncludeGen::HasModuleSlot<T>::value,
                      "T is not registered in module_include_gen.h");
        return static_cast<T*>(GetRaw(ModuleIncludeGen::ModuleSlot<T>::kIndex));
    }

    /**
     * @brief 按类型获取模块指针 (const 版本)
     * @tparam T 模块类型
     * @return 模块指针，如果未绑定则返回 nullptr
     */
    template<typename T>
    const T* Get() const noexcept {
        static_assert(ModuleIncludeGen::HasModuleSlot<T>::value,
                      "T is not registered in module_include_gen.h");
        return static_cast<const T*>(GetRaw(ModuleIncludeGen::ModuleSlot<T>::kIndex));
    }

protected:
    /**
     * @brief 设置指定槽位的指针
     * @param index 槽位索引
     * @param ptr 模块指针
     */
    void SetSlot(std::size_t index, void* ptr) noexcept;

    /**
     * @brief 获取指定槽位的原始指针
     * @param index 槽位索引
     * @return 槽位指针
     */
    void* GetRaw(std::size_t index) noexcept;

    /**
     * @brief 获取指定槽位的原始指针 (const 版本)
     * @param index 槽位索引
     * @return 槽位指针
     */
    const void* GetRaw(std::size_t index) const noexcept;

private:
    void* slots_[ModuleIncludeGen::kModuleCount];
};

/**
 * @brief 模板化的模块上下文管理器
 * 
 * 模板参数 Modules... 表示"本上下文需要初始化哪些模块类型"
 * 初始化时根据类型参数拿到全局槽位编号，向 ModuleManager 或 ModuleHostAPI 查询模块指针
 * 
 * @tparam Modules 模块类型列表
 */
template<typename... Modules>
class ModuleContextManager : public ModuleContext {
public:
    /**
     * @brief 从 ModuleManager 初始化上下文
     * @param manager 模块管理器引用
     * @return 如果所有模块都成功初始化则返回 true
     */
    bool Initialize(ModuleManager& manager) noexcept {
        Reset();
        bool allSuccess = true;
        (..., [this, &manager, &allSuccess] {
            using T = Modules;
            constexpr std::size_t index = ModuleIncludeGen::ModuleSlot<T>::kIndex;
            auto* module = manager.GetModule<T>();
            if (module == nullptr) {
                allSuccess = false;
            }
            SetSlot(index, module);
        }());
        return allSuccess;
    }

    /**
     * @brief 从 ModuleHostAPI 初始化上下文
     * @param api 模块主机 API 引用
     * @return 如果所有模块都成功初始化则返回 true
     */
    bool Initialize(const ModuleHostAPI& api) noexcept {
        Reset();
        bool allSuccess = true;
        (..., [this, &api, &allSuccess] {
            using T = Modules;
            constexpr std::size_t index = ModuleIncludeGen::ModuleSlot<T>::kIndex;
            auto* module = QueryModuleFromHostAPI<T>(api);
            if (module == nullptr) {
                allSuccess = false;
            }
            SetSlot(index, module);
        }());
        return allSuccess;
    }
};

// 在 ModuleContextManager 定义之后，包含 module_context_gen.h 以生成 GeneratedModuleContext
#include "module_context_gen.h"
