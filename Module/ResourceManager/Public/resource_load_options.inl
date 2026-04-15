#pragma once

#include "resource_load_options.h"

namespace Resource {

template<typename T>
LoadOptions<T> FromConfig(const std::string& fileConfig,
    std::function<std::unique_ptr<T>(const std::string&)> loader,
    std::function<void(const std::string&, T*)> onZeroCallFunc,
    std::function<void(const std::string&, T*)> onGetRestoreCallFunc) {

    return LoadOptions<T>(
        fileConfig,
        [loader, fileConfig]() -> std::unique_ptr<T> {
            return loader(fileConfig);
        },
        nullptr,  // 默认使用 default delete
        std::move(onZeroCallFunc),
        std::move(onGetRestoreCallFunc)
    );
}

template<typename T>
LoadOptions<T> FromPointer(const std::string& key,
    std::unique_ptr<T> ptr,
    std::function<void(T*)> destroyFunc,
    std::function<void(const std::string&, T*)> onZeroCallFunc,
    std::function<void(const std::string&, T*)> onGetRestoreCallFunc) {

    return LoadOptions<T>(
        key,
        [ptr = std::move(ptr), key]() mutable {
            if (!ptr) {
                return std::unique_ptr<T>(nullptr);
            }
            return std::move(ptr);
        },
        std::move(destroyFunc),
        std::move(onZeroCallFunc),
        std::move(onGetRestoreCallFunc)
    );
}

template<typename T>
LoadOptions<T> FromGenerator(const std::string& key,
    std::function<std::unique_ptr<T>()> generator,
    std::function<void(T*)> destroyFunc,
    std::function<void(const std::string&, T*)> onZeroCallFunc,
    std::function<void(const std::string&, T*)> onGetRestoreCallFunc) {

    return LoadOptions<T>(
        key,
        std::move(generator),
        std::move(destroyFunc),
        std::move(onZeroCallFunc),
        std::move(onGetRestoreCallFunc)
    );
}

template<typename T>
LoadOptions<T> FromKey(const std::string& key) {
    return LoadOptions<T>(
        key,
        nullptr,
        nullptr,
        nullptr,
        nullptr
    );
}

} // namespace Resource
