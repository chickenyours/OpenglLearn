#include "pass.h"

#include "code/RenderPipe/RenderContext/PassRenderContext.h"
#include "code/RenderPipe/RenderContext/PassConfig.h"
#include "code/RenderPipe/RenderContext/renderItem.h"

using namespace Render;

Pass::Pass(){}

void Pass::Release(){}

Pass::~Pass(){
    Release();
}