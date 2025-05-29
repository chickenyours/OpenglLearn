#pragma once

#include "code/ECS/Core/Resource/resource.h"

namespace Resource{
    class ShaderProgram : public ILoadFromConfig{
        bool LoadFromConfigFile(const std::string& configFile) override;
        virtual void Release() override;
    };
}