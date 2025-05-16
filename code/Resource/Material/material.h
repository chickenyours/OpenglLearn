// 使用UBO上传技术
#pragma once

#include <memory>
#include <json/json.h>
#include <string>
#include <glad/glad.h>
#include "code/ECS/Core/Resource/resource.h"

namespace Resource {

class MaterialUBO;

class Material : public AbstractResource {
    public:
        
        bool LoadFromConfigFile(const std::string& configFile) override;

        /*
        你需要实现相应配套的UBO的定义的构造
        会接收到一整个material json对象
        "material": {
            "materialType": "BPR or some type",
            "args": {
                "textures" : {
                    "diffuseMap": "./materials/tite/diffuse.json",
                    ...
                },
                "properties" : { 
                    "color" : [1.0, 1.0, 1.0],
                    "roughness" : 0.5,
                    "needNormalMap" : true,
                    "needShadow" : false,
                    ...
                    },
                "shaders" : { ... }
            }
        */ 
        virtual bool LoadArgs(
            const std::string& materialType,
            const Json::Value& textures,
            const Json::Value& properties,
            const Json::Value& shaders
        ) = 0;

        

        virtual void UploadPropertyToGPU() = 0;


        virtual void Release() = 0;


    protected:
        std::string name_;
};

} // namespace Resource

