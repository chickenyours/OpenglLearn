#include "engine/RenderPipe/Pass/pass.h"

#include "engine/RenderPipe/RenderContext/PassRenderContext.h"
#include "engine/RenderPipe/RenderContext/PassConfig.h"
#include "engine/RenderPipe/RenderContext/renderItem.h"

#include "engine/RenderPipe/Pass/RenderPassFlag.h"

using namespace Render;

Pass::Pass(){}

bool Pass::CheckPass(RenderPassFlag flag, uint64_t renderEnablePassFlag, uint64_t renderDisablePassFlag){
    auto a = renderDisablePassFlag & static_cast<uint64_t>(flag);
    auto b = renderEnablePassFlag & static_cast<uint64_t>(flag);

    return (defaultPassFlag && !(renderDisablePassFlag & static_cast<uint64_t>(flag))) || (!defaultPassFlag && (renderEnablePassFlag & static_cast<uint64_t>(flag)));
}

void Pass::Release(){}

Pass::~Pass(){
    Release();
}
