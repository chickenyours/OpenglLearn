#include "CSMpass.h"
#include <iostream>
#include <cmath>

#include "code/RenderPipe/RenderContext/PassRenderContext.h"
#include "code/RenderPipe/RenderContext/PassConfig.h"
#include "code/RenderPipe/RenderContext/renderItem.h"
#include "code/Material/material.h"
#include "code/Model/mesh.h"
#include "code/Camera/camera.h"
#include "code/shader.h"

#include "code/RenderPipe/Pass/RenderPassFlag.h"

#include "code/TerminalLog/color_log.h"
#include "code/DebugTool/dynamic_change_vars.h"
#include "code/ToolAndAlgorithm/transformation.h"

using namespace Render;

CSMPass::CSMPass(){
    // std::cout<<"创建Pass(未初始化): CSMPass"<<std::endl;
}

void CSMPass::Init(const PassConfig& cfg){
    Log::Info("CSM PASS","初始化");
    // std::cout<<"初始化Pass: CSMPass"<<std::endl;
    lightDir_ = glm::vec3(-1,-1,-1); //前期没有完善的架构,所以使用固定光照
    distanceLayersCount_ = 5;      //层级数
    // DistanceLayers_ = {100.0f, 25.0f, 10.0f, 2.0f};
    // camera_ = cfg.camera;

    //初始化FBO以及多级深度贴图
    glGenFramebuffers(1, &lightFBO_);
    glGenTextures(1, &lightDepthMaps_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, lightDepthMaps_);
    glTexImage3D(
        GL_TEXTURE_2D_ARRAY,
        0,
        GL_DEPTH_COMPONENT32F,
        depthMapResolution_,
        depthMapResolution_,
        distanceLayersCount_,
        0,
        GL_DEPTH_COMPONENT,
        GL_FLOAT,
        nullptr);
        
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        
    constexpr float bordercolor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, bordercolor);
        
    glBindFramebuffer(GL_FRAMEBUFFER, lightFBO_);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, lightDepthMaps_, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
        
    int status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        std::cout << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!";
        throw 0;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // 初始化 UBO_BINDING_LIGHT_MATRICES
    if (DistanceLayers_.size() > 16) {
        std::cerr << "[CSM PASS] cascade layer size exceeds max supported!" << std::endl;
        return;
    }
    glGenBuffers(1, &matricesUBO_);
    glBindBuffer(GL_UNIFORM_BUFFER, matricesUBO_);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(glm::mat4x4) * 16, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, UBO_BINDING_LIGHT_MATRICES, matricesUBO_);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    // 初始化 UBO_BINDING_CSM_INFO
    // 由于不是每帧更新,CSMInfo UBO 数据可以提前提交到GPU,同时可以不用参与更新
    glGenBuffers(1, &CSMInfoUBO_);
    glBindBuffer(GL_UNIFORM_BUFFER, CSMInfoUBO_);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(CSMInfoUBOLayout) , nullptr, GL_DYNAMIC_DRAW);
    // CSMUBOdata_.cascadeCount = DistanceLayers_.size();
    // for(int i = 0;i<DistanceLayers_.size();i++){
    //     CSMUBOdata_.F_CascadePlaneDistances[i].x = DistanceLayers_[i];
    // }
    // CSMUBOdata_.lightDir = glm::normalize(lightDir_);
    // CSMUBOdata_.F_camfarPlane.x = camera_->GetFar();
    // glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(CSMInfoUBOLayout), &CSMUBOdata_);
    glBindBufferBase(GL_UNIFORM_BUFFER, UBO_BINDING_CSM_INFO, CSMInfoUBO_);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    SetCamera(cfg.camera);

    // 可视化
    glGenTextures(1,&visualMap1);
    glGenTextures(1,&visualMap2);
    glGenTextures(1,&visualMap3);
    glGenTextures(1,&visualMap4);
    glGenRenderbuffers(1,&visualRBO_);
    glGenFramebuffers(1,&visualFBO_);
    glBindTexture(GL_TEXTURE_2D,visualMap1);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGB16F,depthMapResolution_, depthMapResolution_,0,GL_RGB,GL_FLOAT,nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, bordercolor);
    glBindTexture(GL_TEXTURE_2D,visualMap2);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGB16F,depthMapResolution_, depthMapResolution_,0,GL_RGB,GL_FLOAT,nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, bordercolor);
    glBindTexture(GL_TEXTURE_2D,visualMap3);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGB16F,depthMapResolution_, depthMapResolution_,0,GL_RGB,GL_FLOAT,nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, bordercolor);
    glBindTexture(GL_TEXTURE_2D,visualMap4);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGB16F,depthMapResolution_, depthMapResolution_,0,GL_RGB,GL_FLOAT,nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, bordercolor);
    glBindRenderbuffer(GL_RENDERBUFFER,visualRBO_);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,depthMapResolution_,depthMapResolution_);
    glBindFramebuffer(GL_FRAMEBUFFER,visualFBO_);
    glFramebufferTexture(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,visualMap1,0);
    glFramebufferTexture(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT1,visualMap2,0);
    glFramebufferTexture(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT2,visualMap3,0);
    glFramebufferTexture(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT3,visualMap4,0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,visualRBO_);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        // std::cerr << "[ERROR] [CSM PASS] Framebuffer not complete!" << std::endl;
        Log::Error("CSM PASS","Framebuffer not complete!");
    }

    // 检查4个颜色附件是否存在
    for (int i = 0; i < 4; ++i) {
        GLint colorAttachmentType;
        glGetFramebufferAttachmentParameteriv(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &colorAttachmentType
        );
        if (colorAttachmentType == GL_NONE) {
            Log::Error("CSM PASS", "Color attachment " + std::to_string(i) + " is missing!");
        }
    }
    
    GLint depthAttachmentType;
    glGetFramebufferAttachmentParameteriv(
        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &depthAttachmentType
    );
    if (depthAttachmentType == GL_NONE) {
        Log::Error("CSM PASS","Depth attachment is missing!");
        // std::cerr << "[ERROR] [CSM PASS] Depth attachment is missing!" << std::endl;
    }

    GLenum drawBuffers[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3};
    glDrawBuffers(sizeof(drawBuffers) / sizeof(drawBuffers[0]), drawBuffers);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);


    

    

    //初始化深度贴图ShaderProgram
    depthShader_ = std::make_unique<ShaderProgram>(
        "./shaders/Final/CSM/.vs",
        "./shaders/Final/CSM/.fs",
        "./shaders/Final/CSM/.gs"
    );

    visualMap1Shader_ = std::make_unique<ShaderProgram>(
        "./shaders/Final/base_render_world_b.vs",
        "./shaders/Final/CSM/visualdepth1.fs"
    );
}

