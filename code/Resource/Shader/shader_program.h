#pragma once

#include <glad/glad.h>
#include "code/ECS/Core/Resource/resource_interface.h"



namespace Resource{

class ShaderProgram : public ILoadable{
    public:
        virtual void Release() override;
        GLuint GetID(){return id_;}
    private:
        GLuint id_ = 0;
        friend class ShaderProgramFactory;
};

} // namespace Resource