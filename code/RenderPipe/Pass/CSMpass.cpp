#include "CSMpass.h"
#include <iostream>
#include <cmath>

#include "code/RenderPipe/RenderContext/PassRenderContext.h"
#include "code/RenderPipe/RenderContext/PassConfig.h"
#include "code/RenderPipe/RenderContext/renderItem.h"
#include "code/Model/mesh.h"
#include "code/Camera/camera.h"
#include "code/shader.h"

using namespace Render;

CSMPass::CSMPass(){
    std::cout<<"创建Pass(未初始化): CSMPass"<<std::endl;
}

void CSMPass::Init(const PassConfig& cfg){
    std::cout<<"初始化Pass: CSMPass"<<std::endl;
    lightDir_ = glm::vec3(-1,-1,-1); //前期没有完善的架构,所以使用固定光照
    DistanceLayers_ = {50.0f, 25.0f, 10.0f, 2.0f};
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
        int(DistanceLayers_.size()) + 1,
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

    // 初始化UBO,储存光照投影矩阵
    // configure UBO
    // --------------------
    glGenBuffers(1, &matricesUBO_);
    glBindBuffer(GL_UNIFORM_BUFFER, matricesUBO_);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(glm::mat4x4) * 16, nullptr, GL_STATIC_DRAW);
    //绑定到整个上下文全局UBO索引
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, matricesUBO_);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    //初始化深度贴图ShaderProgram
    depthShader_ = std::make_unique<ShaderProgram>(
        "./shaders/Final/CSM/.vs",
        "./shaders/Final/CSM/.fs",
        "./shaders/Final/CSM/.gs"
    );
}

void CSMPass::SetConfig(const PassConfig& cfg){
    
}

void CSMPass::Update(const PassRenderContext& ctx, const std::vector<RenderItem>& renderItemList){
    auto camera = ctx.camera;
    float cameraNearPlane = camera->GetNear();
    float cameraFarPlane = camera->GetFar();
    std::vector<float> shadowCascadeLevels{cameraNearPlane , 
        cameraFarPlane / DistanceLayers_[0], 
        cameraFarPlane / DistanceLayers_[1], 
        cameraFarPlane / DistanceLayers_[2], 
        cameraFarPlane / DistanceLayers_[3], 
        cameraFarPlane};
    auto lightSpaceMatrices = GetLightSpaceMatrices(*camera,shadowCascadeLevels);
    //更新UBO
    glBindBuffer(GL_UNIFORM_BUFFER, matricesUBO_);
    for (size_t i = 0; i < lightSpaceMatrices.size(); ++i)
    {
        glBufferSubData(GL_UNIFORM_BUFFER, i * sizeof(glm::mat4x4), sizeof(glm::mat4x4), &lightSpaceMatrices[i]);
    }
    // void* ptr = glMapBufferRange(GL_UNIFORM_BUFFER, 0, sizeof(glm::mat4) * N, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
    // memcpy(ptr, lightSpaceMatrices.data(), sizeof(glm::mat4) * N);
    // glUnmapBuffer(GL_UNIFORM_BUFFER);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    //执行绘制,更新多级深度贴图
    depthShader_->Use();
    glBindFramebuffer(GL_FRAMEBUFFER,lightFBO_);
    glViewport(0,0,depthMapResolution_,depthMapResolution_);
    glClear(GL_DEPTH_BUFFER_BIT);
    //正面剔除
    glCullFace(GL_FRONT);
    for(auto& renderItem : renderItemList){
        ShaderUmatf4(*depthShader_,"model",renderItem.model);
        glDrawElements(GL_TRIANGLES, renderItem.mesh->GetIndicesSize(), GL_UNSIGNED_INT, 0);
    }
    glCullFace(GL_BACK);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

}

void CSMPass::Release(){
    // 释放 UBO
    glDeleteBuffers(1, &matricesUBO_);
    // 释放 FBO
    glDeleteFramebuffers(1, &lightFBO_);
    // 释放 Texture
    glDeleteTextures(1, &lightDepthMaps_);
    std::cout<<"CSMPass 释放"<<std::endl;
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

    const auto lightView = glm::lookAt(center + lightDir_, center, glm::vec3(0.0f, 1.0f, 0.0f));

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
    constexpr float zMult = 10.0f;
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