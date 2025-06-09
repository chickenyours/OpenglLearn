#pragma once

#include <memory>
#include <string>
#include <functional>

#include "code/DebugTool/ConsoleHelp/color_log.h"

namespace Resource{
    template <typename T>
    class ResourceHandle;

    template<typename T>
    struct LoadOptions;
    
}

namespace ECS::Core::ResourceModule{
    
    template <typename T>
    class LoadOptions;
    

    template <typename T>
    class ResourcePool;

    class ResourceManager{
        public:
            static ResourceManager& GetInctance(){
                static ResourceManager instance;
                return instance;
            }

            template <typename T>
            Resource::ResourceHandle<T> Get(const LoadOptions<T>& options, Log::StackLogErrorHandle errHandle = nullptr);

            template <typename T>
            void OnHandleRelease(const std::string& fileConfig);

            template <typename T>
            void OnZeroRefRelease(const std::string& key);

            template <typename T>
            void TryReleaseAll();

        private:

            template <typename T>
            ResourcePool<T>& GetPool();
    };
}

#include "code/ECS/Core/Resource/resource_interface.h"
#include "code/ECS/Core/Resource/resource_pool.h"
#include "code/ECS/Core/Resource/resource_handle.h"
#include "code/ECS/Core/Resource/resource_load_option.h"

using namespace ECS::Core::ResourceModule;

template <typename T>
Resource::ResourceHandle<T> ResourceManager::Get(const LoadOptions<T>& options, Log::StackLogErrorHandle errHandle) {
    auto handle = GetPool<T>().Get(options, errHandle); 
    if(!handle){
        REPORT_STACK_ERROR(errHandle, "ResourceManager", "Failed to load resource");
    }
    return handle;
}

template <typename T>
void ResourceManager::OnHandleRelease(const std::string& key){
    GetPool<T>().OnHandleRelease(key);
}

template <typename T>
void ResourceManager::OnZeroRefRelease(const std::string& key){
    GetPool<T>().OnZeroRefRelease(key);
}

template <typename T>
void ResourceManager::TryReleaseAll(){
    GetPool<T>().OnHandleReleaseAll();
}

template <typename T>
ResourcePool<T>& ResourceManager::GetPool(){
    static ResourcePool<T> pool;
    return pool;
}
