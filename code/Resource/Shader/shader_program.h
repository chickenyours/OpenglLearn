#pragma once



#include <glad/glad.h>
#include <string>
#include <vector>
#include "code/ECS/Core/Resource/resource_interface.h"

namespace Resource{

    enum class ShaderProgramType{
        VERTEX_FRAGMENT,
        VERTEX_GEOMETRY_FRAGMENT,
        COMPUTE,
        UNKNOWN,
    };

    class ShaderProgram : public ILoadFromConfig{
        public:
            bool LoadFromConfigFile(const std::string& configFile, Log::StackLogErrorHandle errHandle) override;
            virtual void Release() override;
            GLuint GetID(){return id_;}
            inline ShaderProgramType GetShaderType() const { return shaderType_; }
        private:
            GLuint id_ = 0;
            ShaderProgramType shaderType_;
    };
}