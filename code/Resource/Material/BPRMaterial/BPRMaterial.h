#pragma once


#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "code/Resource/Material/material.h"
#include "code/ECS/Core/Resource/resource.h"


namespace Resource{
    class Texture;
    

    class BPRMaterial : public Material {

        public:
            virtual bool LoadArgs(
                const std::string& materialType,
                const Json::Value& textures,
                const Json::Value& properties,
                const Json::Value& shaders
            ) override;

            virtual void UploadPropertyToGPU() override;

            virtual void Release(){
                sizeof(Property);
            }

            inline auto& GetProperty(){return property; }

        private:

            size_t shaderIndex = -1;

            Resource::ResourceHandle<Texture> diffuseMap;
            Resource::ResourceHandle<Texture> normalMap;
            Resource::ResourceHandle<Texture> roughnessMap;
            Resource::ResourceHandle<Texture> aoMap;

            struct alignas(16) Property{
                glm::vec3 color;
                float roughness;
                float metallic;
                float ped1;
                float ped2;
                alignas(4) bool needNormalMap;
                alignas(4) bool needRoughnessMap;
                alignas(4) bool needMetallicMap;
                alignas(4) bool needAOMap;
            } property;



    };


}