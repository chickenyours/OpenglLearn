#pragma once
#include "code/ToolAndAlgorithm/Opengl/debug.h"
#include "code/ToolAndAlgorithm/Opengl/api.h"
#include "code/Resource/RenderPipeDAT/node.h"
#include "code/Resource/RenderPipe/RenderItems/model_render_item.h"
#include "code/Resource/Material/Interfaces/BPR.h"
#include "code/ToolAndAlgorithm/Opengl/debug.h"
#include "code/ToolAndAlgorithm/DAT/target.h"
#include "code/ECS/Component/Render/mesh_renderer.h"
#include "code/Resource/RenderPipeDAT/data_interface.h"

using namespace DATNode;



namespace Render{

    struct SceneNodeInput{
        
    };
    struct SceneNodeOutput{
        TextureBuffer colorBuffer;
        DepthBuffer depthBuffer;
    };
    struct SceneNodeDepend{
        VersionTargetGroup<ModelRenderItem>* renderItems;
    };

    class SceneNode : public Node<SceneNodeInput,SceneNodeOutput,SceneNodeDepend>{
        private:
            struct MeshSimpleItem{
                const Mesh* mesh;
                glm::mat4* modelMatrix;
                ECS::Component::MeshRenderer* com;
            };
            std::unordered_map<IBPR*,std::vector<MeshSimpleItem>> cache;
            GLuint FBO_ = 0;
            GLuint colorBuffer_ = 0;
            GLuint depthBuffer_ = 0;
            GLuint UBOComponent_ = 0;
            glm::ivec2 colorBufferResolution;
        public:
            virtual int Init(const NodeContext& cxt) override {
                if (cxt.RenderPipeOutputResolution.x > 3000 || cxt.RenderPipeOutputResolution.y > 3000 || cxt.RenderPipeOutputResolution.x <= 15 || cxt.RenderPipeOutputResolution.y <= 15) {
                    LOG_ERROR("RenderPass:ScenePass->SetConfig", "Invalid output resolution: x=" + std::to_string(cxt.RenderPipeOutputResolution.x) + ", y=" + std::to_string(cxt.RenderPipeOutputResolution.y));
                    return 1;
                }
                colorBufferResolution = cxt.RenderPipeOutputResolution;
                glGenFramebuffers(1,&FBO_);
                CHECK_GL_ERROR("glGenFramebuffers");
                glGenTextures(1,&colorBuffer_);
                CHECK_GL_ERROR("glGenTextures");
                glGenRenderbuffers(1, &depthBuffer_);
                CHECK_GL_ERROR("glGenRenderbuffers");

                glBindTexture(GL_TEXTURE_2D, colorBuffer_);
                CHECK_GL_ERROR("glBindTexture");

                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, cxt.RenderPipeOutputResolution.x, cxt.RenderPipeOutputResolution.y, 0, GL_RGBA, GL_FLOAT, nullptr);
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

                glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, cxt.RenderPipeOutputResolution.x, cxt.RenderPipeOutputResolution.y);
                CHECK_GL_ERROR("glRenderbufferStorage");

                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthBuffer_);
                CHECK_GL_ERROR("glFramebufferRenderbuffer");
                

                GLint colorAttachmentType;
                glGetFramebufferAttachmentParameteriv(
                    GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &colorAttachmentType
                );
                if (colorAttachmentType == GL_NONE) {
                    LOG_ERROR("RenderPass:ScenePass->Init", "Color attachment 0 is missing!");
                    return 1;
                }
                glGetFramebufferAttachmentParameteriv(
                    GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &colorAttachmentType
                );
                if (colorAttachmentType == GL_NONE) {
                    LOG_ERROR("RenderPass:ScenePass->Init", "Depth attachment is missing!");
                    return 1;
                }

                if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                    LOG_ERROR("RenderPass:ScenePass->Init", "Framebuffer not complete!");
                    return 1;
                }
                
                glBindFramebuffer(GL_FRAMEBUFFER, 0);

                // UBO

                glGenBuffers(1, &UBOComponent_);
                if(CHECK_GL_ERROR("glGenBuffers")) return 1;
                glBindBuffer(GL_UNIFORM_BUFFER, UBOComponent_);
                if(CHECK_GL_ERROR("glBindBuffer")) return 1;
                glBufferData(GL_UNIFORM_BUFFER, sizeof(StaticModelComponentDataUBOLayout), nullptr, GL_DYNAMIC_DRAW);
                if(CHECK_GL_ERROR("glBufferData")) return 1;
                glBindBufferBase(GL_UNIFORM_BUFFER, UBO_STATIC_MODEL_COMPONENT_DATA, UBOComponent_);
                if(CHECK_GL_ERROR("glBindBufferBase")) return 1;
                glBindBuffer(GL_UNIFORM_BUFFER, 0);

                return 0;
            }
            virtual int Set(const NodeContext& cxt) override {
                if(colorBufferResolution == cxt.RenderPipeOutputResolution) return 0;
                if (cxt.RenderPipeOutputResolution.x > 3000 || cxt.RenderPipeOutputResolution.y > 3000 || cxt.RenderPipeOutputResolution.x <= 15 || cxt.RenderPipeOutputResolution.y <= 15) {
                    LOG_ERROR("RenderPass:ScenePass->SetConfig", "Invalid output resolution: x=" + std::to_string(cxt.RenderPipeOutputResolution.x) + ", y=" + std::to_string(cxt.RenderPipeOutputResolution.y));
                    return 1;
                }
                colorBufferResolution = cxt.RenderPipeOutputResolution;
                glBindTexture(GL_TEXTURE_2D, colorBuffer_);
                CHECK_GL_ERROR("glBindTexture");
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, colorBufferResolution.x, colorBufferResolution.y, 0, GL_RGBA, GL_FLOAT, nullptr);
                CHECK_GL_ERROR("glTexImage2D");
                glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer_);
                CHECK_GL_ERROR("glBindRenderbuffer");
                glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, colorBufferResolution.x, colorBufferResolution.y);
                CHECK_GL_ERROR("glRenderbufferStorage");
                glBindFramebuffer(GL_FRAMEBUFFER, FBO_);
                CHECK_GL_ERROR("glBindFramebuffer");
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorBuffer_, 0);
                CHECK_GL_ERROR("glFramebufferTexture2D");
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthBuffer_);
                CHECK_GL_ERROR("glFramebufferRenderbuffer");
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                return 0;
            }
            virtual int Update() override {
                
                return 0;
            }
    };
}