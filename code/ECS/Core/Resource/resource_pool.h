#pragma once

#include <unordered_map>
#include <memory>
#include <string>
#include <json/json.h>
#include <functional>

#include "code/DebugTool/ConsoleHelp/color_log.h"

namespace Resource{

    template <typename T>
    class ResourceHandle;

    class ILoadFromConfig;
}


namespace ECS::Core::ResourceModule {
    
template <typename T>
struct LoadOptions;

template <typename T>
class ResourcePool {
    public:
        Resource::ResourceHandle<T> Get(const LoadOptions<T>& options, Log::StackLogErrorHandle errHandle = nullptr);
        void OnHandleReleaseAll();
        void OnZeroRefRelease(const std::string& key);
        void OnHandleRelease(const std::string& configfile);
        ~ResourcePool(); 
    private:
        struct ResourceEntry;
        std::unordered_map<std::string, ResourceEntry> pool_;
    };
} // namespace ECS::Core

#include "code/ECS/Core/Resource/resource_interface.h"
#include "code/ECS/Core/Resource/resource_handle.h"
#include "code/ECS/Core/Resource/resource_load_option.h"

using namespace ECS::Core::ResourceModule;
using namespace Resource;

template <typename T>
struct ResourcePool<T>::ResourceEntry {
    std::unique_ptr<Resource::ILoadable> resource = nullptr;
    size_t refCount = 0;                                //引用计数器
    // 策略回调
    OnZeroRefStrategyFunc<T> onZeroCallFunc = nullptr;               //Get回调(如果包含资源)
    OnGetRestoreStrategyFunc<T> onGetRestoreCallFunc = nullptr;      //计时器结束回调
};

template <typename T>
ResourceHandle<T> ResourcePool<T>::Get(const LoadOptions<T>& options, Log::StackLogErrorHandle errHandle){
    
    auto& entry = pool_[options.key];

    if(!entry.resource){
        if (options.generator){
            entry.resource = options.generator(errHandle);
            if(!entry.resource){
                REPORT_STACK_ERROR(errHandle, "ResourcePool", "Failed to create resource: generator returned null.");
                return nullptr;
            }
            entry.onZeroCallFunc = options.onZeroCallFunc;
            entry.onGetRestoreCallFunc = options.onGetRestoreCallFunc;
            LOG_INFO_TEMPLATE("RESOURCE POOL", std::string("Create resource: [key = ") + LOG_COLOR_BRIGHT_GREEN + options.key + LOG_INFO_COLOR +" ]");
        }
        else{
            REPORT_STACK_ERROR(errHandle, "Resource Pool", "Failed to create resource: generator is null.");
            return nullptr;
        }
    }
    else { // 如果有资源,则执行相应策略并返回句柄
        if(entry.onGetRestoreCallFunc){
            entry.onGetRestoreCallFunc(options.key, static_cast<T*>(entry.resource.get()));
        }
    }

    entry.refCount++;
    
    return Resource::ResourceHandle<T>(
        options.key,
        static_cast<T*>(entry.resource.get()),
        [this](const std::string& key) {
            this->OnHandleRelease(key);
        }
    );
}
        
template <typename T>
void ResourcePool<T>::OnHandleReleaseAll(){
    for (auto it = pool_.begin(); it != pool_.end(); ) {
        if (it->second.refCount == 0 && it->second.resource) {
            it->second.resource->Release();
            LOG_INFO_TEMPLATE("RESOURCE POOL", std::string("已释放配置文件相关资源: ") + LOG_COLOR_BRIGHT_MAGENTA + it->first + LOG_INFO_COLOR);
            it = pool_.erase(it);
        } else {
            ++it;
        }
    }
}

template <typename T>
void ResourcePool<T>::OnZeroRefRelease(const std::string& key){
    auto it = pool_.find(key);
    if(it != pool_.end()){
        if(it->second.refCount == 0 && it->second.resource){
            it->second.resource->Release();
            LOG_INFO_TEMPLATE("RESOURCE POOL", std::string("已释放配置文件相关资源: ") + LOG_COLOR_BRIGHT_MAGENTA + it->first + LOG_INFO_COLOR);
            it = pool_.erase(it);
        }
    }
    else{
        LOG_WARNING_TEMPLATE("RESOURCE POOL", std::string("已释放配置文件相关资源: ") + LOG_COLOR_BRIGHT_MAGENTA + it->first + LOG_INFO_COLOR);
    }
}

template <typename T>
ResourcePool<T>::~ResourcePool(){
    for (auto& [configfile, entry] : pool_) {
        if (entry.resource) {
            entry.resource->Release();
            if(entry.refCount != 0){
                LOG_WARNING_TEMPLATE("RESOURCE POOL", std::string("资源引用计数不为0,但仍存在未释放的资源: ") + LOG_COLOR_BRIGHT_MAGENTA + configfile + LOG_INFO_COLOR);
            }
            else if(entry.refCount == 0){
                LOG_INFO_TEMPLATE("RESOURCE POOL", std::string("资源引用计数为0,但仍存在未释放的资源: ") + LOG_COLOR_BRIGHT_MAGENTA + configfile + LOG_INFO_COLOR);
            }
            else{
                LOG_INFO_TEMPLATE("RESOURCE POOL", std::string("已释放配置文件相关资源: ") + LOG_COLOR_BRIGHT_MAGENTA + configfile + LOG_INFO_COLOR);
            }
        }
    }
    pool_.clear();
}


template <typename T>
void ResourcePool<T>::OnHandleRelease(const std::string& key){
    auto it = pool_.find(key);
    if(it != pool_.end()){
        if(!it->second.resource){
            LOG_ERROR_TEMPLATE("RESOURCE POOL", std::string("资源不存在: ") + LOG_COLOR_BRIGHT_MAGENTA + key + LOG_INFO_COLOR);
            return;
        }
        it->second.refCount--;
        if(it->second.refCount == 0){
            if (it->second.onZeroCallFunc) {
                it->second.onZeroCallFunc(key, static_cast<T*>(it->second.resource.get())); // 外部接管资源
                LOG_INFO_TEMPLATE("RESOURCE POOL", std::string("资源已被外部策略执行移除操作: ") + LOG_COLOR_BRIGHT_MAGENTA + key + LOG_INFO_COLOR);
            } else {
                // 默认行为：释放并删除
                it->second.resource->Release();
                pool_.erase(it);
                LOG_INFO_TEMPLATE("RESOURCE POOL", std::string("资源已被默认策略移除: ") + LOG_COLOR_BRIGHT_MAGENTA + key + LOG_INFO_COLOR);
            }
        }
    }
    else{
        if(!key.empty()){
            LOG_WARNING_TEMPLATE("RESOURCE POOL", std::string("尝试释放不存在的资源: ") + LOG_COLOR_BRIGHT_MAGENTA + key + LOG_INFO_COLOR);
        }
    }
}


