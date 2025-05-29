#include "shader_manager.h"

#include <list>
#include <unordered_map>

#include "code/Resource/Shader/shader.h"
#include "code/ECS/Core/Resource/resource_manager.h"
#include "code/Resource/Shader/shader_factory.h"

using namespace Resource;

namespace {
    constexpr size_t MAX_SHADER_CACHE_SIZE = 16;
    std::list<std::string> lruList;
    std::unordered_map<std::string, std::list<std::string>::iterator> lruMap;
}

static void AddToLRUCache(const std::string& key, ShaderFactory* factory) {
    if (lruMap.count(key)) return;
    if (lruList.size() >= MAX_SHADER_CACHE_SIZE) {
        const std::string& oldKey = lruList.back();
        lruMap.erase(oldKey);
        ECS::Core::ResourceModule::ResourceManager::GetInctance().OnZeroRefRelease<ShaderFactory>(oldKey);
        LOG_INFO("Resource ShaderManager", "LRU Cache Evicted shader factory: " + oldKey);
        lruList.pop_back();
    }
    lruList.push_front(key);
    lruMap[key] = lruList.begin();
}

static void RemoveFromLRUCache(const std::string& key) {
    auto it = lruMap.find(key);
    if (it != lruMap.end()) {
        lruList.erase(it->second);
        lruMap.erase(it);
    }
}

ShaderManager& ShaderManager::GetInstance(){
    static ShaderManager instance;
    return instance;
} 

ResourceHandle<ShaderFactory> ShaderManager::GetShaderFactoryFromConfigFile(const std::string& configFile){
    return ECS::Core::ResourceModule::ResourceManager::GetInctance().Get<ShaderFactory>(
        ECS::Core::ResourceModule::FromConfig<ShaderFactory>(
            configFile,
            [configFile](const std::string& key, ShaderFactory* resource) {
                AddToLRUCache(key, resource);
                LOG_INFO("Resource ShaderManager", "trigger Zero Function, key: " + key + ", file: " + configFile);
            },
            [configFile](const std::string& key, ShaderFactory* resource) {
                RemoveFromLRUCache(key);
                LOG_INFO("Resource ShaderManager", "trigger Restore Function, key: " + key + ", file: " + configFile);
            }
        )
    );
}

ResourceHandle<ShaderFactory> ShaderManager::GetShaderFactoryFromSahderFile(unsigned int shaderType, const std::string& shaderFile){
    return ECS::Core::ResourceModule::ResourceManager::GetInctance().Get<ShaderFactory>(
        ECS::Core::ResourceModule::FromGenerator<ShaderFactory>(
            shaderFile + "::FromGenerator",
            [shaderType, shaderFile]() -> std::unique_ptr<ShaderFactory> {
                auto factory = std::make_unique<ShaderFactory>();
                if(!factory->LoadShaderCodeFromFile(shaderType, shaderFile)){
                    LOG_ERROR("Resource ShaderManager", "Can't load successfully ShaderFactory, source: " + shaderFile);
                    return nullptr;
                }
                return factory;
            },
            [shaderFile](const std::string& key, ShaderFactory* resource) {
                AddToLRUCache(key, resource);
                LOG_INFO("Resource ShaderManager", "trigger Zero Function, key: " + key + ", file: " + shaderFile);
            },
            [shaderFile](const std::string& key, ShaderFactory* resource) {
                RemoveFromLRUCache(key);
                LOG_INFO("Resource ShaderManager", "trigger Restore Function, key: " + key + ", file: " + shaderFile);
            }
        )
    );
} 

            
bool ShaderManager::GetShader(unsigned int shaderType, const std::string& shaderFile, Shader& out){
    auto factory = GetShaderFactoryFromSahderFile(shaderType, shaderFile);
    if(!factory) return false;
    std::string ErrMsg;
    if(!factory->GenerateShader(ErrMsg, out)){
        LOG_ERROR("Resource ShaderManager", "Failed to generate shader: " + ErrMsg);
        return false;
    }
    return true;
}