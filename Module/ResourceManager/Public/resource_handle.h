#pragma once

#include <string>
#include <memory>
#include <functional>

namespace Resource {

// 前向声明
template<typename T>
class ControlBlock;

/**
 * @brief 资源句柄模板类 - 管理资源生命周期
 *
 * ResourceHandle 是资源管理的核心句柄类，提供：
 * - 自动引用计数管理
 * - 资源访问操作符重载
 * - 自动释放回调
 *
 * @tparam T 资源类型（可以是任意类型，无需继承特定接口）
 */
template<typename T>
class ResourceHandle {
public:
    /**
     * @brief 默认构造函数
     */
    ResourceHandle() = default;

    /**
     * @brief 空指针构造函数
     * @param nullptr_t 空指针标签
     */
    ResourceHandle(std::nullptr_t);

    /**
     * @brief 构造函数
     * @param name 资源名称
     * @param controlBlock 控制块
     */
    ResourceHandle(const std::string& name, std::shared_ptr<ControlBlock<T>> controlBlock);

    /**
     * @brief 析构函数
     *
     * 调用释放回调函数（如果存在）。
     */
    ~ResourceHandle();

    /**
     * @brief 拷贝构造函数
     *
     * 拷贝构造会共享控制块，增加引用计数。
     */
    ResourceHandle(const ResourceHandle& other);

    /**
     * @brief 移动构造函数
     */
    ResourceHandle(ResourceHandle&& other) noexcept;

    /**
     * @brief 拷贝赋值运算符
     *
     * 拷贝赋值会共享控制块，增加引用计数。
     */
    ResourceHandle& operator=(const ResourceHandle& other);

    /**
     * @brief 移动赋值运算符
     */
    ResourceHandle& operator=(ResourceHandle&& other) noexcept;

    /**
     * @brief 指针访问运算符
     */
    T* operator->();

    /**
     * @brief 常量指针访问运算符
     */
    const T* operator->() const;

    /**
     * @brief 解引用运算符
     */
    T& operator*();

    /**
     * @brief 常量解引用运算符
     */
    const T& operator*() const;

    /**
     * @brief 获取原始指针
     */
    T* get() const;

    /**
     * @brief 布尔转换运算符
     * @return true 如果资源指针有效
     */
    explicit operator bool() const;

    /**
     * @brief 获取资源名称
     */
    const std::string& GetName() const;

    /**
     * @brief 获取引用计数
     */
    size_t GetRefCount() const;

private:
    std::string name_;
    std::shared_ptr<ControlBlock<T>> controlBlock_;
};

} // namespace Resource

// 注意：resource_handle.inl 的实现需要 ControlBlock 完整定义
// 因此不在这里包含，而是在 resource_manager_module.inl 中统一包含
