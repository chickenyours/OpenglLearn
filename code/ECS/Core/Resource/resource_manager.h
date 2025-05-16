#pragma once

#include <memory>

#include "code/ECS/Core/Resource/resource.h"
#include "code/ECS/Core/Resource/resource_pool.h"

namespace ECS::Core::ResourceSystem{
    class ResourceManager{
        public:
            static ResourceManager& GetInctance(){
                static ResourceManager instance;
                return instance;
            }

            template <typename T>
            void Load(const std::string& fileConfig){
                GetPool<T>().Load(fileConfig);
            }

            template <typename T>
            Resource::ResourceHandle<T> Get(const std::string& fileConfig){
                return GetPool<T>().Get(fileConfig);
            }

            template <typename T>
            void TryRelease(const std::string& fileConfig){
                GetPool<T>().OnHandleRelease(fileConfig);
            }

            template <typename T>
            void TryReleaseAll(){
                GetPool<T>().OnHandleReleaseAll();
            }

        private:
            // std::unordered_map<std::type_index, std::unique_ptr<void>> pools_;

            template <typename T>
            ResourcePool<T>& GetPool(){
                static ResourcePool<T> pool;
                return pool;
            }
    };
}