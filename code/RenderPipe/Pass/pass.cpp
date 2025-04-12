#include "pass.h"

#include "code/RenderPipe/RenderContext/PassRenderContext.h"
#include "code/RenderPipe/RenderContext/PassConfig.h"
#include "code/RenderPipe/RenderContext/renderItem.h"

#include "code/RenderPipe/Pass/RenderPassFlag.h"

using namespace Render;

Pass::Pass(){}

bool Pass::CheckPass(RenderPassFlag flag, uint64_t renderEnablePassFlag_, uint64_t renderDisablePassFlag_){
    return (defaultPassFlag && !(renderEnablePassFlag_ & static_cast<uint64_t>(flag))) || (!defaultPassFlag && (renderDisablePassFlag_ & static_cast<uint64_t>(flag)));
}

Pass::~Pass(){
    Release();
}