void CSMPass::SetConfig(const PassConfig& cfg){
    
}

void CSMPass::Update(const PassRenderContext& ctx, const std::vector<RenderItem>& renderItemList){
    auto camera = camera_;


    // 计算每层级对应的光空间矩阵
    std::vector<glm::mat4> lightSpaceMatrices = GetLightSpaceMatrices(*camera, DistanceLayers_);

    // 更新光空间矩阵 UBO
    glBindBufferBase(GL_UNIFORM_BUFFER, UBO_BINDING_LIGHT_MATRICES, matricesUBO_); 
    for (size_t i = 0; i < lightSpaceMatrices.size(); ++i) {
        glBufferSubData(GL_UNIFORM_BUFFER, i * sizeof(glm::mat4), sizeof(glm::mat4), &lightSpaceMatrices[i]);
    }
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    // 阴影绘制：绑定帧缓并设置视口
    glBindFramebuffer(GL_FRAMEBUFFER, lightFBO_);
    depthShader_->Use();
    glViewport(0, 0, depthMapResolution_, depthMapResolution_);
    glClear(GL_DEPTH_BUFFER_BIT);

    // 使用正面剔除避免 Peter Panning（阴影自交错）
    glCullFace(GL_FRONT);

    // 遍历所有渲染项并绘制阴影
    for (auto& renderItem : renderItemList) {
        if (CheckPass(RenderPassFlag::ShadowPass,
                    renderItem.material->renderEnablePassFlag_,
                    renderItem.material->renderDisablePassFlag_)) {
            glBindVertexArray(renderItem.mesh->GetVAO());
            ShaderUmatf4(*depthShader_, "model", renderItem.model);
            glDrawElements(GL_TRIANGLES, renderItem.mesh->GetIndicesSize(), GL_UNSIGNED_INT, 0);
        }
    }
    // 恢复状态
    glCullFace(GL_BACK);

    //绑定纹理
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D_ARRAY,lightDepthMaps_);
    
    // 更新调试
    glBindFramebuffer(GL_FRAMEBUFFER,visualFBO_);
    glEnable(GL_DEPTH_TEST);
    visualMap1Shader_->Use();
    // ShaderU1f(*visualMap1Shader_,"value",GlobalVars::CSMVar1);
    glClearColor(1.0f, 0.00f, 1.00f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    for (auto& renderItem : renderItemList) {
        if (CheckPass(RenderPassFlag::ShadowPass,
                    renderItem.material->renderEnablePassFlag_,
                    renderItem.material->renderDisablePassFlag_)) {
            glBindVertexArray(renderItem.mesh->GetVAO());
            ShaderUmatf4(*visualMap1Shader_, "model", renderItem.model);
            glDrawElements(GL_TRIANGLES, renderItem.mesh->GetIndicesSize(), GL_UNSIGNED_INT, 0);
        }
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);



}

