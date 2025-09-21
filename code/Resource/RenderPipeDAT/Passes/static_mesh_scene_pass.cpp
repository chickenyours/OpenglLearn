#include "static_mesh_scene_pass.h"
#include "code/ToolAndAlgorithm/Opengl/api.h"
#include "code/Resource/RenderPipe/RenderItems/model_render_item.h"
#include "code/Resource/Material/Interfaces/BPR.h"
#include "code/ToolAndAlgorithm/Opengl/debug.h"

using namespace Render;

int StaticMeshScenePass::InitSelf(){
    colorBufferResolution = environment_->screenResolution;
    glGenFramebuffers(1,&FBO_);
    CHECK_GL_ERROR("glGenFramebuffers");
    glGenTextures(1,&colorBuffer_);
    CHECK_GL_ERROR("glGenTextures");
    glGenRenderbuffers(1, &depthBuffer_);
    CHECK_GL_ERROR("glGenRenderbuffers");
    glBindTexture(GL_TEXTURE_2D, colorBuffer_);
    CHECK_GL_ERROR("glBindTexture");
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, colorBufferResolution.x, colorBufferResolution.y, 0, GL_RGBA, GL_FLOAT, nullptr);
    CHECK_GL_ERROR("glTexImage2D");
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    CHECK_GL_ERROR("glTexParameteri(GL_TEXTURE_MIN_FILTER)");
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    CHECK_GL_ERROR("glTexParameteri(GL_TEXTURE_MAG_FILTER)");
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    CHECK_GL_ERROR("glTexParameteri(GL_TEXTURE_WRAP_S)");
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    CHECK_GL_ERROR("glTexParameteri(GL_TEXTURE_WRAP_T)");
    glBindFramebuffer(GL_FRAMEBUFFER, FBO_);
    CHECK_GL_ERROR("glBindFramebuffer");
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorBuffer_, 0);
    CHECK_GL_ERROR("glFramebufferTexture2D");
    glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer_);
    CHECK_GL_ERROR("glBindRenderbuffer");
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, colorBufferResolution.x, colorBufferResolution.y);
    CHECK_GL_ERROR("glRenderbufferStorage");
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthBuffer_);
    CHECK_GL_ERROR("glFramebufferRenderbuffer");
    GLint colorAttachmentType;
    glGetFramebufferAttachmentParameteriv(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &colorAttachmentType
    );
    if (colorAttachmentType == GL_NONE) {
        LOG_ERROR("RenderPass:ScenePass->Init", "Color attachment 0 is missing!");
        return 9;
    }
    glGetFramebufferAttachmentParameteriv(
        GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &colorAttachmentType
    );
    if (colorAttachmentType == GL_NONE) {
        LOG_ERROR("RenderPass:ScenePass->Init", "Depth attachment is missing!");
        return 8;
    }
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        LOG_ERROR("RenderPass:ScenePass->Init", "Framebuffer not complete!");
        return 7;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    // UBO
    glGenBuffers(1, &UBOComponent_);
    if(CHECK_GL_ERROR("glGenBuffers")) return 3;
    glBindBuffer(GL_UNIFORM_BUFFER, UBOComponent_);
    if(CHECK_GL_ERROR("glBindBuffer")) return 4;
    glBufferData(GL_UNIFORM_BUFFER, sizeof(StaticModelComponentDataUBOLayout), nullptr, GL_DYNAMIC_DRAW);
    if(CHECK_GL_ERROR("glBufferData")) return 5;
    glBindBufferBase(GL_UNIFORM_BUFFER, UBO_STATIC_MODEL_COMPONENT_DATA, UBOComponent_);
    if(CHECK_GL_ERROR("glBindBufferBase")) return 6;
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    return 0;
}
int StaticMeshScenePass::UpdateSelf(){
    if(!models){
        return 1;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, FBO_);
    CHECK_GL_ERROR("glBindFramebuffer");
    glEnable(GL_DEPTH_TEST);
    CHECK_GL_ERROR("glEnable(GL_DEPTH_TEST)");
    glClearColor(0.2f, 0.2f, 0.6f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT| GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glViewport(0,0,colorBufferResolution.x,colorBufferResolution.y);
    glDepthFunc(GL_LESS);
    CHECK_GL_ERROR("glDepthFunc(GL_LESS)");
    for(auto& group : *models){
        for(auto& item : group){
            
        }
    }
}