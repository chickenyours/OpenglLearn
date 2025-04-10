#include "RenderPipe.h"

#include "code/shader.h"
#include "code/Model/mesh.h"
#include "code/Material/material.h"
#include "code/RenderPipe/Pass/CSMpass.h"
#include "code/Camera/camera.h"
#include "code/RenderPipe/RenderContext/RenderPipeConfig.h"
#include "code/RenderPipe/RenderContext/RenderPipeRenderContext.h"
#include "code/RenderPipe/RenderContext/renderItem.h"

using namespace Render;

void RenderPipe::Push(const RenderItem& renderItem){
    renderItemList_.push_back(renderItem);
}

void RenderPipe::RenderCall(){
    Update(renderItemList_);
    renderItemList_.clear();
}