void CSMPass::Release(){
    // 释放 UBO
    glDeleteBuffers(1, &matricesUBO_);
    // 释放 FBO
    glDeleteFramebuffers(1, &lightFBO_);
    // 释放 Texture
    glDeleteTextures(1, &lightDepthMaps_);
    // 释放调试组件
    glDeleteFramebuffers(1, &visualFBO_);
    glDeleteTextures(1,&visualMap1);
    glDeleteTextures(1,&visualMap2);
    glDeleteTextures(1,&visualMap3);
    glDeleteTextures(1,&visualMap4);

    std::cout<<"CSMPass 释放"<<std::endl;
}

void CSMPass::SetCamera(Camera* camera){

    if (!camera) {
        std::cerr << "[CSM PASS] Invalid camera pointer provided!" << std::endl;
        return;
    }
    camera_ = camera;

    float nearPlane = camera_->GetNear();
    float farPlane = camera_->GetFar();
    int cascadeCount = distanceLayersCount_; // 你可以将这个值改为任意层级数
    float lambda = 0.9f;   // 越大越偏对数分布（更关注近处精度）

    DistanceLayers_.clear();
    DistanceLayers_.reserve(cascadeCount + 1);

    // 实用分层方案
    for (int i = 0; i <= cascadeCount; ++i) {
        float p = static_cast<float>(i) / cascadeCount;

        float logSplit = nearPlane * std::pow(farPlane / nearPlane, p);
        float linearSplit = nearPlane + (farPlane - nearPlane) * p;
        float split = lambda * logSplit + (1.0f - lambda) * linearSplit;

        DistanceLayers_.push_back(split);
    }

    // 更新 UBO 数据
    glBindBufferBase(GL_UNIFORM_BUFFER, UBO_BINDING_CSM_INFO, CSMInfoUBO_);
    CSMUBOdata_.cascadeCount = static_cast<int>(DistanceLayers_.size());

    for (int i = 0; i < CSMUBOdata_.cascadeCount; ++i) {
        CSMUBOdata_.F_CascadePlaneDistances[i].x = DistanceLayers_[i];
    }

    CSMUBOdata_.lightDir = glm::normalize(lightDir_);
    CSMUBOdata_.F_camfarPlane.x = farPlane;

    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(CSMInfoUBOLayout), &CSMUBOdata_);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);


}

