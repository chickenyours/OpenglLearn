#include "engine/RenderPipe/RenderPipe.h"

#include "engine/shader.h"
#include "engine/Model/mesh.h"
#include "engine/Material/material.h"
#include "engine/RenderPipe/Pass/CSMpass.h"
#include "engine/Camera/camera.h"
#include "engine/RenderPipe/RenderContext/RenderPipeConfig.h"
#include "engine/RenderPipe/RenderContext/RenderPipeRenderContext.h"
#include "engine/RenderPipe/RenderContext/renderItem.h"

using namespace Render;

void RenderPipe::Push(const RenderItem& renderItem){
    renderItemList_.push_back(renderItem);
}

void RenderPipe::RenderCall(){
    Update(renderItemList_);
    renderItemList_.clear();
}



