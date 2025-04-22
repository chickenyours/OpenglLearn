#version 460 core 

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTex;
layout(location = 2) in vec3 aNormal;
layout(location = 3) in vec3 aTangent;

out vec3 FragPos;
out vec2 TexCoords;
out vec3 Normal;
out vec3 Tangent;
out mat3 TBN;

layout (std140, binding = 1) uniform CameraData
{
    mat4 view;
    mat4 projection;
    vec3 viewPos;
    vec3 viewDir;
};

// uniform mat4 view;
// uniform mat4 projection;
uniform mat4 model;

void main(){
    vec4 Pos =  model * vec4(aPos,1.0);
    gl_Position = projection * view * Pos;
    FragPos = Pos.xyz;
    TexCoords = aTex;
    mat3 normalMatrix = transpose(inverse(mat3(model)));
    Normal = normalMatrix * aNormal;
    Tangent = normalMatrix * aTangent;
    vec3 N = normalize(Normal);
    vec3 T = normalize(Tangent - dot(Tangent,N) * N);
    vec3 B = cross(N,T);
    TBN = mat3(T,B,N);
}