std::vector<glm::vec4> CSMPass::GetFrustumCornersWorldSpace(const glm::mat4& projview)
{
    const auto inv = glm::inverse(projview);

    std::vector<glm::vec4> frustumCorners;
    for (unsigned int x = 0; x < 2; ++x)
    {
        for (unsigned int y = 0; y < 2; ++y)
        {
            for (unsigned int z = 0; z < 2; ++z)
            {
                const glm::vec4 pt = inv * glm::vec4(2.0f * x - 1.0f, 2.0f * y - 1.0f, 2.0f * z - 1.0f, 1.0f);
                frustumCorners.push_back(pt / pt.w);
            }
        }
    }

    return frustumCorners;
}


std::vector<glm::vec4> CSMPass::GetFrustumCornersWorldSpace(const glm::mat4& proj, const glm::mat4& view)
{
    return GetFrustumCornersWorldSpace(proj * view);
}

glm::mat4 CSMPass::GetLightSpaceMatrix(const float nearPlane, const float farPlane,const Camera& camera)
{
    const glm::mat4 proj = glm::perspective(camera.GetZoom(), camera.GetAspectRatio(), nearPlane,farPlane);
    const std::vector<glm::vec4> corners = GetFrustumCornersWorldSpace(proj,camera.GetViewMatrixRef());

    glm::vec3 center = glm::vec3(0, 0, 0);
    for (const auto& v : corners)
    {
        center += glm::vec3(v);
    }
    center /= corners.size();

    // 严重错误: 
    // const auto lightView = glm::lookAt(center + glm::normalize(lightDir_) * GlobalVars::CSMVar1 , center, glm::vec3(0.0f, 1.0f, 0.0f));
    const auto lightView = Algorithm::GetViewMat4(center,lightDir_);
    // const auto lightView = GetStableLightViewMatrix(lightDir_,);
    // glm::vec3 center = ; // frustum center or camera pos
    // glm::vec3 eye = center - glm::normalize(lightDir_) * 200.0f;
    // glm::mat4 lightView = glm::lookAt(eye, center, glm::vec3(0, 1, 0));

    float minX = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float minY = std::numeric_limits<float>::max();
    float maxY = std::numeric_limits<float>::lowest();
    float minZ = std::numeric_limits<float>::max();
    float maxZ = std::numeric_limits<float>::lowest();
    for (const auto& v : corners)
    {
        const auto trf = lightView * v;
        minX = std::min(minX, trf.x);
        maxX = std::max(maxX, trf.x);
        minY = std::min(minY, trf.y);
        maxY = std::max(maxY, trf.y);
        minZ = std::min(minZ, trf.z);
        maxZ = std::max(maxZ, trf.z);
    }


    // Tune this parameter according to the scene
    // constexpr float zMult = 15.0f;
    float zMult = GlobalVars::CSMVarZmult;
    if (minZ < 0)
    {
        minZ *= zMult;
    }
    else
    {
        minZ /= zMult;
    }
    if (maxZ < 0)
    {
        maxZ /= zMult;
    }
    else
    {
        maxZ *= zMult;
    }

    const glm::mat4 lightProjection = glm::ortho(minX, maxX, minY, maxY, minZ, maxZ);
    return lightProjection * lightView;
}

std::vector<glm::mat4> CSMPass::GetLightSpaceMatrices(const Camera &camera,const std::vector<float>& shadowCascadeLevels)
{
    std::vector<glm::mat4> ret;
    for (size_t i = 1; i < shadowCascadeLevels.size(); ++i)
    {
        ret.push_back(GetLightSpaceMatrix(shadowCascadeLevels[i - 1], shadowCascadeLevels[i], camera));
    }
    return ret;
}

glm::mat4 GetStableLightViewMatrix(const glm::vec3& lightDir, const glm::vec3& targetPos, float dist = 500.0f)
{
    glm::vec3 eye = targetPos - glm::normalize(lightDir) * dist;
    return glm::lookAt(eye, targetPos, glm::vec3(0, 1, 0));
}
