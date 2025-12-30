#pragma once

#include <glad/glad.h>
#include "engine/ECS/Core/Resource/resource_interface.h"



namespace Resource{

class ShaderProgram : public ILoadable{
    public:
        virtual void Release() override;
        // return ShaderProgram Gluint id
        GLuint GetID(){return id_;}
    private:
        GLuint id_ = 0;
        friend class ShaderProgramFactory;
};

} // namespace Resource