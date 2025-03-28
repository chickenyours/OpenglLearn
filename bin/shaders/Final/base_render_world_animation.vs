#version 460 core 

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTex;
layout(location = 2) in vec3 aNormal;
layout(location = 3) in vec3 aTangent;
layout(location = 5) in ivec4 aBoneIds; 
layout(location = 6) in vec4 aWeights;

out vec3 FragPos;
out vec2 TexCoords;
out vec3 Normal;
out vec3 Tangent;
out mat3 TBN;

uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;

const int MAX_BONES = 100;
const int MAX_BONE_INFLUENCE = 4;
uniform mat4 finalBonesMatrices[MAX_BONES];

void main()
{
    vec4 totalPosition = vec4(0.0);
    vec3 blendedNormal = vec3(0.0);
    vec3 blendedTangent = vec3(0.0);
    float totalWeight = 0.0;  // 记录总权重

    for(int i = 0; i < MAX_BONE_INFLUENCE; ++i)
    {
        int boneID = aBoneIds[i];
        float weight = aWeights[i];

        if(boneID < 0 || boneID >= MAX_BONES)
            continue;

        mat4 boneMatrix = finalBonesMatrices[boneID];
        mat3 boneNormalMatrix = transpose(inverse(mat3(boneMatrix)));
        totalWeight += weight;
        totalPosition += boneMatrix * vec4(aPos, 1.0) * weight;
        blendedNormal  += boneNormalMatrix * aNormal * weight;
        blendedTangent += boneNormalMatrix * aTangent * weight;
    }

    //插值
    if(totalWeight < 0.002){
        totalPosition = vec4(aPos,1.0);
    }
    

    // Apply model and view transforms
    mat4 viewModel = view * model;
    FragPos = vec3(viewModel * totalPosition);
    gl_Position = projection * vec4(FragPos, 1.0);
    TexCoords = aTex;

    // 变换法线和切线到世界空间
    mat3 normalMatrix = transpose(inverse(mat3(viewModel)));
    Normal  = normalize(normalMatrix * blendedNormal);
    Tangent = normalize(normalMatrix * blendedTangent);

    // 构建正交 TBN 矩阵
    vec3 N = normalize(Normal);
    vec3 T = normalize(Tangent - dot(Tangent, N) * N); // Gram-Schmidt
    vec3 B = cross(N, T);
    TBN = mat3(T, B, N);
}
