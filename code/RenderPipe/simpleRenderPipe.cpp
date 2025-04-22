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

#include "code/RenderPipe/Pass/CameraPass.h"
#include "code/RenderPipe/Pass/CSMpass.h"
#include "code/RenderPipe/Pass/ImageToBuffer.h"
#include "code/RenderPipe/Pass/ScenePass.h"
using namespace Render;

SimpleRenderPipe::SimpleRenderPipe(){
    std::cout<<"创建渲染管线对象(还未初始化): SimpleRenderPipe"<<std::endl;

}

bool SimpleRenderPipe::Init(const RenderPipeConfig& cfg){

    viewWidth_ = cfg.targetBufferWidth;
    viewHeight_ = cfg.targetBufferHeight;
    mainCamera_ = cfg.camera;

    pCameraPass = std::make_unique<CameraPass>();
    pCSMPass = std::make_unique<CSMPass>();
    pImageToScreenPass = std::make_unique<ImageToBufferPass>();
    pScenePass = std::make_unique<ScenePass>();

    PassConfig passConfig;
    passConfig.targetBufferWidth = cfg.targetBufferWidth;
    passConfig.targetBufferHeight = cfg.targetBufferHeight;
    passConfig.camera = mainCamera_;
    //初始化pCameraPass
    pCameraPass->Init(passConfig);
    //初始化pCSMPass
    pCSMPass->Init(passConfig);
    //初始化pScenePass
    pScenePass->Init(passConfig);
    //初始化pImageToScreenPass
    pImageToScreenPass->Init(passConfig);
    //配置IBRenderItem
    ImageToBufferRenderItem IBRenderItem;
    IBRenderItem.scaleViewPortflag = 1;
    IBRenderItem.viewPortScaleOffsetX = 0.0f;
    IBRenderItem.viewPortScaleOffsetY = 0.0f;
    IBRenderItem.viewPortScaleWidth = 0.5;
    IBRenderItem.viewPortScaleHeight = 0.9;
    IBRenderItem.GLTextureType = GL_TEXTURE_2D;
    IBRenderItem.textureID = pScenePass->GetColorBufferTexture();
    pImageToScreenPass->AddPassRenderItem(IBRenderItem);
    // for(int i = 0;i<2;i++){
    //     for(int j = 0;j<2;j++){
    //         IBRenderItem.scaleViewPortflag = 1;
    //         IBRenderItem.viewPortScaleOffsetX = 0.5f + 0.25f * i;
    //         IBRenderItem.viewPortScaleOffsetY = 0.0f + 0.5f * j;
    //         IBRenderItem.viewPortScaleWidth = 0.25;
    //         IBRenderItem.viewPortScaleHeight = 0.5;
    //         IBRenderItem.GLTextureType = GL_TEXTURE_2D_ARRAY;
    //         IBRenderItem.textureID = pCSMPass->GetlightDepthMaps();
    //         IBRenderItem.textureArraylayerIndex = i * 2 + j;
    //         pImageToScreenPass->AddPassRenderItem(IBRenderItem);
    //     }
    // }
    IBRenderItem.scaleViewPortflag = 1;
    IBRenderItem.viewPortScaleOffsetX = 0.5f;
    IBRenderItem.viewPortScaleOffsetY = 0.0f;
    IBRenderItem.viewPortScaleWidth = 0.25;
    IBRenderItem.viewPortScaleHeight = 0.5;
    IBRenderItem.GLTextureType = GL_TEXTURE_2D;
    IBRenderItem.textureID = pCSMPass->visualMap1;
    pImageToScreenPass->AddPassRenderItem(IBRenderItem);
    IBRenderItem.viewPortScaleOffsetX = 0.5f;
    IBRenderItem.viewPortScaleOffsetY = 0.5f;
    IBRenderItem.textureID = pCSMPass->visualMap2;
    pImageToScreenPass->AddPassRenderItem(IBRenderItem);
    IBRenderItem.viewPortScaleOffsetX = 0.75f;
    IBRenderItem.viewPortScaleOffsetY = 0.5f;
    IBRenderItem.textureID = pCSMPass->visualMap3;
    pImageToScreenPass->AddPassRenderItem(IBRenderItem);
    IBRenderItem.viewPortScaleOffsetX = 0.75f;
    IBRenderItem.viewPortScaleOffsetY = 0.0f;
    IBRenderItem.textureID = pCSMPass->visualMap4;
    pImageToScreenPass->AddPassRenderItem(IBRenderItem);
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
    pScenePass->SetConfig(passConfig);
    pImageToScreenPass->SetConfig(passConfig);
}

void SimpleRenderPipe::Update(const std::vector<RenderItem>& renderItemList){
    //小建议
    //     ✅ 效率总评（满分为 ⭐⭐⭐⭐⭐）

    // 维度	评价	说明
    // 架构清晰度	⭐⭐⭐⭐☆	每个 Pass 独立调用，容易维护
    // 渲染状态效率	⭐⭐☆☆☆	没有做 RenderItem 分组，可能会出现频繁状态切换
    // 可扩展性	⭐⭐⭐☆☆	固定调用顺序，未来可改为任务队列系统
    // 重复代码控制	⭐⭐⭐☆☆	多次构造 ctx，可轻微优化
    //支持 renderItemList 分组（opaque、transparent、UI）
    //增加 RenderTaskGroup，一类任务使用一组共享设置（如光照/材质）
    //支持 Pass 优先级执行或 RenderQueue 机制

    // CameraPass 更新当前UBO
    PassRenderContext CameraCtx;
    CameraCtx.camera = mainCamera_;
    pCameraPass->Update(CameraCtx,renderItemList);
    //CSM深度采集
    PassRenderContext CSMPassCtx;
    CSMPassCtx.camera = mainCamera_;
    pCSMPass->Update(CSMPassCtx,renderItemList);
    PassRenderContext ScenePassCtx;
    pScenePass->Update(ScenePassCtx,renderItemList);
    PassRenderContext ImageToScreenPassCtx;
    pImageToScreenPass->Update(ImageToScreenPassCtx,renderItemList);
}

void SimpleRenderPipe::SetCamera(Camera* camera){
    mainCamera_ = camera;
}

void SimpleRenderPipe::Release(){
    pCameraPass->Release();
    pCSMPass->Release();
    pScenePass->Release();
    pImageToScreenPass->Release();
}


SimpleRenderPipe::~SimpleRenderPipe(){
    Release();
    std::cout<<"销毁渲染管线对象: SimpleRenderPipe"<<std::endl;
}