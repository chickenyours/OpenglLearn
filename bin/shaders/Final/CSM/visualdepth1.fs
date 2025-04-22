#version 460 core

// out vec4 FragColor;
layout (location = 0) out vec3 FragColor0;
layout (location = 1) out vec3 FragColor1;
layout (location = 2) out vec3 FragColor2;
layout (location = 3) out vec3 FragColor3;


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
// float ShadowCalculation(vec3 fragPosWorldSpace);
//---------------End CSM---------------//

// float ShadowCalculation(vec3 fragPosWorldSpace)
// {
   
// }

// 调试
uniform float value;

void main()
{		

     // select cascade layer
    vec4 fragPosViewSpace = view * vec4(fs_in.FragPos, 1.0);
    float depthValue = abs(fragPosViewSpace.z);

    int layer = -1;

    for (int i = 1; i < cascadeCount; ++i)
    {
        if (depthValue < cascadePlaneDistances[i].x)
        {
            layer = i - 1 ;
            break;
        }
    }
    if (layer == -1)
    {
        // 光矩阵和深度贴图有 cascadeCount 个
        layer = cascadeCount - 1;
    }

    // return float(layer) / 4.0;

    vec4 fragPosLightSpace = lightSpaceMatrices[layer] * vec4(fs_in.FragPos, 1.0);
    // perform perspective divide
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;

    // transform to [0,1] range
    projCoords = projCoords * 0.5 + 0.5;

    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
    projCoords.y < 0.0 || projCoords.y > 1.0 ||
    projCoords.z < 0.0 || projCoords.z > 1.0)
    {
        discard; // 或者直接 FragColor0 = vec3(1.0); 表示采样无效
    }

    // get depth of current fragment from light's perspective
    float currentDepth = projCoords.z;

    float pcfDepth = texture(CSMshadowMap, vec3(projCoords.xy, layer)).r;

    // bias
    vec3 normal = normalize(fs_in.Normal);
    // float bias = max(0.000623 * dot(normal, lightDir),0.000);
    float bias = max(0.0013 * dot(normal, lightDir),0.0003);
    // const float biasModifier = 1.0f;
    // if (layer == cascadeCount)
    // {
    //     bias *= 1 / (farPlane.x * biasModifier);
    // }
    // else
    // {
    //     bias *= 1 / (cascadePlaneDistances[layer].x * biasModifier);
    // }

    // PCF
    // keep the shadow at 0.0 when outside the far_plane region of the light's frustum.
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(CSMshadowMap, 0));
    for(int x = -1; x <= 1; ++x)
    {
        for(int y = -1; y <= 1; ++y)
        {
            float pcfDepthh = texture(CSMshadowMap, vec3(projCoords.xy + vec2(x, y) * texelSize, layer)).r;
            shadow += (currentDepth - pcfDepthh + bias) > 0.0 ? 1.0 : 0.0;        
        }    
    }
    shadow /= 9.0;

    FragColor0 = vec3(pcfDepth);
    // FragColor1 = vec3(currentDepth);
    // FragColor2 = (vec3(currentDepth - pcfDepth + 0.00015)) * 10000.0;
    FragColor1 = currentDepth - pcfDepth + bias > 0.0 ? vec3(1.0) : vec3(0.0);
    FragColor2 = vec3(shadow);
    FragColor3 = normal * 0.5 + vec3(0.5);

    // FragColor2 = vec3(1.0);

    // vec3 color = vec3(ShadowCalculation(fs_in.FragPos));
    // FragColor = vec4(color, 1.0);

}  