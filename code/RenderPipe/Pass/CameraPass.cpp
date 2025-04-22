#include "CameraPass.h"

#include <iostream>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "code/RenderPipe/RenderContext/PassConfig.h"
#include "code/RenderPipe/RenderContext/renderItem.h"
#include "code/RenderPipe/RenderContext/PassRenderContext.h"

#include "code/Camera/camera.h"



using namespace Render;

void CameraPass::Init(const PassConfig& cfg){
    // 初始化UBOCamera_
    glGenBuffers(1, &UBOCamera_);
    glBindBuffer(GL_UNIFORM_BUFFER, UBOCamera_);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(CameraDataUBOLayout), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, UBO_BINDING_CAMERA, UBOCamera_);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    sizeof(CSMInfoUBOLayout);
    std::cout<<"[Camera Pass] : 创建并初始化"<<std::endl;
}
void CameraPass::SetConfig(const PassConfig& cfg){}

void CameraPass::Update(const PassRenderContext& ctx, const std::vector<RenderItem>& renderItemList){
    if(ctx.camera){
        glBindBufferBase(GL_UNIFORM_BUFFER,UBO_BINDING_CAMERA,UBOCamera_);
        UBOdata_.projectionMatrix = ctx.camera->GetProjectionMatrixRef();
        UBOdata_.viewMatrix = ctx.camera->GetViewMatrixRef();
        UBOdata_.viewPos = ctx.camera->GetPositionRef();
        UBOdata_.viewDereict = ctx.camera->GetCameraFrontRef();
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(CameraDataUBOLayout), &UBOdata_);
    }
    else{
        std::cerr << "[Camera Pass] : Warning! No camera assigned, skipping update." << std::endl;
    }
}

// void CameraPass::SetCamera(Camera* camera){
//     camera_ = camera;
// }


void CameraPass::Release() {
    if (UBOCamera_ != 0) {
        glDeleteBuffers(1, &UBOCamera_);
        UBOCamera_ = 0;
    }
    std::cout<<"[Camera Pass] : 已释放"<<std::endl;
}

