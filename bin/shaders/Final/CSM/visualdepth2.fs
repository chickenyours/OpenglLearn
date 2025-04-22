#version 460 core

out vec4 FragColor;

in VertexSurfaceData {
    vec3 FragPos;   // 世界空间位置
    vec2 TexCoords; // 纹理坐标
    vec3 Normal;    // 世界空间法线
    vec3 Tangent;   // 世界空间切线
    mat3 TBN;       // 切线空间变换矩阵
} fs_in;

//---------------CAMERA_UBO---------------//
layout (std140, binding = 1) uniform CameraData
{
    mat4 view;
    mat4 projection;
    vec3 viewPos;
    vec3 viewDir;
};
//---------------End CAMERA_UBO---------------//

//---------------CSM_UBO---------------//
// depend UBO : CAMERA_UBO->view
layout(binding = 5) uniform sampler2DArray CSMshadowMap;
layout (std140, binding = 4) uniform LightMatrices
{
    mat4 lightSpaceMatrices[16];
};
layout(std140, binding = 5) uniform CSMInfo {
    vec4 cascadePlaneDistances[16];
    int cascadeCount;
    vec3 lightDir;
    vec4 farPlane;
};
float ShadowCalculation(vec3 fragPosWorldSpace);
//---------------End CSM---------------//

float ShadowCalculation(vec3 fragPosWorldSpace)
{
    // select cascade layer
    vec4 fragPosViewSpace = view * vec4(fragPosWorldSpace, 1.0);
    float depthValue = abs(fragPosViewSpace.z);

    int layer = -1;

    if(depthValue < cascadePlaneDistances[0].x){
        return 0.0;
    }

    for (int i = 1; i < cascadeCount; ++i)
    {
        if (depthValue < cascadePlaneDistances[i].x)
        {
            layer = i - 1;
            break;
        }
    }
    if (layer == -1)
    {
        // 光矩阵和深度贴图有 cascadeCount 个
        layer = cascadeCount - 1;
    }

    // return float(layer) / 4.0;

    vec4 fragPosLightSpace = lightSpaceMatrices[layer] * vec4(fragPosWorldSpace, 1.0);
    // perform perspective divide
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;

    // transform to [0,1] range
    projCoords = projCoords * 0.5 + 0.5;

    // get depth of current fragment from light's perspective
    float currentDepth = projCoords.z;

    float pcfDepth = texture(CSMshadowMap, vec3(projCoords.xy, layer)).r;


    // keep the shadow at 0.0 when outside the far_plane region of the light's frustum.
    if (currentDepth > 1.0)
    {
        return 0.5;
    }
   
    vec3 normal = normalize(fs_in.Normal);
    float bias = max(0.05 * (1.0 - dot(normal, lightDir)), 0.005);
    const float biasModifier = 1.0f;
    if (layer == cascadeCount)
    {
        bias *= 1 / (farPlane.x * biasModifier);
    }
    else
    {
        bias *= 1 / (cascadePlaneDistances[layer].x * biasModifier);
    }
    float shadow = 0.0;
    
    return currentDepth;

}


void main()
{		

    vec3 color = vec3(ShadowCalculation(fs_in.FragPos));
    FragColor = vec4(color, 1.0);

}  