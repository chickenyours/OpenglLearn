#pragma once

#include "code/Resource/RenderPipe/pass.h"

namespace Render{
    class EnvironmentPass : public Pass{
        public:
            virtual bool Init(const PassContex& cfg) override{
                glGenBuffers(1,&UBOEnvironment_);
                if(CHECK_GL_ERROR("glGenBuffers")) return false;
                glBindBuffer(GL_UNIFORM_BUFFER, UBOEnvironment_);
                if(CHECK_GL_ERROR("glBindBuffer")) return false;
                glBufferData(GL_UNIFORM_BUFFER, sizeof(EnvironmentDataUBOLayout), nullptr, GL_DYNAMIC_DRAW);
                if(CHECK_GL_ERROR("glBufferData")) return false;
                glBindBufferBase(GL_UNIFORM_BUFFER, UBO_BINDING_ENVIRONMENT, UBOEnvironment_);
                if(CHECK_GL_ERROR("glBindBufferBase")) return false;
                glBindBuffer(GL_UNIFORM_BUFFER, 0);
                return true;
            }
            virtual void SetConfig(const PassContex& cfg) override{
                
            }
            virtual void Update() override {
                if (UBOEnvironment_ == 0) {
                    LOG_ERROR("EnvironmentPass", "UBO not initialized before Update.");
                    return;
                }
                glBindBuffer(GL_UNIFORM_BUFFER, UBOEnvironment_);
                // glBindBufferBase(GL_UNIFORM_BUFFER, UBO_BINDING_ENVIRONMENT, UBOEnvironment_); // 存在一个
                glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(UBOdata_), &UBOdata_);
            }
            // void AddItem(...) // input
            // output
            virtual void ClearCache() override {

            }
            EnvironmentDataUBOLayout UBOdata_;
            ~EnvironmentPass(){
                if(UBOEnvironment_){
                    glDeleteBuffers(1, &UBOEnvironment_);
                    UBOEnvironment_ = 0;  // 防止悬空指针
                }
            }
            private:
                GLuint UBOEnvironment_ = 0;
    };
}