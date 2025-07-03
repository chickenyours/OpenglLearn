#pragma once

#include <vector>
#include <glad/glad.h>
#include "code/ECS/Core/Resource/resource_handle.h"
#include "code/ECS/Core/Resource/resource_interface.h"
#include "code/Resource/Model/mesh.h"

// #define MAX_BONE_INFLUENCE 4


namespace Resource {
    class Model : public ILoadFromConfig{
        public:
            virtual bool LoadFromConfigFile(const std::string& configFile, Log::StackLogErrorHandle errHandle) override;
            virtual void Release() override;
            std::string GetInfo();
        private:
            std::vector<Mesh> meshes_;
            static std::vector<Mesh> LoadMeshes(const aiScene& scene);
            
    };
}