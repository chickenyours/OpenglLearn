#pragma once
#include <string>
#include <functional>
#include <memory>

namespace ECS::Core::ResourceModule {


// OnZeroRefStrategyFunc 和 OnGetRestoreStrategyFunc 都是资源管理器中的类
template <typename T>
using OnZeroRefStrategyFunc = std::function<void(const std::string&, T*)>;

template <typename T>
using OnGetRestoreStrategyFunc = std::function<void(const std::string&, T*)>;

template<typename T>
LoadOptions<T> FromConfig(const std::string& fileConfig,
    OnZeroRefStrategyFunc<T> onZeroCallFunc = nullptr,
    OnGetRestoreStrategyFunc<T> onGetRestoreCallFunc = nullptr
);

template<typename T>
LoadOptions<T> FromPointer(const std::string& key, 
    std::unique_ptr<T> ptr,
    OnZeroRefStrategyFunc<T> onZeroCallFunc = nullptr,
    OnGetRestoreStrategyFunc<T> onGetRestoreCallFunc = nullptr
);

template<typename T>
LoadOptions<T> FromGenerator(const std::string& key, 
    std::function<std::unique_ptr<T>()>,
    OnZeroRefStrategyFunc<T> onZeroCallFunc = nullptr,
    OnGetRestoreStrategyFunc<T> onGetRestoreCallFunc = nullptr);

template <typename T>
struct LoadOptions {
    std::string key;
    std::function<std::unique_ptr<T>()> generator;          // 如果加载器失败需要返回空指针,资源管理器会识别这个结果
    OnZeroRefStrategyFunc<T> onZeroCallFunc = nullptr;
    OnGetRestoreStrategyFunc<T> onGetRestoreCallFunc = nullptr;

private:
    LoadOptions(const std::string& k,
            std::function<std::unique_ptr<T>()> gen,
            OnZeroRefStrategyFunc<T> onZero,
            OnGetRestoreStrategyFunc<T> onRestore)
    : key(k), generator(std::move(gen)),
      onZeroCallFunc(onZero), onGetRestoreCallFunc(onRestore) {}
    template<typename U>
    friend LoadOptions<U> FromConfig(const std::string& fileConfig,
        OnZeroRefStrategyFunc<U> onZeroCallFunc,
        OnGetRestoreStrategyFunc<U> onGetRestoreCallFunc
    );// 👈 只有合法工厂函数能构造

    template<typename U>
    friend LoadOptions<U> FromPointer(const std::string& key, 
        std::unique_ptr<U> ptr,
        OnZeroRefStrategyFunc<U> onZeroCallFunc,
        OnGetRestoreStrategyFunc<U> onGetRestoreCallFunc
    );


    template<typename U>
    friend LoadOptions<U> FromGenerator(const std::string& key, 
        std::function<std::unique_ptr<U>()>,
        OnZeroRefStrategyFunc<U> onZeroCallFunc,
        OnGetRestoreStrategyFunc<U> onGetRestoreCallFunc
    );

};

}

// tpp
#include "code/ECS/Core/Resource/resource_interface.h" // for ILoadFromConfig
#include "code/ECS/Core/Resource/resource_handle.h"

using namespace ECS::Core::ResourceModule;

template<typename T>
ECS::Core::ResourceModule::LoadOptions<T> ECS::Core::ResourceModule::FromConfig(
    const std::string& fileConfig,
    OnZeroRefStrategyFunc<T> onZeroCallFunc,
    OnGetRestoreStrategyFunc<T> onGetRestoreCallFunc
) {
    static_assert(std::is_base_of_v<Resource::ILoadFromConfig, T>,
                  "FromConfig<T> requires T to implement ILoadFromConfig");

    return LoadOptions<T>(
        fileConfig,
        [fileConfig]() -> std::unique_ptr<T> {
            auto ptr = new T();
            if (!ptr->LoadFromConfigFile(fileConfig)) {
                LOG_ERROR_TEMPLATE("Resource LoadOption", "fail to load configFile,in " + fileConfig);
                return nullptr;
            }
            return std::unique_ptr<T>(ptr);
        },
        onZeroCallFunc,
        onGetRestoreCallFunc
    );
}

template<typename T>
ECS::Core::ResourceModule::LoadOptions<T> ECS::Core::ResourceModule::FromPointer(
    const std::string& key,
    std::unique_ptr<T> ptr,
    OnZeroRefStrategyFunc<T> onZeroCallFunc,
    OnGetRestoreStrategyFunc<T> onGetRestoreCallFunc
) {
    return LoadOptions<T>(
        key,
        [ptr = std::move(ptr), key]() mutable {
            if (!ptr) {
                LOG_ERROR_TEMPLATE("Resource LoadOption", "Pointer is null for key: " + key);
                return nullptr;
            }
            return std::move(ptr);
        },
        onZeroCallFunc,
        onGetRestoreCallFunc
    );
}

template<typename T>
ECS::Core::ResourceModule::LoadOptions<T> ECS::Core::ResourceModule::FromGenerator(
    const std::string& key,
    std::function<std::unique_ptr<T>()> generator,
    OnZeroRefStrategyFunc<T> onZeroCallFunc,
    OnGetRestoreStrategyFunc<T> onGetRestoreCallFunc
) {
    return LoadOptions<T>(
        key,
        std::move(generator),
        onZeroCallFunc,
        onGetRestoreCallFunc
    );
}