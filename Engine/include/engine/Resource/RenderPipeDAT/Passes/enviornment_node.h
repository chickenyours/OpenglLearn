#pragma once

#include "code/Resource/RenderPipeDAT/pass.h"
#include "code/Resource/RenderPipeDAT/UniformBindings.h"

namespace Render{
    struct EnvironmentPassInput{
        
    };
    struct EnvironmentPassOutput{
        
    };
    struct EnvironmentPassDepend{
        
    };

  
    class EnvironmentPass : public Pass<EnvironmentPassInput,EnvironmentPassOutput,EnvironmentPassDepend>{
        private:
            GLuint UBOEnvironment_ = 0;
        protected:
            virtual int Init(const NodeContext& cxt){
                glGenBuffers(1,&UBOEnvironment_);
                glBindBuffer(GL_UNIFORM_BUFFER, UBOEnvironment_);
                glBufferData(GL_UNIFORM_BUFFER, sizeof(EnvironmentDataUBOLayout), nullptr, GL_DYNAMIC_DRAW);
                glBindBufferBase(GL_UNIFORM_BUFFER, UBO_BINDING_ENVIRONMENT, UBOEnvironment_);
                glBindBuffer(GL_UNIFORM_BUFFER, 0);
                return 0;
            }
            virtual int Set(const NodeContext& cxt){
                return 0;
            }
            virtual int Update(){
                return 0;
            }
            ~EnvironmentPass(){
                if(UBOEnvironment_){
                    glDeleteBuffers(1, &UBOEnvironment_);
                    UBOEnvironment_ = 0; 
                }
            }
    };
}