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

// 目前是超出释放,可能之后改为只释放code缓存,保留工厂,因为工厂会持有shader产品缓存
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

ResourceHandle<ShaderFactory> ShaderManager::GetShaderFactoryFromShaderFile(const std::string& shaderFile, Log::StackLogErrorHandle errHandle){
    return ECS::Core::ResourceModule::ResourceManager::GetInctance().Get<ShaderFactory>(
        ECS::Core::ResourceModule::FromGenerator<ShaderFactory>(
            "ShaderFactory: " + shaderFile,
            [shaderFile](Log::StackLogErrorHandle errHandle) -> std::unique_ptr<ShaderFactory> {
                auto shaderFactory = std::make_unique<ShaderFactory>();
                shaderFactory->SetCodeFilePath(shaderFile);
                return shaderFactory;
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

            
ResourceHandle<Shader> ShaderManager::GetShaderFromShaderFile(const std::string& shaderFile, const ShaderDescription& description, Log::StackLogErrorHandle errHandle){
    auto shaderFactory = GetShaderFactoryFromShaderFile(shaderFile);
    if(!shaderFactory){
        REPORT_STACK_ERROR(errHandle, "ShaderManager", "Failed to get ShaderFactory for shader file: " + shaderFile);
        return nullptr;
    }
    auto shader = shaderFactory->TryGetShaderInstance(description, errHandle);  
    if(!shader){
        REPORT_STACK_ERROR(errHandle, "ShaderManager", "Failed to create Shader instance for shader file: " + shaderFile);
        return nullptr;
    }
    return shader;
}