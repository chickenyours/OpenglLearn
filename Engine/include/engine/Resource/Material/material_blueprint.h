#pragma once

#include <string>

#include "engine/ECS/Core/Resource/resource_handle.h"
#include "engine/Resource/Shader/shader_program.h"

namespace Json{
    class Value;
}

namespace Resource {

    class Material;

    template<typename T>
    class MaterialBlueprint {
    public:
        virtual ~MaterialBlueprint() = default;
        
        // Virtual methods that implementations might need to override
        virtual bool LoadMaterialFromConfigFile(
            Material& material,
            const Json::Value* textures,
            const Json::Value* properties,
            const Json::Value* shaders
        ) = 0;
    };

}