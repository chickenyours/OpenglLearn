#pragma once

#include <unordered_map>
#include <string>
#include <memory>
#include <glad/glad.h>
#include <typeindex>
#include <any>


#include "code/ECS/Core/Resource/resource_interface.h"
#include "code/Resource/Material/material_interface.h"


namespace Resource {

class Material: public ILoadFromConfig{
    private:
        std::unordered_map<std::type_index, std::unique_ptr<IMaterial>> features;
    public:
        virtual bool LoadFromConfigFile(const std::string& configFile, Log::StackLogErrorHandle errHandle) override;
        virtual void Release() override;

        template<typename T>
        T* TryGetFeature() const {
            auto it = features.find(std::type_index(typeid(T)));
            if (it != features.end()) {
                return static_cast<T*>(it->second.get());
            }
            return nullptr;
        }
        
};


} // namespace Resource

