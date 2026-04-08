#include "resource_manager_module.h"
#include <iostream>

namespace Resource {

// ResourceManagerModule 单例实现
ResourceManagerModule& ResourceManagerModule::GetInstance() {
    static ResourceManagerModule instance;
    return instance;
}

bool ResourceManagerModule::Startup() {
    if (started_) {
        return true;
    }
    started_ = true;
    std::cout << "ResourceManager started" << std::endl;
    return true;
}

void ResourceManagerModule::Shutdown() {
    if (!started_) {
        return;
    }
    std::cout << "ResourceManager shutting down" << std::endl;
    started_ = false;
}

} // namespace Resource
