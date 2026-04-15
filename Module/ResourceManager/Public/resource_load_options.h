#pragma once

#include <string>
#include <functional>
#include <memory>

namespace Resource {

/**
 * @brief 加载选项模板类
 *
 * 用于配置资源加载的各种参数，包括键名、生成器、回调函数等。
 *
 * @tparam T 资源类型（可以是任意类型，无需继承特定接口）
 */
template<typename T>
struct LoadOptions {
    std::string key;  ///< 资源键名
    std::function<std::unique_ptr<T>()> generator;  ///< 资源生成器
    std::function<void(T*)> destroyFunc = nullptr;  ///< 资源销毁函数
    std::function<void(const std::string&, T*)> onZeroCallFunc = nullptr;  ///< 零引用时回调
    std::function<void(const std::string&, T*)> onGetRestoreCallFunc = nullptr;  ///< 获取时恢复回调

private:
    /**
     * @brief 私有构造函数，只能通过工厂函数创建
     */
    LoadOptions(const std::string& k,
                std::function<std::unique_ptr<T>()> gen,
                std::function<void(T*)> destroyFunc,
                std::function<void(const std::string&, T*)> onZero,
                std::function<void(const std::string&, T*)> onRestore)
        : key(k), generator(std::move(gen)), destroyFunc(std::move(destroyFunc)),
          onZeroCallFunc(std::move(onZero)), onGetRestoreCallFunc(std::move(onRestore)) {}

    // 工厂函数友元声明
    template<typename U>
    friend LoadOptions<U> FromConfig(const std::string& fileConfig,
        std::function<void(const std::string&, U*)> onZeroCallFunc,
        std::function<void(const std::string&, U*)> onGetRestoreCallFunc);

    template<typename U>
    friend LoadOptions<U> FromPointer(const std::string& key,
        std::unique_ptr<U> ptr,
        std::function<void(U*)> destroyFunc,
        std::function<void(const std::string&, U*)> onZeroCallFunc,
        std::function<void(const std::string&, U*)> onGetRestoreCallFunc);

    template<typename U>
    friend LoadOptions<U> FromGenerator(const std::string& key,
        std::function<std::unique_ptr<U>()> generator,
        std::function<void(U*)> destroyFunc,
        std::function<void(const std::string&, U*)> onZeroCallFunc,
        std::function<void(const std::string&, U*)> onGetRestoreCallFunc);

    template<typename U>
    friend LoadOptions<U> FromKey(const std::string& key);
};

/**
 * @brief 从配置文件创建加载选项
 *
 * @tparam T 资源类型
 * @param fileConfig 配置文件路径
 * @param loader 加载函数，负责从配置文件创建资源
 * @param onZeroCallFunc 零引用时回调函数（可选）
 * @param onGetRestoreCallFunc 获取时恢复回调函数（可选）
 * @return LoadOptions<T> 加载选项对象
 */
template<typename T>
LoadOptions<T> FromConfig(const std::string& fileConfig,
    std::function<std::unique_ptr<T>(const std::string&)> loader,
    std::function<void(const std::string&, T*)> onZeroCallFunc = nullptr,
    std::function<void(const std::string&, T*)> onGetRestoreCallFunc = nullptr);

/**
 * @brief 从已有指针创建加载选项
 *
 * @tparam T 资源类型
 * @param key 资源键名
 * @param ptr 资源指针（所有权转移）
 * @param destroyFunc 资源销毁函数（可选，为 nullptr 则使用 default delete）
 * @param onZeroCallFunc 零引用时回调函数（可选）
 * @param onGetRestoreCallFunc 获取时恢复回调函数（可选）
 * @return LoadOptions<T> 加载选项对象
 */
template<typename T>
LoadOptions<T> FromPointer(const std::string& key,
    std::unique_ptr<T> ptr,
    std::function<void(T*)> destroyFunc = nullptr,
    std::function<void(const std::string&, T*)> onZeroCallFunc = nullptr,
    std::function<void(const std::string&, T*)> onGetRestoreCallFunc = nullptr);

/**
 * @brief 从生成器创建加载选项
 *
 * @tparam T 资源类型
 * @param key 资源键名
 * @param generator 资源生成器函数
 * @param destroyFunc 资源销毁函数（可选，为 nullptr 则使用 default delete）
 * @param onZeroCallFunc 零引用时回调函数（可选）
 * @param onGetRestoreCallFunc 获取时恢复回调函数（可选）
 * @return LoadOptions<T> 加载选项对象
 */
template<typename T>
LoadOptions<T> FromGenerator(const std::string& key,
    std::function<std::unique_ptr<T>()> generator,
    std::function<void(T*)> destroyFunc = nullptr,
    std::function<void(const std::string&, T*)> onZeroCallFunc = nullptr,
    std::function<void(const std::string&, T*)> onGetRestoreCallFunc = nullptr);

/**
 * @brief 从键名创建加载选项（用于从已有资源恢复）
 *
 * @tparam T 资源类型
 * @param key 资源键名
 * @return LoadOptions<T> 加载选项对象
 */
template<typename T>
LoadOptions<T> FromKey(const std::string& key);

} // namespace Resource

// 模板实现放在 .inl 文件中
#include "resource_load_options.inl"
