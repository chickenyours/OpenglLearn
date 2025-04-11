#include "ScenePass.h"

#include "code/RenderPipe/RenderContext/PassRenderContext.h"
#include "code/RenderPipe/RenderContext/PassConfig.h"
#include "code/RenderPipe/RenderContext/renderItem.h"
#include "code/Model/mesh.h"
#include "code/Camera/camera.h"
#include "code/shader.h"

using namespace Render;

ScenePass::ScenePass(){
    std::cout<<"创建ScenePass(未初始化)"<<std::endl;
}

void ScenePass::Init(const PassConfig& cfg){
    targetBufferWidth_ = cfg.targetBufferWidth;
    targetBufferHeight_ = cfg.targetBufferHeight;
    GLuint FBO, colorBufferTexture, depthBuffer;
    glGenFramebuffers(1,&FBO);
    glGenTextures(1,&colorBufferTexture);
    glGenRenderbuffers(1,&depthBuffer);
    glBindTexture(GL_TEXTURE_2D,colorBufferTexture);
    //glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,targetBufferWidth_,targetBufferHeight_,0,GL_RGBA,);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, targetBufferWidth_, targetBufferHeight_, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, FBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorBufferTexture, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, targetBufferWidth_, targetBufferHeight_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthBuffer);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "Framebuffer is not complete!" << std::endl;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void ScenePass::SetConfig(const PassConfig& cfg){
    targetBufferWidth_ = cfg.targetBufferWidth;
    targetBufferHeight_ = cfg.targetBufferHeight;
}

void ScenePass::Update(const PassRenderContext& ctx, const std::vector<RenderItem>& renderItemList){
    // glViewport(0,0,viewWidth_,viewHeight_);
    // glBindFramebuffer(GL_FRAMEBUFFER,0);
    // glClearColor(0.25f, 0.05f, 0.05f, 1.0f);
    // glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    // for(auto& renderItem : renderItemList){
    //     glBindVertexArray(renderItem.mesh->GetVAO());
    //     renderItem.material->BindAllTexture();
    //     renderItem.material->SetMaterialPropertiesToShader();
    //     // 设定shaderProgram的一些值
    //     ShaderProgram* materialShader = renderItem.material->shaderProgram.get(); 
    //     ShaderUmatf4(*materialShader,"model",renderItem.model);
    //     glDrawElements(GL_TRIANGLES, renderItem.mesh->GetIndicesSize(), GL_UNSIGNED_INT, 0);
    // }
}