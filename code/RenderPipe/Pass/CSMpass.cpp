#include "CSMpass.h"
#include <cmath>

using namespace Render;

void CSMPass::Init(const InitRenderContext& ctx){
    lightDir = glm::vec3(-1,-1,-1); //前期没有完善的架构,所以使用固定光照
    DistanceLayers_ = {50.0f, 25.0f, 10.0f, 2.0f};
    //初始化FBO以及多级深度贴图
    
}

void CSMPass::Update(const RenderContext& ctx){
    auto camera = ctx.camera;
    float cameraNearPlane = camera->GetNear();
    float cameraFarPlane = camera->GetFar();
    std::vector<float> shadowCascadeLevels{cameraNearPlane , cameraFarPlane / 50.0f, cameraFarPlane / 25.0f, cameraFarPlane / 10.0f, cameraFarPlane / 2.0f , cameraFarPlane};
    auto lightSpaceMatrices = GetLightSpaceMatrices(*camera,shadowCascadeLevels);
    //执行绘制,更新多级深度贴图
}

void CSMPass::Release(){

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

    const auto lightView = glm::lookAt(center + lightDir, center, glm::vec3(0.0f, 1.0f, 0.0f));

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
    for (size_t i = 1; i < shadowCascadeLevels.size() + 1; ++i)
    {
        ret.push_back(GetLightSpaceMatrix(shadowCascadeLevels[i - 1], shadowCascadeLevels[i], camera));
    }
    return ret;
}