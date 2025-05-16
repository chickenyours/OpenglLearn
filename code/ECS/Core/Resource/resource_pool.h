#pragma once

#include <unordered_map>
#include <memory>
#include <string>
#include <json/json.h>

#include "code/ECS/Core/Resource/resource.h"

#include "code/DebugTool/ConsoleHelp/color_log.h"


namespace ECS::Core::ResourceSystem {

template <typename T>
class ResourcePool {
    public:

        // 用于预添加
        void Load(const std::string& configfile){
            static_assert(std::is_base_of_v<Resource::AbstractResource, T>,
                "T must inherit from AbstractResource");
            auto& entry = pool_[configfile];
            if(!entry){
                // 如果是空指针,则加载
                entry.resource = std::make_unique<T>();
                entry.resource->Load(configfile);
                LOG_INFO("RESOURCE POOL", "已创建配置文件相关资源: " + configfile);
            }
        }

        Resource::ResourceHandle<T> Get(const std::string& configfile){
            static_assert(std::is_base_of_v<Resource::AbstractResource, T>,
                "T must inherit from AbstractResource");
            // 如果pool_无configfile键,则会自动生成这一对键值元素,值会采用默认初始化,std::unique_ptr会初始化为空指针
            auto& entry = pool_[configfile];
            if(!entry.resource){
                // 如果是空指针,则加载
                entry.resource = std::make_unique<T>();
                entry.resource->Load(configfile);
                LOG_INFO("RESOURCE POOL", "已创建配置文件相关资源: " + configfile);
            }

            entry.refCount++;

            // 构造资源句柄对象并返回
            return Resource::ResourceHandle<T>(
                configfile,
                static_cast<T*>(entry.resource.get()),
                [this](const std::string& configfile){ this->OnHandleRelease(configfile); }
            );

        }

        void OnHandleRelease(const std::string& configfile){
            auto it = pool_.find(configfile);
            if(it != pool_.end()){
                it->second.refCount--;
                if(it->second.refCount == 0){
                    // 释放资源
                    it->second.resource->Release();
                    pool_.erase(configfile);
                    LOG_INFO("RESOURCE POOL", "已释放配置文件相关资源: " + configfile);
                }
            }
        }

        void OnHandleReleaseAll(){
            for (auto it = pool_.begin(); it != pool_.end(); ) {
                if (it->second.refCount == 0 && it->second.resource) {
                    it->second.resource->Release();
                    LOG_INFO("RESOURCE POOL", "已释放配置文件相关资源: " + it->first);
                    it = pool_.erase(it);
                } else {
                    ++it;
                }
            }
        }

        ~ResourcePool(){
            for (auto& [configfile, entry] : pool_) {
                if (entry.resource) {
                    if(entry.refCount == 0){
                        LOG_ERROR("RESOURCE POOL", "资源引用计数为0,但仍存在未释放的资源: " + configfile);
                    }
                    entry.resource->Release();
                    LOG_INFO("RESOURCE POOL", "已释放配置文件相关资源: " + configfile);
                }
            }
            pool_.clear();
        }

    private:

        struct ResourceEntry {
            std::unique_ptr<Resource::AbstractResource> resource;
            size_t refCount = 0;
        };

        std::unordered_map<std::string, ResourceEntry> pool_;

        


        
        
};
} // namespace ECS::Core

