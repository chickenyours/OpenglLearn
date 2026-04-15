#pragma once

#include <memory>
#include <functional>
#include <atomic>

namespace Resource {

/**
 * @brief 资源控制块 - 管理资源的生命周期
 * 
 * 控制块负责：
 * - 引用计数管理
 * - 资源销毁策略
 * - 零引用时的回调处理
 * 
 * @tparam T 资源类型（可以是任意类型，无需继承特定接口）
 */
template<typename T>
class ControlBlock {
public:
    /**
     * @brief 销毁函数类型
     * @param T* 资源指针
     */
    using DestroyFunc = std::function<void(T*)>;

    /**
     * @brief 零引用回调函数类型
     * @param T* 资源指针
     */
    using ZeroRefCallback = std::function<void(T*)>;

    /**
     * @brief 默认构造函数
     */
    ControlBlock() = default;

    /**
     * @brief 构造函数
     * @param resource 资源指针（unique_ptr 所有权转移）
     * @param destroyFunc 销毁函数（如果为空则使用 unique_ptr 的默认删除器）
     * @param zeroRefCallback 零引用回调
     */
    ControlBlock(std::unique_ptr<T> resource,
                 DestroyFunc destroyFunc,
                 ZeroRefCallback zeroRefCallback);

    /**
     * @brief 增加引用计数
     * @return 新的引用计数值
     */
    size_t AddRef();

    /**
     * @brief 减少引用计数
     * @return 新的引用计数值
     */
    size_t ReleaseRef();

    /**
     * @brief 获取当前引用计数
     * @return 引用计数值
     */
    size_t GetRefCount() const;

    /**
     * @brief 获取资源指针
     * @return 资源指针
     */
    T* GetResource() const;

    /**
     * @brief 检查资源是否有效
     * @return true 如果资源指针非空
     */
    bool IsValid() const;

    /**
     * @brief 强制释放资源（不管引用计数）
     */
    void ForceRelease();

private:
    std::unique_ptr<T> resource_;                    ///< 资源所有权（unique_ptr）
    DestroyFunc destroyFunc_;                        ///< 销毁函数
    ZeroRefCallback zeroRefCallback_;                ///< 零引用回调
    std::atomic<size_t> refCount_{0};                ///< 引用计数
};

} // namespace Resource

// 模板实现
#include "resource_control_block.inl"
