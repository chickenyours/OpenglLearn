#include <glad/glad.h>
#include "code/ToolAndAlgorithm/Opengl/debug.h"
#include "code/Resource/RenderPipeDAT/node.h"
#include "code/Resource/RenderPipe/UniformBindings.h"
#include "code/ToolAndAlgorithm/Opengl/debug.h"
#include "code/ECS/Component/Camera/camera.h"
#include "code/ToolAndAlgorithm/DAT/target.h"
#include "code/ToolAndAlgorithm/RegisterData/registered_data.h"

namespace Render{
    struct CameraNodeInput{
        
    };
    struct CameraNodeOutput{
        
    };
    struct CameraNodeDepend{
        HandleViewer<CameraDataUBOLayout> mainCameraData;
    };
    class CameraPass : public Node<CameraNodeInput,CameraNodeOutput,CameraNodeDepend>{
        private:
            GLuint UBOCamera_ = 0;
        protected:
            virtual int Init(const NodeContext& cxt) override {
                glGenBuffers(1, &UBOCamera_);
                if(CHECK_GL_ERROR("glGenBuffers")) return 1;
                glBindBuffer(GL_UNIFORM_BUFFER, UBOCamera_);
                if(CHECK_GL_ERROR("glBindBuffer")) return 1;
                glBufferData(GL_UNIFORM_BUFFER, sizeof(CameraDataUBOLayout), nullptr, GL_DYNAMIC_DRAW);
                if(CHECK_GL_ERROR("glBufferData")) return 1;
                glBindBufferBase(GL_UNIFORM_BUFFER, UBO_BINDING_CAMERA, UBOCamera_);
                if(CHECK_GL_ERROR("glBindBufferBase")) return 1;
                glBindBuffer(GL_UNIFORM_BUFFER, 0);
                return 0;
            }
            virtual int Set(const NodeContext& cxt) override {
                return 0;
            }
            virtual int Update() override{
                auto mainCameraUBO = depend.mainCameraData.Get();
                if(mainCameraUBO){
                    glBindBufferBase(GL_UNIFORM_BUFFER,UBO_BINDING_CAMERA,UBOCamera_);
                    CHECK_GL_ERROR("glBindBufferBase");
                    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(CameraDataUBOLayout), mainCameraUBO);
                    CHECK_GL_ERROR("glBufferSubData");
                    return 0;
                }
                else{
                    LOG_ERROR("Camera Pass", "No camera assigned, skipping update.");
                    return 1;
                }
            }
    };

};