#include "simpleRenderPipe.h"

#include "code/shader.h"
#include "code/Model/mesh.h"
#include "code/Material/material.h"
#include "code/Camera/camera.h"

#include "code/RenderPipe/RenderContext/RenderPipeConfig.h"
#include "code/RenderPipe/RenderContext/RenderPipeRenderContext.h"

#include "code/RenderPipe/RenderContext/PassConfig.h"
#include "code/RenderPipe/RenderContext/PassRenderContext.h"

#include "code/RenderPipe/RenderContext/renderItem.h"

#include "code/RenderPipe/Pass/CSMpass.h"
#include "code/RenderPipe/Pass/ImageToBuffer.h"
using namespace Render;

SimpleRenderPipe::SimpleRenderPipe(){
    std::cout<<"创建渲染管线对象(还未初始化): SimpleRenderPipe"<<std::endl;

}

bool SimpleRenderPipe::Init(const RenderPipeConfig& cfg){

    viewWidth_ = cfg.targetBufferWidth;
    viewHeight_ = cfg.targetBufferHeight;

    pCSMPass = std::make_unique<CSMPass>();
    pImageToScreenPass = std::make_unique<ImageToBufferPass>();

    //初始化pCSMPass
    PassConfig passConfig;
    pCSMPass->Init(passConfig);
    //初始化pImageToScreenPass
    passConfig.targetBufferWidth = cfg.targetBufferWidth;
    passConfig.targetBufferHeight = cfg.targetBufferHeight;
    pImageToScreenPass->Init(passConfig);
    pImageToScreenPass->SetTexture(GL_TEXTURE_2D_ARRAY,pCSMPass->GetlightDepthMaps());
    pImageToScreenPass->SetTextureArrayLayerIndex(1);
    pImageToScreenPass->SetTargetFrameBuffer(0);

    std::cout<<"已初始化渲染管线对象: SimpleRenderPipe"<<std::endl;

    return true;
}

void SimpleRenderPipe::SetConfig(const RenderPipeConfig& cfg){
    viewWidth_ = cfg.targetBufferWidth;
    viewHeight_ = cfg.targetBufferHeight;
    PassConfig passConfig;
    passConfig.targetBufferWidth = cfg.targetBufferWidth;
    passConfig.targetBufferHeight = cfg.targetBufferHeight;
    pCSMPass->SetConfig(passConfig);

    pImageToScreenPass->SetConfig(passConfig);
}

void SimpleRenderPipe::Update(const std::vector<RenderItem>& renderItemList){
    //小建议
    //支持 renderItemList 分组（opaque、transparent、UI）
    //增加 RenderTaskGroup，一类任务使用一组共享设置（如光照/材质）
    //支持 Pass 优先级执行或 RenderQueue 机制
    //CSM深度采集
    PassRenderContext CSMPassCtx;
    CSMPassCtx.camera = mainCamera_;
    pCSMPass->Update(CSMPassCtx,renderItemList);
    //场景绘制
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

    PassRenderContext ImageToScreenPassCtx;
    ImageToScreenPassCtx.renderTargetBuffer = 0;    //默认颜色缓冲区(屏幕)
    pImageToScreenPass->Update(ImageToScreenPassCtx,renderItemList);
    

}

void SimpleRenderPipe::SetCamera(Camera* camera){
    mainCamera_ = camera;
}

void SimpleRenderPipe::Release(){
    pCSMPass->Release();
}


SimpleRenderPipe::~SimpleRenderPipe(){
    Release();
    std::cout<<"销毁渲染管线对象: SimpleRenderPipe"<<std::endl;
}