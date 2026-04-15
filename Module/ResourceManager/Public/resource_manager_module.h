#pragma once

#include "module.h"
#include "resource_control_block.h"
#include "resource_handle.h"
#include "resource_load_options.h"

#include <string>
#include <unordered_map>
#include <mutex>
#include <functional>
#include <memory>
#include <any>

namespace Resource {

/**
 * @brief 资源管理器模块类
 *
 * 负责管理所有资源的生命周期，提供资源获取、释放等功能。
 * 采用单例模式，通过 GetInstance() 获取全局实例。
 * 
 * 新设计特点：
 * - 不再要求资源类型继承 ILoadable 接口
 * - 通过 ControlBlock 管理资源生命周期
 * - 支持任意类型 T 的资源管理
 */
class ResourceManagerModule final : public IModule {
public:
    /**
     * @brief 获取单例实例
     */
    static ResourceManagerModule& GetInstance();

    ResourceManagerModule() = default;
    ~ResourceManagerModule() override = default;

    ResourceManagerModule(const ResourceManagerModule&) = delete;
    ResourceManagerModule& operator=(const ResourceManagerModule&) = delete;

    const char* GetName() const noexcept override { return "ResourceManager"; }

    bool Startup() override;

    void Shutdown() override;

    bool IsStarted() const noexcept override { return started_; }

    /**
     * @brief 获取资源句柄
     *
     * @tparam T 资源类型
     * @param options 加载选项
     * @return ResourceHandle<T> 资源句柄
     */
    template<typename T>
    ResourceHandle<T> Get(const LoadOptions<T>& options);

    /**
     * @brief 尝试释放所有零引用资源
     *
     * @tparam T 资源类型
     */
    template<typename T>
    void TryReleaseAll();

private:
    /**
     * @brief 资源池模板类（内部实现）
     *
     * 负责管理特定类型资源的池化存储和引用计数。
     * 使用 ControlBlock 管理资源生命周期。
     *
     * @note 此类型为内部实现细节，不应在外部直接使用。
     */
    template<typename T>
    class ResourcePool;

    /**
     * @brief 获取指定类型的资源池
     *
     * @tparam T 资源类型
     * @return ResourcePool<T>& 资源池引用
     */
    template<typename T>
    ResourcePool<T>& GetPool();

    bool started_ = false;  ///< 模块是否已启动
};

} // namespace Resource

// 包含模板实现
#include "resource_manager_module.inl"
