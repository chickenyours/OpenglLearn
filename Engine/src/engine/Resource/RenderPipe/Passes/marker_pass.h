#pragma once

#include "engine/Resource/RenderPipe/pass.h"

#include "engine/ECS/Core/Resource/resource_handle.h"
#include "engine/ECS/Core/Resource/resource_load_option.h"
#include "engine/ECS/Core/Resource/resource_manager.h"

#include "engine/Resource/Shader/shader_program_factory.h"

namespace Render
{



    struct VAOWrapper {
        GLuint vao = 0;
        GLuint vbo = 0;

        VAOWrapper() {
            float data[] = {0.0,0.0,0.5,
                            0.5,0.0,0.0,
                            0.0,0.5,0.0};
            glGenVertexArrays(1,&vao);
            glGenBuffers(1,&vbo);

            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER,vbo);
            glBufferData(GL_ARRAY_BUFFER,sizeof(data),data,GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,3*sizeof(float),(void*)0);
            glBindVertexArray(0);
        }

        ~VAOWrapper() {
            glDeleteVertexArrays(1,&vao);
            glDeleteBuffers(1,&vbo);
        }
    };

    inline GLuint GetSimpleAxisVAO() {
        static VAOWrapper axis;
        return axis.vao;
    }

    class MarkerPass : public Pass{

        ResourceHandle<ShaderProgram> shader;
        public:
            virtual bool Init(const PassContex& cfg){
                shader = std::move(
                    ECS::Core::ResourceModule::ResourceManager::GetInctance().Get<Resource::ShaderProgramFactory>(
                        FromConfig<Resource::ShaderProgramFactory>("./RenderShader/axis.json"))->GetShaderProgramInstance()
                );
                return shader.get() != nullptr;
            }
            virtual void SetConfig(const PassContex& cfg){

            }
            virtual void Update(){
                // 绘制坐标系
                glDisable(GL_DEPTH_TEST);
                glBindVertexArray(GetSimpleAxisVAO());
                glUseProgram(shader->GetID());
                glDrawArrays(GL_POINTS,0,3);
                glEnable(GL_DEPTH_TEST);

            }
            virtual void ClearCache(){

            }
    };
}