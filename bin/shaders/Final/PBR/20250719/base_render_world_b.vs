#version 460 core 

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTex;
layout(location = 2) in vec3 aNormal;
layout(location = 3) in vec3 aTangent;

out VertexSurfaceData {
    vec3 FragPos;   // 世界空间位置
    vec2 TexCoords; // 纹理坐标
    vec3 Normal;    // 世界空间法线
    vec3 Tangent;   // 世界空间切线
    mat3 TBN;       // 切线空间变换矩阵
} vs_out;

layout (std140, binding = 1) uniform CameraData
{
    mat4 view;
    mat4 projection;
    vec3 viewPos;
    vec3 viewDir;
};

// 小建议：你可以考虑把 model 放入一个 ObjectData UBO 以支持 instancing 批量上传（可选优化）
uniform mat4 model;

void main(){
    vec4 Pos =  model * vec4(aPos,1.0);
    gl_Position = projection * view * Pos;
    vs_out.FragPos = Pos.xyz;
    vs_out.TexCoords = aTex;
    mat3 normalMatrix = transpose(inverse(mat3(model)));
    vs_out.Normal = normalMatrix * aNormal;
    vs_out.Tangent = normalMatrix * aTangent;
    vec3 N = normalize(vs_out.Normal);
    vec3 T = normalize(vs_out.Tangent - dot(vs_out.Tangent, N) * N);
    vec3 B = cross(N, T);
    vs_out.TBN = mat3(T, B, N);
}